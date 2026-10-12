module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.Script.SceneScripts;

import Dot.Core.Result;
import Dot.World.Scene;
import Dot.Script.Source;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Script.SceneHost;

namespace Dot::Script {
  namespace {
    using Json = Serialization::Detail::Json;
    constexpr std::array hooks{"onStart", "onUpdate", "onFixedUpdate", "onStop"};

    struct Access {
      bool preparing{};
    };

    struct PrepareScope {
      Access& access;

      explicit PrepareScope(Access& value) : access(value) {
        access.preparing = true;
      }

      ~PrepareScope() {
        access.preparing = false;
      }
    };

    struct Object {
      Runtime* runtime;
      InstanceId id;

      Object(Runtime& owner, InstanceId value) : runtime(&owner), id(value) {
      }

      Object(const Object&) = delete;
      auto operator=(const Object&) -> Object& = delete;

      ~Object() {
        if (id.value != 0) {
          static_cast<void>(runtime->releaseInstance(id));
        }
      }
    };

    struct ActiveScope {
      bool& active;

      explicit ActiveScope(bool& value) : active(value) {
        active = true;
      }

      ~ActiveScope() {
        active = false;
      }
    };

    auto guardBindings(HostBindings bindings, const std::shared_ptr<Access>& access)
        -> HostBindings {
      for (auto& binding : bindings) {
        const bool read = binding.name == "scene.info" || binding.name == "entity.list"
                          || binding.name == "entity.get" || binding.name == "transform.get"
                          || binding.name == "transform.world" || binding.name == "component.get"
                          || binding.name == "entity.exists"
                          || binding.name == "scene.primaryCamera"
                          || binding.name == "scene.primaryListener";
        binding.callback = [access, read, callback = std::move(binding.callback)](
                               std::string_view args) -> Core::Result<std::optional<std::string>> {
          if (access->preparing && !read) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Scene mutations are unavailable while preparing scripts");
          }

          return callback(args);
        };
      }

      return bindings;
    }
  }  // namespace

  struct SceneScripts::Impl {
    struct Record {
      World::EntityId entity;
      ComponentProperties description;
      std::unique_ptr<Object> object;
      std::array<bool, 4> methods{};
      bool started{};
      bool failed{};
    };

    std::shared_ptr<World::Scene> m_scene;
    CompiledProgram m_program;
    SceneScriptsConfig m_config;
    std::shared_ptr<Access> m_access;
    Runtime m_runtime;
    std::map<std::uint64_t, Record> m_records;
    std::uint64_t m_sceneId;
    std::thread::id m_thread{std::this_thread::get_id()};
    bool m_active{};
    bool m_stopped{};
    bool m_faulted{};

    Impl(std::shared_ptr<World::Scene> scene, CompiledProgram program, SceneScriptsConfig config,
         std::shared_ptr<Access> access, Runtime runtime)
        : m_scene(std::move(scene)),
          m_program(std::move(program)),
          m_config(config),
          m_access(std::move(access)),
          m_runtime(std::move(runtime)),
          m_sceneId(m_scene->id()) {
    }

    auto check(bool allowStopped = false) const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id() || m_active || m_scene->id() != m_sceneId
          || (!allowStopped && (m_stopped || m_faulted))) {
        return Core::fail(
            Core::ErrorCode::InvalidState,
            "Scene scripts require their original owner thread/scene without reentry or stop");
      }

      auto name = m_scene->name();

      if (!name) {
        return std::unexpected(name.error());
      }

      return {};
    }

    auto addFailure(ScriptReport& report, World::EntityId entity, ScriptPhase phase,
                    Core::Error error) -> void {
      report.failures.push_back({entity, phase, std::move(error)});
      report.realmFaulted = m_runtime.stats()->faulted;
    }

    auto invoke(Record& record, std::size_t hook, ScriptPhase phase, std::string_view args,
                ScriptReport& report) -> bool {
      if (!record.object || !record.methods[hook]) {
        return true;
      }

      ++report.calls;
      auto result = m_runtime.callMethod(record.object->id, hooks[hook], args);

      if (!result) {
        addFailure(report, record.entity, phase, result.error());
        return false;
      }

      if (result->has_value()) {
        auto error
            = Core::fail(Core::ErrorCode::InvalidArgument,
                         "Lifecycle hook must return undefined: " + std::string(hooks[hook]));
        addFailure(report, record.entity, phase, error.error());
        return false;
      }

      return true;
    }

    auto retire(Record& record, ScriptReport& report) -> void {
      const bool started = std::exchange(record.started, false);

      if (started && record.object && !m_runtime.stats()->faulted) {
        static_cast<void>(invoke(record, 3, ScriptPhase::Stop, "[]", report));
      }

      record.object.reset();
    }

    auto releaseFaulted() -> void {
      for (auto& [id, record] : m_records) {
        static_cast<void>(id);
        record.failed = record.description.enabled;
        record.started = false;
        record.object.reset();
      }
    }

    auto prepare(const World::EntityInfo& entity) -> Core::Result<Record> {
      Record record{entity.id, *entity.script, {}, {}, false, false};

      if (!record.description.enabled) {
        return record;
      }

      auto path = emittedModule(record.description);
      auto modules = m_program.modules();

      if (!path || !modules) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid component source or compiled generation");
      }

      if (!std::ranges::binary_search(*modules, *path)) {
        return Core::fail(Core::ErrorCode::NotFound, "Compiled script module is missing: " + *path);
      }

      auto module = m_runtime.loadModule(*path);

      if (!module) {
        return std::unexpected(module.error());
      }

      const Json context{
          {"entity",
           {{"scene", std::to_string(entity.id.scene)},
            {"value", std::to_string(entity.id.value)}}},
          {"properties", Serialization::Detail::parseJson(record.description.properties, 32)}};
      auto owner = std::make_unique<Object>(m_runtime, InstanceId{});
      auto instance = m_runtime.createInstance(*module, record.description.className,
                                               Json::array({context}).dump());

      if (!instance) {
        return std::unexpected(instance.error());
      }

      owner->id = *instance;
      record.object = std::move(owner);

      for (std::size_t index = 0; index < hooks.size(); ++index) {
        auto method = m_runtime.hasMethod(*instance, hooks[index]);

        if (!method) {
          return std::unexpected(method.error());
        }

        record.methods[index] = *method;
      }

      return record;
    }

    auto synchronize() -> Core::Result<ScriptReport> {
      auto snapshot = m_scene->entities();

      if (!snapshot) {
        return std::unexpected(snapshot.error());
      }

      std::map<std::uint64_t, World::EntityInfo> desired;

      for (auto& entity : *snapshot) {
        if (entity.script) {
          desired.emplace(entity.id.value, std::move(entity));
        }
      }

      if (desired.size() > m_config.maxComponents) {
        return Core::fail(Core::ErrorCode::Unavailable, "Scene script component limit exceeded");
      }

      ScriptReport report;

      if (m_runtime.stats()->faulted) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Scene script realm is faulted; recreate service");
      }

      std::map<std::uint64_t, Record> prepared;

      {
        PrepareScope preparing{*m_access};

        for (const auto& [id, entity] : desired) {
          const auto existing = m_records.find(id);

          if (existing != m_records.end() && existing->second.description == *entity.script) {
            continue;
          }

          auto candidate = prepare(entity);

          if (!candidate) {
            addFailure(report, entity.id, ScriptPhase::Prepare, candidate.error());

            if (report.realmFaulted) {
              releaseFaulted();
            }

            return report;
          }

          prepared.emplace(id, std::move(*candidate));
        }
      }

      for (auto iterator = m_records.begin(); iterator != m_records.end();) {
        if (!desired.contains(iterator->first) || prepared.contains(iterator->first)) {
          retire(iterator->second, report);
          iterator = m_records.erase(iterator);
          ++report.removed;
        } else {
          ++iterator;
        }
      }

      while (!prepared.empty()) {
        const auto& candidate = prepared.begin()->second;

        if (!m_scene->entity(candidate.entity)) {
          auto error = Core::fail(Core::ErrorCode::NotFound,
                                  "Script owner was removed while stopping previous components");
          addFailure(report, candidate.entity, ScriptPhase::Prepare, error.error());
          prepared.erase(prepared.begin());
          continue;
        }

        m_records.insert(prepared.extract(prepared.begin()));
        ++report.created;
      }

      report.realmFaulted = m_runtime.stats()->faulted;

      if (report.realmFaulted) {
        releaseFaulted();
      }

      return report;
    }

    auto dispatch(ScriptPhase phase, double seconds) -> Core::Result<ScriptReport> {
      auto synced = synchronize();

      if (!synced || !synced->failures.empty() || synced->realmFaulted) {
        return synced;
      }

      auto report = std::move(*synced);
      const auto args = std::format("[{}]", seconds);
      const auto hook = phase == ScriptPhase::FixedUpdate ? 2U : 1U;

      const auto visit = [&](bool startPass) -> void {
        for (auto iterator = m_records.begin(); iterator != m_records.end();) {
          auto current = iterator++;
          auto& record = current->second;

          if (!m_scene->entity(record.entity)) {
            retire(record, report);
            m_records.erase(current);
            ++report.removed;

            if (report.realmFaulted) {
              break;
            }

            continue;
          }

          if (!record.object || record.failed) {
            continue;
          }

          if (startPass && !record.started) {
            record.started = true;

            if (!invoke(record, 0, ScriptPhase::Start, "[]", report)) {
              record.failed = true;
              retire(record, report);
            }
          } else if (!startPass && record.started && !invoke(record, hook, phase, args, report)) {
            record.failed = true;
            retire(record, report);
          }

          if (report.realmFaulted) {
            break;
          }

          if (!m_scene->entity(record.entity)) {
            retire(record, report);
            m_records.erase(current);
            ++report.removed;
            if (report.realmFaulted) {
              break;
            }
          }
        }
      };

      visit(true);

      if (phase != ScriptPhase::Start && !report.realmFaulted) {
        visit(false);
      }

      if (report.realmFaulted) {
        releaseFaulted();
      }

      return report;
    }
  };

  SceneScripts::SceneScripts(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  SceneScripts::SceneScripts(SceneScripts&&) noexcept = default;

  auto SceneScripts::operator=(SceneScripts&&) noexcept -> SceneScripts& = default;

  SceneScripts::~SceneScripts() = default;

  auto SceneScripts::create(std::shared_ptr<World::Scene> scene, const CompiledProgram& program,
                            const SceneScriptsConfig& config) -> Core::Result<SceneScripts> {
    if (config.maxComponents == 0 || config.maxComponents > 5000
        || config.runtime.maxInstances < config.maxComponents * 2 || config.reloadStateBytes == 0
        || config.reloadStateBytes > 64 * 1024 * 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid component/transient instance capacity");
    }

    auto bindings = sceneBindings(scene);
    auto files = program.files();

    if (!bindings) {
      return std::unexpected(bindings.error());
    }

    if (!files) {
      return std::unexpected(files.error());
    }

    try {
      auto access = std::make_shared<Access>();
      auto runtime
          = Runtime::create(*files, config.runtime, guardBindings(std::move(*bindings), access));

      if (!runtime) {
        return std::unexpected(runtime.error());
      }

      return SceneScripts(std::make_unique<Impl>(std::move(scene), program, config,
                                                 std::move(access), std::move(*runtime)));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create scene scripts: " + std::string(error.what()));
    }
  }

  auto SceneScripts::sync() -> Core::Result<ScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene scripts were moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    ActiveScope active{m_impl->m_active};

    try {
      return m_impl->synchronize();
    } catch (const std::exception& error) {
      m_impl->m_faulted = true;
      return Core::fail(Core::ErrorCode::External,
                        "Synchronize scene scripts: " + std::string(error.what()));
    }
  }

  auto SceneScripts::start() -> Core::Result<ScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene scripts were moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    ActiveScope active{m_impl->m_active};

    try {
      return m_impl->dispatch(ScriptPhase::Start, 0);
    } catch (const std::exception& error) {
      m_impl->m_faulted = true;
      return Core::fail(Core::ErrorCode::External,
                        "Start scene scripts: " + std::string(error.what()));
    }
  }

  auto SceneScripts::update(double seconds) -> Core::Result<ScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene scripts were moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Script frame seconds must be finite in [0,1]");
    }

    ActiveScope active{m_impl->m_active};

    try {
      return m_impl->dispatch(ScriptPhase::Update, seconds);
    } catch (const std::exception& error) {
      m_impl->m_faulted = true;
      return Core::fail(Core::ErrorCode::External,
                        "Update scene scripts: " + std::string(error.what()));
    }
  }

  auto SceneScripts::fixedUpdate(double seconds) -> Core::Result<ScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene scripts were moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Script fixed seconds must be finite in [0,1]");
    }

    ActiveScope active{m_impl->m_active};

    try {
      return m_impl->dispatch(ScriptPhase::FixedUpdate, seconds);
    } catch (const std::exception& error) {
      m_impl->m_faulted = true;
      return Core::fail(Core::ErrorCode::External,
                        "Fixed update scene scripts: " + std::string(error.what()));
    }
  }

  auto SceneScripts::reload(const CompiledProgram& program) -> Core::Result<ReloadReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene scripts were moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto* old = m_impl.get();
    std::unique_ptr<Impl> retired;
    ActiveScope active{old->m_active};
    bool committed = false;

    try {
      auto candidate = create(old->m_scene, program, old->m_config);

      if (!candidate) {
        return std::unexpected(candidate.error());
      }

      auto prepared = candidate->sync();

      if (!prepared) {
        return std::unexpected(prepared.error());
      }

      if (!prepared->failures.empty() || prepared->realmFaulted) {
        const auto& failure = prepared->failures.front();
        return Core::fail(failure.error.code, "Reload prepare entity "
                                                  + std::to_string(failure.entity.value) + ": "
                                                  + failure.error.message);
      }

      ReloadReport report{
          static_cast<std::uint32_t>(candidate->m_impl->m_records.size()), 0, 0, {}};
      const bool oldHealthy = !old->m_runtime.stats()->faulted;
      {
        PrepareScope protectOld{*old->m_access};
        PrepareScope protectNew{*candidate->m_impl->m_access};

        for (auto& [id, next] : candidate->m_impl->m_records) {
          const auto found = old->m_records.find(id);

          if (!oldHealthy || found == old->m_records.end() || !next.object) {
            continue;
          }

          const auto& previous = found->second;

          if (!previous.object || previous.failed || !previous.started
              || previous.description.source != next.description.source
              || previous.description.className != next.description.className) {
            continue;
          }

          auto save = old->m_runtime.hasMethod(previous.object->id, "onSaveState");

          if (!save) {
            return Core::fail(save.error().code, "Reload state discovery entity "
                                                     + std::to_string(id) + ": "
                                                     + save.error().message);
          }

          if (*save) {
            auto load = candidate->m_impl->m_runtime.hasMethod(next.object->id, "onLoadState");

            if (!load) {
              return std::unexpected(load.error());
            }

            if (!*load) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Reload entity " + std::to_string(id)
                                    + " exports state but candidate has no onLoadState");
            }

            auto state = old->m_runtime.callMethod(previous.object->id, "onSaveState");

            if (!state) {
              return Core::fail(state.error().code, "Reload save entity " + std::to_string(id)
                                                        + ": " + state.error().message);
            }

            if (!state->has_value()) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Reload onSaveState must return finite JSON rather than undefined");
            }

            static_cast<void>(Serialization::Detail::parseJson(**state));

            if ((**state).size() > old->m_config.reloadStateBytes - report.stateBytes) {
              return Core::fail(Core::ErrorCode::Unavailable,
                                "Reload aggregate state byte limit exceeded");
            }

            report.stateBytes += (**state).size();
            auto loaded = candidate->m_impl->m_runtime.callMethod(next.object->id, "onLoadState",
                                                                  "[" + **state + "]");

            if (!loaded) {
              return Core::fail(loaded.error().code, "Reload load entity " + std::to_string(id)
                                                         + ": " + loaded.error().message);
            }

            if (loaded->has_value()) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Reload onLoadState must return undefined");
            }

            ++report.migrated;
          }

          next.started = previous.started;
        }
      }

      /** Swap complete owners; object guards borrow their original Runtime address.
       * 切换完整所有者，对象守卫借用原始 Runtime 地址，不能分别移动其成员。 */
      retired = std::move(m_impl);
      m_impl = std::move(candidate->m_impl);
      committed = true;

      for (auto& [id, previous] : retired->m_records) {
        const auto found = m_impl->m_records.find(id);
        const bool compatible
            = found != m_impl->m_records.end() && found->second.object
              && found->second.description.source == previous.description.source
              && found->second.description.className == previous.description.className;

        if (!compatible) {
          retired->retire(previous, report.retired);
          ++report.retired.removed;
        }
      }

      for (auto iterator = m_impl->m_records.begin(); iterator != m_impl->m_records.end();) {
        if (!m_impl->m_scene->entity(iterator->second.entity)) {
          auto error
              = Core::fail(Core::ErrorCode::NotFound, "Reload owner was removed during cleanup");
          report.retired.failures.push_back(
              {iterator->second.entity, ScriptPhase::Prepare, error.error()});
          iterator = m_impl->m_records.erase(iterator);
        } else {
          ++iterator;
        }
      }

      report.components = static_cast<std::uint32_t>(m_impl->m_records.size());
      return report;
    } catch (const std::exception& error) {
      if (committed) {
        m_impl->m_faulted = true;
        return Core::fail(Core::ErrorCode::External, "Reload committed but native cleanup failed: "
                                                         + std::string(error.what()));
      }

      return Core::fail(Core::ErrorCode::External,
                        "Reload scene scripts: " + std::string(error.what()));
    }
  }

  auto SceneScripts::stop() -> Core::Result<ScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene scripts were moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    ActiveScope active{m_impl->m_active};

    try {
      ScriptReport report;

      for (auto& [id, record] : m_impl->m_records) {
        static_cast<void>(id);
        m_impl->retire(record, report);
        ++report.removed;
      }

      m_impl->m_records.clear();
      m_impl->m_stopped = true;
      report.realmFaulted = m_impl->m_runtime.stats()->faulted;
      return report;
    } catch (const std::exception& error) {
      m_impl->m_faulted = true;
      return Core::fail(Core::ErrorCode::External,
                        "Stop scene scripts: " + std::string(error.what()));
    }
  }

  auto SceneScripts::stats() const -> Core::Result<SceneScriptsStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene scripts were moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto runtime = m_impl->m_runtime.stats();

    if (!runtime) {
      return std::unexpected(runtime.error());
    }

    SceneScriptsStats result{static_cast<std::uint32_t>(m_impl->m_records.size()),
                             0,
                             0,
                             0,
                             0,
                             m_impl->m_stopped,
                             m_impl->m_faulted || runtime->faulted,
                             *runtime};

    for (const auto& [id, record] : m_impl->m_records) {
      static_cast<void>(id);

      if (record.failed) {
        ++result.failed;
      } else if (!record.description.enabled) {
        ++result.disabled;
      } else if (record.started) {
        ++result.running;
      } else {
        ++result.pending;
      }
    }

    return result;
  }
}  // namespace Dot::Script

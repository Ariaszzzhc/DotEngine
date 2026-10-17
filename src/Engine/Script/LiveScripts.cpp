module;

#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

module Dot.Script.LiveScripts;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.World.Scene;
import Dot.Script.Compiler;
import Dot.Script.Compilation;
import Dot.Script.SceneScripts;

namespace Dot::Script {
  namespace {
    struct ActiveScope {
      bool& active;

      explicit ActiveScope(bool& value) : active(value) {
        active = true;
      }

      ~ActiveScope() {
        active = false;
      }
    };

    auto validate(const std::shared_ptr<World::Scene>& scene, const LiveScriptsConfig& config)
        -> Core::Result<void> {
      if (!scene || scene->id() == 0) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Live scripts require a live owned scene");
      }

      auto owner = scene->name();

      if (!owner) {
        return std::unexpected(owner.error());
      }

      if (config.pollInterval.count() < 0 || config.pollInterval > std::chrono::minutes{1}) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid live-script poll interval");
      }

      return {};
    }
  }  // namespace

  struct LiveScripts::Impl {
    struct Compilation {
      SourceSnapshot m_source;
      CompilationJob m_job;
      bool m_obsolete{};

      Compilation(SourceSnapshot source, CompilationJob job)
          : m_source(std::move(source)), m_job(std::move(job)) {
      }

      auto discard() -> void {
        m_obsolete = true;
        m_job.requestStop();
      }
    };

    using Clock = std::chrono::steady_clock;
    std::shared_ptr<World::Scene> m_scene;
    Core::FileSystem m_project;
    TypeScriptCompiler m_compiler;
    LiveScriptsConfig m_config;
    SourceSnapshot m_attempt;
    SceneScripts m_scripts;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_sceneId;
    std::uint64_t m_compileAttempts;
    std::uint64_t m_reloads{};
    std::optional<Core::Error> m_compileError;
    std::optional<Core::Error> m_captureError;
    std::uint64_t m_discarded{};
    std::unique_ptr<Compilation> m_compilation;
    Clock::time_point m_next;
    bool m_active{};
    bool m_stopped{};
    bool m_forcePending{};

    Impl(std::shared_ptr<World::Scene> scene, Core::FileSystem project, TypeScriptCompiler compiler,
         LiveScriptsConfig config, SourceSnapshot attempt, SceneScripts scripts,
         std::uint64_t compileAttempts)
        : m_scene(std::move(scene)),
          m_project(std::move(project)),
          m_compiler(std::move(compiler)),
          m_config(std::move(config)),
          m_attempt(std::move(attempt)),
          m_scripts(std::move(scripts)),
          m_sceneId(m_scene->id()),
          m_compileAttempts(compileAttempts),
          m_next(Clock::now() + m_config.pollInterval) {
    }

    auto check(bool allowStopped = false) const -> Core::Result<void> {
      if (m_active || m_thread != std::this_thread::get_id() || m_scene->id() != m_sceneId
          || (!allowStopped && m_stopped)) {
        return Core::fail(
            Core::ErrorCode::InvalidState,
            "Live scripts require their original owner thread/scene without reentry or stop");
      }

      return m_scene->name().transform([](const auto&) {});
    }

    auto error() const -> std::optional<Core::Error> {
      return m_captureError ? m_captureError : m_compileError;
    }

    auto poll(bool force) -> Core::Result<ReloadStatus> {
      ReloadStatus status;
      const auto finish = [&] {
        status.compiling = m_compilation != nullptr;
        status.error = error();
        return status;
      };
      auto readiness = m_compilation ? m_compilation->m_job.ready() : Core::Result<bool>{false};

      if (!readiness) {
        return std::unexpected(readiness.error());
      }

      const bool completed = *readiness;

      if (!force && !completed && Clock::now() < m_next) {
        return finish();
      }

      m_next = Clock::now() + m_config.pollInterval;
      status.checked = true;
      auto captured = m_compiler.captureProject(m_project, m_config.directory);

      if (!captured) {
        m_captureError = captured.error();
        m_forcePending = false;

        if (m_compilation) {
          m_compilation->discard();

          if (completed) {
            m_compilation.reset();
            ++m_discarded;
            status.discarded = true;
          }
        }

        return finish();
      }

      m_captureError.reset();
      m_forcePending = m_forcePending || force;
      status.changed = !captured->equals(m_attempt);

      if (m_compilation && !captured->equals(m_compilation->m_source)) {
        m_compilation->discard();
      } else if (m_compilation && !m_compilation->m_obsolete) {
        /** A current job satisfies force; an obsolete one must retain the retry.
         * 当前任务满足强制请求，旧任务取消期间则保留重试意图。 */
        m_forcePending = false;
      }

      if (completed) {
        const bool obsolete = m_compilation->m_obsolete;
        auto program = m_compilation->m_job.take();

        if (!obsolete) {
          m_attempt = m_compilation->m_source;
        }

        m_compilation.reset();

        if (obsolete) {
          ++m_discarded;
          status.discarded = true;
        } else {
          if (!program) {
            m_compileError = program.error();
          } else {
            auto reloaded = m_scripts.reload(*program);

            if (!reloaded) {
              m_compileError = reloaded.error();
            } else {
              ++m_reloads;
              m_compileError.reset();
              status.committed = true;
              status.reload = std::move(*reloaded);
            }
          }
        }
      }

      if (!m_compilation && (m_forcePending || !captured->equals(m_attempt))) {
        m_forcePending = false;
        ++m_compileAttempts;

        try {
          auto job = CompilationJob::begin(m_compiler, *captured);

          if (job) {
            m_compilation = std::make_unique<Compilation>(*captured, std::move(*job));
          } else {
            m_attempt = *captured;
            m_compileError = job.error();
          }
        } catch (const std::exception& exception) {
          m_attempt = *captured;
          m_compileError
              = Core::fail(Core::ErrorCode::External,
                           "Start background compilation: " + std::string(exception.what()))
                    .error();
        }
      }

      return finish();
    }

    auto frame(double seconds, bool fixed) -> Core::Result<LiveScriptReport> {
      auto polled = poll(false);

      if (!polled) {
        return std::unexpected(polled.error());
      }

      auto dispatched = fixed ? m_scripts.fixedUpdate(seconds) : m_scripts.update(seconds);

      if (!dispatched) {
        return std::unexpected(dispatched.error());
      }

      return LiveScriptReport{std::move(*polled), std::move(*dispatched)};
    }
  };

  LiveScripts::LiveScripts(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  LiveScripts::LiveScripts(LiveScripts&&) noexcept = default;

  auto LiveScripts::operator=(LiveScripts&&) noexcept -> LiveScripts& = default;

  LiveScripts::~LiveScripts() = default;

  auto LiveScripts::create(std::shared_ptr<World::Scene> scene, const Core::FileSystem& project,
                           const TypeScriptCompiler& compiler, const LiveScriptsConfig& config)
      -> Core::Result<LiveScripts> {
    auto checked = validate(scene, config);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto snapshot = compiler.captureProject(project, config.directory);

    if (!snapshot) {
      return std::unexpected(snapshot.error());
    }

    auto program = compiler.compileSnapshot(*snapshot);

    if (!program) {
      return std::unexpected(program.error());
    }

    return prepare(std::move(scene), project, compiler, std::move(*snapshot), *program, config, 1);
  }

  auto LiveScripts::createPrepared(std::shared_ptr<World::Scene> scene,
                                   const Core::FileSystem& project,
                                   const TypeScriptCompiler& compiler,
                                   const CompiledProgram& program, const LiveScriptsConfig& config)
      -> Core::Result<LiveScripts> {
    auto checked = validate(scene, config);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto source = program.source();
    auto current = compiler.captureProject(project, config.directory);

    if (!source || !current) {
      return std::unexpected(!source ? source.error() : current.error());
    }

    if (!source->equals(*current)) {
      return Core::fail(Core::ErrorCode::Unavailable,
                        "Prepared scripts no longer match project source");
    }

    return prepare(std::move(scene), project, compiler, std::move(*source), program, config, 0);
  }

  auto LiveScripts::prepare(std::shared_ptr<World::Scene> scene, const Core::FileSystem& project,
                            const TypeScriptCompiler& compiler, SourceSnapshot source,
                            const CompiledProgram& program, const LiveScriptsConfig& config,
                            std::uint64_t compileAttempts) -> Core::Result<LiveScripts> {
    auto scripts = SceneScripts::create(scene, program, config.scripts);

    if (!scripts) {
      return std::unexpected(scripts.error());
    }

    auto prepared = scripts->sync();

    if (!prepared) {
      return std::unexpected(prepared.error());
    }

    if (!prepared->failures.empty()) {
      return std::unexpected(prepared->failures.front().error);
    }

    if (prepared->realmFaulted) {
      return Core::fail(Core::ErrorCode::InvalidState, "Prepare live script realm faulted");
    }

    try {
      return LiveScripts(std::make_unique<Impl>(std::move(scene), project, compiler, config,
                                                std::move(source), std::move(*scripts),
                                                compileAttempts));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create live scripts: " + std::string(error.what()));
    }
  }

  auto LiveScripts::poll(bool force) -> Core::Result<ReloadStatus> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Live scripts were moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    ActiveScope active{m_impl->m_active};

    try {
      return m_impl->poll(force);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Poll live scripts: " + std::string(error.what()));
    }
  }

  auto LiveScripts::start() -> Core::Result<ScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Live scripts were moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    ActiveScope active{m_impl->m_active};
    return m_impl->m_scripts.start();
  }

  auto LiveScripts::update(double seconds) -> Core::Result<LiveScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Live scripts were moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Live frame seconds must be finite in [0,1]");
    }

    ActiveScope active{m_impl->m_active};

    try {
      return m_impl->frame(seconds, false);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Update live scripts: " + std::string(error.what()));
    }
  }

  auto LiveScripts::fixedUpdate(double seconds) -> Core::Result<LiveScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Live scripts were moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Live fixed seconds must be finite in [0,1]");
    }

    ActiveScope active{m_impl->m_active};

    try {
      return m_impl->frame(seconds, true);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Fixed update live scripts: " + std::string(error.what()));
    }
  }

  auto LiveScripts::stop() -> Core::Result<ScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Live scripts were moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    ActiveScope active{m_impl->m_active};
    m_impl->m_stopped = true;
    cancelCompilation();
    return m_impl->m_scripts.stop();
  }

  auto LiveScripts::cancelCompilation() noexcept -> void {
    if (m_impl) {
      m_impl->m_forcePending = false;
      m_impl->m_compilation.reset();
    }
  }

  auto LiveScripts::stats() const -> Core::Result<LiveScriptsStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Live scripts were moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto scripts = m_impl->m_scripts.stats();

    if (!scripts) {
      return std::unexpected(scripts.error());
    }

    return LiveScriptsStats{
        m_impl->m_compileAttempts,        m_impl->m_reloads,  m_impl->error(), *scripts,
        m_impl->m_compilation != nullptr, m_impl->m_discarded};
  }
}  // namespace Dot::Script

module;

#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "ComponentJson.hpp"

module Dot.Script.PhysicsSource;

import Dot.Core.Result;
import Dot.Core.Text;
import Dot.World.Scene;
import Dot.Math;
import Dot.Physics.World;
import Dot.Physics.SceneSimulation;

namespace Dot::Script {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    using Reply = Core::Result<std::optional<std::string>>;
    using EntityId = World::EntityId;

    constexpr std::array methods{"physics.body",
                                 "physics.stats",
                                 "physics.rayCast",
                                 "physics.setVelocity",
                                 "physics.setAngularVelocity",
                                 "physics.addImpulse",
                                 "physics.moveKinematic",
                                 "physics.contacts"};

    auto entity(const Json& value) -> EntityId {
      Codec::requireFields(value, {"scene", "value"});
      return {Codec::id(value["scene"]), Codec::id(value["value"])};
    }

    auto idJson(EntityId value) -> Json {
      return {{"scene", std::to_string(value.scene)}, {"value", std::to_string(value.value)}};
    }

    auto bodyJson(const Physics::BodyState& value) -> Json {
      const auto motion = value.motion == Physics::Motion::Static      ? "static"
                          : value.motion == Physics::Motion::Kinematic ? "kinematic"
                                                                       : "dynamic";
      return {{"motion", motion},
              {"position", Detail::xyz(value.position)},
              {"rotation", Detail::xyzw(value.rotation)},
              {"velocity", Detail::xyz(value.velocity)},
              {"angularVelocity", Detail::xyz(value.angularVelocity)},
              {"active", value.active},
              {"sensor", value.sensor}};
    }

    auto contactJson(const Physics::SceneContactEvent& event) -> Json {
      const auto phase = event.phase == Physics::ContactPhase::Begin   ? "begin"
                         : event.phase == Physics::ContactPhase::Stay  ? "stay"
                         : event.phase == Physics::ContactPhase::Sleep ? "sleep"
                                                                       : "end";
      const auto reason
          = event.endReason == Physics::ContactEndReason::Separated     ? "separated"
            : event.endReason == Physics::ContactEndReason::PoseChanged ? "poseChanged"
            : event.endReason == Physics::ContactEndReason::Destroyed   ? "destroyed"
                                                                        : "reconfigured";
      Json geometry = nullptr;

      if (event.geometry) {
        auto points = Json::array();

        for (const auto& point : event.geometry->points) {
          points.push_back(
              {{"first", Detail::xyz(point.first)}, {"second", Detail::xyz(point.second)}});
        }

        geometry = {{"normal", Detail::xyz(event.geometry->normal)},
                    {"penetrationDepth", event.geometry->penetrationDepth},
                    {"points", std::move(points)}};
      }

      return {
          {"phase", phase},
          {"endReason", event.phase == Physics::ContactPhase::End ? Json(reason) : Json(nullptr)},
          {"first", idJson(event.first)},
          {"second", idJson(event.second)},
          {"sensor", event.sensor},
          {"step", std::to_string(event.step)},
          {"geometry", std::move(geometry)}};
    }

    auto written(Core::Result<void> value) -> Reply {
      if (!value) {
        return std::unexpected(value.error());
      }

      return std::optional<std::string>{};
    }
  }  // namespace

  struct PhysicsSource::Impl {
    std::shared_ptr<World::Scene> m_scene;
    Physics::SceneSimulation m_simulation;
    PhysicsSourceConfig m_config;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_identity;
    std::uint64_t m_generation{1};
    std::uint64_t m_batch{};
    std::string m_contacts;
    bool m_faulted{};

    Impl(std::shared_ptr<World::Scene> scene, Physics::SceneSimulation simulation,
         PhysicsSourceConfig config)
        : m_scene(std::move(scene)),
          m_simulation(std::move(simulation)),
          m_config(config),
          m_identity(m_scene->id()) {
    }

    auto check(bool mutate = false) const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id() || m_scene->id() != m_identity) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Physics source requires original scene/owner thread");
      }

      if (mutate && m_faulted) {
        return Core::fail(Core::ErrorCode::InvalidState, "Physics source faulted; reset to resume");
      }

      return m_scene->name().transform([](const auto&) {});
    }

    auto capacity() const -> Core::Result<void> {
      if (m_batch == std::numeric_limits<std::uint64_t>::max() - 1) {
        return Core::fail(Core::ErrorCode::Unavailable, "Physics contact batch index exhausted");
      }

      return {};
    }

    auto batchJson(std::uint64_t generation, const Json& events) const -> std::string {
      return Json{{"generation", std::to_string(generation)},
                  {"index", std::to_string(m_batch + 1)},
                  {"events", events}}
          .dump();
    }

    auto publish() -> Core::Result<void> {
      try {
        auto events = m_simulation.drainContactEvents();

        if (!events) {
          m_faulted = true;
          return std::unexpected(events.error());
        }

        auto entries = Json::array();

        for (const auto& event : *events) {
          entries.push_back(contactJson(event));
        }

        auto encoded = batchJson(m_generation, entries);

        if (encoded.size() > m_config.contactJsonBytes) {
          m_faulted = true;
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Physics contacts exceeded JSON budget after mutation; reset required");
        }

        m_contacts = std::move(encoded);
        ++m_batch;
        return {};
      } catch (const std::exception& error) {
        m_faulted = true;
        return Core::fail(Core::ErrorCode::External,
                          "Publish physics contacts after mutation; reset required: "
                              + std::string(error.what()));
      }
    }

    auto dispatch(std::string_view method, const Json& args) -> Reply {
      const auto count = method == "physics.body"                                    ? 1U
                         : method == "physics.stats" || method == "physics.contacts" ? 0U
                         : method == "physics.moveKinematic"                         ? 4U
                                                                                     : 2U;

      if (args.size() != count) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Incorrect physics argument count");
      }

      if (method == "physics.stats") {
        auto stats = m_simulation.stats();

        if (!stats) {
          return std::unexpected(stats.error());
        }

        return std::optional{Json{{"bodies", stats->bodies},
                                  {"active", stats->active},
                                  {"faulted", stats->faulted || m_faulted},
                                  {"contactPairs", stats->contactPairs},
                                  {"queuedContactEvents", stats->queuedContactEvents},
                                  {"generation", std::to_string(m_generation)}}
                                 .dump()};
      }

      if (method == "physics.contacts") {
        auto stats = m_simulation.stats();

        if (!stats) {
          return std::unexpected(stats.error());
        }

        if (m_faulted || stats->faulted) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Faulted physics contacts are unavailable; reset required");
        }

        return std::optional{m_contacts};
      }

      if (method == "physics.rayCast") {
        auto hit = m_simulation.rayCast(Detail::vector<Math::Vec3>(args[0]),
                                        Detail::vector<Math::Vec3>(args[1]));

        if (!hit) {
          return std::unexpected(hit.error());
        }

        if (!*hit) {
          return std::optional<std::string>{"null"};
        }

        const auto& value = **hit;

        if (!m_scene->entity(value.entity)) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Physics ray hit has a removed owner; synchronize required");
        }

        return std::optional{
            Json{{"entity", idJson(value.entity)},
                 {"fraction", value.fraction},
                 {"point", Detail::xyz(value.point)},
                 {"normal", value.normal ? Detail::xyz(*value.normal) : Json(nullptr)}}
                .dump()};
      }

      const auto id = entity(args[0]);
      auto owner = m_scene->entity(id);

      if (!owner) {
        return std::unexpected(owner.error());
      }

      if (method == "physics.body") {
        auto value = m_simulation.body(id);

        if (!value) {
          return std::unexpected(value.error());
        }

        return std::optional{bodyJson(*value).dump()};
      }

      auto checked = check(true);

      if (!checked) {
        return std::unexpected(checked.error());
      }

      const auto value = Detail::vector<Math::Vec3>(args[1]);

      if (method == "physics.setVelocity") {
        return written(m_simulation.setVelocity(id, value));
      }

      if (method == "physics.setAngularVelocity") {
        return written(m_simulation.setAngularVelocity(id, value));
      }

      if (method == "physics.addImpulse") {
        return written(m_simulation.addImpulse(id, value));
      }

      const auto q = Detail::color<Math::Quat>(args[2]);

      if (!args[3].is_number() || !std::isfinite(args[3].get<double>())) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Kinematic seconds require a finite number");
      }

      return written(m_simulation.moveKinematic(id, value, q, args[3].get<double>()));
    }

    auto invoke(std::string_view method, std::string_view arguments) -> Reply {
      auto checked = check();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (arguments.empty() || arguments.size() > 64 * 1024 || !Core::isValidUtf8(arguments)
          || arguments.find('\0') != std::string_view::npos) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid physics host argument bytes");
      }

      try {
        const auto args = Codec::parseJson(arguments, 16);

        if (!args.is_array()) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Physics arguments require an array");
        }

        return dispatch(method, args);
      } catch (const std::invalid_argument& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Physics JSON: " + std::string(error.what()));
      } catch (const Json::exception& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Physics JSON: " + std::string(error.what()));
      } catch (const std::exception& error) {
        return Core::fail(Core::ErrorCode::External,
                          "Physics host operation: " + std::string(error.what()));
      }
    }
  };

  PhysicsSource::PhysicsSource(std::shared_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  PhysicsSource::PhysicsSource(PhysicsSource&&) noexcept = default;

  auto PhysicsSource::operator=(PhysicsSource&&) noexcept -> PhysicsSource& = default;

  PhysicsSource::~PhysicsSource() = default;

  auto PhysicsSource::create(std::shared_ptr<World::Scene> scene, const PhysicsSourceConfig& config)
      -> Core::Result<PhysicsSource> {
    if (!scene || scene->id() == 0 || config.contactJsonBytes == 0
        || config.contactJsonBytes > 16 * 1024 * 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid physics source scene/contact budget");
    }

    auto simulation = Physics::SceneSimulation::create(*scene, config.simulation);

    if (!simulation) {
      return std::unexpected(simulation.error());
    }

    try {
      auto impl = std::make_shared<Impl>(std::move(scene), std::move(*simulation), config);
      auto empty = impl->batchJson(1, Json::array());

      if (empty.size() > config.contactJsonBytes) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Physics contact budget cannot hold empty batch");
      }

      impl->m_contacts = std::move(empty);
      ++impl->m_batch;
      return PhysicsSource(std::move(impl));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create physics source: " + std::string(error.what()));
    }
  }

  auto PhysicsSource::synchronize() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics source was moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return checked;
    }

    auto capacity = m_impl->capacity();

    if (!capacity) {
      return capacity;
    }

    auto result = m_impl->m_simulation.synchronize(*m_impl->m_scene);
    return result ? m_impl->publish() : result;
  }

  auto PhysicsSource::step(double seconds) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics source was moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return checked;
    }

    auto capacity = m_impl->capacity();

    if (!capacity) {
      return capacity;
    }

    auto result = m_impl->m_simulation.step(*m_impl->m_scene, seconds);
    return result ? m_impl->publish() : result;
  }

  auto PhysicsSource::reset() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics source was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    auto capacity = m_impl->capacity();

    if (!capacity) {
      return capacity;
    }

    if (m_impl->m_generation == std::numeric_limits<std::uint64_t>::max() - 1) {
      return Core::fail(Core::ErrorCode::Unavailable, "Physics generation exhausted");
    }

    try {
      auto empty = m_impl->batchJson(m_impl->m_generation + 1, Json::array());

      if (empty.size() > m_impl->m_config.contactJsonBytes) {
        return Core::fail(Core::ErrorCode::Unavailable, "Physics empty batch exceeds JSON budget");
      }

      auto result = m_impl->m_simulation.reset(*m_impl->m_scene);

      if (!result) {
        return result;
      }

      m_impl->m_contacts = std::move(empty);
      ++m_impl->m_generation;
      ++m_impl->m_batch;
      m_impl->m_faulted = false;
      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Reset physics source: " + std::string(error.what()));
    }
  }

  auto PhysicsSource::bindings() const -> Core::Result<HostBindings> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics source was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      HostBindings result;
      result.reserve(methods.size());

      for (const auto* method : methods) {
        const bool read = std::string_view(method) == "physics.body"
                          || std::string_view(method) == "physics.stats"
                          || std::string_view(method) == "physics.rayCast"
                          || std::string_view(method) == "physics.contacts";
        result.push_back({method,
                          [impl = m_impl, method](std::string_view args) -> Reply {
                            return impl->invoke(method, args);
                          },
                          read ? HostAccess::ReadOnly : HostAccess::ReadWrite});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Bind physics source: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Script

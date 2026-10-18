module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ComponentJson.hpp"

module Dot.Script.NavigationSource;

import Dot.Core.Text;
import Dot.Physics.Character;
import Dot.Math;
import Dot.Navigation.Geometry;
import Dot.Navigation.Movement;

namespace Dot::Script {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    using Reply = Core::Result<std::optional<std::string>>;

    auto body(const ControlledCharacter& value) -> Navigation::MovementBody {
      return {{value.entity.value,
               value.config.position,
               {value.measuredVelocity.x, 0, value.measuredVelocity.z},
               {},
               value.config.radius,
               2 * (value.config.radius + value.config.halfHeight)},
              value.ground == Physics::GroundState::Grounded};
    }

    auto moveRequest(const Json& value) -> Navigation::MoveRequest {
      Codec::requireFields(value, {"goal", "speed"},
                           {"acceleration", "stoppingDistance", "neighborDistance", "timeHorizon"});
      Navigation::MoveRequest result;
      result.goal = Detail::vector<Math::Vec3>(value["goal"]);
      result.speed = Codec::number(value["speed"]);

      for (const auto& [name, destination] :
           std::array{std::pair{"acceleration", &result.acceleration},
                      std::pair{"stoppingDistance", &result.stoppingDistance},
                      std::pair{"neighborDistance", &result.neighborDistance},
                      std::pair{"timeHorizon", &result.timeHorizon}}) {
        if (value.contains(name)) {
          *destination = Codec::number(value[name]);
        }
      }

      return result;
    }

    auto errorName(Core::ErrorCode code) -> const char* {
      switch (code) {
        case Core::ErrorCode::InvalidArgument:
          return "InvalidArgument";
        case Core::ErrorCode::NotFound:
          return "NotFound";
        case Core::ErrorCode::Io:
          return "Io";
        case Core::ErrorCode::Unavailable:
          return "Unavailable";
        case Core::ErrorCode::InvalidState:
          return "InvalidState";
        case Core::ErrorCode::External:
          return "External";
      }
      return "External";
    }

    auto moveJson(const Navigation::MoveState& value) -> Json {
      const auto phase = value.phase == Navigation::MovePhase::Failed    ? "failed"
                         : value.phase == Navigation::MovePhase::Arrived ? "arrived"
                                                                         : "moving";
      return {{"phase", phase},
              {"goal", Detail::xyz(value.request.goal)},
              {"speed", value.request.speed},
              {"acceleration", value.request.acceleration},
              {"stoppingDistance", value.request.stoppingDistance},
              {"neighborDistance", value.request.neighborDistance},
              {"timeHorizon", value.request.timeHorizon},
              {"velocity", Detail::xyz(value.velocity)},
              {"distance", value.distance},
              {"blocked", value.blocked},
              {"corner", value.corner},
              {"corners", value.corners},
              {"neighbors", value.neighbors},
              {"error", value.error ? Json{{"code", errorName(value.error->code)},
                                           {"message", value.error->message}}
                                    : Json(nullptr)}};
    }

  }  // namespace

  struct NavigationSource::Impl {
    std::shared_ptr<World::Scene> m_scene;
    std::optional<Navigation::Surface> m_surface;
    std::vector<Physics::MeshAsset> m_meshes;
    Navigation::GeometryState m_geometry;
    CharacterReader m_character;
    CharacterControl m_control;
    std::optional<Navigation::Movement> m_movement;
    bool m_retired{};
    std::optional<std::string> m_binding;
    std::thread::id m_owner{std::this_thread::get_id()};
    std::uint64_t m_identity{};

    auto check() const -> Core::Result<void> {
      if (m_retired || m_owner != std::this_thread::get_id() || m_identity != m_scene->id()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Navigation source requires original scene/owner thread");
      }

      auto binding = m_scene->navigation();

      if (!binding) {
        return std::unexpected(binding.error());
      }

      if (*binding != m_binding) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Navigation binding changed after preparation");
      }

      return {};
    }

    auto prove() const -> Core::Result<void> {
      auto checked = check();

      if (!checked) {
        return checked;
      }

      if (!m_surface) {
        return Core::fail(Core::ErrorCode::Unavailable, "Scene has no baked navigation surface");
      }

      auto described = m_surface->description();

      if (!described) {
        return std::unexpected(described.error());
      }

      const auto description = *described;
      auto geometry = Navigation::sceneGeometry(*m_scene, description->settings, m_meshes);
      auto identity = geometry ? Navigation::geometryIdentity(*geometry)
                               : Core::Result<std::string>{std::unexpected(geometry.error())};

      if (!identity) {
        return std::unexpected(identity.error());
      }

      return *identity == description->source
                 ? Core::Result<void>{}
                 : Core::fail(Core::ErrorCode::InvalidState,
                              "Static collision geometry changed; navigation requires rebaking");
    }

    auto current() const -> Core::Result<void> {
      auto checked = check();

      if (!checked) {
        return checked;
      }

      if (!m_surface) {
        return Core::fail(Core::ErrorCode::Unavailable, "Scene has no baked navigation surface");
      }

      auto unchanged = m_geometry.unchanged(*m_scene);
      return !unchanged ? Core::Result<void>{std::unexpected(unchanged.error())}
             : *unchanged
                 ? Core::Result<void>{}
                 : Core::fail(Core::ErrorCode::InvalidState,
                              "Static collision geometry changed; navigation requires rebaking");
    }

    auto fits(const Physics::CharacterConfig& character) const -> Core::Result<void> {
      if (!m_surface) {
        return Core::fail(Core::ErrorCode::Unavailable, "Scene has no baked navigation surface");
      }

      const auto& settings = m_surface->description().value()->settings;

      if (settings.agentRadius + 1e-5F < character.radius
          || settings.agentHeight + 1e-5F < 2 * (character.radius + character.halfHeight)
          || settings.maxClimb > character.stepHeight + 1e-5F
          || settings.maxSlopeDegrees > character.maxSlopeDegrees + 1e-5F) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Navigation agent profile does not fit character");
      }

      return {};
    }

    auto observed(World::EntityId id) const -> Core::Result<ControlledCharacter> {
      auto entity = m_scene->entity(id);

      if (!entity || !entity->character) {
        return entity ? Core::fail(Core::ErrorCode::InvalidArgument,
                                   "Navigation requires a native character")
                      : Core::Result<ControlledCharacter>{std::unexpected(entity.error())};
      }

      auto values = m_control.snapshot();

      if (!values) {
        return std::unexpected(values.error());
      }

      auto found = std::ranges::find(*values, id, &ControlledCharacter::entity);
      return found != values->end() ? Core::Result<ControlledCharacter>{*found}
                                    : Core::fail(Core::ErrorCode::NotFound,
                                                 "Navigation character is not synchronized");
    }

    auto state(World::EntityId id) const -> Core::Result<Json> {
      auto measured = observed(id);

      if (!measured) {
        return std::unexpected(measured.error());
      }

      if (!m_movement) {
        return Json(nullptr);
      }

      auto value = m_movement->state(body(*measured));

      if (!value) {
        return std::unexpected(value.error());
      }

      if (!*value) {
        return Json(nullptr);
      }

      auto valid = current();

      if (!valid) {
        (**value).phase = Navigation::MovePhase::Failed;
        (**value).velocity = {};
        (**value).error = valid.error();
      }

      return moveJson(**value);
    }

    auto operation(const Json& request) -> Core::Result<Json> {
      const auto command = request.at("command").get<std::string>();

      if (command == "agents") {
        Codec::requireFields(request, {"command"});
        auto result = Json::array();

        if (m_movement) {
          auto entities = m_movement->entities();

          if (!entities) {
            return std::unexpected(entities.error());
          }

          for (const auto entity : *entities) {
            const World::EntityId id{m_identity, entity};
            auto value = state(id);

            if (!value) {
              return std::unexpected(value.error());
            }

            result.push_back(
                {{"entity",
                  {{"scene", std::to_string(id.scene)}, {"value", std::to_string(id.value)}}},
                 {"state", std::move(*value)}});
          }
        }

        return Json{{"agents", std::move(result)}};
      }

      if (command != "move" && command != "stop" && command != "state") {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown native navigation command");
      }

      if (command == "move") {
        Codec::requireFields(request, {"command", "entity", "value"});
      } else {
        Codec::requireFields(request, {"command", "entity"});
      }

      Codec::requireFields(request["entity"], {"scene", "value"});
      const World::EntityId id{Codec::id(request["entity"]["scene"]),
                               Codec::id(request["entity"]["value"])};

      if (id.scene != m_identity || id.value == 0) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Navigation requires current scoped entity");
      }

      if (command == "stop") {
        if (m_movement) {
          auto entities = m_movement->entities();

          if (!entities) {
            return std::unexpected(entities.error());
          }

          if (std::ranges::contains(*entities, id.value)) {
            auto owner = m_scene->entity(id);

            if (owner && owner->character) {
              const CharacterIntent intent{id, {}, {}};
              auto stopped = m_control.apply({&intent, 1});

              if (!stopped) {
                return std::unexpected(stopped.error());
              }
            }

            auto stopped = m_movement->stop(id.value);

            if (!stopped) {
              return std::unexpected(stopped.error());
            }
          }
        }

        return Json{{"agent", nullptr}};
      }

      if (command == "move") {
        auto valid = current();

        if (!valid) {
          return std::unexpected(valid.error());
        }

        auto measured = observed(id);

        if (!measured) {
          return std::unexpected(measured.error());
        }

        auto profile = fits(measured->config);

        if (!profile) {
          return std::unexpected(profile.error());
        }

        const auto wanted = moveRequest(request["value"]);
        auto moved = m_movement->move(body(*measured), wanted);

        if (!moved) {
          return std::unexpected(moved.error());
        }
      }

      auto result = state(id);
      return result ? Core::Result<Json>{Json{{"agent", std::move(*result)}}}
                    : std::unexpected(result.error());
    }

    auto invoke(std::string_view method, std::string_view arguments) -> Reply {
      auto checked = check();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (arguments.empty() || arguments.size() > 65536 || !Core::isValidUtf8(arguments)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid navigation argument bytes");
      }

      try {
        const auto args = Codec::parseJson(arguments, 16);

        if (method == "navigation.move" || method == "navigation.stop"
            || method == "navigation.state" || method == "navigation.agents") {
          const bool move = method == "navigation.move", agents = method == "navigation.agents";

          if (!args.is_array() || args.size() != (move ? 4U : agents ? 0U : 1U)) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Incorrect navigation movement argument count");
          }

          Json request{{"command", std::string(method.substr(11))}};

          if (!agents) {
            request["entity"] = args[0];
          }

          if (move) {
            Codec::requireFields(
                args[3], {},
                {"acceleration", "stoppingDistance", "neighborDistance", "timeHorizon"});
            request["value"] = args[3];
            request["value"]["goal"] = args[1];
            request["value"]["speed"] = args[2];
          }

          auto result = operation(request);

          if (!result) {
            return std::unexpected(result.error());
          }

          return move || method == "navigation.stop"
                     ? Reply{std::optional<std::string>{}}
                     : Reply{std::optional{(*result)[agents ? "agents" : "agent"].dump()}};
        }

        const bool stats = method == "navigation.stats";

        if (!args.is_array() || args.size() != (stats ? 0U : 4U)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Incorrect navigation argument count");
        }

        if (stats && !m_surface) {
          return std::optional<std::string>{"null"};
        }

        auto valid = current();

        if (!valid) {
          return std::unexpected(valid.error());
        }

        const auto& settings = m_surface->description().value()->settings;

        if (stats) {
          const auto value = m_surface->stats().value();
          return std::optional{Json{{"path", *m_binding},
                                    {"source", m_surface->description().value()->source},
                                    {"tiles", value.tiles},
                                    {"polygons", value.polygons},
                                    {"agentHeight", settings.agentHeight},
                                    {"agentRadius", settings.agentRadius},
                                    {"maxClimb", settings.maxClimb},
                                    {"maxSlopeDegrees", settings.maxSlopeDegrees}}
                                   .dump()};
        }

        Codec::requireFields(args[0], {"scene", "value"});
        const World::EntityId id{Codec::id(args[0]["scene"]), Codec::id(args[0]["value"])};
        auto entity = m_scene->entity(id);

        if (!entity) {
          return std::unexpected(entity.error());
        }

        if (!entity->character) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Navigation requires a native character");
        }

        auto character = m_character(id);

        if (!character) {
          return std::unexpected(character.error());
        }

        auto profile = fits(*character);

        if (!profile) {
          return std::unexpected(profile.error());
        }

        if (!args[3].is_number_integer() || args[3] < 2 || args[3] > 512) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Navigation corner budget requires 2..512");
        }

        const Navigation::RouteQuery query{character->position, Detail::vector<Math::Vec3>(args[1]),
                                           Detail::vector<Math::Vec3>(args[2]),
                                           args[3].get<std::uint32_t>()};
        auto route = m_surface->route(query);

        if (!route) {
          return std::unexpected(route.error());
        }

        auto points = Json::array();

        for (const auto point : route->points) {
          points.push_back(Detail::xyz(point));
        }

        return std::optional{Json{
            {"start", Detail::xyz(route->start)},
            {"goal", Detail::xyz(route->goal)},
            {"points",
             std::move(points)}}.dump()};
      } catch (const std::invalid_argument& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Navigation JSON: " + std::string(error.what()));
      } catch (const Json::exception& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Navigation JSON: " + std::string(error.what()));
      } catch (const std::exception& error) {
        return Core::fail(Core::ErrorCode::External,
                          "Navigation operation: " + std::string(error.what()));
      }
    }
  };

  NavigationSource::NavigationSource(std::shared_ptr<Impl> impl) noexcept
      : m_impl(std::move(impl)) {
  }

  auto NavigationSource::create(std::shared_ptr<World::Scene> scene, const PhysicsSource& physics,
                                std::optional<Navigation::Surface> surface,
                                std::vector<Physics::MeshAsset> meshes)
      -> Core::Result<NavigationSource> {
    if (!scene) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Navigation requires scene ownership");
    }

    auto binding = scene->navigation();

    if (!binding) {
      return std::unexpected(binding.error());
    }

    if (binding->has_value() != surface.has_value()) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Navigation binding/prepared surface mismatch");
    }

    try {
      auto reader = physics.characterReader();
      auto control = physics.characterControl();

      if (!reader || !control) {
        return std::unexpected(!reader ? reader.error() : control.error());
      }

      auto impl = std::make_shared<Impl>();
      impl->m_character = std::move(*reader);
      impl->m_control = std::move(*control);
      impl->m_identity = scene->id();
      impl->m_scene = std::move(scene);
      impl->m_binding = std::move(*binding);
      impl->m_surface = std::move(surface);
      impl->m_meshes = std::move(meshes);
      auto checked = impl->m_surface ? impl->prove() : impl->check();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      auto geometry = Navigation::GeometryState::capture(*impl->m_scene);

      if (!geometry) {
        return std::unexpected(geometry.error());
      }

      impl->m_geometry = std::move(*geometry);

      if (impl->m_surface) {
        auto movement = Navigation::Movement::create(*impl->m_surface);

        if (!movement) {
          return std::unexpected(movement.error());
        }

        impl->m_movement.emplace(std::move(*movement));
      }

      return NavigationSource{std::move(impl)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create navigation source: " + std::string(error.what()));
    }
  }

  auto NavigationSource::bindings() const -> Core::Result<HostBindings> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Navigation source was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      HostBindings result;

      for (const auto* method : {"navigation.stats", "navigation.route", "navigation.move",
                                 "navigation.stop", "navigation.state", "navigation.agents"}) {
        const bool write = std::string_view(method) == "navigation.move"
                           || std::string_view(method) == "navigation.stop";
        result.push_back(
            {method,
             [impl = m_impl, method](std::string_view args) { return impl->invoke(method, args); },
             write ? HostAccess::ReadWrite : HostAccess::ReadOnly});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Bind navigation source: " + std::string(error.what()));
    }
  }

  auto NavigationSource::perform(std::string_view request) -> Core::Result<std::string> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Navigation source was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (request.empty() || request.size() > 65536 || !Core::isValidUtf8(request)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid navigation request bytes");
    }

    try {
      auto result = m_impl->operation(Codec::parseJson(request, 16));
      return result ? Core::Result<std::string>{result->dump()} : std::unexpected(result.error());
    } catch (const std::invalid_argument& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Navigation request: " + std::string(error.what()));
    } catch (const Json::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Navigation request: " + std::string(error.what()));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Navigation request: " + std::string(error.what()));
    }
  }

  auto NavigationSource::advance(double seconds) -> Core::Result<void> {
    if (!std::isfinite(seconds) || seconds < 1e-6 || seconds > 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Navigation duration requires 1 microsecond..1 second");
    }

    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Navigation source was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (!m_impl->m_movement) {
      return {};
    }

    auto entities = m_impl->m_movement->entities();

    if (!entities) {
      return std::unexpected(entities.error());
    }

    if (entities->empty()) {
      return {};
    }

    try {
      auto current = m_impl->current();

      if (!current) {
        auto failed = m_impl->m_movement->fail(current.error());

        if (!failed) {
          return failed;
        }
      }

      auto snapshot = m_impl->m_control.snapshot();

      if (!snapshot) {
        return std::unexpected(snapshot.error());
      }

      std::vector<Navigation::MovementBody> bodies;
      bodies.reserve(snapshot->size());

      for (const auto& measured : *snapshot) {
        bodies.push_back(body(measured));

        if (std::ranges::contains(*entities, measured.entity.value)) {
          auto profile = m_impl->fits(measured.config);

          if (!profile) {
            auto failed = m_impl->m_movement->fail(measured.entity.value, profile.error());

            if (!failed) {
              return failed;
            }
          }
        }
      }

      auto intents = m_impl->m_movement->advance(bodies, seconds);

      if (!intents) {
        return std::unexpected(intents.error());
      }

      std::vector<CharacterIntent> output;
      output.reserve(intents->size());

      for (const auto& intent : *intents) {
        const auto velocity = intent.velocity;
        const auto yaw = std::hypot(velocity.x, velocity.z) > .01F
                             ? std::optional<float>{std::atan2(-velocity.x, -velocity.z) * 180
                                                    / std::numbers::pi_v<float>}
                             : std::nullopt;
        output.push_back({{m_impl->m_identity, intent.entity}, {velocity, false}, yaw});
      }

      return m_impl->m_control.apply(output);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Advance scoped navigation: " + std::string(error.what()));
    }
  }

  auto NavigationSource::retire() noexcept -> void {
    if (m_impl) {
      m_impl->m_retired = true;
      m_impl->m_movement.reset();
      m_impl->m_control = {};
      m_impl->m_character = {};
    }
  }
}  // namespace Dot::Script

module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ComponentJson.hpp"

module Dot.Script.NavigationSource;

import Dot.Core.Text;
import Dot.Math;
import Dot.Navigation.Geometry;

namespace Dot::Script {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    using Reply = Core::Result<std::optional<std::string>>;
  }  // namespace

  struct NavigationSource::Impl {
    std::shared_ptr<World::Scene> m_scene;
    std::optional<Navigation::Surface> m_surface;
    std::vector<Physics::MeshAsset> m_meshes;
    Navigation::GeometryState m_geometry;
    CharacterReader m_character;
    std::optional<std::string> m_binding;
    std::thread::id m_owner{std::this_thread::get_id()};
    std::uint64_t m_identity{};

    auto check() const -> Core::Result<void> {
      if (m_owner != std::this_thread::get_id() || m_identity != m_scene->id()) {
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

    auto invoke(std::string_view method, std::string_view arguments) const -> Reply {
      auto checked = check();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (arguments.empty() || arguments.size() > 65536 || !Core::isValidUtf8(arguments)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid navigation argument bytes");
      }

      try {
        const auto args = Codec::parseJson(arguments, 16);
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

        if (settings.agentRadius + 1e-5F < character->radius
            || settings.agentHeight + 1e-5F < 2 * (character->radius + character->halfHeight)
            || settings.maxClimb > character->stepHeight + 1e-5F
            || settings.maxSlopeDegrees > character->maxSlopeDegrees + 1e-5F) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Navigation agent profile does not fit character");
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

      if (!reader) {
        return std::unexpected(reader.error());
      }

      auto impl = std::make_shared<Impl>();
      impl->m_character = std::move(*reader);
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
      return HostBindings{{"navigation.stats",
                           [impl = m_impl](std::string_view args) {
                             return impl->invoke("navigation.stats", args);
                           },
                           HostAccess::ReadOnly},
                          {"navigation.route",
                           [impl = m_impl](std::string_view args) {
                             return impl->invoke("navigation.route", args);
                           },
                           HostAccess::ReadOnly}};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Bind navigation source: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Script

module;

#include <exception>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

#include "../../Engine/Serialization/Json.hpp"

module Dot.Editor.NavigationBake;

import Dot.World.SceneFiles;
import Dot.Physics.MeshFiles;
import Dot.Physics.Body;
import Dot.Navigation.Geometry;
import Dot.Navigation.Files;

namespace Dot::Editor {
  namespace {
    auto prepare(const Core::FileSystem& files, const std::string& initial,
                 const Navigation::BuildSettings& settings, std::stop_token stop)
        -> Core::Result<NavigationCreation> {
      auto scene = World::decodeScene(initial);

      if (!scene) {
        return std::unexpected(scene.error());
      }

      auto colliders = scene->colliders();

      if (!colliders) {
        return std::unexpected(colliders.error());
      }

      NavigationCreation result;
      result.root = files.root();
      std::vector<Physics::MeshAsset> meshes;
      std::set<std::string> visited;
      std::size_t total{};

      for (const auto& collider : *colliders) {
        if (stop.stop_requested()) {
          return Core::fail(Core::ErrorCode::Unavailable, "Navigation bake cancelled");
        }

        const auto& body = collider.rigidBody;

        if (!body || body->motion != Physics::Motion::Static || body->sensor
            || body->shape != Physics::Shape::Mesh) {
          continue;
        }

        if (!body->meshAsset) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Navigation mesh requires asset path");
        }

        const auto& path = *body->meshAsset;

        if (!visited.insert(path).second) {
          continue;
        }

        if (visited.size() > 64) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Navigation mesh sources exceed 64 assets");
        }

        auto bytes = Physics::readCollisionMeshBytes(files, path);

        if (!bytes) {
          return std::unexpected(bytes.error());
        }

        if (bytes->size() > 64 * 1024 * 1024 - total) {
          return Core::fail(Core::ErrorCode::Unavailable, "Navigation mesh sources exceed 64 MiB");
        }

        total += bytes->size();
        auto mesh = Physics::decodeCollisionMesh(*bytes);

        if (!mesh) {
          return Core::fail(mesh.error().code,
                            "Navigation source '" + path + "': " + mesh.error().message);
        }

        meshes.push_back({path, std::move(*mesh)});
        result.inputs.push_back({path, std::move(*bytes)});
      }

      auto geometry = Navigation::sceneGeometry(*scene, settings, meshes, stop);
      auto surface = geometry
                         ? Navigation::Surface::build(*geometry, settings, stop)
                         : Core::Result<Navigation::Surface>{std::unexpected(geometry.error())};

      if (!surface) {
        return std::unexpected(surface.error());
      }

      if (stop.stop_requested()) {
        return Core::fail(Core::ErrorCode::Unavailable, "Navigation bake cancelled");
      }

      auto bytes = Navigation::encodeSurface(*surface);

      if (!bytes) {
        return std::unexpected(bytes.error());
      }

      result.surface = std::move(*surface);
      result.bytes = std::move(*bytes);
      return result;
    }
  }  // namespace

  auto decodeNavigationSettings(std::string_view text) -> Core::Result<Navigation::BuildSettings> {
    namespace Codec = Serialization::Detail;

    if (text.empty() || text.size() > 4096) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid navigation settings bytes");
    }

    try {
      const auto value = Codec::parseJson(text, 8);
      Codec::requireFields(value, {"cellSize", "cellHeight", "agentHeight", "agentRadius",
                                   "maxClimb", "maxSlopeDegrees", "tileCells"});

      if (!value["tileCells"].is_number_integer() || value["tileCells"] < 32
          || value["tileCells"] > 256) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Navigation tile size requires integer 32..256");
      }

      Navigation::BuildSettings settings{
          Codec::number(value["cellSize"]),       Codec::number(value["cellHeight"]),
          Codec::number(value["agentHeight"]),    Codec::number(value["agentRadius"]),
          Codec::number(value["maxClimb"]),       Codec::number(value["maxSlopeDegrees"]),
          value["tileCells"].get<std::uint32_t>()};
      auto valid = Navigation::validateSettings(settings);
      return valid ? Core::Result<Navigation::BuildSettings>{settings}
                   : std::unexpected(valid.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Navigation settings: " + std::string(error.what()));
    }
  }

  auto encodeNavigationSettings(const Navigation::BuildSettings& settings)
      -> Core::Result<std::string> {
    auto valid = Navigation::validateSettings(settings);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    return Serialization::Detail::Json{
        {"cellSize", settings.cellSize},       {"cellHeight", settings.cellHeight},
        {"agentHeight", settings.agentHeight}, {"agentRadius", settings.agentRadius},
        {"maxClimb", settings.maxClimb},       {"maxSlopeDegrees", settings.maxSlopeDegrees},
        {"tileCells", settings.tileCells}}
        .dump();
  }

  auto validateNavigationInputs(const Core::FileSystem& files, const NavigationCreation& creation)
      -> Core::Result<void> {
    if (files.root() != creation.root || creation.inputs.size() > 64) {
      return Core::fail(Core::ErrorCode::InvalidState, "Navigation source root/capacity mismatch");
    }

    std::size_t total{};
    std::set<std::string> seen;

    for (const auto& input : creation.inputs) {
      if (!seen.insert(input.path).second || input.bytes.size() > 64 * 1024 * 1024 - total) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid navigation source proof");
      }

      total += input.bytes.size();
      auto bytes = Physics::readCollisionMeshBytes(files, input.path);

      if (!bytes) {
        return std::unexpected(bytes.error());
      }

      if (*bytes != input.bytes) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Navigation source changed; bake again: " + input.path);
      }
    }

    return {};
  }

  struct NavigationBake::Impl {
    struct Completion {
      std::mutex m_mutex;
      std::optional<Core::Result<NavigationCreation>> m_result;
    };

    std::thread::id m_owner{std::this_thread::get_id()};
    bool m_taken{};
    std::shared_ptr<Completion> m_completion{std::make_shared<Completion>()};
    std::jthread m_worker;

    Impl(Core::FileSystem files, std::string initial, Navigation::BuildSettings settings)
        : m_worker([state = m_completion, files = std::move(files), initial = std::move(initial),
                    settings](std::stop_token stop) {
            auto result = [&]() -> Core::Result<NavigationCreation> {
              try {
                return prepare(files, initial, settings, stop);
              } catch (const std::exception& error) {
                return Core::fail(Core::ErrorCode::External,
                                  "Background navigation bake: " + std::string(error.what()));
              }
            }();
            std::lock_guard lock(state->m_mutex);
            state->m_result.emplace(std::move(result));
          }) {
    }

    auto check() const -> Core::Result<void> {
      return m_owner == std::this_thread::get_id()
                 ? Core::Result<void>{}
                 : Core::fail(Core::ErrorCode::InvalidState,
                              "Navigation bake requires owner thread");
    }
  };

  NavigationBake::NavigationBake(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  NavigationBake::NavigationBake(NavigationBake&&) noexcept = default;

  auto NavigationBake::operator=(NavigationBake&&) noexcept -> NavigationBake& = default;

  NavigationBake::~NavigationBake() = default;

  auto NavigationBake::begin(const Core::FileSystem& files, const World::Scene& scene,
                             const Navigation::BuildSettings& settings)
      -> Core::Result<NavigationBake> {
    auto valid = Navigation::validateSettings(settings);
    auto initial = World::encodeScene(scene);

    if (!valid || !initial) {
      return std::unexpected(!valid ? valid.error() : initial.error());
    }

    try {
      return NavigationBake{std::make_unique<Impl>(files, std::move(*initial), settings)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Begin navigation bake: " + std::string(error.what()));
    }
  }

  auto NavigationBake::ready() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Navigation bake was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    std::unique_lock lock(m_impl->m_completion->m_mutex, std::try_to_lock);
    return lock.owns_lock() && m_impl->m_completion->m_result.has_value();
  }

  auto NavigationBake::take() -> Core::Result<NavigationCreation> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Navigation bake was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_taken) {
      return Core::fail(Core::ErrorCode::InvalidState, "Navigation result was already taken");
    }

    std::unique_lock lock(m_impl->m_completion->m_mutex, std::try_to_lock);

    if (!lock.owns_lock() || !m_impl->m_completion->m_result) {
      return Core::fail(Core::ErrorCode::Unavailable, "Navigation bake is pending");
    }

    m_impl->m_taken = true;
    return std::move(*m_impl->m_completion->m_result);
  }

  auto NavigationBake::requestStop() noexcept -> void {
    if (m_impl) {
      m_impl->m_worker.request_stop();
    }
  }
}  // namespace Dot::Editor

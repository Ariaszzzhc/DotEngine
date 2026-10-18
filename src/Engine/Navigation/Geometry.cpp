module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

module Dot.Navigation.Geometry;

import Dot.Math;
import Dot.Core.Paths;
import Dot.Physics.Body;

namespace Dot::Navigation {
  namespace {
    constexpr std::size_t maxGeometry = 262144;

    auto solidStatic(const World::ColliderInfo& collider) -> bool {
      return collider.rigidBody && !collider.rigidBody->sensor
             && collider.rigidBody->motion == Physics::Motion::Static;
    }

    struct GeometrySignature {
      std::uint64_t entity;
      Math::Mat4 matrix;
      Physics::Shape shape;
      Math::Vec3 halfExtents{};
      float radius{}, halfHeight{};
      std::optional<std::string> mesh{};
      auto operator==(const GeometrySignature&) const -> bool = default;
    };

    auto signatures(const World::Scene& scene) -> Core::Result<std::vector<GeometrySignature>> {
      auto colliders = scene.colliders();

      if (!colliders) {
        return std::unexpected(colliders.error());
      }

      std::vector<GeometrySignature> result;

      for (const auto& collider : *colliders) {
        if (!solidStatic(collider)) {
          continue;
        }

        auto matrix = scene.worldTransform(collider.id);

        if (!matrix) {
          return std::unexpected(matrix.error());
        }

        const auto& body = *collider.rigidBody;
        GeometrySignature value{collider.id.value, *matrix, body.shape};

        if (body.shape == Physics::Shape::Box) {
          value.halfExtents = body.halfExtents;
        } else if (body.shape == Physics::Shape::Mesh) {
          value.mesh = body.meshAsset;
        } else {
          value.radius = body.radius;
          value.halfHeight = body.shape == Physics::Shape::Capsule ? body.halfHeight : 0;
        }

        result.push_back(std::move(value));
      }

      return result;
    }

    auto append(InputGeometry& target, const InputGeometry& source, const Math::Mat4& matrix,
                bool reflected = false) -> Core::Result<void> {
      if (source.vertices.size() > maxGeometry - target.vertices.size()
          || source.indices.size() > maxGeometry * 3 - target.indices.size()) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Navigation input geometry exceeds capacity");
      }

      const auto base = static_cast<std::uint32_t>(target.vertices.size());

      for (const auto vertex : source.vertices) {
        auto transformed = Math::transformPoint(matrix, vertex);

        if (!transformed) {
          return std::unexpected(transformed.error());
        }

        target.vertices.push_back(*transformed);
      }

      for (std::size_t index = 0; index < source.indices.size(); index += 3) {
        target.indices.push_back(base + source.indices[index]);
        target.indices.push_back(base + source.indices[index + (reflected ? 2 : 1)]);
        target.indices.push_back(base + source.indices[index + (reflected ? 1 : 2)]);
      }

      return {};
    }

    auto box(Math::Vec3 h) -> InputGeometry {
      return {{{-h.x, -h.y, -h.z},
               {h.x, -h.y, -h.z},
               {h.x, -h.y, h.z},
               {-h.x, -h.y, h.z},
               {-h.x, h.y, -h.z},
               {h.x, h.y, -h.z},
               {h.x, h.y, h.z},
               {-h.x, h.y, h.z}},
              {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
               1, 5, 6, 1, 6, 2, 2, 6, 7, 2, 7, 3, 3, 7, 4, 3, 4, 0}};
    }

    auto roundGeometry(float radius, float halfHeight, float error) -> Core::Result<InputGeometry> {
      for (std::uint32_t sectors = 8, rings = 2; sectors <= 256; sectors *= 2, rings *= 2) {
        InputGeometry result;
        result.vertices.push_back({0, 1, 0});
        const auto ring = [&](double latitude) {
          for (std::uint32_t sector = 0; sector < sectors; ++sector) {
            const double longitude = 2 * std::numbers::pi * sector / sectors;
            result.vertices.push_back(
                {static_cast<float>(std::sin(latitude) * std::cos(longitude)),
                 std::abs(std::cos(latitude)) < 1e-12 ? 0 : static_cast<float>(std::cos(latitude)),
                 static_cast<float>(std::sin(latitude) * std::sin(longitude))});
          }
        };

        for (std::uint32_t latitude = 1; latitude < rings * 2; ++latitude) {
          ring(std::numbers::pi * latitude / (rings * 2));

          if (latitude == rings && halfHeight > 0) {
            ring(std::numbers::pi / 2);
          }
        }

        const auto ringCount = static_cast<std::uint32_t>((result.vertices.size() - 1) / sectors);
        const auto bottom = static_cast<std::uint32_t>(result.vertices.size());
        result.vertices.push_back({0, -1, 0});

        for (std::uint32_t sector = 0; sector < sectors; ++sector) {
          const auto next = (sector + 1) % sectors;
          result.indices.insert(result.indices.end(), {0, 1 + next, 1 + sector});

          for (std::uint32_t row = 0; row + 1 < ringCount; ++row) {
            const auto a = 1 + row * sectors + sector;
            const auto b = 1 + row * sectors + next;
            const auto c = b + sectors;
            const auto d = a + sectors;
            result.indices.insert(result.indices.end(), {a, b, c, a, c, d});
          }

          result.indices.insert(result.indices.end(),
                                {bottom, bottom - sectors + sector, bottom - sectors + next});
        }

        double minimum = 1;

        for (std::size_t index = 0; index < result.indices.size(); index += 3) {
          const auto a = result.vertices[result.indices[index]];
          const auto b = result.vertices[result.indices[index + 1]];
          const auto c = result.vertices[result.indices[index + 2]];
          /** The duplicated equator has no spherical facet; axial offsets create it later.
           * 重复赤道此时没有球面面片，轴向偏移后才形成圆柱面。 */
          if (halfHeight > 0 && a.y == 0 && b.y == 0 && c.y == 0) {
            continue;
          }

          const auto u = b - a;
          const auto v = c - a;
          const Math::Vec3 cross{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z,
                                 u.x * v.y - u.y * v.x};
          const auto length = Math::length(cross);

          if (length > 0) {
            minimum = std::min(minimum, Math::dot(cross, a) / length);
          }
        }

        if (minimum <= 0 || !std::isfinite(minimum)) {
          return Core::fail(Core::ErrorCode::InvalidState, "Invalid round navigation tessellation");
        }

        /** Round outward after bounding float facet/normal error; retain the precision limit.
         * 在浮点面片/法线误差界外向舍入，同时保留精度上限。 */
        const auto expanded = std::nextafter(
            static_cast<float>(radius / minimum * (1 + 8 * std::numeric_limits<float>::epsilon())),
            std::numeric_limits<float>::infinity());

        if (double(expanded) - radius > error) {
          continue;
        }

        const auto upperVertices = 1 + rings * sectors;

        for (std::size_t index = 0; index < result.vertices.size(); ++index) {
          auto& value = result.vertices[index];
          const auto offset = index < upperVertices ? halfHeight : -halfHeight;
          value = value * expanded;
          value.y += offset;
        }

        return result;
      }

      return Core::fail(Core::ErrorCode::Unavailable,
                        "Round collider navigation precision exceeds tessellation capacity");
    }
  }  // namespace

  struct GeometryState::Data {
    std::vector<GeometrySignature> values;
  };

  GeometryState::GeometryState(std::shared_ptr<const Data> data) noexcept
      : m_data(std::move(data)) {
  }

  auto GeometryState::capture(const World::Scene& scene) -> Core::Result<GeometryState> {
    try {
      auto values = signatures(scene);
      return values ? Core::Result<GeometryState>{GeometryState{
                          std::make_shared<const Data>(Data{std::move(*values)})}}
                    : std::unexpected(values.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Capture navigation geometry state: " + std::string(error.what()));
    }
  }

  auto GeometryState::unchanged(const World::Scene& scene) const -> Core::Result<bool> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Navigation geometry state is empty/moved");
    }

    try {
      auto values = signatures(scene);
      return values ? Core::Result<bool>{*values == m_data->values}
                    : std::unexpected(values.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Compare navigation geometry state: " + std::string(error.what()));
    }
  }

  auto sceneGeometry(const World::Scene& scene, const BuildSettings& settings,
                     std::span<const Physics::MeshAsset> meshes, std::stop_token stop)
      -> Core::Result<InputGeometry> {
    auto valid = validateSettings(settings);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto colliders = scene.colliders();

    if (!colliders) {
      return std::unexpected(colliders.error());
    }

    if (meshes.size() > 64) {
      return Core::fail(Core::ErrorCode::Unavailable,
                        "Navigation collision assets exceed capacity");
    }

    try {
      std::set<std::string> paths;
      std::size_t bytes{};

      for (const auto& mesh : meshes) {
        if (mesh.path.size() > 1024 || !Core::isPortableRelativePath(mesh.path)
            || !mesh.path.starts_with("assets/") || !mesh.path.ends_with(".dotmesh")
            || !paths.insert(mesh.path).second || mesh.value.bytes() > 64 * 1024 * 1024 - bytes) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid/duplicate navigation collision assets");
        }

        auto validMesh = mesh.value.view();

        if (!validMesh) {
          return std::unexpected(validMesh.error());
        }

        bytes += mesh.value.bytes();
      }

      InputGeometry result;

      for (const auto& collider : *colliders) {
        if (stop.stop_requested()) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Navigation geometry preparation cancelled");
        }

        if (!solidStatic(collider)) {
          continue;
        }

        auto world = scene.worldTransform(collider.id);

        if (!world) {
          return std::unexpected(world.error());
        }

        auto pose = Math::decomposeTrs(*world);

        if (!pose) {
          return std::unexpected(pose.error());
        }

        Physics::TriangleMesh mesh;

        if (collider.rigidBody->shape == Physics::Shape::Mesh) {
          const auto& path = collider.rigidBody->meshAsset;
          const auto found
              = path ? std::ranges::find(meshes, *path, &Physics::MeshAsset::path) : meshes.end();

          if (found == meshes.end()) {
            return Core::fail(Core::ErrorCode::NotFound,
                              "Navigation static collision mesh was not prepared");
          }

          mesh = found->value;
        }

        auto config = Physics::scaledBodyConfig(*collider.rigidBody, *pose, std::move(mesh));

        if (!config) {
          return Core::fail(config.error().code, "Navigation collider: " + config.error().message);
        }

        auto matrix = Math::compose(config->position, config->rotation, config->meshScale);

        if (!matrix) {
          return std::unexpected(matrix.error());
        }

        InputGeometry local;
        bool reflected{};

        if (config->shape == Physics::Shape::Mesh) {
          auto view = config->mesh.view();

          if (!view) {
            return std::unexpected(view.error());
          }

          local.vertices.assign(view->vertices.begin(), view->vertices.end());
          local.indices.assign(view->indices.begin(), view->indices.end());
          reflected = config->meshScale.x * config->meshScale.y * config->meshScale.z < 0;
        } else if (config->shape == Physics::Shape::Box) {
          local = box(config->halfExtents);
        } else {
          auto round = roundGeometry(
              config->radius, config->shape == Physics::Shape::Capsule ? config->halfHeight : 0,
              settings.cellSize * .05F);

          if (!round) {
            return std::unexpected(round.error());
          }

          local = std::move(*round);
        }

        auto added = append(result, local, *matrix, reflected);

        if (!added) {
          return std::unexpected(added.error());
        }
      }

      auto identity = geometryIdentity(result);
      return identity ? Core::Result<InputGeometry>{std::move(result)}
                      : std::unexpected(identity.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare navigation geometry: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Navigation

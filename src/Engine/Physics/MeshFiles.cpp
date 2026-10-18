module;

#include <algorithm>
#include <exception>
#include <expected>
#include <filesystem>
#include <numeric>
#include <set>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

module Dot.Physics.MeshFiles;

import Dot.Core.Paths;
import Dot.Math;
import Dot.Physics.Body;
import Dot.Assets.Mesh;

namespace Dot::Physics {
  auto readCollisionMeshBytes(const Core::FileSystem& files, std::string_view source)
      -> Core::Result<std::vector<std::byte>> {
    if (source.size() > 1024 || !Core::isPortableRelativePath(source)
        || !source.starts_with("assets/") || !source.ends_with(".dotmesh")) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Collision mesh requires a portable assets/ .dotmesh path");
    }

    try {
      const auto path = std::filesystem::path{std::u8string(source.begin(), source.end())};
      auto current = files.root();
      std::size_t depth{};
      std::size_t entries{};

      for (const auto& part : path) {
        if (++depth > 32) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Collision mesh path exceeds 32 components");
        }

        std::error_code listingError;
        std::filesystem::directory_iterator listing(current, listingError);

        if (listingError) {
          return Core::fail(Core::ErrorCode::Io,
                            "Read collision mesh directory: " + listingError.message());
        }

        bool exact{};

        for (const auto& entry : listing) {
          if (++entries > 65536) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Collision mesh path exceeds 65536 lookup entries");
          }

          if (entry.path().filename() == part) {
            exact = true;
            break;
          }
        }

        if (!exact) {
          return Core::fail(
              Core::ErrorCode::NotFound,
              "Collision mesh path is missing or has different spelling: " + std::string(source));
        }

        current /= part;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(current, error);
        const bool final = current == files.root() / path;

        if (error || std::filesystem::is_symlink(status)
            || (final ? !std::filesystem::is_regular_file(status)
                      : !std::filesystem::is_directory(status))) {
          return Core::fail(
              Core::ErrorCode::Io,
              "Collision mesh path is missing, inaccessible, a symlink or special file: "
                  + std::string(source));
        }
      }

      return files.readBytes(path);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Load collision mesh '" + std::string(source) + "': " + error.what());
    }
  }

  auto decodeCollisionMesh(std::span<const std::byte> bytes) -> Core::Result<TriangleMesh> {
    try {
      auto loaded = Assets::decodeMesh(bytes, Assets::MeshFormat::DotMesh);

      if (!loaded) {
        return Core::fail(loaded.error().code, "Decode collision mesh: " + loaded.error().message);
      }

      if (loaded->data.vertices.size() > 262144 || loaded->data.indices.size() > 262144 * 3) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Collision mesh exceeds vertex/triangle capacity");
      }

      MeshDescription description;
      description.vertices.reserve(loaded->data.vertices.size());

      for (const auto& vertex : loaded->data.vertices) {
        description.vertices.push_back(vertex.position);
      }

      description.indices = std::move(loaded->data.indices);

      if (description.indices.empty()) {
        description.indices.resize(description.vertices.size());
        std::iota(description.indices.begin(), description.indices.end(), std::uint32_t{0});
      }

      auto mesh = TriangleMesh::create(std::move(description));
      return mesh ? std::move(mesh)
                  : Core::fail(mesh.error().code, "Decode collision mesh: " + mesh.error().message);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Decode collision mesh: " + std::string(error.what()));
    }
  }

  auto loadCollisionMesh(const Core::FileSystem& files, std::string_view source)
      -> Core::Result<TriangleMesh> {
    auto bytes = readCollisionMeshBytes(files, source);

    if (!bytes) {
      return std::unexpected(bytes.error());
    }

    auto mesh = decodeCollisionMesh(*bytes);
    return mesh ? std::move(mesh)
                : Core::fail(mesh.error().code, "Collision mesh '" + std::string(source)
                                                    + "': " + mesh.error().message);
  }

  auto loadSceneCollisionMeshes(const Core::FileSystem& files, const Dot::World::Scene& scene,
                                std::stop_token stop) -> Core::Result<std::vector<MeshAsset>> {
    auto entities = scene.entities();

    if (!entities) {
      return std::unexpected(entities.error());
    }

    try {
      std::vector<MeshAsset> meshes;
      std::set<std::string> visited;
      std::size_t bytes{};
      std::size_t triangles{};

      for (const auto& entity : *entities) {
        if (stop.stop_requested()) {
          return Core::fail(Core::ErrorCode::Unavailable, "Collision mesh preparation cancelled");
        }

        if (!entity.rigidBody || entity.rigidBody->shape != Shape::Mesh) {
          continue;
        }

        const auto& path = *entity.rigidBody->meshAsset;

        if (visited.insert(path).second) {
          if (visited.size() > 64) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Scene collision meshes exceed 64 assets");
          }

          auto mesh = loadCollisionMesh(files, path);

          if (!mesh) {
            return std::unexpected(mesh.error());
          }

          bytes += mesh->bytes();

          if (bytes > 64 * 1024 * 1024) {
            return Core::fail(Core::ErrorCode::Unavailable, "Scene collision meshes exceed 64 MiB");
          }

          meshes.push_back({path, std::move(*mesh)});
        }

        const auto found = std::ranges::find(meshes, path, &MeshAsset::path);
        auto matrix = scene.worldTransform(entity.id);

        if (!matrix) {
          return std::unexpected(matrix.error());
        }

        auto pose = Math::decomposeTrs(*matrix);

        if (!pose) {
          return std::unexpected(pose.error());
        }

        auto scaled = found->value.validateScale(pose->scale);

        if (!scaled) {
          return Core::fail(scaled.error().code,
                            "Collision mesh '" + path + "': " + scaled.error().message);
        }

        triangles += found->value.triangles();

        if (triangles > 1048576) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Scene exceeds 1048576 collision triangles");
        }
      }

      return meshes;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare scene collision meshes: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Physics

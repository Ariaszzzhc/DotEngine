module;

#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>

module Dot.Assets.SceneSkins;

import Dot.Animation.SceneSkin;
import Dot.Animation.SkinFiles;
import Dot.Assets.Mesh;
import Dot.Graphics.Geometry;
import Dot.Graphics.Skinning;

namespace Dot::Assets {
  auto validateSceneSkins(const Core::FileSystem& files, const World::Scene& scene)
      -> Core::Result<void> {
    auto entities = scene.entities();

    if (!entities) {
      return std::unexpected(entities.error());
    }

    try {
      std::unordered_map<std::string, Animation::Skin> skins;
      std::unordered_map<std::string, Graphics::MeshData> meshes;
      std::uint64_t vertices{};

      for (const auto& entity : *entities) {
        if (!entity.skin || !entity.skin->enabled) {
          continue;
        }

        const auto validate = [&]() -> Core::Result<void> {
          const auto path = [](const std::string& text) {
            return std::filesystem::path(std::u8string(text.begin(), text.end()));
          };
          auto skin = skins.find(entity.skin->asset);

          if (skin == skins.end()) {
            auto loaded = Animation::loadSkin(files, path(entity.skin->asset));

            if (!loaded) {
              return std::unexpected(loaded.error());
            }

            skin = skins.emplace(entity.skin->asset, std::move(*loaded)).first;
          }

          vertices += skin->second.description().value()->vertices.size();

          if (vertices > 4'000'000) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Scene skins exceed four million influenced vertices");
          }

          auto binding = Animation::SceneSkin::bind(scene, entity.id, skin->second);

          if (!binding) {
            return std::unexpected(binding.error());
          }

          const auto meshKey = entity.mesh->kind == World::MeshKind::Asset
                                   ? "asset:" + entity.mesh->asset
                               : entity.mesh->kind == World::MeshKind::Cube ? "builtin:cube"
                                                                            : "builtin:quad";
          auto mesh = meshes.find(meshKey);

          if (mesh == meshes.end()) {
            auto geometry
                = entity.mesh->kind == World::MeshKind::Asset
                      ? Assets::loadMesh(files, path(entity.mesh->asset)).transform([](auto value) {
                          return std::move(value.data);
                        })
                  : entity.mesh->kind == World::MeshKind::Cube ? Graphics::cube()
                                                               : Graphics::quad();

            if (!geometry) {
              return std::unexpected(geometry.error());
            }

            mesh = meshes.emplace(meshKey, std::move(*geometry)).first;
          }

          auto palette = binding->palette(scene);

          if (!palette) {
            return std::unexpected(palette.error());
          }

          auto deformed = Graphics::deformMesh(mesh->second, skin->second, *palette);
          return deformed ? Core::Result<void>{} : std::unexpected(deformed.error());
        };
        auto valid = validate();

        if (!valid) {
          return Core::fail(
              valid.error().code,
              "Scene skin owner " + std::to_string(entity.id.value) + ": " + valid.error().message);
        }
      }

      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Validate scene skins: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Assets

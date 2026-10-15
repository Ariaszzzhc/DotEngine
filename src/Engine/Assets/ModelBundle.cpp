module;

#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include "../Serialization/Json.hpp"

module Dot.Assets.ModelBundle;

import Dot.Animation.Files;
import Dot.Core.Paths;
import Dot.Core.Text;
import Dot.World.SceneFiles;
import Dot.Assets.SceneSkins;
import Dot.Assets.Material;

namespace Dot::Assets {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;

    template <class T>
    auto take(Core::Result<T> result) -> T {
      if (!result) {
        throw result.error();
      }

      return std::move(*result);
    }

    auto regular(const Core::FileSystem& files, const std::filesystem::path& relative) -> void {
      auto current = files.root();

      for (const auto& component : relative) {
        current /= component;
        const auto status = std::filesystem::symlink_status(current);

        if (std::filesystem::is_symlink(status)
            || (current == files.root() / relative ? !std::filesystem::is_regular_file(status)
                                                   : !std::filesystem::is_directory(status))) {
          throw std::invalid_argument("Model bundle path is missing, a link or special file");
        }
      }
    }
  }  // namespace

  auto loadModelBundle(const Core::FileSystem& files, const std::filesystem::path& descriptor)
      -> Core::Result<ModelInstance> {
    try {
      const auto bytes = descriptor.generic_u8string();
      const std::string path(bytes.begin(), bytes.end());

      if (!Core::isPortableRelativePath(path) || !path.starts_with("assets/")
          || !path.ends_with(".dotmodel")) {
        throw std::invalid_argument("Model bundle requires a portable assets .dotmodel path");
      }

      regular(files, descriptor);
      const auto source = take(files.readText(descriptor, 1024 * 1024));

      if (source.find('\0') != std::string::npos || !Core::isValidUtf8(source)) {
        throw std::invalid_argument("Model bundle requires UTF-8 without NUL");
      }

      const auto root = Codec::parseJson(source);
      Codec::requireFields(root, {"scene", "animations", "nodes"});
      const auto parent = path.substr(0, path.find_last_of('/') + 1);

      if (root["scene"] != parent + "model.dotscene" || !root["nodes"].is_array()
          || root["nodes"].empty() || root["nodes"].size() > 10000 || !root["animations"].is_array()
          || root["animations"].size() > 1024) {
        throw std::invalid_argument("Invalid model bundle scene/nodes/animations");
      }

      const auto sceneName = parent + "model.dotscene";
      const auto scenePath
          = std::filesystem::path(std::u8string(sceneName.begin(), sceneName.end()));
      regular(files, scenePath);
      ModelInstance result{take(World::loadScene(files, scenePath)), {}, {}};
      std::set<std::uint64_t> ids;

      for (std::size_t i = 0; i < root["nodes"].size(); ++i) {
        const auto id = Codec::id(root["nodes"][i]);

        if (!ids.insert(id).second) {
          throw std::invalid_argument("Duplicate model node binding");
        }

        result.bindings.push_back(
            {static_cast<std::uint32_t>(i), take(result.scene.findEntity(id))});
      }

      std::size_t clipBytes{}, tracks{}, keys{};

      for (std::size_t i = 0; i < root["animations"].size(); ++i) {
        const auto expected = std::format("{}animations/{}.dotanimation", parent, i);

        if (root["animations"][i] != expected) {
          throw std::invalid_argument("Model clips require ordered paths inside this bundle");
        }

        const auto relative
            = std::filesystem::path(std::u8string(expected.begin(), expected.end()));
        regular(files, relative);
        auto encoded = take(files.readText(relative, 64 * 1024 * 1024 - clipBytes));
        clipBytes += encoded.size();
        auto clip = take(Animation::decodeClip(encoded));
        const auto& description = *take(clip.description());

        if (description.tracks.size() > 30000 - tracks) {
          throw std::invalid_argument("Model clip track budget exceeded");
        }

        tracks += description.tracks.size();

        for (const auto& track : description.tracks) {
          if (track.node >= result.bindings.size() || track.keys.size() > 1000000 - keys) {
            throw std::invalid_argument("Model clip target/key budget exceeded");
          }

          keys += track.keys.size();
        }

        result.animations.push_back(std::move(clip));
      }

      for (const auto& entity : take(result.scene.entities())) {
        const auto contained = [&](const std::string& name, const char* directory) {
          if (!name.starts_with(parent + directory)) {
            throw std::invalid_argument("Model asset references a foreign bundle path");
          }

          regular(files, std::filesystem::path(std::u8string(name.begin(), name.end())));
        };

        if (entity.mesh && entity.mesh->kind == World::MeshKind::Asset) {
          contained(entity.mesh->asset, "meshes/");
        }

        if (entity.skin) {
          contained(entity.skin->asset, "skins/");
        }

        if (entity.material) {
          contained(entity.material->asset, "materials/");
          const auto& name = entity.material->asset;
          const auto material = take(Assets::loadMaterial(
              files, std::filesystem::path(std::u8string(name.begin(), name.end()))));

          for (const auto* texture :
               {&material.baseColorTexture, &material.metallicRoughnessTexture,
                &material.normalTexture, &material.emissiveTexture, &material.occlusionTexture}) {
            if (*texture) {
              contained((*texture)->path, "images/");
            }
          }
        }
      }

      auto skins = Assets::validateSceneSkins(files, result.scene);

      if (!skins) {
        throw skins.error();
      }

      return result;
    } catch (const Core::Error& error) {
      return Core::fail(error.code, "Load model bundle: " + error.message);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Load model bundle: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Assets

module;

#include <algorithm>
#include <array>
#include <exception>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.Project.GamePackage;

import Dot.Physics.MeshFiles;

import Dot.Core.Paths;
import Dot.Core.StagedDirectory;
import Dot.Core.Text;
import Dot.World.SceneFiles;
import Dot.Project.GameSession;
import Dot.Assets.Mesh;
import Dot.Assets.Material;
import Dot.Assets.Image;
import Dot.Graphics.Geometry;
import Dot.Graphics.Renderer;
import Dot.Audio.Clip;

namespace Dot::Project {
  namespace {
    namespace Fs = std::filesystem;
    using Json = Serialization::Detail::Json;

    auto path(std::string_view value) -> Fs::path {
      return Fs::path(std::u8string(value.begin(), value.end()));
    }

    auto utf(const Fs::path& value) -> std::string {
      const auto bytes = value.generic_u8string();
      return {bytes.begin(), bytes.end()};
    }

    auto folded(std::string value) -> std::string {
      for (auto& c : value) {
        if (c >= 'A' && c <= 'Z') {
          c = static_cast<char>(c - 'A' + 'a');
        }
      }

      return value;
    }

    auto valid(const PackageLimits& limits) -> bool {
      return limits.entries >= 1 && limits.entries <= 65536 && limits.fileBytes >= 1
             && limits.fileBytes <= 64 * 1024 * 1024 && limits.totalBytes >= 1
             && limits.totalBytes <= 1024ULL * 1024 * 1024 && limits.depth >= 1
             && limits.depth <= 128;
    }

    auto number(const Json& value) -> std::uint64_t {
      if (!value.is_number_unsigned()) {
        throw std::invalid_argument("Package counts require unsigned integers");
      }

      return value.get<std::uint64_t>();
    }

    auto error(std::string_view operation, const std::exception& value)
        -> std::unexpected<Core::Error> {
      return Core::fail(dynamic_cast<const Fs::filesystem_error*>(&value)
                            ? Core::ErrorCode::Io
                            : Core::ErrorCode::External,
                        std::string(operation) + ": " + value.what());
    }

    auto parentFor(const Core::FileSystem& files, const Fs::path& relative) -> Core::Result<void> {
      return files.createDirectories(relative.parent_path().empty() ? Fs::path{"."}
                                                                    : relative.parent_path());
    }
  }  // namespace

  struct GamePackage::Impl {
    Core::StagedDirectory storage;
    Core::FileSystem content;
    Description description;
    std::string scene;
    Script::CompiledProgram program;
  };

  GamePackage::GamePackage(std::shared_ptr<const Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  GamePackage::GamePackage(const GamePackage&) noexcept = default;

  auto GamePackage::operator=(const GamePackage&) noexcept -> GamePackage& = default;

  GamePackage::GamePackage(GamePackage&&) noexcept = default;

  auto GamePackage::operator=(GamePackage&&) noexcept -> GamePackage& = default;

  GamePackage::~GamePackage() = default;

  auto GamePackage::publish(const Project& project, const World::Scene& scene,
                            const Script::TypeScriptCompiler& compiler,
                            const Core::FileSystem& parent, const Fs::path& folder,
                            const PackageLimits& limits, std::optional<std::string> scenePath)
      -> Core::Result<Core::FileSystem> {
    if (!valid(limits) || folder.filename() != folder
        || !Core::isPortableRelativePath(utf(folder))) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid package limits/destination name");
    }

    try {
      auto destination = parent.resolve(folder);

      if (!destination) {
        return std::unexpected(destination.error());
      }

      if (Fs::exists(Fs::symlink_status(*destination))) {
        return Core::fail(Core::ErrorCode::InvalidState, "Game package destination already exists");
      }

      auto files = project.files();
      auto description = project.description();
      auto encoded = World::encodeScene(scene);

      if (!files || !description || !encoded) {
        return std::unexpected(!files         ? files.error()
                               : !description ? description.error()
                                              : encoded.error());
      }

      if (scenePath) {
        if (!Core::isPortableRelativePath(*scenePath) || !scenePath->ends_with(".dotscene")) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Export scene path requires a portable relative .dotscene file");
        }

        for (const auto& part : path(*scenePath)) {
          if (utf(part).starts_with(".dot-stage-")) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Export scene path uses an engine staging directory");
          }
        }

        description->startScene = std::move(*scenePath);
      }

      auto program = compiler.compileProject(*files, description->scriptsDirectory);

      if (!program) {
        return std::unexpected(program.error());
      }

      auto staging = Core::StagedDirectory::create(parent);

      if (!staging) {
        return std::unexpected(staging.error());
      }

      auto output = staging->files().value();
      auto published = program->publish(output, "code");

      if (!published) {
        return std::unexpected(published.error());
      }

      auto directory = output.createDirectories("content");

      if (!directory) {
        return std::unexpected(directory.error());
      }

      auto content = Core::FileSystem::open(output.root() / "content").value();
      std::vector<std::string> names;
      std::unordered_set<std::string> aliases;
      std::size_t entries = 0;
      const auto scriptRoot = path(description->scriptsDirectory);

      for (auto iterator = Fs::recursive_directory_iterator(files->root());
           iterator != Fs::recursive_directory_iterator{}; ++iterator) {
        const auto relative = iterator->path().lexically_relative(files->root());
        const auto name = utf(relative);
        const auto filename = utf(iterator->path().filename());

        if (filename.starts_with(".dot-stage-")) {
          iterator.disable_recursion_pending();
          continue;
        }

        if (++entries > limits.entries
            || static_cast<std::size_t>(iterator.depth()) > limits.depth) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Game resource entry/depth limit exceeded");
        }

        const auto status = iterator->symlink_status();

        if (!Core::isPortableRelativePath(name) || Fs::is_symlink(status)
            || (!Fs::is_directory(status) && !Fs::is_regular_file(status))
            || !aliases.insert(folded(name)).second) {
          return Core::fail(
              Core::ErrorCode::InvalidArgument,
              "Game resources contain non-portable paths, aliases, symlinks or special files");
        }

        if (Fs::is_directory(status)) {
          if (relative == scriptRoot / "dot") {
            iterator.disable_recursion_pending();
          }

          continue;
        }

        if (name == "project.dotproject" || name == description->startScene) {
          continue;
        }

        const auto extension = relative.extension();

        if (extension == ".ts" || extension == ".mts" || extension == ".map") {
          continue;
        }

        names.push_back(name);
      }

      std::ranges::sort(names);
      names.push_back(description->startScene);
      std::ranges::sort(names);
      Json manifestFiles = Json::array();
      std::uint64_t bytes = 0;

      for (const auto& name : names) {
        const auto relative = path(name);
        auto made = parentFor(content, relative);

        if (!made) {
          return std::unexpected(made.error());
        }

        if (name == description->startScene) {
          if (encoded->size() > limits.fileBytes || encoded->size() > limits.totalBytes - bytes) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Game scene exceeds resource byte limits");
          }

          auto written = content.writeTextAtomic(relative, *encoded);

          if (!written) {
            return std::unexpected(written.error());
          }

          bytes += encoded->size();
          manifestFiles.push_back({{"path", name}, {"bytes", encoded->size()}});
        } else {
          auto source = files->readBytes(
              relative,
              static_cast<std::size_t>(std::min(limits.fileBytes, limits.totalBytes - bytes)));

          if (!source) {
            return std::unexpected(source.error());
          }

          auto written = content.writeAtomic(relative, *source);

          if (!written) {
            return std::unexpected(written.error());
          }

          bytes += source->size();
          manifestFiles.push_back({{"path", name}, {"bytes", source->size()}});
        }
      }

      description->hotReload = false;
      auto metadata = encodeDescription(*description).value();
      const auto manifest = Json{
          {"version", 1},
          {"description", Serialization::Detail::parseJson(metadata)},
          {"bytes", bytes},
          {"files",
           manifestFiles}}.dump();

      if (manifest.size() > 1024 * 1024) {
        return Core::fail(Core::ErrorCode::Unavailable, "Game manifest exceeds 1 MiB");
      }

      auto written = output.writeTextAtomic("game.json", manifest);

      if (!written) {
        return std::unexpected(written.error());
      }

      auto checked = GamePackage::open(output, limits);

      if (!checked) {
        return Core::fail(checked.error().code,
                          "Staged game package validation: " + checked.error().message);
      }

      auto validationScene = checked->scene();

      if (!validationScene) {
        return std::unexpected(validationScene.error());
      }

      auto collisionMeshes
          = Physics::loadSceneCollisionMeshes(checked->files().value(), *validationScene);

      if (!collisionMeshes) {
        return Core::fail(collisionMeshes.error().code,
                          "Export collision meshes: " + collisionMeshes.error().message);
      }

      auto environment = validationScene->environment();

      if (!environment) {
        return std::unexpected(environment.error());
      }

      if (*environment) {
        const auto& settings = **environment;
        auto image = Assets::loadHdrImage(checked->files().value(), path(settings.asset));

        if (!image) {
          return Core::fail(image.error().code,
                            "Export environment " + settings.asset + ": " + image.error().message);
        }

        auto valid = Graphics::validateEnvironment(*image, settings.processing);

        if (!valid) {
          return Core::fail(valid.error().code,
                            "Export environment " + settings.asset + ": " + valid.error().message);
        }
      }

      std::unordered_set<std::string> meshes;
      std::unordered_map<std::string, Assets::MaterialDescription> materials;
      std::unordered_map<std::string, Graphics::MeshSurface> surfaces;
      std::unordered_set<std::string> images;
      std::unordered_set<std::string> clips;

      for (const auto& entity : validationScene->entities().value()) {
        if (entity.mesh && entity.mesh->kind == World::MeshKind::Asset
            && meshes.insert(entity.mesh->asset).second) {
          auto mesh = Assets::loadMesh(checked->files().value(), path(entity.mesh->asset));

          if (!mesh) {
            return Core::fail(mesh.error().code,
                              "Export mesh " + entity.mesh->asset + ": " + mesh.error().message);
          }
        }

        if (entity.material) {
          auto cached = materials.find(entity.material->asset);

          if (cached == materials.end()) {
            auto loaded
                = Assets::loadMaterial(checked->files().value(), path(entity.material->asset));

            if (!loaded) {
              return Core::fail(loaded.error().code, "Export material " + entity.material->asset
                                                         + ": " + loaded.error().message);
            }

            cached = materials.emplace(entity.material->asset, std::move(*loaded)).first;
          }

          const auto* material = &cached->second;

          if (entity.mesh && material->model == Graphics::SurfaceModel::MetallicRoughness) {
            const auto key = entity.mesh->kind == World::MeshKind::Cube   ? "builtin:cube"
                             : entity.mesh->kind == World::MeshKind::Quad ? "builtin:quad"
                                                                          : entity.mesh->asset;
            auto analyzed = surfaces.find(key);

            if (analyzed == surfaces.end()) {
              auto geometry = entity.mesh->kind == World::MeshKind::Cube ? Graphics::cube()
                                                                         : Graphics::quad();

              if (entity.mesh->kind == World::MeshKind::Asset) {
                auto loaded = Assets::loadMesh(checked->files().value(), path(entity.mesh->asset));

                if (!loaded) {
                  return std::unexpected(loaded.error());
                }

                geometry = std::move(loaded->data);
              }

              if (!geometry) {
                return std::unexpected(geometry.error());
              }

              auto surface = Graphics::meshSurface(*geometry);

              if (!surface) {
                return std::unexpected(surface.error());
              }

              analyzed = surfaces.emplace(key, *surface).first;
            }

            if (!analyzed->second.hasNormals
                || (material->normalTexture && !analyzed->second.hasNormalMapFrame)) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Export physical mesh lacks required normals/UVs: " + entity.name);
            }
          }

          const std::array sources{&material->baseColorTexture, &material->metallicRoughnessTexture,
                                   &material->normalTexture, &material->emissiveTexture,
                                   &material->occlusionTexture};

          for (std::size_t index = 0; index < sources.size(); ++index) {
            if (!*sources[index]) {
              continue;
            }

            const auto& source = **sources[index];
            auto found = images.find(source.path);

            if (found == images.end()) {
              auto image = Assets::loadImage(checked->files().value(), path(source.path));

              if (!image) {
                return Core::fail(image.error().code,
                                  "Export texture " + source.path + ": " + image.error().message);
              }

              images.insert(source.path);
            }
          }
        }

        if (entity.audioSource && clips.insert(entity.audioSource->asset).second) {
          auto clip = Audio::loadClip(checked->files().value(), path(entity.audioSource->asset));

          if (!clip) {
            return Core::fail(clip.error().code, "Export audio " + entity.audioSource->asset + ": "
                                                     + clip.error().message);
          }
        }
      }

      auto session
          = GameSession::createCompiled(checked->files().value(), checked->description().value(),
                                        *validationScene, checked->program().value());

      if (!session) {
        return Core::fail(session.error().code,
                          "Export runtime preparation: " + session.error().message);
      }

      auto closed = session->close();

      if (!closed) {
        return std::unexpected(closed.error());
      }

      return staging->publish(folder);
    } catch (const std::exception& value) {
      return error("Publish game package", value);
    }
  }

  auto GamePackage::open(const Core::FileSystem& package, const PackageLimits& limits)
      -> Core::Result<GamePackage> {
    if (!valid(limits)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid game package limits");
    }

    try {
      if (!Fs::is_regular_file(Fs::symlink_status(package.root() / "game.json"))) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Game manifest must be a regular file");
      }

      auto source = package.readText("game.json", 1024 * 1024);

      if (!source) {
        return std::unexpected(source.error());
      }

      auto manifest = Serialization::Detail::parseJson(*source);
      Serialization::Detail::requireFields(manifest, {"version", "description", "bytes", "files"});
      auto description = decodeDescription(manifest["description"].dump());

      if (!description) {
        return std::unexpected(description.error());
      }

      if (number(manifest["version"]) != 1 || description->hotReload
          || !manifest["files"].is_array() || manifest["files"].empty()
          || manifest["files"].size() > limits.entries
          || number(manifest["bytes"]) > limits.totalBytes) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid game manifest format/count/size");
      }

      std::unordered_set<std::string> required;
      std::unordered_set<std::string> aliases;
      std::vector<std::pair<std::string, std::uint64_t>> records;
      std::uint64_t bytes = 0;

      for (const auto& entry : manifest["files"]) {
        Serialization::Detail::requireFields(entry, {"path", "bytes"});
        const auto name = entry["path"].get<std::string>();
        const auto size = number(entry["bytes"]);

        if (!Core::isPortableRelativePath(name) || !aliases.insert(folded(name)).second
            || (!records.empty() && records.back().first >= name) || size > limits.fileBytes
            || size > limits.totalBytes - bytes) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid/duplicate/unsorted game resource or size");
        }

        bytes += size;
        records.emplace_back(name, size);
        auto file = path(name);
        required.insert(name);

        for (auto parent = file.parent_path(); !parent.empty(); parent = parent.parent_path()) {
          required.insert(utf(parent));
        }
      }

      aliases.clear();

      for (const auto& name : required) {
        if (!aliases.insert(folded(name)).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Game resource directories collide by ASCII case");
        }
      }

      if (bytes != number(manifest["bytes"]) || !required.contains(description->startScene)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Game resource sizes/startup scene disagree with manifest");
      }

      for (const auto& entry : Fs::directory_iterator(package.root())) {
        const auto name = utf(entry.path().filename());

        if (name != "game.json" && name != "code" && name != "content") {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Unexpected game package root entry");
        }

        const auto status = entry.symlink_status();

        if (Fs::is_symlink(status) || (name != "game.json" && !Fs::is_directory(status))) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Game package contains a symlink or invalid root type");
        }
      }

      auto code = Core::FileSystem::open(package.root() / "code");
      auto content = Core::FileSystem::open(package.root() / "content");

      if (!code || !content) {
        return std::unexpected(code ? content.error() : code.error());
      }

      auto program = Script::CompiledProgram::loadPublished(*code);

      if (!program) {
        return std::unexpected(program.error());
      }

      std::size_t count = 0;

      for (auto iterator = Fs::recursive_directory_iterator(content->root());
           iterator != Fs::recursive_directory_iterator{}; ++iterator) {
        const auto name = utf(iterator->path().lexically_relative(content->root()));
        const auto status = iterator->symlink_status();

        if (++count > limits.entries || static_cast<std::size_t>(iterator.depth()) > limits.depth
            || !required.contains(name) || Fs::is_symlink(status)
            || (!Fs::is_directory(status) && !Fs::is_regular_file(status))) {
          return Core::fail(
              Core::ErrorCode::InvalidArgument,
              "Game resource tree is excessive or has unexpected/symlink/special entries");
        }
      }

      if (count != required.size()) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Game resource tree is missing entries");
      }

      auto temporary = Core::FileSystem::open(Fs::temp_directory_path()).value();
      auto storage = Core::StagedDirectory::create(temporary);

      if (!storage) {
        return std::unexpected(storage.error());
      }

      auto output = storage->files().value();

      for (const auto& [name, size] : records) {
        const auto file = path(name);
        auto data = content->readBytes(file, static_cast<std::size_t>(limits.fileBytes));

        if (!data) {
          return std::unexpected(data.error());
        }

        if (data->size() != size) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Game resource size changed after manifest");
        }

        auto directory = parentFor(output, file);

        if (!directory) {
          return std::unexpected(directory.error());
        }

        auto written = output.writeAtomic(file, *data);

        if (!written) {
          return std::unexpected(written.error());
        }
      }

      auto scene = output.readText(path(description->startScene), 64 * 1024 * 1024);

      if (!scene) {
        return std::unexpected(scene.error());
      }

      auto decoded = World::decodeScene(*scene);

      if (!decoded) {
        return std::unexpected(decoded.error());
      }

      return GamePackage(std::make_shared<Impl>(std::move(*storage), std::move(output),
                                                std::move(*description), std::move(*scene),
                                                std::move(*program)));
    } catch (const Json::exception& value) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Game package JSON: " + std::string(value.what()));
    } catch (const std::invalid_argument& value) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Game package: " + std::string(value.what()));
    } catch (const std::exception& value) {
      return error("Open game package", value);
    }
  }

  auto GamePackage::description() const -> Core::Result<Description> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game package was moved");
    }

    return m_impl->description;
  }

  auto GamePackage::files() const -> Core::Result<Core::FileSystem> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game package was moved");
    }

    return m_impl->content;
  }

  auto GamePackage::scene() const -> Core::Result<World::Scene> {
    return m_impl ? World::decodeScene(m_impl->scene)
                  : Core::fail(Core::ErrorCode::InvalidState, "Game package was moved");
  }

  auto GamePackage::program() const -> Core::Result<Script::CompiledProgram> {
    return m_impl ? Core::Result<Script::CompiledProgram>{m_impl->program}
                  : Core::fail(Core::ErrorCode::InvalidState, "Game package was moved");
  }
}  // namespace Dot::Project

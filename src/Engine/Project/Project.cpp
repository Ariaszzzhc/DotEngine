module;

#include <algorithm>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include "../Serialization/Json.hpp"

module Dot.Project;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Core.Text;
import Dot.Core.Paths;
import Dot.Core.StagedDirectory;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace Dot::Project {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    constexpr auto manifest = "project.dotproject";

    auto path(std::string_view text) -> std::filesystem::path {
      return std::filesystem::path{std::u8string(text.begin(), text.end())};
    }

    auto utf(const std::filesystem::path& value) -> std::string {
      const auto bytes = value.generic_u8string();
      return std::string(bytes.begin(), bytes.end());
    }

    auto regular(const Core::FileSystem& files, const std::filesystem::path& relative,
                 bool directory = false) -> Core::Result<void> {
      auto current = files.root();
      std::error_code error;

      for (const auto& part : relative) {
        current /= part;
        const auto status = std::filesystem::symlink_status(current, error);

        if (error || std::filesystem::is_symlink(status)) {
          return Core::fail(Core::ErrorCode::Io,
                            "Project path is missing, inaccessible or a symlink: " + utf(current));
        }
      }

      const auto status = std::filesystem::status(current, error);

      if (error
          || (directory ? !std::filesystem::is_directory(status)
                        : !std::filesystem::is_regular_file(status))) {
        return Core::fail(Core::ErrorCode::Io,
                          "Project path has the wrong file type: " + utf(current));
      }

      return {};
    }

    auto resources(const Core::FileSystem& files, const Description& description)
        -> Core::Result<void> {
      auto checked = validateDescription(description);

      if (!checked) {
        return checked;
      }

      const auto scripts = path(description.scriptsDirectory);

      for (const auto& file :
           {path(description.startScene), scripts / "dot/engine.ts", scripts / "dot/host.d.ts"}) {
        auto found = regular(files, file);

        if (!found) {
          return found;
        }
      }

      auto directory = regular(files, scripts, true);

      if (!directory) {
        return directory;
      }

      auto scene = World::loadScene(files, path(description.startScene));

      if (!scene) {
        return std::unexpected(scene.error());
      }

      for (const auto& file : {scripts / "dot/engine.ts", scripts / "dot/host.d.ts"}) {
        auto source = files.readText(file, 1024 * 1024);

        if (!source) {
          return std::unexpected(source.error());
        }

        if (source->empty() || !Core::isValidUtf8(*source)
            || source->find('\0') != std::string::npos) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Project SDK requires nonempty scalar UTF-8 source");
        }
      }

      return {};
    }

    auto integer(const Json& value, std::uint32_t maximum) -> std::uint32_t {
      if (!value.is_number_integer() || value.get<std::uint64_t>() == 0
          || value.get<std::uint64_t>() > maximum) {
        throw std::invalid_argument("Project integer is outside its valid range");
      }

      return value.get<std::uint32_t>();
    }
  }  // namespace

  auto validateDescription(const Description& description) -> Core::Result<void> {
    if (description.name.empty() || description.name.size() > 128
        || !Core::isValidUtf8(description.name)
        || std::ranges::any_of(description.name, [](unsigned char c) { return c < 32 || c == 127; })
        || !Core::isPortableRelativePath(description.startScene)
        || !description.startScene.ends_with(".dotscene")
        || !Core::isPortableRelativePath(description.scriptsDirectory) || description.width == 0
        || description.width > 8192 || description.height == 0 || description.height > 8192
        || description.fixedHz == 0 || description.fixedHz > 240) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid project name/paths/window/fixed rate");
    }

    return {};
  }

  auto encodeDescription(const Description& description) -> Core::Result<std::string> {
    auto checked = validateDescription(description);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      return Json{{"version", 1},
                  {"name", description.name},
                  {"startScene", description.startScene},
                  {"scriptsDirectory", description.scriptsDirectory},
                  {"window", {{"width", description.width}, {"height", description.height}}},
                  {"fixedHz", description.fixedHz},
                  {"hotReload", description.hotReload}}
                 .dump(2)
             + "\n";
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Encode project: " + std::string(error.what()));
    }
  }

  auto decodeDescription(std::string_view source) -> Core::Result<Description> {
    if (source.empty() || source.size() > 64 * 1024 || !Core::isValidUtf8(source)
        || source.find('\0') != std::string_view::npos) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid project JSON bytes");
    }

    try {
      auto value = Codec::parseJson(source, 8);
      Codec::requireFields(value, {"version", "name", "startScene", "scriptsDirectory", "window",
                                   "fixedHz", "hotReload"});
      Codec::requireFields(value["window"], {"width", "height"});

      if (integer(value["version"], 1) != 1) {
        throw std::invalid_argument("Unsupported project version");
      }

      Description result{value["name"].get<std::string>(),
                         value["startScene"].get<std::string>(),
                         value["scriptsDirectory"].get<std::string>(),
                         integer(value["window"]["width"], 8192),
                         integer(value["window"]["height"], 8192),
                         integer(value["fixedHz"], 240),
                         value["hotReload"].get<bool>()};
      auto checked = validateDescription(result);
      return checked ? Core::Result<Description>{std::move(result)}
                     : Core::Result<Description>{std::unexpected(checked.error())};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Project JSON: " + std::string(error.what()));
    }
  }

  Project::Project(Core::FileSystem files, Description description) noexcept
      : m_files(std::move(files)), m_description(std::move(description)) {
  }

  Project::Project(const Project&) = default;

  auto Project::operator=(const Project&) -> Project& = default;

  Project::Project(Project&&) noexcept = default;

  auto Project::operator=(Project&&) noexcept -> Project& = default;

  Project::~Project() = default;

  auto Project::open(const std::filesystem::path& root) -> Core::Result<Project> {
    auto files = Core::FileSystem::open(root);

    if (!files) {
      return std::unexpected(files.error());
    }

    auto type = regular(*files, manifest);

    if (!type) {
      return std::unexpected(type.error());
    }

    auto source = files->readText(manifest, 64 * 1024);

    if (!source) {
      return std::unexpected(source.error());
    }

    auto description = decodeDescription(*source);

    if (!description) {
      return std::unexpected(description.error());
    }

    auto valid = resources(*files, *description);
    return valid ? Core::Result<Project>{Project(std::move(*files), std::move(*description))}
                 : Core::Result<Project>{std::unexpected(valid.error())};
  }

  auto Project::create(const Core::FileSystem& parent, const std::filesystem::path& folder,
                       const Description& description, const Core::FileSystem& sdk)
      -> Core::Result<Project> {
    auto metadata = encodeDescription(description);
    const auto utf = folder.generic_u8string();
    const std::string name(utf.begin(), utf.end());

    if (!metadata || folder.filename() != folder || !Core::isPortableRelativePath(name)) {
      return metadata ? Core::Result<Project>{Core::fail(
                            Core::ErrorCode::InvalidArgument,
                            "Project folder must be one portable relative name")}
                      : Core::Result<Project>{std::unexpected(metadata.error())};
    }

    try {
      auto ownedDescription = description;
      auto engine = sdk.readText("dot/engine.ts", 1024 * 1024);
      auto host = sdk.readText("dot/host.d.ts", 1024 * 1024);

      if (!engine || !host) {
        return std::unexpected(engine ? host.error() : engine.error());
      }

      if (engine->empty() || host->empty() || !Core::isValidUtf8(*engine)
          || !Core::isValidUtf8(*host) || engine->find('\0') != std::string::npos
          || host->find('\0') != std::string::npos) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Template SDK requires nonempty scalar UTF-8 source");
      }

      auto staging = Core::StagedDirectory::create(parent);

      if (!staging) {
        return std::unexpected(staging.error());
      }

      auto files = staging->files();

      if (!files) {
        return std::unexpected(files.error());
      }

      const auto scripts = path(description.scriptsDirectory);
      const auto scenePath = path(description.startScene);

      for (const auto& directory :
           {scripts / "dot", scenePath.parent_path(), std::filesystem::path{"assets"}}) {
        if (!directory.empty()) {
          auto made = files->createDirectories(directory);

          if (!made) {
            return std::unexpected(made.error());
          }
        }
      }

      auto scene = World::Scene::create(description.name);

      if (!scene) {
        return std::unexpected(scene.error());
      }

      auto camera = scene->createEntity("Camera");
      auto cube = scene->createEntity("Cube");

      if (!camera || !cube) {
        return std::unexpected(camera ? cube.error() : camera.error());
      }

      World::Camera lens;
      lens.primary = true;
      World::MeshRenderer mesh;
      mesh.tint = {0.2F, 0.7F, 1, 1};
      auto positioned = scene->setTransform(*camera, {{0, 0, 4}});
      auto cameraSet = scene->setCamera(*camera, lens);
      auto meshSet = scene->setMeshRenderer(*cube, mesh);
      auto scriptSet
          = scene->setScript(*cube, World::ScriptComponent{"mover.ts", "Mover", "{\"speed\":1.5}"});

      for (const auto* result : {&positioned, &cameraSet, &meshSet, &scriptSet}) {
        if (!*result) {
          return std::unexpected(result->error());
        }
      }

      const auto write = [&](const std::filesystem::path& file, std::string_view text) {
        return files->writeTextAtomic(file, text);
      };
      auto engineSet = write(scripts / "dot/engine.ts", *engine);
      auto hostSet = write(scripts / "dot/host.d.ts", *host);
      auto moverSet = write(scripts / "mover.ts", R"ts(
import {ScriptComponent, input} from './dot/engine.js';

/** Frame-driven movement of an attached native entity.
 * 逐帧驱动附着原生实体的移动。 */
export class Mover extends ScriptComponent<{speed: number}> {
  onUpdate(seconds: number): void {
    const direction = (input.key('D').down ? 1 : 0) - (input.key('A').down ? 1 : 0);
    const transform = this.self.transform;
    transform.position.x += direction * this.properties.speed * seconds;
    this.self.transform = transform;
  }
}
)ts");
      auto manifestSet = write(manifest, *metadata);
      auto sceneSet = World::saveScene(*files, scenePath, *scene);
      auto guideSet = write("README.md", "# " + description.name + "\n\n"
          "Open project.dotproject with DotEngine. The startup scene attaches Mover to the cube; "
          "use A/D to move. Scripts live in " + description.scriptsDirectory + ".\n\n"
          "Engine objects are authored in scenes; TypeScript components control their behavior.\n");

      for (const auto* result :
           {&engineSet, &hostSet, &moverSet, &manifestSet, &sceneSet, &guideSet}) {
        if (!*result) {
          return std::unexpected(result->error());
        }
      }

      auto valid = resources(*files, description);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      auto published = staging->publish(folder);

      if (!published) {
        return std::unexpected(published.error());
      }

      return Project(std::move(*published), std::move(ownedDescription));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Create project: " + std::string(error.what()));
    }
  }

  auto Project::description() const -> Core::Result<Description> {
    if (m_files.root().empty()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Project was moved");
    }

    return m_description;
  }

  auto Project::files() const -> Core::Result<Core::FileSystem> {
    if (m_files.root().empty()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Project was moved");
    }

    return m_files;
  }

  auto Project::loadStartup() const -> Core::Result<World::Scene> {
    auto valid = files();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto type = regular(*valid, path(m_description.startScene));
    return type ? World::loadScene(*valid, path(m_description.startScene))
                : Core::Result<World::Scene>{std::unexpected(type.error())};
  }

  auto Project::save(const Description& description) -> Core::Result<void> {
    if (m_files.root().empty()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Project was moved");
    }

    auto encoded = encodeDescription(description);

    if (!encoded) {
      return std::unexpected(encoded.error());
    }

    auto checked = resources(m_files, description);

    if (!checked) {
      return checked;
    }

    auto manifestType = regular(m_files, manifest);

    if (!manifestType) {
      return manifestType;
    }

    auto candidate = description;
    auto written = m_files.writeTextAtomic(manifest, *encoded);

    if (written) {
      static_assert(std::is_nothrow_move_assignable_v<Description>);
      m_description = std::move(candidate);
    }

    return written;
  }

  auto Project::saveStartup(const World::Scene& scene) const -> Core::Result<void> {
    auto valid = files();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto checked = regular(*valid, path(m_description.startScene));
    return checked ? World::saveScene(*valid, path(m_description.startScene), scene) : checked;
  }
}  // namespace Dot::Project

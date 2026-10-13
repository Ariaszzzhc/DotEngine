module;

#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <string>
#include <unordered_set>

module Dot.Project.Templates;

import Dot.Core.Paths;
import Dot.Core.StagedDirectory;
import Dot.Core.Text;
import Dot.Project.GamePackage;

namespace Dot::Project {
  namespace {
    namespace Fs = std::filesystem;

    auto utf(const Fs::path& value) -> std::string {
      auto bytes = value.generic_u8string();
      return {bytes.begin(), bytes.end()};
    }

    auto path(std::string_view value) -> Fs::path {
      return Fs::path(std::u8string(value.begin(), value.end()));
    }

    auto fold(std::string value) -> std::string {
      for (auto& c : value) {
        if (c >= 'A' && c <= 'Z') {
          c = static_cast<char>(c - 'A' + 'a');
        }
      }

      return value;
    }
  }  // namespace

  auto instantiateTemplate(const Core::FileSystem& source, const Core::FileSystem& parent,
                           const Fs::path& folder, std::string_view name,
                           const Core::FileSystem& sdk, const Script::TypeScriptCompiler& compiler)
      -> Core::Result<Project> {
    if (folder.filename() != folder || !Core::isPortableRelativePath(utf(folder))) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Template target requires one portable name");
    }

    try {
      const auto destination = parent.resolve(folder);

      if (!destination) {
        return std::unexpected(destination.error());
      }

      if (Fs::exists(Fs::symlink_status(*destination))) {
        return Core::fail(Core::ErrorCode::InvalidState, "Template destination already exists");
      }

      for (auto current = parent.root();;) {
        std::error_code error;

        if (Fs::equivalent(current, source.root(), error) && !error) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Cannot create inside the source template");
        }

        const auto next = current.parent_path();

        if (next == current) {
          break;
        }

        current = next;
      }

      if (!Fs::is_regular_file(Fs::symlink_status(source.root() / "project.dotproject"))) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Template manifest must be regular");
      }

      auto text = source.readText("project.dotproject", 64 * 1024);
      auto description = text ? decodeDescription(*text)
                              : Core::Result<Description>{std::unexpected(text.error())};

      if (!description) {
        return std::unexpected(description.error());
      }

      description->name = name;
      auto metadata = encodeDescription(*description);

      if (!metadata) {
        return std::unexpected(metadata.error());
      }

      auto staging = Core::StagedDirectory::create(parent);

      if (!staging) {
        return std::unexpected(staging.error());
      }

      auto output = staging->files().value();
      const auto sdkDirectory = path(description->scriptsDirectory) / "dot";
      std::uint64_t total = 0;
      std::size_t count = 0;
      std::unordered_set<std::string> aliases;

      for (auto iterator = Fs::recursive_directory_iterator(source.root());
           iterator != Fs::recursive_directory_iterator{}; ++iterator) {
        const auto relative = iterator->path().lexically_relative(source.root());
        const auto value = utf(relative);
        const auto status = iterator->symlink_status();

        if (++count > 8192 || iterator.depth() > 64 || !Core::isPortableRelativePath(value)
            || Fs::is_symlink(status) || (!Fs::is_directory(status) && !Fs::is_regular_file(status))
            || !aliases.insert(fold(value)).second) {
          return Core::fail(
              Core::ErrorCode::InvalidArgument,
              "Template tree is excessive, non-portable, aliased or non-regular: " + value);
        }

        if (utf(relative.filename()).starts_with(".dot-stage-")) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Template contains private staging data");
        }

        if (Fs::is_directory(status)) {
          if (relative == sdkDirectory) {
            iterator.disable_recursion_pending();
          } else {
            auto made = output.createDirectories(relative);

            if (!made) {
              return std::unexpected(made.error());
            }
          }

          continue;
        }

#if defined(__APPLE__)
        if (relative.filename() == ".DS_Store" && iterator->file_size() <= 64 * 1024) {
          continue;
        }
#endif

        if (relative == "project.dotproject") {
          continue;
        }

        const auto size = iterator->file_size();

        if (size > 64 * 1024 * 1024 || size > 512 * 1024 * 1024 - total) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Template byte limit exceeded: " + value);
        }

        auto data = source.readBytes(relative, 64 * 1024 * 1024);

        if (!data) {
          return std::unexpected(data.error());
        }

        if (data->size() != size) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Template file changed during copying: " + value);
        }

        auto written = output.writeAtomic(relative, *data);

        if (!written) {
          return std::unexpected(written.error());
        }

        total += size;
      }

      auto made = output.createDirectories(sdkDirectory);

      if (!made) {
        return std::unexpected(made.error());
      }

      for (const auto* file : {"engine.ts", "host.d.ts"}) {
        auto data = sdk.readText(Fs::path{"dot"} / file, 1024 * 1024);

        if (!data) {
          return std::unexpected(data.error());
        }

        auto written = output.writeTextAtomic(sdkDirectory / file, *data);

        if (!written) {
          return std::unexpected(written.error());
        }
      }

      auto written = output.writeTextAtomic("project.dotproject", *metadata);

      if (!written) {
        return std::unexpected(written.error());
      }

      auto project = Project::open(output.root());

      if (!project) {
        return std::unexpected(project.error());
      }

      auto scene = project->loadStartup();

      if (!scene) {
        return std::unexpected(scene.error());
      }

      auto validationOwner = Core::StagedDirectory::create(parent);

      if (!validationOwner) {
        return std::unexpected(validationOwner.error());
      }

      auto validation = GamePackage::publish(*project, *scene, compiler,
                                             validationOwner->files().value(), "game");

      if (!validation) {
        return Core::fail(validation.error().code,
                          "Template preparation: " + validation.error().message);
      }

      auto published = staging->publish(folder);
      return published ? Project::open(published->root())
                       : Core::Result<Project>{std::unexpected(published.error())};
    } catch (const Fs::filesystem_error& error) {
      return Core::fail(Core::ErrorCode::Io, "Instantiate template: " + std::string(error.what()));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Instantiate template: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Project

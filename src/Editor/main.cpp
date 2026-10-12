#include <exception>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Project;
import Dot.World.SceneFiles;
import Dot.Script.Compiler;

namespace Dot::Editor {
  auto testProject(const std::filesystem::path& compiler, const std::filesystem::path& sdk,
                   const std::filesystem::path& parent, bool render) -> int;
}

namespace {
  using Json = nlohmann::json;

  auto path(std::string_view value) -> std::filesystem::path {
    return std::filesystem::path{std::u8string(value.begin(), value.end())};
  }

  auto utf(const std::filesystem::path& value) -> std::string {
    const auto text = value.generic_u8string();
    return std::string(text.begin(), text.end());
  }

  auto error(const Dot::Core::Error& value) -> int {
    const std::vector names{"InvalidArgument", "NotFound",     "Io",
                            "Unavailable",     "InvalidState", "External"};
    std::println("{}", Json{{"ok", false},
                            {"error",
                             {{"code", names.at(static_cast<std::size_t>(value.code))},
                              {"message", value.message}}}}
                           .dump());
    return 1;
  }

  auto read(const std::filesystem::path& file) -> Dot::Core::Result<std::string> {
    std::error_code ignored;
    auto absolute = std::filesystem::absolute(file, ignored);

    if (ignored) {
      return Dot::Core::fail(Dot::Core::ErrorCode::Io,
                             "Cannot resolve command input file: " + ignored.message());
    }

    auto files = Dot::Core::FileSystem::open(absolute.parent_path());
    return files ? files->readText(absolute.filename(), 64 * 1024 * 1024)
                 : Dot::Core::Result<std::string>{std::unexpected(files.error())};
  }

  auto inspect(const Dot::Project::Project& project) -> int {
    auto files = project.files();
    auto description = project.description();
    auto scene = project.loadStartup();

    if (!files || !description || !scene) {
      return error(!files ? files.error() : !description ? description.error() : scene.error());
    }

    auto manifest = Dot::Project::encodeDescription(*description);
    auto encoded = Dot::World::encodeScene(*scene);

    if (!manifest || !encoded) {
      return error(manifest ? encoded.error() : manifest.error());
    }

    std::println("{}", Json{{"ok", true},
                            {"root", utf(files->root())},
                            {"project", Json::parse(*manifest)},
                            {"scene", Json::parse(*encoded)}}
                           .dump());
    return 0;
  }
}  // namespace

/** Commands expose shared project operations for automation clients.
 * 命令向自动化客户端公开共享项目操作。 */
auto main(int argc, char** argv) -> int {
  try {
    if (argc == 6 && std::string_view(argv[1]) == "create") {
      auto parent = Dot::Core::FileSystem::open(path(argv[2]));
      auto sdk = Dot::Core::FileSystem::open(path(argv[5]));

      if (!parent || !sdk) {
        return error(parent ? sdk.error() : parent.error());
      }

      Dot::Project::Description description;
      description.name = argv[4];
      auto project = Dot::Project::Project::create(*parent, path(argv[3]), description, *sdk);
      return project ? inspect(*project) : error(project.error());
    }

    if (argc == 3 && std::string_view(argv[1]) == "inspect") {
      auto project = Dot::Project::Project::open(path(argv[2]));
      return project ? inspect(*project) : error(project.error());
    }

    if (argc == 4
        && (std::string_view(argv[1]) == "configure"
            || std::string_view(argv[1]) == "save-scene")) {
      auto project = Dot::Project::Project::open(path(argv[2]));
      auto source = read(path(argv[3]));

      if (!project || !source) {
        return error(project ? source.error() : project.error());
      }

      if (std::string_view(argv[1]) == "configure") {
        auto description = Dot::Project::decodeDescription(*source);

        if (!description) {
          return error(description.error());
        }

        auto saved = project->save(*description);
        return saved ? inspect(*project) : error(saved.error());
      }

      auto scene = Dot::World::decodeScene(*source);

      if (!scene) {
        return error(scene.error());
      }

      auto saved = project->saveStartup(*scene);
      return saved ? inspect(*project) : error(saved.error());
    }

    if (argc == 4 && std::string_view(argv[1]) == "check") {
      auto project = Dot::Project::Project::open(path(argv[2]));
      auto compiler = Dot::Script::TypeScriptCompiler::create(path(argv[3]));

      if (!project || !compiler) {
        return error(project ? compiler.error() : project.error());
      }

      auto program = compiler->compileProject(project->files().value(),
                                              project->description()->scriptsDirectory);

      if (!program) {
        return error(program.error());
      }

      auto stats = program->stats().value();
      std::println("{}", Json{{"ok", true},
                              {"modules", program->modules().value()},
                              {"sourceFiles", stats.sourceFiles},
                              {"sourceBytes", std::to_string(stats.sourceBytes)},
                              {"outputBytes", std::to_string(stats.outputBytes)}}
                             .dump());
      return 0;
    }

    if (argc == 5
        && (std::string_view(argv[1]) == "--project-test"
            || std::string_view(argv[1]) == "--project-render-test")) {
      return Dot::Editor::testProject(path(argv[2]), path(argv[3]), path(argv[4]),
                                      std::string_view(argv[1]) == "--project-render-test");
    }

    return error({Dot::Core::ErrorCode::InvalidArgument,
                  "Usage: DotEditor create <parent> <folder> <name> <sdk> | inspect <root> | "
                  "configure <root> <json-file> | save-scene <root> <dotscene-file> | "
                  "check <root> <compiler> | --project[-render]-test <compiler> <sdk> <parent>",
                  {}});
  } catch (const std::exception& value) {
    return error({Dot::Core::ErrorCode::External, value.what(), {}});
  }
}

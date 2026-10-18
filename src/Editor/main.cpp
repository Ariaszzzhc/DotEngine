#include <charconv>
#include <cstddef>
#include <cstdio>
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

#if defined(_WIN32)
#  include <SDL3/SDL_main.h>
#endif

#include "../Engine/Serialization/Json.hpp"

import Dot.Core.Files;
import Dot.Core.Control;
import Dot.Core.Result;
import Dot.Project;
import Dot.Project.GamePackage;
import Dot.Project.GameExport;
import Dot.Editor.Document;
import Dot.Editor.Workspace;
import Dot.Editor.Home;
import Dot.Core.Text;
import Dot.World.SceneFiles;
import Dot.Script.Compiler;

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

  auto read(const std::filesystem::path& file, std::size_t limit = 64 * 1024 * 1024)
      -> Dot::Core::Result<std::string> {
    std::error_code ignored;
    auto absolute = std::filesystem::absolute(file, ignored);

    if (ignored) {
      return Dot::Core::fail(Dot::Core::ErrorCode::Io,
                             "Cannot resolve command input file: " + ignored.message());
    }

    auto files = Dot::Core::FileSystem::open(absolute.parent_path());
    return files ? files->readText(absolute.filename(), limit)
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

  auto document(const std::filesystem::path& root, const std::filesystem::path& requests) -> int {
    auto source = read(requests, 1024 * 1024);

    if (!source) {
      return error(source.error());
    }

    if (source->empty() || source->find('\0') != std::string::npos
        || !Dot::Core::isValidUtf8(*source)) {
      return error({Dot::Core::ErrorCode::InvalidArgument,
                    "Document request requires UTF-8 without NUL",
                    {}});
    }

    try {
      auto values = Dot::Serialization::Detail::parseJson(*source);

      if (values.is_object()) {
        values = Json::array({std::move(values)});
      }

      if (!values.is_array() || values.empty() || values.size() > 256) {
        return error({Dot::Core::ErrorCode::InvalidArgument,
                      "Document command requires 1..256 requests",
                      {}});
      }

      auto project = Dot::Project::Project::open(root);

      if (!project) {
        return error(project.error());
      }

      auto document = Dot::Editor::Document::open(*project);

      if (!document) {
        return error(document.error());
      }

      for (const auto& value : values) {
        auto response = document->dispatch(value.dump());

        if (!response) {
          return error(response.error());
        }

        std::println("{}", *response);
      }

      return 0;
    } catch (const Json::exception& value) {
      return error({Dot::Core::ErrorCode::InvalidArgument,
                    "Document request JSON: " + std::string(value.what()),
                    {}});
    } catch (const std::invalid_argument& value) {
      return error({Dot::Core::ErrorCode::InvalidArgument,
                    "Document request: " + std::string(value.what()),
                    {}});
    }
  }

  auto home(std::size_t frames, const std::filesystem::path& controlParent = {}) -> int {
    Dot::Editor::HomeConfig config;
    config.controlParent = controlParent;
    auto opened = Dot::Editor::Home::open(config);

    if (!opened) {
      return error(opened.error());
    }

    auto selector = std::move(*opened);
    std::println("{}", Json{{"ok", true},
                            {"kind", "project-home"},
                            {"control", utf(selector.controlDirectory().value())},
                            {"receipts", utf(selector.receiptDirectory().value())}}
                           .dump());
    std::fflush(stdout);
    auto run = selector.run(frames);

    if (!run) {
      return error(run.error());
    }

    auto selected = selector.selection();

    if (!selected) {
      return error(selected.error());
    }

    auto released = selector.releaseWindow();

    if (!released) {
      return error(released.error());
    }

    if (!selected->has_value()) {
      return 0;
    }

    auto workspace = Dot::Editor::Workspace::open(**selected);

    if (!workspace) {
      return error({workspace.error().code,
                    "Project is ready at " + utf((**selected).files()->root())
                        + "; opening workspace failed: " + workspace.error().message,
                    {}});
    }

    std::println("{}", Json{{"ok", true},
                            {"kind", "workspace"},
                            {"control", utf(workspace->controlDirectory().value())},
                            {"receipts", utf(workspace->receiptDirectory().value())}}
                           .dump());
    std::fflush(stdout);
    auto shown = workspace->run(frames);
    return shown ? 0 : error(shown.error());
  }

}  // namespace

/** Commands expose shared project operations for automation clients.
 * 命令向自动化客户端公开共享项目操作。 */
auto main(int argc, char** argv) -> int {
  try {
    if (argc == 1 || (argc >= 2 && std::string_view(argv[1]) == "home")) {
      std::size_t frames{};
      bool budget{};
      std::filesystem::path controlParent;

      for (int argument = 2; argument < argc; ++argument) {
        const std::string_view value(argv[argument]);

        if (value == "--control" && controlParent.empty() && argument + 1 < argc) {
          controlParent = path(argv[++argument]);

          if (controlParent.empty()) {
            return error({Dot::Core::ErrorCode::InvalidArgument,
                          "Home control parent must not be empty",
                          {}});
          }
        } else if (!budget && !value.starts_with("--")) {
          const auto count = std::from_chars(value.data(), value.data() + value.size(), frames);

          if (value.empty() || count.ec != std::errc{}
              || count.ptr != value.data() + value.size()) {
            return error({Dot::Core::ErrorCode::InvalidArgument, "Invalid home frame budget", {}});
          }

          budget = true;
        } else {
          return error(
              {Dot::Core::ErrorCode::InvalidArgument, "Invalid home control/frame option", {}});
        }
      }

      return home(frames, controlParent);
    }

    if (argc == 4 && std::string_view(argv[1]) == "project-action") {
      auto resources = Dot::Core::FileSystem::open(path(argv[2]));
      auto request = read(path(argv[3]), 1024 * 1024);

      if (!resources || !request) {
        return error(resources ? request.error() : resources.error());
      }

      auto project = Dot::Editor::performProjectAction(*resources, *request);
      return project ? inspect(*project) : error(project.error());
    }

    if ((argc == 6 && std::string_view(argv[1]) == "export-data")
        || (argc == 7 && std::string_view(argv[1]) == "export")) {
      auto project = Dot::Project::Project::open(path(argv[2]));
      auto parent = Dot::Core::FileSystem::open(path(argv[3]));
      auto compiler = Dot::Script::TypeScriptCompiler::create(path(argv[5]));

      if (!project || !parent || !compiler) {
        return error(!project ? project.error() : !parent ? parent.error() : compiler.error());
      }

      auto scene = project->loadStartup();

      if (!scene) {
        return error(scene.error());
      }

      auto output = [&]() -> Dot::Core::Result<Dot::Core::FileSystem> {
        if (argc == 6) {
          return Dot::Project::GamePackage::publish(*project, *scene, *compiler, *parent,
                                                    path(argv[4]));
        }

        auto files = Dot::Core::FileSystem::open(path(argv[6]));
        auto kit
            = files ? Dot::Project::RuntimeKit::open(*files)
                    : Dot::Core::Result<Dot::Project::RuntimeKit>{std::unexpected(files.error())};
        return kit ? Dot::Project::exportGame(*project, *scene, *compiler, *kit, *parent,
                                              path(argv[4]))
                   : std::unexpected(kit.error());
      }();

      if (!output) {
        return error(output.error());
      }

      std::println("{}", Json{{"ok", true},
                              {"package", utf(output->root())},
                              {"kind", argc == 7 ? "game-native" : "game-data"}}
                             .dump());
      return 0;
    }

    if ((argc == 3 || argc == 4) && std::string_view(argv[1]) == "open") {
      std::size_t frames = 0;

      if (argc == 4) {
        const std::string_view text(argv[3]);
        auto parsed = std::from_chars(text.data(), text.data() + text.size(), frames);

        if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
          return error({Dot::Core::ErrorCode::InvalidArgument, "Invalid editor frame budget", {}});
        }
      }

      auto project = Dot::Project::Project::open(path(argv[2]));

      if (!project) {
        return error(project.error());
      }

      auto workspace = Dot::Editor::Workspace::open(*project);

      if (!workspace) {
        return error(workspace.error());
      }

      std::println("{}", Json{{"ok", true},
                              {"control", utf(workspace->controlDirectory().value())},
                              {"receipts", utf(workspace->receiptDirectory().value())}}
                             .dump());
      std::fflush(stdout);
      auto result = workspace->run(frames);
      return result ? 0 : error(result.error());
    }

    if (argc == 3
        && (std::string_view(argv[1]) == "control-status"
            || std::string_view(argv[1]) == "ack-control"
            || std::string_view(argv[1]) == "clear-control")) {
      auto result = Dot::Core::performControlReceiptAction(argv[1], path(argv[2]));

      if (!result) {
        return error(result.error());
      }

      std::println("{}", *result);
      return 0;
    }

    if (argc == 5 && std::string_view(argv[1]) == "send") {
      auto request = read(path(argv[4]), 1024 * 1024);

      if (!request) {
        return error(request.error());
      }

      auto response = Dot::Editor::submitWorkspaceRequest(path(argv[2]), argv[3], *request);

      if (!response) {
        return error(response.error());
      }

      std::println("{}", Json{{"ok", true}, {"queued", true}, {"response", utf(*response)}}.dump());
      return 0;
    }

    if (argc == 4 && std::string_view(argv[1]) == "document") {
      return document(path(argv[2]), path(argv[3]));
    }

    if (argc == 5 && std::string_view(argv[1]) == "create") {
      auto parent = Dot::Core::FileSystem::open(path(argv[2]));

      if (!parent) {
        return error(parent.error());
      }

      Dot::Project::Description description;
      description.name = argv[4];
      auto project = Dot::Project::Project::create(*parent, path(argv[3]), description);
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

    return error({Dot::Core::ErrorCode::InvalidArgument,
                  "Usage: DotEditor [home [frames] [--control <parent>]] | project-action "
                  "<resources> <request-file> | "
                  "export <root> <parent> <folder> <compiler> <kit> | export-data "
                  "<root> <parent> <folder> <compiler> | open <root> "
                  "[frames] | send <control> <id> <request-file> | control-status <receipts> | "
                  "ack-control <response> | clear-control <receipts> | "
                  "create <parent> <folder> <name> | "
                  "inspect <root> | "
                  "configure <root> <json-file> | save-scene <root> <dotscene-file> | "
                  "check <root> <compiler> | document <root> <requests-json-file>",
                  {}});
  } catch (const std::exception& value) {
    return error({Dot::Core::ErrorCode::External, value.what(), {}});
  }
}

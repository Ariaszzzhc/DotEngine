#include <doctest/doctest.h>

#include <chrono>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.Process;
import Dot.Core.StagedDirectory;
import Dot.Project;
import Dot.Project.GameExport;

namespace {
  using namespace Dot;
  using Json = nlohmann::json;

  auto utf(const std::filesystem::path& path) -> std::string {
    const auto value = path.generic_u8string();
    return {value.begin(), value.end()};
  }

  auto run(const std::filesystem::path& executable, const std::filesystem::path& cwd,
           std::vector<std::string> args) -> Core::ProcessResult {
    auto result = Core::runProcess({executable, std::move(args), cwd, std::chrono::seconds{30}});
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    CHECK_MESSAGE(result->exitCode == 0, (result->standardError + result->standardOutput));
    return std::move(*result);
  }
}  // namespace

TEST_CASE("distribution.relocated_editor_creates_checks_plays_and_exports_without_build_paths") {
  auto temporary = Core::FileSystem::open(std::filesystem::temp_directory_path()).value();
  auto storage = Core::StagedDirectory::create(temporary).value();
  auto files = storage.files().value();
  std::filesystem::copy(DOT_EDITOR_DISTRIBUTION, files.root() / "relocated editor",
                        std::filesystem::copy_options::recursive);
  auto root = files.root() / "relocated editor";
#ifdef _WIN32
  auto editor = root / "DotEditor.exe";
  auto compiler = root / "resources/typescript-native/tsc.exe";
#else
  auto editor = root / "DotEditor";
  auto compiler = root / "resources/typescript-native/tsc";
#endif
  run(editor, files.root(), {"home", "2"});
  auto runtime = Core::FileSystem::open(root / "resources/runtime-kit").value();
  REQUIRE(Project::RuntimeKit::open(runtime));
  auto request = Json{{"command", "create"},
                      {"parent", utf(files.root())},
                      {"folder", "game project"},
                      {"name", "Portable editor game"},
                      {"template", "prism"}};
  REQUIRE(files.writeTextAtomic("create.json", request.dump()));
  auto created
      = run(editor, files.root(), {"project-action", utf(root / "resources"), "create.json"});
  CHECK(Json::parse(created.standardOutput)["ok"] == true);
  auto projectRoot = files.root() / "game project";
  auto checked = run(editor, files.root(), {"check", utf(projectRoot), utf(compiler)});
  CHECK(Json::parse(checked.standardOutput)["ok"] == true);
  run(editor, files.root(), {"open", utf(projectRoot), "3"});
#ifdef _WIN32
  const auto runtimePlayer = root / "DotRuntime.exe";
#else
  const auto runtimePlayer = root / "DotRuntime";
#endif
  run(runtimePlayer, files.root(), {"--project", utf(projectRoot), utf(compiler), "6"});
  auto exported = run(editor, files.root(),
                      {"export", utf(projectRoot), utf(files.root()), "exported", utf(compiler),
                       utf(runtime.root())});
  CHECK(Json::parse(exported.standardOutput)["kind"] == "game-native");
  std::filesystem::remove_all(projectRoot);
  std::filesystem::remove_all(root);
#ifdef _WIN32
  auto game = files.root() / "exported/DotGame.exe";
#else
  auto game = files.root() / "exported/DotGame";
#endif
  run(game, files.root(), {"--frames", "4"});
  CHECK(Core::processResources().children == 0);
  CHECK(Core::processResources().handles == 0);
}

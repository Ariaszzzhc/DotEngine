#include <doctest/doctest.h>

#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

import Dot.Core.Files;
import Dot.Core.Process;
import Dot.Project;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace {
  using Json = nlohmann::json;

  struct Directory {
    std::filesystem::path root;

    Directory() {
      std::random_device random;
      root = std::filesystem::temp_directory_path()
             / ("dot-editor-commands-" + std::to_string(random()) + "-" + std::to_string(random()));
      REQUIRE(std::filesystem::create_directory(root));
    }

    ~Directory() {
      std::error_code ignored;
      std::filesystem::remove_all(root, ignored);
    }

    auto files() const -> Dot::Core::FileSystem {
      return Dot::Core::FileSystem::open(root).value();
    }
  };

  auto utf(const std::filesystem::path& path) -> std::string {
    auto value = path.generic_u8string();
    return std::string(value.begin(), value.end());
  }

  auto command(const Directory& directory, std::vector<std::string> args, bool success = true)
      -> Json {
    auto result = Dot::Core::runProcess({DOT_EDITOR_EXECUTABLE, std::move(args), directory.root});
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    INFO(result->standardError);
    CHECK(result->exitCode == (success ? 0U : 1U));
    auto output = Json::parse(result->standardOutput);
    CHECK(output["ok"] == success);
    CHECK(Dot::Core::processResources().handles == 0);
    CHECK(Dot::Core::processResources().children == 0);
    return output;
  }

  auto create(const Directory& directory) -> Json {
    return command(directory, {"create", utf(directory.root), "game folder", "游戏 $ literal",
                               DOT_SCRIPT_SDK_DIRECTORY});
  }
}  // namespace

TEST_CASE(
    "editor_commands_create_inspect_and_check_use_real_project_and_typescript_with_literal_args") {
  Directory directory;
  auto created = create(directory);
  CHECK(created["project"]["name"] == "游戏 $ literal");
  REQUIRE(created["scene"]["entities"].size() == 2);
  const auto root = directory.root / "game folder";
  auto inspected = command(directory, {"inspect", utf(root)});
  CHECK(inspected["project"] == created["project"]);
  auto checked = command(directory, {"check", utf(root), DOT_TYPESCRIPT_COMPILER});
  CHECK(checked["sourceFiles"] == 3);
  CHECK(checked["modules"].size() == 2);
  CHECK(Dot::Project::Project::open(root)->loadStartup()->entities()->size() == 2);
}

TEST_CASE("runtime_project_command_rejects_invalid_budgets_before_opening_resources") {
  Directory directory;

  for (const auto& budget :
       std::vector<std::string>{"", "-1", "+1", "1.5", "1x", "18446744073709551616"}) {
    auto result = Dot::Core::runProcess(
        {DOT_RUNTIME_EXECUTABLE, {"--project", "missing", "missing", budget}, directory.root});
    REQUIRE(result);
    CHECK(result->exitCode == 2);
    CHECK(result->standardError.find("frame budget") != std::string::npos);
  }

  for (const auto& budget : {"0", "2"}) {
    auto result = Dot::Core::runProcess(
        {DOT_RUNTIME_EXECUTABLE, {"--project", "missing", "missing", budget}, directory.root});
    REQUIRE(result);
    CHECK(result->exitCode == 1);
    CHECK_FALSE(result->standardError.empty());
    CHECK(result->standardError.find("frame budget") == std::string::npos);
  }

  CHECK(Dot::Core::processResources().children == 0);
  CHECK(Dot::Core::processResources().handles == 0);
}

TEST_CASE("editor_document_command_sequences_native_edits_history_selection_and_explicit_save") {
  Directory directory;
  create(directory);
  Json requests = Json::array(
      {{{"command", "inspect"}},
       {{"command", "edit"},
        {"revision", "1"},
        {"operations", Json::array({{{"op", "name"}, {"entity", "2"}, {"name", "Edited"}}})}},
       {{"command", "select"}, {"revision", "2"}, {"entity", "2"}},
       {{"command", "undo"}, {"revision", "3"}},
       {{"command", "redo"}, {"revision", "4"}},
       {{"command", "save"}, {"revision", "5"}},
       {{"command", "inspect"}}});
  REQUIRE(directory.files().writeTextAtomic("requests.json", requests.dump()));
  auto result = Dot::Core::runProcess(
      {DOT_EDITOR_EXECUTABLE,
       {"document", utf(directory.root / "game folder"), utf(directory.root / "requests.json")},
       directory.root});
  REQUIRE(result);
  INFO(result->standardError);
  REQUIRE(result->exitCode == 0);
  std::istringstream lines(result->standardOutput);
  std::vector<Json> responses;

  for (std::string line; std::getline(lines, line);) {
    responses.push_back(Json::parse(line));
  }

  REQUIRE(responses.size() == 7);
  CHECK(responses[0]["revision"] == "1");
  CHECK(responses[2]["selected"] == "2");
  CHECK(responses[3]["scene"]["entities"][1]["name"] == "Cube");
  CHECK(responses[4]["scene"]["entities"][1]["name"] == "Edited");
  CHECK_FALSE(responses[5]["dirty"].get<bool>());
  CHECK(responses[6]["revision"] == "6");
  CHECK(Dot::Project::Project::open(directory.root / "game folder")
            ->loadStartup()
            ->entities()
            ->at(1)
            .name
        == "Edited");
}

TEST_CASE("editor_document_command_reports_partial_sequence_and_stops_after_stale_revision") {
  Directory directory;
  create(directory);
  auto edit
      = Json{{"command", "edit"},
             {"revision", "1"},
             {"operations", Json::array({{{"op", "name"}, {"entity", "2"}, {"name", "Saved"}}})}};
  Json requests = Json::array({edit,
                               {{"command", "save"}, {"revision", "2"}},
                               edit,
                               {{"command", "undo"}, {"revision", "3"}}});
  REQUIRE(directory.files().writeTextAtomic("requests.json", requests.dump()));
  auto result = Dot::Core::runProcess(
      {DOT_EDITOR_EXECUTABLE,
       {"document", utf(directory.root / "game folder"), utf(directory.root / "requests.json")},
       directory.root});
  REQUIRE(result);
  CHECK(result->exitCode == 1);
  std::istringstream lines(result->standardOutput);
  std::vector<Json> responses;

  for (std::string line; std::getline(lines, line);) {
    responses.push_back(Json::parse(line));
  }

  REQUIRE(responses.size() == 3);
  CHECK(responses[0]["ok"] == true);
  CHECK(responses[1]["dirty"] == false);
  CHECK(responses[2]["ok"] == false);
  CHECK(responses[2]["error"]["code"] == "InvalidState");
  CHECK(Dot::Project::Project::open(directory.root / "game folder")
            ->loadStartup()
            ->entities()
            ->at(1)
            .name
        == "Saved");
  CHECK(Dot::Core::processResources().children == 0);
}

TEST_CASE("editor_document_single_requests_require_explicit_save_and_reject_duplicate_fields") {
  Directory directory;
  create(directory);
  auto projectRoot = directory.root / "game folder";
  auto edit
      = Json{{"command", "edit"},
             {"revision", "1"},
             {"operations", Json::array({{{"op", "name"}, {"entity", "2"}, {"name", "Unsaved"}}})}};
  REQUIRE(directory.files().writeTextAtomic("requests.json", edit.dump()));
  auto result = Dot::Core::runProcess(
      {DOT_EDITOR_EXECUTABLE,
       {"document", utf(projectRoot), utf(directory.root / "requests.json")},
       directory.root});
  REQUIRE(result);
  CHECK(result->exitCode == 0);
  CHECK(Json::parse(result->standardOutput)["dirty"] == true);
  CHECK(Dot::Project::Project::open(projectRoot)->loadStartup()->entities()->at(1).name == "Cube");
  REQUIRE(directory.files().writeTextAtomic(
      "requests.json", "{\"command\":\"inspect\",\"command\":\"save\",\"revision\":\"1\"}"));
  auto bad = Dot::Core::runProcess(
      {DOT_EDITOR_EXECUTABLE,
       {"document", utf(projectRoot), utf(directory.root / "requests.json")},
       directory.root});
  REQUIRE(bad);
  CHECK(bad->exitCode == 1);
  CHECK(Json::parse(bad->standardOutput)["error"]["code"] == "InvalidArgument");
}

TEST_CASE("editor_commands_configure_and_save_scene_persist_the_same_validated_operations") {
  Directory directory;
  create(directory);
  auto root = directory.root / "game folder";
  auto project = Dot::Project::Project::open(root).value();
  auto description = project.description().value();
  description.name = "Changed";
  description.width = 640;
  description.hotReload = false;
  REQUIRE(directory.files().writeTextAtomic("update.json",
                                            Dot::Project::encodeDescription(description).value()));
  auto configured
      = command(directory, {"configure", utf(root), utf(directory.root / "update.json")});
  CHECK(configured["project"]["name"] == "Changed");
  CHECK(configured["project"]["window"]["width"] == 640);
  CHECK_FALSE(configured["project"]["hotReload"].get<bool>());
  auto scene = project.loadStartup().value();
  REQUIRE(scene.rename("Saved Command Scene"));
  REQUIRE(
      directory.files().writeTextAtomic("input.dotscene", Dot::World::encodeScene(scene).value()));
  auto saved
      = command(directory, {"save-scene", utf(root), utf(directory.root / "input.dotscene")});
  CHECK(saved["scene"]["name"] == "Saved Command Scene");
  CHECK(Dot::Project::Project::open(root)->loadStartup()->name().value() == "Saved Command Scene");
}

TEST_CASE(
    "editor_commands_existing_destinations_and_bad_configurations_return_structured_failures") {
  Directory directory;
  create(directory);
  auto root = directory.root / "game folder";
  auto old = directory.files().readText("game folder/project.dotproject").value();
  auto collision = command(
      directory,
      {"create", utf(directory.root), "game folder", "Overwrite", DOT_SCRIPT_SDK_DIRECTORY}, false);
  CHECK(collision["error"]["code"] == "Io");
  CHECK(directory.files().readText("game folder/project.dotproject").value() == old);
  REQUIRE(directory.files().writeTextAtomic("bad.json", "{\"version\":99}"));
  auto failed
      = command(directory, {"configure", utf(root), utf(directory.root / "bad.json")}, false);
  CHECK(failed["error"]["code"] == "InvalidArgument");
  CHECK(directory.files().readText("game folder/project.dotproject").value() == old);
  auto unknown = command(directory, {"--unknown"}, false);
  CHECK(unknown["error"]["code"] == "InvalidArgument");
}

TEST_CASE("editor_commands_check_reports_actual_type_errors_and_relocated_projects_recompile") {
  Directory directory;
  create(directory);
  auto root = directory.root / "game folder";
  auto original = directory.files().readText("game folder/scripts/mover.ts").value();
  REQUIRE(directory.files().writeTextAtomic("game folder/scripts/mover.ts",
                                            "export const value: number = 'bad';"));
  auto failed = command(directory, {"check", utf(root), DOT_TYPESCRIPT_COMPILER}, false);
  CHECK(failed["error"]["message"].get<std::string>().find("TS") != std::string::npos);
  REQUIRE(directory.files().writeTextAtomic("game folder/scripts/mover.ts", original));
  std::filesystem::rename(root, directory.root / "moved");
  auto checked
      = command(directory, {"check", utf(directory.root / "moved"), DOT_TYPESCRIPT_COMPILER});
  CHECK(checked["sourceFiles"] == 3);
}

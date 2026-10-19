#include <doctest/doctest.h>

#include <chrono>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

import Dot.Core.Files;
import Dot.Core.Control;
import Dot.Core.Result;
import Dot.Core.Process;
import Dot.Project;
import Dot.Project.GamePackage;
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

  auto command(const Directory& directory, std::vector<std::string> args, bool success = true,
               std::chrono::milliseconds timeout = std::chrono::seconds{10}) -> Json {
    auto result
        = Dot::Core::runProcess({DOT_EDITOR_EXECUTABLE, std::move(args), directory.root, timeout});
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
    return command(directory, {"create", utf(directory.root), "game folder", "游戏 $ literal"});
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
  CHECK(checked["sourceFiles"] == 1);
  CHECK(checked["modules"].size() == 1);
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

TEST_CASE("runtime_profile_options_reject_malformed_flags_and_bad_outputs_without_windows") {
  Directory directory;
  const std::vector<std::vector<std::string>> malformed{
      {"--profile"},
      {"--profile", ""},
      {"2", "--profile"},
      {"2", "--profile", "trace.json", "extra"},
      {"2", "--unknown", "trace.json"},
      {"2", "--profile", "one.json", "--profile", "two.json"}};

  for (const auto& tail : malformed) {
    std::vector<std::string> args{"--project", "missing", "missing"};
    args.insert(args.end(), tail.begin(), tail.end());
    auto result = Dot::Core::runProcess({DOT_RUNTIME_EXECUTABLE, args, directory.root});
    REQUIRE(result);
    CHECK(result->exitCode == 2);
  }

  for (const auto& args :
       std::vector<std::vector<std::string>>{{"--profile"},
                                             {"--profile", ""},
                                             {"--profile", "one", "--profile", "two"},
                                             {"--frames", "1", "--frames", "2"},
                                             {"--unknown", "trace.json"}}) {
    auto result = Dot::Core::runProcess({DOT_GAME_EXECUTABLE, args, directory.root});
    REQUIRE(result);
    CHECK(result->exitCode == 2);
  }

  create(directory);

  for (const auto& output : {directory.root / "absent/trace.json", directory.root}) {
    auto result = Dot::Core::runProcess({DOT_RUNTIME_EXECUTABLE,
                                         {"--project", utf(directory.root / "game folder"),
                                          DOT_TYPESCRIPT_COMPILER, "2", "--profile", utf(output)},
                                         directory.root});
    REQUIRE(result);
    CHECK(result->exitCode == 1);
    CHECK(result->standardError.find("profile") != std::string::npos);
  }

  CHECK_FALSE(std::filesystem::exists(directory.root / "trace.json"));
  CHECK(Dot::Core::processResources().children == 0);
  CHECK(Dot::Core::processResources().handles == 0);
}

TEST_CASE("editor_workspace_commands_validate_before_opening_native_resources") {
  Directory directory;

  for (const auto* budget : {"", "-1", "+1", "1.5", "1x", "18446744073709551616"}) {
    auto result = Dot::Core::runProcess(
        {DOT_EDITOR_EXECUTABLE, {"open", "missing", budget}, directory.root});
    REQUIRE(result);
    CHECK(result->exitCode == 1);
    auto response = Json::parse(result->standardOutput);
    CHECK(response["error"]["code"] == "InvalidArgument");
    CHECK(response["error"]["message"].get<std::string>().find("frame budget")
          != std::string::npos);
  }

  auto files = Dot::Core::FileSystem::open(directory.root).value();
  REQUIRE(files.writeTextAtomic("request.json", "{\"command\":\"inspect\"}"));
  auto invalidId = Dot::Core::runProcess(
      {DOT_EDITOR_EXECUTABLE, {"send", "missing", "../bad", "request.json"}, directory.root});
  REQUIRE(invalidId);
  CHECK(invalidId->exitCode == 1);
  CHECK(Json::parse(invalidId->standardOutput)["error"]["code"] == "InvalidArgument");
  REQUIRE(files.writeTextAtomic("request.json", "{\"command\":1,\"command\":2}"));
  auto malformed = Dot::Core::runProcess(
      {DOT_EDITOR_EXECUTABLE, {"send", "missing", "valid-id", "request.json"}, directory.root});
  REQUIRE(malformed);
  CHECK(malformed->exitCode == 1);
  CHECK(Json::parse(malformed->standardOutput)["error"]["code"] == "InvalidArgument");
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
  auto collision
      = command(directory, {"create", utf(directory.root), "game folder", "Overwrite"}, false);
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
  CHECK(checked["sourceFiles"] == 1);
}

TEST_CASE("editor_commands_export_data_and_runtime_game_validate_real_packages") {
  Directory directory;
  create(directory);
  auto result = command(directory, {"export-data", utf(directory.root / "game folder"),
                                    utf(directory.root), "published", DOT_TYPESCRIPT_COMPILER});
  CHECK(result["kind"] == "game-data");
  CHECK(result["package"] == utf(std::filesystem::canonical(directory.root / "published")));
  auto files = Dot::Core::FileSystem::open(directory.root / "published").value();
  auto package = Dot::Project::GamePackage::open(files);
  REQUIRE_MESSAGE(package, (package ? "" : package.error().message));
  CHECK(package->scene()->entities()->size() == 2);
  CHECK_FALSE(package->description()->hotReload);
  const auto baseline = files.readText("game.json").value();
  command(directory,
          {"export-data", utf(directory.root / "game folder"), utf(directory.root), "published",
           DOT_TYPESCRIPT_COMPILER},
          false);
  CHECK(files.readText("game.json").value() == baseline);
  command(directory,
          {"export-data", utf(directory.root / "game folder"), utf(directory.root), "../escape",
           DOT_TYPESCRIPT_COMPILER},
          false);

  for (const auto* budget : {"", "-1", "+1", "1.5", "1x", "18446744073709551616"}) {
    auto invalid = Dot::Core::runProcess(
        {DOT_RUNTIME_EXECUTABLE, {"--game", "missing", budget}, directory.root});
    REQUIRE(invalid);
    CHECK(invalid->exitCode == 2);
    CHECK(invalid->standardError.find("frame budget") != std::string::npos);
  }

  CHECK(Dot::Core::processResources().children == 0);
  CHECK(Dot::Core::processResources().handles == 0);
}

TEST_CASE("editor_commands_export_native_game_with_packaged_runtime") {
  Directory directory;
  create(directory);
  auto result
      = command(directory, {"export", utf(directory.root / "game folder"), utf(directory.root),
                            "native", DOT_TYPESCRIPT_COMPILER, DOT_RUNTIME_KIT});
  CHECK(result["kind"] == "game-native");
  auto files = Dot::Core::FileSystem::open(directory.root / "native/game").value();
  REQUIRE(Dot::Project::GamePackage::open(files));
  CHECK(std::filesystem::exists(directory.root / "native/runtime.json"));
#ifdef _WIN32
  const auto player = directory.root / "native/DotGame.exe";
#else
  const auto player = directory.root / "native/DotGame";
#endif

  for (const auto* budget : {"", "-1", "+1", "1.5", "1x", "18446744073709551616"}) {
    auto invalid = Dot::Core::runProcess({player, {"--frames", budget}, directory.root});
    REQUIRE(invalid);
    CHECK(invalid->exitCode == 2);
    CHECK(invalid->standardError.find("frame budget") != std::string::npos);
  }

  command(directory,
          {"export", utf(directory.root / "game folder"), utf(directory.root), "bad-kit",
           DOT_TYPESCRIPT_COMPILER, "missing"},
          false);
  CHECK_FALSE(std::filesystem::exists(directory.root / "bad-kit"));
}

TEST_CASE("editor_home_cli_actions_and_bad_budgets_share_project_operations") {
  Directory directory;
  auto files = directory.files();
  auto resources = std::filesystem::path(DOT_EDITOR_EXECUTABLE).parent_path() / "resources";
  auto request = Json{{"command", "create"},
                      {"parent", utf(directory.root)},
                      {"folder", "from-home"},
                      {"name", "Home project"},
                      {"template", "lab"}};
  REQUIRE(files.writeTextAtomic("request.json", request.dump()));
  auto result = command(directory, {"project-action", utf(resources), "request.json"}, true,
                        std::chrono::seconds{20});
  CHECK(result["project"]["name"] == "Home project");
  CHECK(result["scene"]["entities"].size()
        == Dot::Project::Project::open(resources / "templates/FeatureLab")
               ->loadStartup()
               ->entityCount()
               .value());
  command(directory, {"project-action", utf(resources), "request.json"}, false);

  for (const auto* budget : {"", "-1", "+1", "1.5", "1x", "18446744073709551616"}) {
    auto invalid = Dot::Core::runProcess({DOT_EDITOR_EXECUTABLE, {"home", budget}, directory.root});
    REQUIRE(invalid);
    CHECK(invalid->exitCode == 1);
    auto error = Json::parse(invalid->standardOutput);
    CHECK(error["error"]["code"] == "InvalidArgument");
    CHECK(error["error"]["message"].get<std::string>().find("frame budget") != std::string::npos);
  }

  for (const auto& args : std::vector<std::vector<std::string>>{
           {"home", "--control", ""},
           {"home", "--control", "missing"},
           {"home", "--control"},
           {"home", "--control", utf(directory.root), "--control", utf(directory.root)}}) {
    command(directory, args, false);
  }
}

TEST_CASE("editor_document_scene_files_use_one_cli_session_and_keep_startup_bytes") {
  Directory directory;
  create(directory);
  auto project = Dot::Project::Project::open(directory.root / "game folder").value();
  auto files = project.files().value();
  const auto startup = files.readText("scenes/main.dotscene").value();
  const auto requests = Json::array(
      {{{"command", "create-scene"},
        {"revision", "1"},
        {"path", "scenes/level.dotscene"},
        {"name", "Level"},
        {"discard", false}},
       {{"command", "edit"},
        {"revision", "2"},
        {"operations", Json::array({{{"op", "create"}, {"name", "Persistent entity"}}})}},
       {{"command", "save-scene-as"}, {"revision", "3"}, {"path", "scenes/copied.dotscene"}},
       {{"command", "open-scene"},
        {"revision", "4"},
        {"path", "scenes/main.dotscene"},
        {"discard", false}},
       {{"command", "open-scene"},
        {"revision", "5"},
        {"path", "scenes/copied.dotscene"},
        {"discard", false}}});
  REQUIRE(directory.files().writeTextAtomic("scene-requests.json", requests.dump()));
  auto result = Dot::Core::runProcess(
      {DOT_EDITOR_EXECUTABLE,
       {"document", utf(files.root()), utf(directory.root / "scene-requests.json")},
       directory.root});
  REQUIRE(result);
  CHECK(result->exitCode == 0);
  CHECK(result->standardError.empty());
  std::istringstream lines(result->standardOutput);
  std::vector<Json> responses;

  for (std::string line; std::getline(lines, line);) {
    responses.push_back(Json::parse(line));
  }

  REQUIRE(responses.size() == 5);
  CHECK(responses.back()["scenePath"] == "scenes/copied.dotscene");
  CHECK(responses.back()["scene"]["entities"][0]["name"] == "Persistent entity");
  CHECK(responses.back()["dirty"] == false);
  CHECK(files.readText("scenes/main.dotscene").value() == startup);
  CHECK(Dot::World::loadScene(files, "scenes/level.dotscene")->entities()->empty());
  CHECK(Dot::Core::processResources().handles == 0);
  CHECK(Dot::Core::processResources().children == 0);
}

TEST_CASE("runtime_control_and_data_options_fail_before_opening_windows_or_creating_fallbacks") {
  Directory directory;

  for (const auto* executable : {DOT_RUNTIME_EXECUTABLE, DOT_GAME_EXECUTABLE}) {
    for (const auto& tail : std::vector<std::vector<std::string>>{
             {"--control"},
             {"--control", ""},
             {"--data-directory"},
             {"--data-directory", ""},
             {"--control", "a", "--control", "b"},
             {"--data-directory", "a", "--data-directory", "b"}}) {
      std::vector<std::string> args;

      if (std::string_view(executable) == DOT_RUNTIME_EXECUTABLE) {
        args = {"--project", "missing", "missing"};
      }

      args.insert(args.end(), tail.begin(), tail.end());
      auto result = Dot::Core::runProcess({executable, args, directory.root});
      REQUIRE(result);
      CHECK(result->exitCode == 2);
    }
  }

  create(directory);

  for (const auto* option : {"--control", "--data-directory"}) {
    const auto missing = directory.root / "absent";
    auto result = Dot::Core::runProcess({DOT_RUNTIME_EXECUTABLE,
                                         {"--project", utf(directory.root / "game folder"),
                                          DOT_TYPESCRIPT_COMPILER, option, utf(missing)},
                                         directory.root});
    REQUIRE(result);
    CHECK(result->exitCode == 1);
    CHECK(result->standardError.find("Prepare player") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(missing));
  }
}

TEST_CASE("runtime_and_standalone_send_queue_the_shared_owned_product_protocol_without_windows") {
  Directory directory;
  auto parent = Dot::Core::FileSystem::open(directory.root).value();
  auto mailbox = Dot::Core::Control::create(parent).value();
  const auto control = mailbox.files()->root();
  REQUIRE(parent.writeTextAtomic("输入 request.json", R"({"command":"inspect"})"));
  std::filesystem::path response;

  for (const auto* executable : {DOT_RUNTIME_EXECUTABLE, DOT_GAME_EXECUTABLE}) {
    auto result = Dot::Core::runProcess(
        {executable,
         {"send", utf(control), "same-id", utf(directory.root / "输入 request.json")},
         directory.root});
    REQUIRE(result);
    REQUIRE(result->exitCode == 0);
    const auto receipt = Json::parse(result->standardOutput);
    CHECK(receipt["queued"] == true);
    response = receipt["response"].get<std::string>();
    CHECK_FALSE(std::filesystem::exists(response));
  }

  std::size_t calls{};
  REQUIRE(mailbox.process([&](std::string_view) -> Dot::Core::Result<std::string> {
    ++calls;
    return R"({"ok":true,"value":"actual"})";
  }));
  CHECK(calls == 1);
  CHECK(Json::parse(
            mailbox.receipts()->readText("responses/same-id.json").value())["response"]["value"]
        == "actual");
  REQUIRE(parent.writeTextAtomic("输入 request.json", R"({"command":"close"})"));
  auto conflict = Dot::Core::runProcess(
      {DOT_GAME_EXECUTABLE,
       {"send", utf(control), "same-id", utf(directory.root / "输入 request.json")},
       directory.root});
  REQUIRE(conflict);
  CHECK(conflict->exitCode == 1);
  CHECK(calls == 1);
  const auto archive = mailbox.receipts()->root();

  for (const auto* executable :
       {DOT_EDITOR_EXECUTABLE, DOT_RUNTIME_EXECUTABLE, DOT_GAME_EXECUTABLE}) {
    auto status
        = Dot::Core::runProcess({executable, {"control-status", utf(archive)}, directory.root});
    REQUIRE(status);
    REQUIRE(status->exitCode == 0);
    CHECK(Json::parse(status->standardOutput)["state"] == "ready");
    auto activeClear
        = Dot::Core::runProcess({executable, {"clear-control", utf(archive)}, directory.root});
    REQUIRE(activeClear);
    CHECK(activeClear->exitCode == 1);
  }

  REQUIRE(mailbox.close());
  CHECK_FALSE(std::filesystem::exists(control));
  CHECK(std::filesystem::exists(response));

  for (const auto* executable :
       {DOT_EDITOR_EXECUTABLE, DOT_RUNTIME_EXECUTABLE, DOT_GAME_EXECUTABLE}) {
    auto status
        = Dot::Core::runProcess({executable, {"control-status", utf(archive)}, directory.root});
    REQUIRE(status);
    REQUIRE(status->exitCode == 0);
    CHECK(Json::parse(status->standardOutput)["state"] == "closed");
    auto ack = Dot::Core::runProcess({executable, {"ack-control", utf(response)}, directory.root});
    REQUIRE(ack);
    CHECK(ack->exitCode == 0);
    CHECK(Json::parse(
              Dot::Core::FileSystem::open(archive)->readText("responses/same-id.json").value())
          == (Json{{"consumed", true}}));
  }

  auto cleared = Dot::Core::runProcess(
      {DOT_GAME_EXECUTABLE, {"clear-control", utf(archive)}, directory.root});
  REQUIRE(cleared);
  CHECK(cleared->exitCode == 0);
  CHECK_FALSE(std::filesystem::exists(archive));
  CHECK(parent.readText("输入 request.json").value() == R"({"command":"close"})");
  CHECK(Dot::Core::processResources().children == 0);
  CHECK(Dot::Core::processResources().handles == 0);
}

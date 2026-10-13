#include <doctest/doctest.h>

#include <chrono>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <utility>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Editor.Workspace;
import Dot.Project;
import Dot.Project.GameSession;
import Dot.Script.Compiler;
import Dot.Script.Host;
import Dot.Graphics.Renderer;
import Dot.World.SceneFiles;

namespace {
  using namespace Dot::Editor;
  using namespace std::chrono_literals;
  using Json = nlohmann::json;

  struct Fixture {
    Dot::Core::StagedDirectory storage;
    Dot::Project::Project project;

    Fixture()
        : storage(Dot::Core::StagedDirectory::create(
                      Dot::Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
                      .value()),
          project(Dot::Project::Project::create(
                      storage.files().value(), "project", {},
                      Dot::Core::FileSystem::open(DOT_SCRIPT_SDK_DIRECTORY).value())
                      .value()) {
    }

    auto open() -> Workspace {
      WorkspaceConfig config;
      config.font = DOT_EDITOR_FONT;
      config.compiler = DOT_TYPESCRIPT_COMPILER;
      config.play.session.audioMode = Dot::Project::SessionAudio::Offline;
      config.play.session.pollInterval = 0ms;
      auto result = Workspace::open(project, config);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }
  };

  auto snapshot(Workspace& workspace) -> Json {
    auto text = workspace.snapshot();
    REQUIRE(text);
    return Json::parse(*text);
  }

  auto command(Workspace& workspace, Json request) -> Json {
    request["revision"] = snapshot(workspace)["revision"];
    auto response = workspace.dispatch(request.dump());
    REQUIRE_MESSAGE(response, (response ? "" : response.error().message));
    return Json::parse(*response);
  }

  auto rename(Workspace& workspace) -> void {
    command(workspace,
            {{"command", "edit"},
             {"operations", Json::array({{{"op", "name"}, {"entity", "2"}, {"name", "玩家"}}})}});
  }
}  // namespace

TEST_CASE("workspace.strict_requests_and_dirty_close_preserve_document") {
  Fixture fixture;
  auto workspace = fixture.open();
  const auto initial = workspace.snapshot().value();

  for (const auto* request : {"", "[]", "{\"command\":\"inspect\",\"command\":\"close\"}",
                              "{\"command\":\"close\",\"discard\":1}",
                              "{\"command\":\"close\",\"discard\":false,\"extra\":0}"}) {
    CHECK_FALSE(workspace.dispatch(request));
    CHECK(workspace.snapshot().value() == initial);
  }

  auto inspected = Json::parse(workspace.dispatch("{\"command\":\"inspect\"}").value());
  CHECK_FALSE(inspected["workspace"]["pendingInspector"].get<bool>());
  command(workspace, {{"command", "apply-inspector"}});
  command(workspace, {{"command", "reset-inspector"}});
  CHECK_FALSE(workspace.dispatch("{\"command\":\"reset-inspector\",\"revision\":\"2\"}"));
  CHECK(workspace.snapshot().value() == initial);
  rename(workspace);
  CHECK(snapshot(workspace)["dirty"] == true);
  auto close = workspace.dispatch("{\"command\":\"close\",\"discard\":false}");
  REQUIRE(close);
  CHECK(Json::parse(*close)["confirmation"] == true);
  CHECK(Json::parse(*close)["running"] == true);
  command(workspace, {{"command", "save"}});
  CHECK_FALSE(snapshot(workspace)["dirty"].get<bool>());
  close = workspace.dispatch("{\"command\":\"close\",\"discard\":false}");
  REQUIRE(close);
  CHECK_FALSE(Json::parse(*close)["confirmation"].get<bool>());
  CHECK_FALSE(Json::parse(*close)["running"].get<bool>());
  auto saved = fixture.project.loadStartup().value();
  CHECK(saved.entities()->at(1).name == "玩家");
}

TEST_CASE("workspace.camera_absence_is_visible_and_undo_recovers_preview") {
  Fixture fixture;
  auto workspace = fixture.open();
  REQUIRE(workspace.step(16ms));
  REQUIRE(workspace.step(16ms));
  REQUIRE(workspace.step(16ms));
  const auto before = workspace.captureViewport().value();
  command(workspace, {{"command", "edit"},
                      {"operations", Json::array({{{"op", "component"},
                                                   {"entity", "1"},
                                                   {"component", "camera"},
                                                   {"value", nullptr}}})}});
  REQUIRE(workspace.step(16ms));
  CHECK_FALSE(workspace.captureViewport());
  command(workspace, {{"command", "undo"}});
  REQUIRE(workspace.step(16ms));
  CHECK(workspace.captureViewport()->pixels == before.pixels);
}

TEST_CASE("workspace.mailbox_bad_commands_return_errors_and_allow_later_requests") {
  Fixture fixture;
  auto workspace = fixture.open();
  auto control = workspace.controlDirectory().value();
  auto files = Dot::Core::FileSystem::open(control).value();
  REQUIRE(files.writeTextAtomic("requests/001.json", "{\"command\":\"close\",\"discard\":1}"));
  REQUIRE(files.writeTextAtomic("requests/001-invalid-utf8.json",
                                std::string(1, static_cast<char>(0xFF))));
  REQUIRE(submitWorkspaceRequest(control, "002", "{\"command\":\"inspect\"}"));
  const auto initial = workspace.snapshot().value();
  REQUIRE(workspace.step(16ms));
  CHECK(Json::parse(files.readText("responses/001.json").value())["response"]["ok"] == false);
  const auto invalidUtf = Json::parse(files.readText("responses/001-invalid-utf8.json").value());
  CHECK(invalidUtf["source"].is_null());
  CHECK(invalidUtf["response"]["error"]["code"] == "InvalidArgument");
  CHECK(Json::parse(files.readText("responses/002.json").value())["response"]["ok"] == true);
  CHECK(workspace.snapshot().value() == initial);
  CHECK_FALSE(submitWorkspaceRequest(control, std::string(65, 'a'), "{}"));
  CHECK_FALSE(submitWorkspaceRequest(control, "oversize", std::string(1024 * 1024 + 1, ' ')));
  CHECK_FALSE(submitWorkspaceRequest(control, "nul", std::string("{}\0", 3)));
}

TEST_CASE("workspace.response_io_failure_is_terminal_and_does_not_replay") {
  Fixture fixture;
  auto workspace = fixture.open();
  const auto control = workspace.controlDirectory().value();
  auto files = Dot::Core::FileSystem::open(control).value();
  Json request{{"command", "edit"},
               {"revision", "1"},
               {"operations", Json::array({{{"op", "create"}, {"name", "Once"}}})}};
  REQUIRE(submitWorkspaceRequest(control, "001", request.dump()));
  std::filesystem::remove(control / "responses");
  REQUIRE(files.writeTextAtomic("responses", "not a directory"));
  auto result = workspace.step(16ms);
  REQUIRE_FALSE(result);
  CHECK(result.error().message.find("may have committed") != std::string::npos);
  const auto after = workspace.snapshot().value();
  CHECK(snapshot(workspace)["scene"]["entities"].size() == 3);
  std::filesystem::remove(control / "responses");
  REQUIRE(files.createDirectories("responses"));
  CHECK_FALSE(workspace.step(16ms));
  CHECK_FALSE(workspace.run(1));
  CHECK(workspace.snapshot().value() == after);
  CHECK_FALSE(std::filesystem::exists(control / "responses/001.json"));
  command(workspace, {{"command", "save"}});
  CHECK_FALSE(snapshot(workspace)["dirty"].get<bool>());
}

TEST_CASE("workspace.pending_queue_overflow_fails_before_effects") {
  Fixture fixture;
  auto workspace = fixture.open();
  auto files = Dot::Core::FileSystem::open(workspace.controlDirectory().value()).value();

  for (int index = 0; index < 257; ++index) {
    REQUIRE(files.writeTextAtomic("requests/" + std::to_string(index) + ".json",
                                  "{\"command\":\"inspect\"}"));
  }

  const auto initial = workspace.snapshot().value();
  auto result = workspace.step(16ms);
  REQUIRE_FALSE(result);
  CHECK(result.error().code == Dot::Core::ErrorCode::Unavailable);
  CHECK(workspace.snapshot().value() == initial);
  CHECK(std::filesystem::is_empty(files.root() / "responses"));
}

TEST_CASE("workspace.invalid_initialization_move_and_reopen_release_resources") {
  Fixture fixture;
  const auto contexts = Dot::Graphics::guiContexts();
  WorkspaceConfig invalid;
  invalid.width = 0;
  invalid.font = DOT_EDITOR_FONT;
  CHECK_FALSE(Workspace::open(fixture.project, invalid));

  for (int cycle = 0; cycle < 2; ++cycle) {
    auto source = fixture.open();
    auto destination = std::move(source);
    CHECK_FALSE(source.snapshot());
    CHECK_FALSE(source.step(0ns));
    CHECK_FALSE(source.dispatch("{}"));
    CHECK_FALSE(source.controlDirectory());
    CHECK_FALSE(source.captureViewport());
    const auto before = destination.snapshot().value();
    CHECK_FALSE(destination.step(-1ns));
    CHECK(destination.snapshot().value() == before);
    REQUIRE(destination.step(16ms));
  }

  CHECK(Dot::Graphics::guiContexts() == contexts);
}

TEST_CASE("workspace.play_uses_unsaved_scene_and_stop_restores_authoring_pixels") {
  Fixture fixture;
  auto workspace = fixture.open();
  auto transform = snapshot(workspace)["scene"]["entities"][1]["transform"];
  transform["position"][0] = 0.25;
  command(workspace,
          {{"command", "edit"},
           {"operations",
            Json::array({{{"op", "transform"}, {"entity", "2"}, {"value", transform}}})}});
  REQUIRE(fixture.project.files()->writeTextAtomic(
      "scripts/mover.ts",
      "import {ScriptComponent} from './dot/engine.js';export class Mover extends ScriptComponent "
      "{onUpdate(){const "
      "t=this.self.transform;t.position.x+=.2;this.self.transform=t;}onStop(){this.self.name='"
      "stopped';}}"));

  for (int frame = 0; frame < 3; ++frame) {
    REQUIRE(workspace.step(std::chrono::milliseconds{16}));
  }

  const auto authored = workspace.snapshot().value();
  const auto before = workspace.captureViewport().value();
  const auto generations = Dot::Script::compilerGenerations();
  auto playing = command(workspace, {{"command", "play"}});
  CHECK(playing["workspace"]["play"]["state"] == "running");
  CHECK_FALSE(workspace.captureViewport());
  CHECK_FALSE(workspace.dispatch("{\"command\":\"play\",\"revision\":\"2\"}"));
  CHECK_FALSE(workspace.dispatch("{\"command\":\"undo\",\"revision\":\"2\"}"));

  for (int frame = 0; frame < 3; ++frame) {
    REQUIRE(workspace.step(std::chrono::milliseconds{16}));
  }

  auto runtime = Json::parse(workspace.dispatch("{\"command\":\"inspect-play\"}").value());
  CHECK(runtime["scene"]["entities"][1]["transform"]["position"][0].get<double>() > 0.25);
  CHECK(runtime["play"]["frames"] == "3");
  CHECK(workspace.captureViewport()->pixels != before.pixels);
  CHECK(workspace.snapshot().value() == authored);
  CHECK(fixture.project.loadStartup()->entities()->at(1).transform.position.x == 0);
  CHECK(command(workspace, {{"command", "stop-play"}})["workspace"]["play"]["state"] == "stopped");
  CHECK_FALSE(workspace.captureViewport());
  REQUIRE(workspace.step(std::chrono::milliseconds{16}));
  CHECK(workspace.captureViewport()->pixels == before.pixels);
  CHECK(workspace.snapshot().value() == authored);
  CHECK(Dot::Script::compilerGenerations() == generations);
  runtime = Json::parse(workspace.dispatch("{\"command\":\"inspect-play\"}").value());
  CHECK(runtime["scene"]["entities"][1]["name"] == "stopped");
  command(workspace, {{"command", "play"}});
  REQUIRE(workspace.dispatch("{\"command\":\"close\",\"discard\":true}"));
  CHECK(Dot::Script::compilerGenerations() == generations);
}

TEST_CASE("workspace.play_failure_and_realm_fault_keep_editor_recoverable") {
  Fixture fixture;
  auto workspace = fixture.open();
  const auto initial = workspace.snapshot().value();
  const auto generations = Dot::Script::compilerGenerations();
  REQUIRE(
      fixture.project.files()->writeTextAtomic("scripts/mover.ts", "export const bad:number='x';"));
  CHECK_FALSE(workspace.dispatch("{\"command\":\"play\",\"revision\":\"1\"}"));
  CHECK(workspace.snapshot().value() == initial);
  CHECK(Dot::Script::compilerGenerations() == generations);
  REQUIRE(fixture.project.files()->writeTextAtomic(
      "scripts/mover.ts",
      "import {ScriptComponent} from './dot/engine.js';export class Mover extends ScriptComponent "
      "{onUpdate(){while(true){}}}"));
  command(workspace, {{"command", "play"}});
  REQUIRE(workspace.step(std::chrono::milliseconds{16}));
  auto runtime = Json::parse(workspace.dispatch("{\"command\":\"inspect-play\"}").value());
  CHECK(runtime["play"]["state"] == "faulted");
  CHECK_FALSE(runtime["play"]["error"].is_null());
  CHECK_FALSE(runtime["play"]["diagnostics"].empty());
  CHECK(workspace.snapshot().value() == initial);
  command(workspace, {{"command", "stop-play"}});
  CHECK(Dot::Script::compilerGenerations() == generations);
  command(workspace,
          {{"command", "edit"},
           {"operations",
            Json::array({{{"op", "name"}, {"entity", "2"}, {"name", "Recovered editor"}}})}});
  CHECK(snapshot(workspace)["scene"]["entities"][1]["name"] == "Recovered editor");
}

TEST_CASE("workspace.direct_play_commands_reject_reentrant_script_services") {
  Fixture fixture;
  REQUIRE(fixture.project.files()->writeTextAtomic(
      "scripts/mover.ts",
      "import {ScriptComponent} from './dot/engine.js';import {invoke} from 'dot:host';export "
      "class Mover extends ScriptComponent "
      "{onStart(){invoke('test.reentry',[]);}onStop(){invoke('test.reentry',[]);}}"));
  WorkspaceConfig config;
  config.font = DOT_EDITOR_FONT;
  config.compiler = DOT_TYPESCRIPT_COMPILER;
  config.play.session.audioMode = Dot::Project::SessionAudio::Offline;
  Workspace* current = nullptr;
  unsigned rejected = 0;
  config.play.session.scripts.services.push_back(
      {"test.reentry", [&](std::string_view) -> Dot::Core::Result<std::optional<std::string>> {
         auto result = current->dispatch("{\"command\":\"inspect\"}");

         if (!result && result.error().code == Dot::Core::ErrorCode::InvalidState
             && !current->snapshot() && !current->captureViewport()) {
           ++rejected;
         }

         return std::optional<std::string>{};
       }});
  auto workspace = Workspace::open(fixture.project, config).value();
  current = &workspace;
  const auto before = workspace.snapshot().value();
  command(workspace, {{"command", "play"}});
  CHECK(rejected == 1);
  command(workspace, {{"command", "stop-play"}});
  CHECK(rejected == 2);
  CHECK(workspace.snapshot().value() == before);
}

TEST_CASE("workspace.close_confirmation_cancel_and_save_close_share_play_cleanup") {
  Fixture fixture;
  auto workspace = fixture.open();
  CHECK_FALSE(workspace.dispatch("{\"command\":\"capture-input\",\"capture\":true}"));
  rename(workspace);
  const auto generations = Dot::Script::compilerGenerations();
  command(workspace, {{"command", "play"}});
  CHECK_FALSE(workspace.dispatch("{\"command\":\"capture-input\",\"capture\":1}"));
  auto captured
      = Json::parse(workspace.dispatch("{\"command\":\"capture-input\",\"capture\":true}").value());
  CHECK(captured["workspace"]["play"]["captureInput"] == true);
  auto closing
      = Json::parse(workspace.dispatch("{\"command\":\"close\",\"discard\":false}").value());
  CHECK(closing["confirmation"] == true);
  CHECK(closing["running"] == true);
  auto canceled = command(workspace, {{"command", "cancel-close"}});
  CHECK_FALSE(canceled["workspace"]["pendingClose"].get<bool>());
  CHECK(canceled["workspace"]["play"]["state"] == "running");
  CHECK_FALSE(workspace.dispatch("{\"command\":\"save-close\",\"revision\":\"1\"}"));
  CHECK(snapshot(workspace)["dirty"] == true);
  auto saved = command(workspace, {{"command", "save-close"}});
  CHECK_FALSE(saved["running"].get<bool>());
  CHECK_FALSE(saved["confirmation"].get<bool>());
  CHECK_FALSE(snapshot(workspace)["dirty"].get<bool>());
  CHECK(fixture.project.loadStartup()->entities()->at(1).name == "玩家");
  CHECK(Dot::Script::compilerGenerations() == generations);
}

TEST_CASE("workspace.save_close_conflict_stops_game_but_keeps_unsaved_editor_open") {
  Fixture fixture;
  auto disk = fixture.project.loadStartup().value();
  const auto initialDisk = Dot::World::encodeScene(disk).value();
  auto workspace = fixture.open();
  rename(workspace);
  const auto authored = workspace.snapshot().value();
  const auto generations = Dot::Script::compilerGenerations();
  command(workspace, {{"command", "play"}});
  REQUIRE(fixture.project.files()->writeTextAtomic("scenes/main.dotscene", "{malformed"));
  auto failed = workspace.dispatch("{\"command\":\"save-close\",\"revision\":\"2\"}");
  CHECK_FALSE(failed);
  CHECK(workspace.snapshot().value() == authored);
  auto inspected = Json::parse(workspace.dispatch("{\"command\":\"inspect\"}").value());
  CHECK(inspected["workspace"]["running"] == true);
  CHECK(inspected["workspace"]["play"]["state"] == "stopped");
  CHECK(Dot::Script::compilerGenerations() == generations);
  REQUIRE(fixture.project.files()->writeTextAtomic("scenes/main.dotscene", initialDisk));
  CHECK_FALSE(command(workspace, {{"command", "save-close"}})["running"].get<bool>());
}

TEST_CASE("workspace.resource_creation_nested_script_check_and_buffer_identity") {
  Fixture fixture;
  auto workspace = fixture.open();
  const auto original = workspace.snapshot().value();
  REQUIRE(
      workspace.dispatch("{\"command\":\"create-directory\",\"path\":\"scripts/controllers\"}"));
  REQUIRE(workspace.dispatch(
      "{\"command\":\"create-script\",\"path\":\"scripts/controllers/player.ts\"}"));
  REQUIRE(workspace.dispatch("{\"command\":\"check-scripts\"}"));
  auto opened = Json::parse(workspace
                                .dispatch("{\"command\":\"open-text\",\"path\":\"scripts/"
                                          "controllers/player.ts\",\"discard\":false}")
                                .value());
  const auto first = opened["file"];
  CHECK(first["text"].get<std::string>().find("../dot/engine.js") != std::string::npos);
  REQUIRE(workspace.dispatch(Json{{"command", "replace-text"},
                                  {"buffer", first["buffer"]},
                                  {"revision", first["revision"]},
                                  {"text", "export const draft=1;"}}
                                 .dump()));
  CHECK_FALSE(workspace.dispatch("{\"command\":\"check-scripts\"}"));
  CHECK_FALSE(workspace.dispatch(
      "{\"command\":\"open-text\",\"path\":\"scripts/mover.ts\",\"discard\":false}"));
  opened = Json::parse(workspace
                           .dispatch("{\"command\":\"open-text\",\"path\":\"scripts/controllers/"
                                     "player.ts\",\"discard\":true}")
                           .value());
  CHECK(opened["file"]["buffer"] != first["buffer"]);
  CHECK_FALSE(workspace.dispatch(Json{{"command", "replace-text"},
                                      {"buffer", first["buffer"]},
                                      {"revision", "1"},
                                      {"text", "stale write"}}
                                     .dump()));
  auto resources = Json::parse(workspace.dispatch("{\"command\":\"resources\"}").value());
  CHECK(resources["file"]["text"] == first["text"]);
  CHECK(workspace.snapshot().value() == original);
  CHECK_FALSE(workspace.dispatch("{\"command\":\"create-script\",\"path\":\"assets/fake.ts\"}"));
  CHECK_FALSE(
      workspace.dispatch("{\"command\":\"create-script\",\"path\":\"scripts/dot/fake.ts\"}"));
}

TEST_CASE("workspace.resource_draft_conflict_and_save_close_preserve_editor") {
  Fixture fixture;
  auto workspace = fixture.open();
  auto file = Json::parse(
      workspace
          .dispatch("{\"command\":\"open-text\",\"path\":\"scripts/mover.ts\",\"discard\":false}")
          .value())["file"];
  const auto baseline = file["text"].get<std::string>();
  REQUIRE(workspace.dispatch(Json{{"command", "replace-text"},
                                  {"buffer", file["buffer"]},
                                  {"revision", "1"},
                                  {"text", "export const edited=1;"}}
                                 .dump()));
  CHECK_FALSE(workspace.dispatch("{\"command\":\"play\",\"revision\":\"1\"}"));
  auto closed
      = Json::parse(workspace.dispatch("{\"command\":\"close\",\"discard\":false}").value());
  CHECK(closed["confirmation"] == true);
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/mover.ts", "external"));
  CHECK_FALSE(workspace.dispatch("{\"command\":\"save-close\",\"revision\":\"1\"}"));
  auto status = Json::parse(workspace.dispatch("{\"command\":\"inspect\"}").value());
  CHECK(status["workspace"]["running"] == true);
  CHECK(status["workspace"]["file"]["dirty"] == true);
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/mover.ts", baseline));
  CHECK_FALSE(command(workspace, {{"command", "save-close"}})["running"].get<bool>());
  CHECK(fixture.project.files()->readText("scripts/mover.ts").value() == "export const edited=1;");
}

TEST_CASE("workspace.saved_resource_mesh_refresh_changes_real_cached_gpu_geometry") {
  Fixture fixture;
  auto workspace = fixture.open();
  const std::string triangle
      = "v -0.8 -0.8 0\nv 0.8 -0.8 0\nv 0 0.8 0\nvn 0 0 1\nf 1//1 2//1 3//1\n";
  REQUIRE(workspace.dispatch(
      Json{{"command", "create-text"}, {"path", "assets/shape.obj"}, {"text", triangle}}.dump()));
  auto mesh = snapshot(workspace)["scene"]["entities"][1]["mesh"];
  mesh["kind"] = "asset";
  mesh["asset"] = "assets/shape.obj";
  command(
      workspace,
      {{"command", "edit"},
       {"operations",
        Json::array(
            {{{"op", "component"}, {"entity", "2"}, {"component", "mesh"}, {"value", mesh}}})}});

  for (int frame = 0; frame < 3; ++frame) {
    REQUIRE(workspace.step(std::chrono::milliseconds{16}));
  }

  const auto before = workspace.captureViewport().value();
  auto file = Json::parse(
      workspace
          .dispatch("{\"command\":\"open-text\",\"path\":\"assets/shape.obj\",\"discard\":false}")
          .value())["file"];
  const std::string smaller
      = "v -0.2 -0.2 0\nv 0.2 -0.2 0\nv 0 0.2 0\nvn 0 0 1\nf 1//1 2//1 3//1\n";
  REQUIRE(workspace.dispatch(Json{
      {"command", "replace-text"}, {"buffer", file["buffer"]}, {"revision", "1"}, {"text", smaller}}
                                 .dump()));
  REQUIRE(workspace.dispatch(
      Json{{"command", "save-text"}, {"buffer", file["buffer"]}, {"revision", "2"}}.dump()));
  REQUIRE(workspace.step(std::chrono::milliseconds{16}));
  CHECK(workspace.captureViewport()->pixels != before.pixels);
}

TEST_CASE("workspace.resource_import_readonly_sdk_and_strict_requests_preserve_state") {
  Fixture fixture;
  auto source = fixture.storage.files().value();
  const std::string bytes("A\0B", 3);
  REQUIRE(source.writeTextAtomic("input.bin", bytes));
  auto workspace = fixture.open();
  const auto utf = (source.root() / "input.bin").generic_u8string();
  const std::string input(utf.begin(), utf.end());
  REQUIRE(workspace.dispatch(
      Json{{"command", "import-resource"}, {"source", input}, {"path", "assets/owned.bin"}}
          .dump()));
  CHECK(fixture.project.files()->readText("assets/owned.bin").value() == bytes);
  CHECK(source.readText("input.bin").value() == bytes);
  auto opened = Json::parse(
      workspace
          .dispatch(
              "{\"command\":\"open-text\",\"path\":\"scripts/dot/engine.ts\",\"discard\":false}")
          .value());
  CHECK(opened["file"]["readOnly"] == true);
  auto file = opened["file"];
  CHECK_FALSE(workspace.dispatch(Json{{"command", "replace-text"},
                                      {"buffer", file["buffer"]},
                                      {"revision", "1"},
                                      {"text", "SDK overwrite"}}
                                     .dump()));
  CHECK_FALSE(workspace.dispatch(
      Json{{"command", "save-text"}, {"buffer", file["buffer"]}, {"revision", "1"}}.dump()));
  CHECK_FALSE(workspace.dispatch("{\"command\":\"resources\",\"extra\":0}"));
  CHECK_FALSE(workspace.dispatch("{\"command\":\"create-script\",\"path\":\"scripts/foo.d.ts\"}"));
  CHECK_FALSE(workspace.dispatch(
      "{\"command\":\"open-text\",\"path\":\"scripts/mover.ts\",\"discard\":1}"));
  auto after = Json::parse(workspace.dispatch("{\"command\":\"resources\"}").value());
  CHECK(after["file"] == file);
}

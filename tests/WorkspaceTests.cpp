#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <chrono>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "EnvironmentFixture.hpp"

import Dot.Core.Files;
import Dot.Assets.Material;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Editor.Workspace;
import Dot.Project;
import Dot.Project.GameSession;
import Dot.Project.GamePackage;
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
          project(Dot::Project::Project::create(storage.files().value(), "project", {}).value()) {
    }

    auto open(WorkspaceConfig config = {}) -> Workspace {
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

TEST_CASE("workspace.camera_absence_keeps_editor_view_and_game_mode_reports_then_recovers") {
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
  CHECK(workspace.captureViewport()->pixels == before.pixels);
  REQUIRE(workspace.dispatch("{\"command\":\"set-view\",\"mode\":\"game\"}"));
  REQUIRE(workspace.step(16ms));
  CHECK_FALSE(workspace.captureViewport());
  command(workspace, {{"command", "undo"}});
  REQUIRE(workspace.step(16ms));
  CHECK(workspace.captureViewport());
  REQUIRE(workspace.dispatch("{\"command\":\"set-view\",\"mode\":\"editor\"}"));
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
      "import {ScriptComponent} from 'dot:engine';export class Mover extends ScriptComponent "
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
      "import {ScriptComponent} from 'dot:engine';export class Mover extends ScriptComponent "
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
      "import {ScriptComponent} from 'dot:engine';import {invoke} from 'dot:host';export "
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
  CHECK(first["text"].get<std::string>().find("dot:engine") != std::string::npos);
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

TEST_CASE("workspace.material_staging_preview_history_save_and_reopen_use_real_scene") {
  Fixture fixture;
  auto workspace = fixture.open();
  REQUIRE(workspace.dispatch(
      "{\"command\":\"create-material\",\"path\":\"assets/surface.dotmaterial\"}"));
  command(workspace,
          {{"command", "edit"},
           {"operations", Json::array({{{"op", "component"},
                                        {"entity", "2"},
                                        {"component", "material"},
                                        {"value", {{"asset", "assets/surface.dotmaterial"}}}}})}});
  auto file = Json::parse(workspace
                              .dispatch("{\"command\":\"open-material\",\"path\":\"assets/"
                                        "surface.dotmaterial\",\"discard\":false}")
                              .value())["material"];
  auto value = file["value"];
  value["model"] = "unlit";
  value["baseColor"] = {1, 0, 0, 1};
  const auto scene = workspace.snapshot().value();
  const auto files = fixture.project.files().value();
  const auto disk = files.readText("assets/surface.dotmaterial").value();

  for (int i = 0; i < 3; ++i) {
    REQUIRE(workspace.step(16ms));
  }

  const auto baseline = workspace.captureViewport()->pixels;
  const auto materialCommand = [&](const char* name, std::optional<Json> payload = {}) {
    auto state = Json::parse(workspace.dispatch("{\"command\":\"resources\"}").value())["material"];
    Json request{{"command", name}, {"buffer", state["buffer"]}, {"revision", state["revision"]}};

    if (payload) {
      request["value"] = *payload;
    }

    auto response = workspace.dispatch(request.dump());
    REQUIRE_MESSAGE(response, (response ? "" : response.error().message));
    return Json::parse(*response)["material"];
  };
  const auto staged = materialCommand("stage-material", value);
  CHECK(staged["revision"] == file["revision"]);
  CHECK(staged["dirty"].get<bool>());
  REQUIRE(workspace.step(16ms));
  const auto red = workspace.captureViewport()->pixels;
  CHECK(red != baseline);
  CHECK(files.readText("assets/surface.dotmaterial").value() == disk);
  CHECK(workspace.snapshot().value() == scene);
  materialCommand("reset-material");
  REQUIRE(workspace.step(16ms));
  CHECK(workspace.captureViewport()->pixels == baseline);
  materialCommand("edit-material", value);
  REQUIRE(workspace.step(16ms));
  CHECK(workspace.captureViewport()->pixels == red);
  const auto saved = materialCommand("save-material");
  CHECK_FALSE(saved["dirty"].get<bool>());
  CHECK(Dot::Assets::loadMaterial(files, "assets/surface.dotmaterial")->baseColor.x == 1);
  materialCommand("undo-material");
  REQUIRE(workspace.step(16ms));
  CHECK(workspace.captureViewport()->pixels == baseline);
  materialCommand("redo-material");
  REQUIRE(workspace.step(16ms));
  CHECK(workspace.captureViewport()->pixels == red);
  REQUIRE(workspace.dispatch(Json{
      {"command", "close-material"},
      {"buffer", saved["buffer"]},
      {"revision", materialCommand("reset-material")["revision"]},
      {"discard", false}}.dump()));
  const auto reopened
      = Json::parse(workspace
                        .dispatch("{\"command\":\"open-material\",\"path\":\"assets/"
                                  "surface.dotmaterial\",\"discard\":false}")
                        .value())["material"];
  CHECK(reopened["buffer"] != file["buffer"]);
  CHECK(reopened["value"] == value);
  const auto savedSource = files.readText("assets/surface.dotmaterial").value();
  auto savedBlue = Dot::Assets::decodeMaterial(savedSource).value();
  savedBlue.baseColor = {0, 0, 1, 1};
  REQUIRE(Dot::Assets::saveMaterial(files, "assets/surface.dotmaterial", savedBlue));
  command(workspace, {{"command", "play"}});
  REQUIRE(workspace.step(16ms));
  auto game = workspace.captureViewport().value();
  bool bluePixel{};

  for (std::size_t i = 0; i < game.pixels.size(); i += 4) {
    bluePixel = bluePixel
                || (game.pixels[i + 2] > game.pixels[i] + 50
                    && game.pixels[i + 2] > game.pixels[i + 1] + 50);
  }

  CHECK(bluePixel);
  CHECK_FALSE(workspace.dispatch(Json{
      {"command", "edit-material"},
      {"buffer", reopened["buffer"]},
      {"revision", reopened["revision"]},
      {"value", value}}.dump()));
  command(workspace, {{"command", "stop-play"}});
  REQUIRE(workspace.step(16ms));
  CHECK(workspace.captureViewport()->pixels == red);
  REQUIRE(files.writeTextAtomic("assets/surface.dotmaterial", savedSource));
}

TEST_CASE("workspace.material_failures_dirty_guards_and_close_save_keep_owned_draft") {
  Fixture fixture;
  auto workspace = fixture.open();
  REQUIRE(workspace.dispatch(
      "{\"command\":\"create-material\",\"path\":\"assets/surface.dotmaterial\"}"));
  CHECK_FALSE(workspace.dispatch(
      "{\"command\":\"create-material\",\"path\":\"assets/surface.dotmaterial\"}"));
  CHECK_FALSE(workspace.dispatch(
      "{\"command\":\"open-material\",\"path\":\"assets/no.dotmaterial\",\"discard\":false}"));
  auto state = Json::parse(workspace
                               .dispatch("{\"command\":\"open-material\",\"path\":\"assets/"
                                         "surface.dotmaterial\",\"discard\":false}")
                               .value())["material"];
  auto value = state["value"];
  value["roughness"] = .15;
  Json request{{"command", "edit-material"},
               {"buffer", state["buffer"]},
               {"revision", state["revision"]},
               {"value", value}};
  auto malformed = request;
  malformed["value"]["alphaMode"] = "invalid";
  CHECK_FALSE(workspace.dispatch(malformed.dump()));
  state = Json::parse(workspace.dispatch(request.dump()).value())["material"];
  CHECK_FALSE(workspace.dispatch(request.dump()));
  CHECK_FALSE(workspace.dispatch(
      "{\"command\":\"open-text\",\"path\":\"scripts/mover.ts\",\"discard\":false}"));
  CHECK_FALSE(workspace.dispatch(
      Json{{"command", "play"}, {"revision", snapshot(workspace)["revision"]}}.dump()));
  CHECK_FALSE(workspace.dispatch(Json{
      {"command", "close-material"},
      {"buffer", state["buffer"]},
      {"revision", state["revision"]},
      {"discard", false}}.dump()));
  auto files = fixture.project.files().value();
  const auto baseline = files.readText("assets/surface.dotmaterial").value();
  REQUIRE(files.writeTextAtomic("assets/surface.dotmaterial", baseline + "\n"));
  CHECK_FALSE(workspace.dispatch(Json{
      {"command", "save-material"}, {"buffer", state["buffer"]}, {"revision", state["revision"]}}
                                     .dump()));
  CHECK(Json::parse(workspace.dispatch("{\"command\":\"resources\"}").value())["material"]["dirty"]
            .get<bool>());
  REQUIRE(files.writeTextAtomic("assets/surface.dotmaterial", baseline));
  auto closing
      = Json::parse(workspace.dispatch("{\"command\":\"close\",\"discard\":false}").value());
  CHECK(closing["confirmation"].get<bool>());
  REQUIRE(workspace.dispatch(
      Json{{"command", "save-close"}, {"revision", snapshot(workspace)["revision"]}}.dump()));
  CHECK(Dot::Assets::loadMaterial(files, "assets/surface.dotmaterial")->roughness
        == doctest::Approx(.15));
}

TEST_CASE("workspace.resource_import_readonly_sdk_and_strict_requests_preserve_state") {
  Fixture fixture;
  auto source = fixture.storage.files().value();
  const std::string bytes("A\0B", 3);
  REQUIRE(source.writeTextAtomic("input.bin", bytes));
  REQUIRE(fixture.project.files()->createDirectories("scripts/dot"));
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/dot/protected.ts",
                                                   "export const marker = true;"));
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
              "{\"command\":\"open-text\",\"path\":\"scripts/dot/protected.ts\",\"discard\":false}")
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

TEST_CASE("workspace.export_data_captures_unsaved_scene_and_guards_pending_state") {
  Fixture fixture;
  auto workspace = fixture.open();
  const auto saved = fixture.project.loadStartup()->entities()->at(1).name;
  rename(workspace);
  const auto before = workspace.snapshot().value();
  const auto parentPath = fixture.storage.files()->root().generic_u8string();
  const std::string parent(parentPath.begin(), parentPath.end());
  Json request{
      {"command", "export-data"}, {"parent", parent}, {"folder", "game"}, {"revision", "1"}};
  CHECK_FALSE(workspace.dispatch(request.dump()));
  auto response = command(workspace, request);
  CHECK(response["kind"] == "game-data");
  auto files = Dot::Core::FileSystem::open(fixture.storage.files()->root() / "game").value();
  auto game = Dot::Project::GamePackage::open(files).value();
  CHECK(game.scene()->entities()->at(1).name == "玩家");
  CHECK(fixture.project.loadStartup()->entities()->at(1).name == saved);
  CHECK(workspace.snapshot().value() == before);
  request["revision"] = snapshot(workspace)["revision"];
  CHECK_FALSE(workspace.dispatch(request.dump()));
  request["folder"] = "blocked";
  command(workspace, {{"command", "play"}});
  CHECK_FALSE(workspace.dispatch(request.dump()));
  command(workspace, {{"command", "stop-play"}});
  auto file = Json::parse(
      workspace
          .dispatch("{\"command\":\"open-text\",\"path\":\"scripts/mover.ts\",\"discard\":false}")
          .value())["file"];
  REQUIRE(workspace.dispatch(Json{{"command", "replace-text"},
                                  {"buffer", file["buffer"]},
                                  {"revision", "1"},
                                  {"text", "export const draft=1;"}}
                                 .dump()));
  CHECK_FALSE(workspace.dispatch(request.dump()));
  CHECK_FALSE(std::filesystem::exists(fixture.storage.files()->root() / "blocked"));
  CHECK(workspace.snapshot().value() == before);
}

TEST_CASE("workspace.export_native_game_preserves_unsaved_authoring") {
  Fixture fixture;
  auto workspace = fixture.open();
  rename(workspace);
  const auto before = workspace.snapshot().value();
  const auto parentPath = fixture.storage.files()->root().generic_u8string();
  const std::string parent(parentPath.begin(), parentPath.end());
  auto exported
      = command(workspace, {{"command", "export"}, {"parent", parent}, {"folder", "native"}});
  CHECK(exported["kind"] == "game-native");
  auto files = Dot::Core::FileSystem::open(fixture.storage.files()->root() / "native/game").value();
  auto package = Dot::Project::GamePackage::open(files).value();
  CHECK(package.scene()->entities()->at(1).name == "玩家");
  CHECK(fixture.project.loadStartup()->entities()->at(1).name != "玩家");
  CHECK(workspace.snapshot().value() == before);
  CHECK(std::filesystem::exists(fixture.storage.files()->root() / "native/runtime.json"));
  CHECK_FALSE(workspace.dispatch(
      Json{{"command", "export"}, {"parent", parent}, {"folder", "stale"}, {"revision", "1"}}
          .dump()));
}

TEST_CASE("workspace_material_and_light_authoring_preview_play_and_save_share_native_paths") {
  Fixture fixture;
  auto workspace = fixture.open();
  REQUIRE(workspace.step(16ms));
  REQUIRE(workspace.step(16ms));
  const auto before = workspace.captureViewport().value();
  Dot::Assets::MaterialDescription material;
  material.baseColor = {0.1F, 0.1F, 0.2F, 1};
  material.emissive = {0, 0, 4};
  material.metallic = 0.75F;
  auto text = Dot::Assets::encodeMaterial(material).value();
  REQUIRE(workspace.dispatch(Json{
      {"command", "create-text"},
      {"path", "assets/metal.dotmaterial"},
      {"text", text}}.dump()));
  Json light{{"kind", "directional"},  {"color", {1, 1, 1}},     {"intensity", 4}, {"range", 10},
             {"innerConeDegrees", 20}, {"outerConeDegrees", 30}, {"enabled", true}};
  command(
      workspace,
      {{"command", "edit"},
       {"operations",
        Json::array(
            {{{"op", "component"},
              {"entity", "2"},
              {"component", "material"},
              {"value", {{"asset", "assets/metal.dotmaterial"}}}},
             {{"op", "component"}, {"entity", "1"}, {"component", "light"}, {"value", light}}})}});
  REQUIRE(workspace.step(16ms));
  CHECK(workspace.captureViewport()->pixels != before.pixels);
  auto authored = snapshot(workspace);
  CHECK(authored["scene"]["entities"][0]["light"]["intensity"] == 4);
  command(workspace, {{"command", "undo"}});
  REQUIRE(workspace.step(16ms));
  const auto restoredImage = workspace.captureViewport().value();
  CHECK(restoredImage.width == before.width);
  CHECK(restoredImage.height == before.height);
  CHECK((restoredImage.pixels == before.pixels));
  command(workspace, {{"command", "redo"}});
  REQUIRE(workspace.step(16ms));
  const auto physical = workspace.captureViewport()->pixels;
  command(workspace, {{"command", "play"}});
  REQUIRE(workspace.step(16ms));
  REQUIRE(workspace.captureViewport());
  auto runtime = Json::parse(workspace.dispatch("{\"command\":\"inspect-play\"}").value());
  CHECK(runtime["scene"]["entities"][1]["material"]["asset"] == "assets/metal.dotmaterial");
  command(workspace, {{"command", "stop-play"}});
  REQUIRE(workspace.step(16ms));
  CHECK(workspace.captureViewport()->pixels == physical);
  command(workspace, {{"command", "save"}});
  auto saved = fixture.project.loadStartup().value();
  CHECK(saved.entities()->at(1).material->asset == "assets/metal.dotmaterial");
  CHECK(saved.entities()->front().light->intensity == 4);
}

TEST_CASE("workspace.external_open_uses_validated_source_and_preserves_agent_and_scene_state") {
  Fixture fixture;
  const auto original = fixture.project.files()->readText("scripts/mover.ts").value();
  std::filesystem::path opened;
  int calls = 0;
  Workspace* active = nullptr;
  WorkspaceConfig config;
  config.openExternal = [&](const std::filesystem::path& path) -> Dot::Core::Result<void> {
    opened = path;
    ++calls;
    CHECK_FALSE(active->dispatch("{\"command\":\"resources\"}"));
    return {};
  };
  auto workspace = fixture.open(config);
  active = &workspace;
  const auto before = workspace.snapshot().value();
  auto result = workspace.dispatch("{\"command\":\"open-external\",\"path\":\"scripts/mover.ts\"}");
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  const auto response = Json::parse(*result);
  CHECK(response["accepted"] == true);
  CHECK(response["uri"].get<std::string>().starts_with("file:///"));
  CHECK(opened == fixture.project.files()->root() / "scripts/mover.ts");
  CHECK(calls == 1);
  CHECK(fixture.project.files()->readText("scripts/mover.ts").value() == original);
  CHECK(workspace.snapshot().value() == before);
  const auto resources = Json::parse(workspace.dispatch("{\"command\":\"resources\"}").value());
  CHECK(resources["file"].is_null());
  CHECK(resources["selected"] == "scripts/mover.ts");
  REQUIRE(workspace.step(16ms));
  CHECK(workspace.captureViewport()->width > 0);
}

TEST_CASE("workspace.external_open_failures_and_pending_drafts_never_report_success") {
  Fixture fixture;
  int calls = 0;
  bool throws = false;
  WorkspaceConfig config;
  config.openExternal = [&](const std::filesystem::path&) -> Dot::Core::Result<void> {
    ++calls;

    if (throws) {
      throw std::runtime_error("Launch service exception");
    }

    return Dot::Core::fail(Dot::Core::ErrorCode::Unavailable, "No default source application");
  };
  auto workspace = fixture.open(config);
  auto missingHandler
      = workspace.dispatch("{\"command\":\"open-external\",\"path\":\"scripts/mover.ts\"}");
  REQUIRE_FALSE(missingHandler);
  CHECK(missingHandler.error().code == Dot::Core::ErrorCode::Unavailable);
  CHECK(missingHandler.error().message == "No default source application");
  CHECK(calls == 1);
  throws = true;
  auto exception
      = workspace.dispatch("{\"command\":\"open-external\",\"path\":\"scripts/mover.ts\"}");
  REQUIRE_FALSE(exception);
  CHECK(exception.error().code == Dot::Core::ErrorCode::External);
  CHECK(exception.error().message.find("Launch service exception") != std::string::npos);
  CHECK(calls == 2);

  for (const auto* path : {"../escape.ts", "scripts/missing.ts", "project.dotproject", "assets"}) {
    CHECK_FALSE(workspace.dispatch(Json{{"command", "open-external"}, {"path", path}}.dump()));
  }

  CHECK(calls == 2);
  auto file = Json::parse(
      workspace
          .dispatch("{\"command\":\"open-text\",\"path\":\"scripts/mover.ts\",\"discard\":false}")
          .value())["file"];
  REQUIRE(workspace.dispatch(Json{{"command", "replace-text"},
                                  {"buffer", file["buffer"]},
                                  {"revision", file["revision"]},
                                  {"text", "export const agentDraft=42;"}}
                                 .dump()));
  CHECK_FALSE(workspace.dispatch("{\"command\":\"open-external\",\"path\":\"scripts/mover.ts\"}"));
  CHECK(calls == 2);
  const auto retained
      = Json::parse(workspace.dispatch("{\"command\":\"resources\"}").value())["file"];
  CHECK(retained["dirty"] == true);
  CHECK(retained["text"] == "export const agentDraft=42;");
  REQUIRE(workspace.dispatch(Json{
      {"command", "close-text"},
      {"buffer", retained["buffer"]},
      {"revision", retained["revision"]},
      {"discard", true}}.dump()));
}

TEST_CASE("workspace.actual_compiler_diagnostics_map_to_openable_project_files_and_clear") {
  Fixture fixture;
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/bad.ts",
                                                   "export const bad:number='wrong';"));
  std::filesystem::path opened;
  WorkspaceConfig config;
  config.openExternal = [&](const std::filesystem::path& path) -> Dot::Core::Result<void> {
    opened = path;
    return {};
  };
  auto workspace = fixture.open(config);
  CHECK_FALSE(workspace.dispatch("{\"command\":\"check-scripts\"}"));
  auto status = Json::parse(workspace.dispatch("{\"command\":\"resources\"}").value());
  INFO(status["scripts"].dump());
  const auto diagnostics = status["scripts"]["diagnostics"];
  REQUIRE(diagnostics.size() == 1);
  CHECK(diagnostics[0]["path"] == "scripts/bad.ts");
  CHECK(diagnostics[0]["line"] == 1);
  CHECK(diagnostics[0]["column"].get<unsigned>() > 0);
  CHECK(diagnostics[0]["message"].get<std::string>().find("TS2322") != std::string::npos);
  REQUIRE(workspace.dispatch(
      Json{{"command", "open-external"}, {"path", diagnostics[0]["path"]}}.dump()));
  CHECK(opened == fixture.project.files()->root() / "scripts/bad.ts");
  REQUIRE(
      fixture.project.files()->writeTextAtomic("scripts/bad.ts", "export const bad:number=42;"));
  REQUIRE(workspace.dispatch("{\"command\":\"check-scripts\"}"));
  status = Json::parse(workspace.dispatch("{\"command\":\"resources\"}").value());
  CHECK(status["scripts"]["diagnostics"].empty());
}

TEST_CASE("workspace.environment_drafts_revision_apply_reset_save_play_and_reopen") {
  Fixture fixture;
  REQUIRE(DotTest::Environment::write(fixture.project.files().value()));
  auto workspace = fixture.open();
  REQUIRE(workspace.dispatch("{\"command\":\"show-pane\",\"pane\":\"environment\"}"));

  for (int frame = 0; frame < 2; ++frame) {
    auto rendered = workspace.step(std::chrono::milliseconds{16});
    REQUIRE_MESSAGE(rendered, (rendered ? "" : rendered.error().message));
  }
  CHECK(Json::parse(workspace.dispatch("{\"command\":\"inspect\"}").value())["workspace"]["pane"]
        == "environment");
  CHECK_FALSE(workspace.dispatch("{\"command\":\"show-pane\",\"pane\":\"missing\"}"));
  Json value{{"asset", "assets/studio.hdr"}, {"intensity", 2.0},   {"rotationDegrees", 45.0},
             {"background", true},           {"radianceSize", 16}, {"diffuseSize", 4},
             {"specularSize", 16},           {"brdfSize", 16},     {"samples", 32}};
  auto staged = command(workspace, {{"command", "stage-environment"}, {"value", value}});
  CHECK(staged["workspace"]["pendingEnvironment"] == true);
  CHECK(staged["scene"]["environment"].is_null());
  CHECK(staged["workspace"]["environmentDraft"] == value);
  auto closing = workspace.dispatch("{\"command\":\"close\",\"discard\":false}");
  REQUIRE(closing);
  CHECK(Json::parse(*closing)["confirmation"] == true);
  command(workspace, {{"command", "cancel-close"}});
  const auto current = snapshot(workspace)["revision"];
  CHECK_FALSE(workspace.dispatch(Json{{"command", "save"}, {"revision", current}}.dump()));
  CHECK_FALSE(workspace.dispatch(Json{{"command", "play"}, {"revision", current}}.dump()));
  CHECK_FALSE(workspace.dispatch(
      Json{{"command", "stage-environment"}, {"revision", "0"}, {"value", value}}.dump()));
  auto reset = command(workspace, {{"command", "reset-environment"}});
  CHECK(reset["workspace"]["pendingEnvironment"] == false);
  command(workspace, {{"command", "stage-environment"}, {"value", value}});
  auto applied = command(workspace, {{"command", "apply-environment"}});
  CHECK(applied["scene"]["environment"] == value);
  CHECK(applied["workspace"]["pendingEnvironment"] == false);
  REQUIRE(workspace.step(std::chrono::milliseconds{16}));
  command(workspace, {{"command", "undo"}});
  CHECK(snapshot(workspace)["scene"]["environment"].is_null());
  command(workspace, {{"command", "redo"}});
  command(workspace, {{"command", "save"}});
  command(workspace, {{"command", "play"}});
  REQUIRE(workspace.step(std::chrono::milliseconds{16}));
  command(workspace, {{"command", "stop-play"}});
  CHECK(snapshot(workspace)["scene"]["environment"] == value);
  value["intensity"] = 3.0;
  command(workspace, {{"command", "stage-environment"}, {"value", value}});
  REQUIRE(workspace.dispatch("{\"command\":\"close\",\"discard\":false}"));
  command(workspace, {{"command", "save-close"}});
  auto reopened = fixture.open();
  CHECK(snapshot(reopened)["scene"]["environment"] == value);
  REQUIRE(reopened.dispatch("{\"command\":\"close\",\"discard\":false}"));
}

TEST_CASE("workspace.occlusion_drafts_revision_apply_reset_save_play_export_and_reopen") {
  Fixture fixture;
  REQUIRE(DotTest::Environment::write(fixture.project.files().value()));
  auto workspace = fixture.open();
  REQUIRE(workspace.dispatch("{\"command\":\"show-pane\",\"pane\":\"rendering\"}"));
  REQUIRE(workspace.step(std::chrono::milliseconds{16}));
  REQUIRE(workspace.step(std::chrono::milliseconds{16}));
  CHECK(Json::parse(workspace.dispatch("{\"command\":\"inspect\"}").value())["workspace"]["pane"]
        == "rendering");
  Json value{{"radius", 0.75F},  {"bias", 0.025F}, {"strength", 2.0F},
             {"directions", 16}, {"steps", 8},     {"filterRadius", 4}};
  auto staged = command(workspace, {{"command", "stage-occlusion"}, {"value", value}});
  CHECK(staged["workspace"]["pendingOcclusion"] == true);
  CHECK(staged["scene"]["occlusion"].is_null());
  CHECK(staged["workspace"]["occlusionDraft"] == value);
  REQUIRE(workspace.step(std::chrono::milliseconds{16}));
  const auto current = snapshot(workspace)["revision"];
  CHECK_FALSE(workspace.dispatch(Json{{"command", "save"}, {"revision", current}}.dump()));
  CHECK_FALSE(workspace.dispatch(Json{{"command", "play"}, {"revision", current}}.dump()));
  CHECK_FALSE(workspace.dispatch(Json{{"command", "export-data"},
                                      {"revision", current},
                                      {"parent", fixture.storage.files()->root().string()},
                                      {"folder", "blocked"}}
                                     .dump()));
  CHECK_FALSE(workspace.dispatch(
      Json{{"command", "stage-occlusion"}, {"revision", "0"}, {"value", value}}.dump()));
  auto bad = value;
  bad["steps"] = 9;
  CHECK_FALSE(workspace.dispatch(
      Json{{"command", "stage-occlusion"}, {"revision", current}, {"value", bad}}.dump()));
  CHECK(Json::parse(
            workspace.dispatch("{\"command\":\"inspect\"}").value())["workspace"]["occlusionDraft"]
        == value);
  auto closing = workspace.dispatch("{\"command\":\"close\",\"discard\":false}");
  REQUIRE(closing);
  CHECK(Json::parse(*closing)["confirmation"] == true);
  command(workspace, {{"command", "cancel-close"}});
  CHECK(command(workspace, {{"command", "reset-occlusion"}})["workspace"]["pendingOcclusion"]
        == false);
  command(workspace, {{"command", "stage-occlusion"}, {"value", value}});
  auto applied = command(workspace, {{"command", "apply-occlusion"}});
  CHECK(applied["scene"]["occlusion"] == value);
  CHECK(applied["workspace"]["pendingOcclusion"] == false);
  REQUIRE(workspace.step(std::chrono::milliseconds{16}));
  command(workspace, {{"command", "undo"}});
  CHECK(snapshot(workspace)["scene"]["occlusion"].is_null());
  command(workspace, {{"command", "redo"}});
  command(workspace, {{"command", "save"}});
  command(workspace, {{"command", "play"}});
  REQUIRE(workspace.step(std::chrono::milliseconds{16}));
  CHECK_FALSE(workspace.dispatch(Json{
      {"command", "stage-occlusion"},
      {"revision", snapshot(workspace)["revision"]},
      {"value", nullptr}}.dump()));
  command(workspace, {{"command", "stop-play"}});
  CHECK(snapshot(workspace)["scene"]["occlusion"] == value);
  value["strength"] = 3.0F;
  command(workspace, {{"command", "stage-occlusion"}, {"value", value}});
  REQUIRE(workspace.dispatch("{\"command\":\"close\",\"discard\":false}"));
  command(workspace, {{"command", "save-close"}});
  auto reopened = fixture.open();
  CHECK(snapshot(reopened)["scene"]["occlusion"] == value);
  command(reopened, {{"command", "stage-occlusion"}, {"value", nullptr}});
  CHECK(command(reopened, {{"command", "apply-occlusion"}})["scene"]["occlusion"].is_null());
  REQUIRE(reopened.dispatch("{\"command\":\"close\",\"discard\":true}"));
}

TEST_CASE("workspace.occlusion_and_environment_drafts_are_exclusive_and_discard_preserves_disk") {
  Fixture fixture;
  REQUIRE(DotTest::Environment::write(fixture.project.files().value()));
  auto workspace = fixture.open();
  const Json environment{
      {"asset", "assets/studio.hdr"}, {"intensity", 1.0F},  {"rotationDegrees", 0.0F},
      {"background", true},           {"radianceSize", 16}, {"diffuseSize", 4},
      {"specularSize", 16},           {"brdfSize", 16},     {"samples", 32}};
  const Json occlusion{{"radius", 0.5F},  {"bias", 0.02F}, {"strength", 1.0F},
                       {"directions", 8}, {"steps", 4},    {"filterRadius", 2}};
  command(workspace, {{"command", "stage-environment"}, {"value", environment}});
  auto current = snapshot(workspace)["revision"];
  CHECK_FALSE(workspace.dispatch(
      Json{{"command", "stage-occlusion"}, {"revision", current}, {"value", occlusion}}.dump()));
  command(workspace, {{"command", "reset-environment"}});
  command(workspace, {{"command", "stage-occlusion"}, {"value", occlusion}});
  current = snapshot(workspace)["revision"];
  CHECK_FALSE(workspace.dispatch(Json{
      {"command", "stage-environment"},
      {"revision", current},
      {"value", environment}}.dump()));
  CHECK_FALSE(workspace.dispatch(
      Json{{"command", "edit"},
           {"revision", current},
           {"operations", Json::array({{{"op", "scene-name"}, {"name", "Blocked"}}})}}
          .dump()));
  REQUIRE(workspace.dispatch("{\"command\":\"close\",\"discard\":false}"));
  REQUIRE(workspace.step(std::chrono::milliseconds{16}));
  REQUIRE(workspace.dispatch("{\"command\":\"close\",\"discard\":true}"));
  auto reopened = fixture.open();
  CHECK(snapshot(reopened)["scene"]["occlusion"].is_null());
  CHECK(snapshot(reopened)["scene"]["environment"].is_null());
  REQUIRE(reopened.dispatch("{\"command\":\"close\",\"discard\":false}"));
}

TEST_CASE("workspace.editor_view_navigation_focus_and_gpu_selection_leave_authoring_unchanged") {
  Fixture fixture;
  auto workspace = fixture.open();
  const auto original = snapshot(workspace);
  auto inspect
      = [&] { return Json::parse(workspace.dispatch("{\"command\":\"inspect\"}").value()); };
  CHECK(inspect()["workspace"]["view"]["mode"] == "editor");
  Json camera{{"position", {0, 0, 5}},
              {"pivot", {0, 0, 0}},
              {"yaw", 0},
              {"pitch", 0},
              {"speed", 5},
              {"orthographic", true},
              {"orthographicHeight", 4},
              {"nearPlane", 0.05F},
              {"farPlane", 10000}};
  REQUIRE(workspace.dispatch(
      Json{{"command", "set-view"}, {"mode", "editor"}, {"camera", camera}}.dump()));
  CHECK_FALSE(workspace.dispatch(Json{
      {"command", "pick-view"},
      {"revision", original["revision"]},
      {"uv", {0.5F, 0.5F}}}.dump()));

  for (int frame = 0; frame < 3; ++frame) {
    REQUIRE(workspace.step(16ms));
  }

  CHECK(inspect()["workspace"]["view"]["ready"] == true);
  const auto before = workspace.captureViewport().value();
  auto picked = command(workspace, {{"command", "pick-view"}, {"uv", {0.5F, 0.5F}}});
  CHECK(picked["selected"] == "2");
  CHECK(picked["dirty"] == false);
  REQUIRE(workspace.step(16ms));
  CHECK(
      command(workspace, {{"command", "pick-view"}, {"uv", {0.01F, 0.01F}}})["selected"].is_null());
  const auto revision = snapshot(workspace)["revision"];
  auto focused = command(workspace, {{"command", "focus-view"}, {"entity", "2"}});
  CHECK(focused["revision"] == revision);
  CHECK(focused["workspace"]["view"]["camera"]["pivot"] == Json::array({0, 0, 0}));
  REQUIRE(workspace.dispatch(
      Json{{"command", "navigate-view"}, {"action", "turn"}, {"delta", {15, 10}}, {"orbit", true}}
          .dump()));
  REQUIRE(workspace.dispatch(
      Json{{"command", "navigate-view"}, {"action", "pan"}, {"delta", {0.1F, 0}}}.dump()));
  REQUIRE(workspace.dispatch(
      Json{{"command", "navigate-view"}, {"action", "dolly"}, {"steps", 1}}.dump()));
  REQUIRE(workspace.step(16ms));
  CHECK(workspace.captureViewport()->pixels != before.pixels);
  CHECK(snapshot(workspace)["scene"] == original["scene"]);
  CHECK(snapshot(workspace)["revision"] == revision);
  CHECK(snapshot(workspace)["historyEntries"] == original["historyEntries"]);
  const auto state = inspect()["workspace"]["view"]["camera"];
  auto bad = state;
  bad["nearPlane"] = -1;
  CHECK_FALSE(workspace.dispatch(
      Json{{"command", "set-view"}, {"mode", "editor"}, {"camera", bad}}.dump()));
  CHECK(inspect()["workspace"]["view"]["camera"] == state);
  REQUIRE(workspace.dispatch("{\"command\":\"set-view\",\"mode\":\"game\"}"));
  CHECK_FALSE(workspace.dispatch(
      Json{{"command", "navigate-view"}, {"action", "dolly"}, {"steps", 1}}.dump()));
  REQUIRE(workspace.step(16ms));
  REQUIRE(workspace.dispatch("{\"command\":\"set-view\",\"mode\":\"editor\"}"));
  command(workspace, {{"command", "play"}});
  CHECK(inspect()["workspace"]["view"]["mode"] == "play");
  CHECK_FALSE(workspace.dispatch("{\"command\":\"set-view\",\"mode\":\"editor\"}"));
  REQUIRE(workspace.step(16ms));
  command(workspace, {{"command", "stop-play"}});
  CHECK(inspect()["workspace"]["view"]["camera"] == state);
  CHECK(snapshot(workspace)["scene"] == original["scene"]);
  REQUIRE(workspace.dispatch("{\"command\":\"close\",\"discard\":false}"));
}

TEST_CASE("workspace.viewport_picks_guard_drafts_stale_revisions_and_render_epochs") {
  Fixture fixture;
  auto workspace = fixture.open();
  REQUIRE(workspace.step(16ms));
  REQUIRE(workspace.step(16ms));
  const auto original = snapshot(workspace);
  Json occlusion{{"radius", 0.5F},  {"bias", 0.02F}, {"strength", 1.0F},
                 {"directions", 8}, {"steps", 4},    {"filterRadius", 2}};
  command(workspace, {{"command", "stage-occlusion"}, {"value", occlusion}});
  REQUIRE(workspace.dispatch(
      Json{{"command", "navigate-view"}, {"action", "turn"}, {"delta", {5, 0}}, {"orbit", false}}
          .dump()));
  REQUIRE(workspace.step(16ms));
  CHECK_FALSE(workspace.dispatch(Json{
      {"command", "pick-view"},
      {"revision", snapshot(workspace)["revision"]},
      {"uv", {0.5F, 0.5F}}}.dump()));
  command(workspace, {{"command", "reset-occlusion"}});
  CHECK_FALSE(workspace.dispatch(
      Json{{"command", "pick-view"}, {"revision", "0"}, {"uv", {0.5F, 0.5F}}}.dump()));
  REQUIRE(workspace.dispatch(Json{
      {"command", "navigate-view"},
      {"action", "move"},
      {"direction", {0, 0, 1}},
      {"seconds", 0.1F},
      {"fast", false}}.dump()));
  CHECK_FALSE(workspace.dispatch(Json{
      {"command", "pick-view"},
      {"revision", snapshot(workspace)["revision"]},
      {"uv", {0.5F, 0.5F}}}.dump()));
  CHECK(snapshot(workspace)["scene"] == original["scene"]);
  REQUIRE(workspace.dispatch("{\"command\":\"close\",\"discard\":true}"));
}

TEST_CASE(
    "workspace.native_viewport_mouse_keyboard_focus_and_release_route_only_editor_navigation") {
  Fixture fixture;
  auto workspace = fixture.open();
  Json camera{{"position", {0, 0, 5}},
              {"pivot", {0, 0, 0}},
              {"yaw", 0},
              {"pitch", 0},
              {"speed", 5},
              {"orthographic", true},
              {"orthographicHeight", 4},
              {"nearPlane", 0.05F},
              {"farPlane", 10000}};
  REQUIRE(workspace.dispatch(
      Json{{"command", "set-view"}, {"mode", "editor"}, {"camera", camera}}.dump()));

  for (int frame = 0; frame < 3; ++frame) {
    REQUIRE(workspace.step(16ms));
  }

  auto inspect
      = [&] { return Json::parse(workspace.dispatch("{\"command\":\"inspect\"}").value()); };
  const auto original = snapshot(workspace)["scene"];
  const auto viewport = inspect()["workspace"]["view"]["viewport"];
  const float x = viewport["origin"][0].get<float>() + viewport["size"][0].get<float>() * 0.5F;
  const float y = viewport["origin"][1].get<float>() + viewport["size"][1].get<float>() * 0.5F;
  int count{};
  auto windows = SDL_GetWindows(&count);
  REQUIRE(windows);
  REQUIRE(count == 1);
  const auto window = SDL_GetWindowID(windows[0]);
  SDL_free(windows);
  const auto pointer = [&](float px, float py, float dx = 0, float dy = 0) {
    SDL_Event event{};
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = window;
    event.motion.x = px;
    event.motion.y = py;
    event.motion.xrel = dx;
    event.motion.yrel = dy;
    REQUIRE(SDL_PushEvent(&event));
  };
  const auto button = [&](Uint8 value, bool down) {
    SDL_Event event{};
    event.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = window;
    event.button.button = value;
    event.button.down = down;
    event.button.x = x;
    event.button.y = y;
    REQUIRE(SDL_PushEvent(&event));
  };
  const auto key = [&](SDL_Scancode value, bool down) {
    SDL_Event event{};
    event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.windowID = window;
    event.key.scancode = value;
    event.key.down = down;
    REQUIRE(SDL_PushEvent(&event));
  };
  pointer(x, y);
  button(SDL_BUTTON_LEFT, true);
  button(SDL_BUTTON_LEFT, false);
  REQUIRE(workspace.step(16ms));
  CHECK(snapshot(workspace)["selected"] == "2");
  const auto before = inspect()["workspace"]["view"]["camera"]["position"];
  pointer(x, y);
  button(SDL_BUTTON_RIGHT, true);
  key(SDL_SCANCODE_W, true);
  REQUIRE(workspace.step(16ms));
  CHECK(inspect()["workspace"]["view"]["camera"]["position"][2].get<float>()
        < before[2].get<float>());
  button(SDL_BUTTON_RIGHT, false);
  key(SDL_SCANCODE_W, false);
  REQUIRE(workspace.step(16ms));
  const auto released = inspect()["workspace"]["view"]["camera"];
  key(SDL_SCANCODE_W, true);
  REQUIRE(workspace.step(16ms));
  CHECK(inspect()["workspace"]["view"]["camera"] == released);
  key(SDL_SCANCODE_W, false);
  pointer(1, 1);
  button(SDL_BUTTON_RIGHT, true);
  key(SDL_SCANCODE_W, true);
  REQUIRE(workspace.step(16ms));
  CHECK(inspect()["workspace"]["view"]["camera"] == released);
  button(SDL_BUTTON_RIGHT, false);
  key(SDL_SCANCODE_W, false);
  REQUIRE(workspace.step(16ms));
  pointer(x, y);
  button(SDL_BUTTON_RIGHT, true);
  key(SDL_SCANCODE_W, true);
  REQUIRE(workspace.step(16ms));
  CHECK(inspect()["workspace"]["view"]["flying"] == true);
  const auto held = inspect()["workspace"]["view"]["camera"];
  SDL_Event focus{};
  focus.window.type = SDL_EVENT_WINDOW_FOCUS_LOST;
  focus.window.windowID = window;
  REQUIRE(SDL_PushEvent(&focus));
  REQUIRE(workspace.step(16ms));
  CHECK(inspect()["workspace"]["view"]["flying"] == false);
  CHECK(inspect()["workspace"]["view"]["camera"] == held);
  CHECK(snapshot(workspace)["scene"] == original);
  CHECK(snapshot(workspace)["dirty"] == false);
  REQUIRE(workspace.dispatch("{\"command\":\"close\",\"discard\":false}"));
}

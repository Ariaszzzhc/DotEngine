#include <doctest/doctest.h>

#include <chrono>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <utility>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Editor.Workspace;
import Dot.Project;
import Dot.Graphics.Renderer;

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

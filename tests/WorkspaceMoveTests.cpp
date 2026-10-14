#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>

import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Editor.Workspace;
import Dot.Project;

namespace {
  using Json = nlohmann::json;
  using namespace std::chrono_literals;

  struct Fixture {
    Dot::Core::StagedDirectory storage
        = Dot::Core::StagedDirectory::create(
              Dot::Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Dot::Project::Project project
        = Dot::Project::Project::create(storage.files().value(), "game", {}).value();

    auto open() -> Dot::Editor::Workspace {
      Dot::Editor::WorkspaceConfig config;
      config.font = DOT_EDITOR_FONT;
      return Dot::Editor::Workspace::open(project, config).value();
    }
  };

  auto inspect(Dot::Editor::Workspace& workspace) -> Json {
    return Json::parse(workspace.dispatch("{\"command\":\"inspect\"}").value());
  }

  auto request(Dot::Editor::Workspace& workspace, Json value, bool revision = true) -> Json {
    if (revision) {
      value["revision"] = inspect(workspace)["revision"];
    }

    auto result = workspace.dispatch(value.dump());
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return Json::parse(*result);
  }

  auto frame(Dot::Editor::Workspace& workspace) -> void {
    auto result = workspace.step(16ms);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  }

  auto prepare(Dot::Editor::Workspace& workspace) -> void {
    Json camera{{"position", {0, 0, 5}},
                {"pivot", {0, 0, 0}},
                {"yaw", 0},
                {"pitch", 0},
                {"speed", 5},
                {"orthographic", true},
                {"orthographicHeight", 4},
                {"nearPlane", 0.05F},
                {"farPlane", 10000}};
    request(workspace, {{"command", "set-view"}, {"mode", "editor"}, {"camera", camera}}, false);
    request(workspace, {{"command", "select"}, {"entity", "2"}});
    request(workspace,
            {{"command", "set-tool"}, {"tool", "move"}, {"space", "world"}, {"snap", 0.5}}, false);

    for (int i = 0; i < 3; ++i) {
      frame(workspace);
    }
  }

}  // namespace

TEST_CASE("workspace.move_tool_draft_pixels_single_history_cancel_save_and_boundaries") {
  Fixture f;
  auto workspace = f.open();
  CHECK_FALSE(workspace.dispatch(
      "{\"command\":\"begin-transform\",\"revision\":\"1\",\"axis\":0,\"uv\":[0.5,0.5]}"));
  prepare(workspace);
  const auto original = inspect(workspace);
  const auto image = workspace.captureViewport().value();
  const auto snapshot = workspace.snapshot().value();
  REQUIRE(original["workspace"]["tool"]["handles"].size() == 2);
  request(workspace, {{"command", "begin-transform"}, {"axis", 0}, {"uv", {0.5, 0.5}}});
  CHECK(inspect(workspace)["workspace"]["tool"]["active"] == true);
  CHECK_FALSE(workspace.captureViewport());
  CHECK(inspect(workspace)["workspace"]["view"]["source"] == "transform");
  CHECK_FALSE(workspace.dispatch(
      Json{{"command", "update-transform"}, {"revision", "0"}, {"uv", {0.6, 0.5}}}.dump()));
  CHECK_FALSE(
      workspace.dispatch(Json{{"command", "save"}, {"revision", original["revision"]}}.dump()));
  CHECK_FALSE(workspace.dispatch("{\"command\":\"reset-layout\"}"));
  CHECK_FALSE(workspace.dispatch(
      "{\"command\":\"set-tool\",\"tool\":\"select\",\"space\":\"world\",\"snap\":0}"));
  request(workspace, {{"command", "update-transform"}, {"uv", {0.65, 0.5}}});
  frame(workspace);
  CHECK(workspace.snapshot().value() == snapshot);
  const auto moved = workspace.captureViewport().value();
  CHECK(moved.width == image.width);
  CHECK((moved.pixels != image.pixels));
  CHECK_FALSE(workspace.dispatch(Json{
      {"command", "update-transform"},
      {"revision", original["revision"]},
      {"uv", {2, 0.5}}}.dump()));
  request(workspace, {{"command", "finish-transform"}, {"commit", false}});
  CHECK_FALSE(workspace.captureViewport());
  frame(workspace);
  CHECK(workspace.snapshot().value() == snapshot);
  CHECK((workspace.captureViewport()->pixels == image.pixels));
  request(workspace, {{"command", "begin-transform"}, {"axis", 0}, {"uv", {0.5, 0.5}}});
  request(workspace, {{"command", "update-transform"}, {"uv", {0.65, 0.5}}});
  request(workspace, {{"command", "update-transform"}, {"uv", {0.7, 0.5}}});
  request(workspace, {{"command", "finish-transform"}, {"commit", true}});
  frame(workspace);
  const auto committed = inspect(workspace);
  CHECK(committed["workspace"]["tool"]["active"] == false);
  CHECK(committed["historyEntries"].get<int>() == original["historyEntries"].get<int>() + 1);
  CHECK(committed["dirty"] == true);
  request(workspace, {{"command", "undo"}});
  frame(workspace);
  CHECK(inspect(workspace)["scene"] == original["scene"]);
  CHECK((workspace.captureViewport()->pixels == image.pixels));
  request(workspace, {{"command", "redo"}});
  frame(workspace);
  CHECK(inspect(workspace)["scene"] == committed["scene"]);
  request(workspace, {{"command", "save"}});
  CHECK(inspect(workspace)["dirty"] == false);
  CHECK_FALSE(workspace.dispatch(
      "{\"command\":\"set-tool\",\"tool\":\"rotate\",\"space\":\"world\",\"snap\":0}"));
  CHECK_FALSE(workspace.dispatch(
      "{\"command\":\"set-tool\",\"tool\":\"move\",\"space\":\"bad\",\"snap\":0}"));
}

TEST_CASE("workspace.move_tool_native_pointer_release_escape_focus_loss_and_no_pick_interference") {
  Fixture f;
  auto workspace = f.open();
  prepare(workspace);
  int count{};
  auto windows = SDL_GetWindows(&count);
  REQUIRE(windows);
  REQUIRE(count == 1);
  const auto window = SDL_GetWindowID(windows[0]);
  SDL_free(windows);
  const auto pointer = [&](float x, float y) {
    SDL_Event event{};
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = window;
    event.motion.x = x;
    event.motion.y = y;
    REQUIRE(SDL_PushEvent(&event));
  };
  const auto button = [&](bool down, float x, float y) {
    SDL_Event event{};
    event.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = window;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.x = x;
    event.button.y = y;
    REQUIRE(SDL_PushEvent(&event));
  };
  const auto begin = [&] {
    const auto handles = inspect(workspace)["workspace"]["tool"]["handles"];
    const auto handle = handles.at(0);
    REQUIRE(handle["axis"] == 0);
    const float x = handle["start"][0].get<float>() * 0.3F + handle["end"][0].get<float>() * 0.7F;
    const float y = handle["start"][1].get<float>() * 0.3F + handle["end"][1].get<float>() * 0.7F;
    pointer(x, y);
    button(true, x, y);
    frame(workspace);
    CHECK(inspect(workspace)["workspace"]["tool"]["active"] == true);
    CHECK(inspect(workspace)["selected"] == "2");
    return std::pair{x, y};
  };
  const auto before = inspect(workspace);
  const auto image = workspace.captureViewport().value();
  const auto initialHandle = before["workspace"]["tool"]["handles"].at(0);
  const float clickX
      = initialHandle["start"][0].get<float>() * 0.3F + initialHandle["end"][0].get<float>() * 0.7F;
  const float clickY
      = initialHandle["start"][1].get<float>() * 0.3F + initialHandle["end"][1].get<float>() * 0.7F;
  pointer(clickX, clickY);
  button(true, clickX, clickY);
  button(false, clickX, clickY);
  frame(workspace);
  CHECK(inspect(workspace)["workspace"]["tool"]["active"] == false);
  CHECK(inspect(workspace)["historyEntries"] == before["historyEntries"]);
  auto [x, y] = begin();
  pointer(x + 90, y);
  frame(workspace);
  CHECK(inspect(workspace)["scene"] == before["scene"]);
  CHECK((workspace.captureViewport()->pixels != image.pixels));
  button(false, x + 90, y);
  frame(workspace);
  CHECK(inspect(workspace)["workspace"]["tool"]["active"] == false);
  CHECK(inspect(workspace)["historyEntries"].get<int>() == before["historyEntries"].get<int>() + 1);
  request(workspace, {{"command", "undo"}});
  frame(workspace);
  CHECK((workspace.captureViewport()->pixels == image.pixels));
  auto start = begin();
  pointer(start.first + 90, start.second);
  frame(workspace);
  SDL_Event escape{};
  escape.key.type = SDL_EVENT_KEY_DOWN;
  escape.key.windowID = window;
  escape.key.scancode = SDL_SCANCODE_ESCAPE;
  escape.key.down = true;
  REQUIRE(SDL_PushEvent(&escape));
  frame(workspace);
  CHECK(inspect(workspace)["workspace"]["tool"]["active"] == false);
  CHECK(inspect(workspace)["scene"] == before["scene"]);
  CHECK((workspace.captureViewport()->pixels == image.pixels));
  button(false, start.first + 90, start.second);
  escape.key.type = SDL_EVENT_KEY_UP;
  escape.key.down = false;
  REQUIRE(SDL_PushEvent(&escape));
  frame(workspace);
  start = begin();
  pointer(start.first + 90, start.second);
  frame(workspace);
  SDL_Event focus{};
  focus.window.type = SDL_EVENT_WINDOW_FOCUS_LOST;
  focus.window.windowID = window;
  REQUIRE(SDL_PushEvent(&focus));
  frame(workspace);
  CHECK(inspect(workspace)["workspace"]["tool"]["active"] == false);
  CHECK(inspect(workspace)["scene"] == before["scene"]);
  CHECK((workspace.captureViewport()->pixels == image.pixels));
}

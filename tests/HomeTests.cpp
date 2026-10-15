#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <chrono>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

import Dot.Animation.Files;
import Dot.World.Scene;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Editor.Home;
import Dot.Editor.Workspace;
import Dot.Graphics.Renderer;
import Dot.Project;
import Dot.Project.GameSession;

namespace {
  using namespace Dot;
  using Json = nlohmann::json;

  struct Fixture {
    Core::StagedDirectory storage;

    Fixture()
        : storage(Core::StagedDirectory::create(
                      Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
                      .value()) {
    }

    auto files() -> Core::FileSystem {
      return storage.files().value();
    }

    auto request(std::string_view choice, std::string_view folder) -> Json {
      auto bytes = files().root().generic_u8string();
      return {{"command", "create"},
              {"parent", std::string(bytes.begin(), bytes.end())},
              {"folder", folder},
              {"name", "项目"},
              {"template", choice}};
    }

    auto home() -> Editor::Home {
      Editor::HomeConfig config;
      config.resources = DOT_EDITOR_RESOURCES;
      auto result = Editor::Home::open(config);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }
  };
}  // namespace

TEST_CASE("home.shared_project_actions_create_validated_blank_game_and_lab") {
  Fixture fixture;
  auto resources = Core::FileSystem::open(DOT_EDITOR_RESOURCES).value();

  for (const auto* choice : {"blank", "prism", "lab"}) {
    auto request = fixture.request(choice, choice);
    auto result = Editor::performProjectAction(resources, request.dump());
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    CHECK(result->description()->name == "项目");
    const auto expectedCount
        = std::string_view(choice) == "blank"
              ? 2
              : Project::Project::open(
                    resources.root() / "templates"
                    / (std::string_view(choice) == "prism" ? "PrismRelay" : "FeatureLab"))
                    ->loadStartup()
                    ->entityCount()
                    .value();
    CHECK(result->loadStartup()->entityCount().value() == expectedCount);

    if (std::string_view(choice) == "lab") {
      const auto startup = result->loadStartup().value();
      const auto players = startup.animationPlayers().value();
      REQUIRE(players.size() == 1);
      CHECK(startup.entity(players[0].entity)->name == "Animated panel");
      REQUIRE(players[0].properties.clips.size() == 2);

      for (const auto& path : players[0].properties.clips) {
        auto clip = Animation::loadClip(result->files().value(), path);
        REQUIRE_MESSAGE(clip, (clip ? "" : clip.error().message));
        CHECK(clip->duration().value() == 2);
      }
    }

    CHECK_FALSE(Editor::performProjectAction(resources, request.dump()));
    auto bytes = result->files()->root().generic_u8string();
    REQUIRE(Editor::performProjectAction(
        resources,
        Json{{"command", "open"}, {"root", std::string(bytes.begin(), bytes.end())}}.dump()));
  }

  auto bad = fixture.request("other", "bad");
  CHECK_FALSE(Editor::performProjectAction(resources, bad.dump()));
  bad = fixture.request("blank", "../bad");
  CHECK_FALSE(Editor::performProjectAction(resources, bad.dump()));
  CHECK_FALSE(Editor::performProjectAction(resources, "{\"command\":\"open\",\"root\":1}"));
  CHECK_FALSE(
      Editor::performProjectAction(resources, "{\"command\":\"open\",\"command\":\"create\"}"));
}

TEST_CASE("home.native_mailbox_selects_project_preserves_responses_and_releases_gui") {
  Fixture fixture;
  const auto contexts = Graphics::guiContexts();
  auto home = fixture.home();
  const auto control = home.controlDirectory().value();
  REQUIRE(home.step(std::chrono::milliseconds{16}));
  CHECK(Graphics::guiContexts() == contexts + 1);
  auto inspected = home.dispatch("{\"command\":\"inspect\"}");
  REQUIRE(inspected);
  CHECK(Json::parse(*inspected)["templates"].size() == 3);
  CHECK_FALSE(home.dispatch("{\"command\":\"open\",\"root\":\"missing\"}"));
  CHECK_FALSE(home.selection()->has_value());
  auto request = fixture.request("prism", "MyGame");
  REQUIRE(Editor::submitWorkspaceRequest(control, "create", request.dump()));
  REQUIRE(home.step(std::chrono::milliseconds{16}));
  auto selected = home.selection();
  REQUIRE(selected);
  REQUIRE(selected->has_value());
  CHECK((**selected).loadStartup()->entityCount().value()
        == Project::Project::open(std::filesystem::path(DOT_EDITOR_RESOURCES) / "templates"
                                  / "PrismRelay")
               ->loadStartup()
               ->entityCount()
               .value());
  auto channel = Core::FileSystem::open(control).value();
  auto response = Json::parse(channel.readText("responses/create.json").value());
  CHECK(response["response"]["ok"] == true);
  CHECK_FALSE(Editor::submitWorkspaceRequest(control, "late", "{\"command\":\"close\"}"));
  REQUIRE(home.releaseWindow());
  CHECK(Graphics::guiContexts() == contexts);
  CHECK(std::filesystem::exists(control / "responses/create.json"));
  auto moved = std::move(home);
  CHECK_FALSE(home.selection());
  CHECK_FALSE(home.controlDirectory());
  CHECK_FALSE(moved.step(std::chrono::milliseconds{16}));
  CHECK_FALSE(moved.dispatch(request.dump()));
  Editor::WorkspaceConfig workspaceConfig;
  workspaceConfig.font = std::filesystem::path(DOT_EDITOR_RESOURCES) / "EditorSans.ttf";
#ifdef _WIN32
  workspaceConfig.compiler
      = std::filesystem::path(DOT_EDITOR_RESOURCES) / "typescript-native/tsc.exe";
#else
  workspaceConfig.compiler = std::filesystem::path(DOT_EDITOR_RESOURCES) / "typescript-native/tsc";
#endif
  workspaceConfig.play.session.audioMode = Project::SessionAudio::Offline;
  auto workspace = Editor::Workspace::open(**selected, workspaceConfig);
  REQUIRE_MESSAGE(workspace, (workspace ? "" : workspace.error().message));
  REQUIRE(workspace->step(std::chrono::milliseconds{16}));
  REQUIRE(workspace->captureViewport());
  REQUIRE(workspace->dispatch("{\"command\":\"play\",\"revision\":\"1\"}"));
  REQUIRE(workspace->step(std::chrono::milliseconds{16}));
  REQUIRE(workspace->dispatch("{\"command\":\"stop-play\",\"revision\":\"1\"}"));
}

TEST_CASE("home.invalid_native_setup_worker_requests_and_response_failure_are_explicit") {
  Fixture fixture;
  Editor::HomeConfig invalid;
  invalid.width = 1;
  CHECK_FALSE(Editor::Home::open(invalid));
  auto home = fixture.home();
  bool rejected = false;
  std::thread worker([&] { rejected = !home.selection(); });
  worker.join();
  CHECK(rejected);
  const auto control = home.controlDirectory().value();
  auto channel = Core::FileSystem::open(control).value();
  std::filesystem::remove_all(control / "responses");
  REQUIRE(channel.writeTextAtomic("responses", "blocked parent"));
  REQUIRE(Editor::submitWorkspaceRequest(control, "create",
                                         fixture.request("blank", "created").dump()));
  CHECK_FALSE(home.step(std::chrono::milliseconds{16}));
  CHECK(std::filesystem::exists(fixture.files().root() / "created/project.dotproject"));
  CHECK_FALSE(home.step(std::chrono::milliseconds{16}));
  CHECK_FALSE(home.dispatch(fixture.request("blank", "again").dump()));
  CHECK_FALSE(std::filesystem::exists(fixture.files().root() / "again"));
}

TEST_CASE("home.native_project_pages_keep_forms_and_route_pointer_navigation") {
  Fixture fixture;
  auto home = fixture.home();
  REQUIRE(home.step(std::chrono::milliseconds(16)));
  const auto inspect
      = [&] { return Json::parse(home.dispatch("{\"command\":\"inspect\"}").value()); };
  auto initial = inspect();
  CHECK(initial["form"]["page"] == "open");
  REQUIRE(initial["controls"].contains("browseProject"));
  CHECK_FALSE(initial["dialog"]["pending"].get<bool>());
  auto form = initial["form"];
  form["page"] = "create";
  form["name"] = "中文 Project";
  form["folder"] = "Demo";
  form["parent"] = fixture.files().root().generic_string();
  form["template"] = "lab";
  REQUIRE(home.dispatch(Json{{"command", "stage-form"}, {"value", form}}.dump()));
  REQUIRE(home.step(std::chrono::milliseconds(16)));
  auto created = inspect();
  CHECK(created["form"] == form);
  CHECK(created["controls"].contains("browseParent"));
  CHECK(created["controls"].contains("create"));
  auto invalid = form;
  invalid["page"] = "bad";
  CHECK_FALSE(home.dispatch(Json{{"command", "stage-form"}, {"value", invalid}}.dump()));
  CHECK(inspect()["form"] == form);
  CHECK_FALSE(home.dispatch("{\"command\":\"browse-folder\",\"target\":\"bad\"}"));
  int count{};
  auto windows = SDL_GetWindows(&count);
  REQUIRE(windows);
  REQUIRE(count == 1);
  const auto window = SDL_GetWindowID(windows[0]);
  SDL_free(windows);
  SDL_Event focus{};
  focus.window.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
  focus.window.windowID = window;
  REQUIRE(SDL_PushEvent(&focus));
  REQUIRE(home.step(std::chrono::milliseconds(16)));
  SDL_PumpEvents();
  const auto row = inspect()["controls"]["openPage"];
  const float x = row["origin"][0].get<float>() + row["size"][0].get<float>() * .5F;
  const float y = row["origin"][1].get<float>() + row["size"][1].get<float>() * .5F;
  SDL_Event motion{};
  motion.motion.type = SDL_EVENT_MOUSE_MOTION;
  motion.motion.windowID = window;
  motion.motion.x = x;
  motion.motion.y = y;
  REQUIRE(SDL_PushEvent(&motion));

  for (const bool down : {true, false}) {
    SDL_Event event{};
    event.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = window;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.x = x;
    event.button.y = y;
    REQUIRE(SDL_PushEvent(&event));
    REQUIRE(home.step(std::chrono::milliseconds(16)));
  }

  auto opened = inspect();
  CHECK(opened["form"]["page"] == "open");
  CHECK(opened["form"]["name"] == form["name"]);
  CHECK(opened["form"]["parent"] == form["parent"]);
  REQUIRE(home.dispatch("{\"command\":\"close\"}"));
  REQUIRE(home.releaseWindow());
}

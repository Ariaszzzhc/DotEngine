#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Editor.Workspace;
import Dot.Project;
import Dot.Project.GameSession;

namespace {
  using Json = nlohmann::json;
  using namespace std::chrono_literals;

  struct Fixture {
    Dot::Core::StagedDirectory storage;
    Dot::Project::Project project;

    Fixture()
        : storage(Dot::Core::StagedDirectory::create(
                      Dot::Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
                      .value()),
          project(Dot::Project::Project::create(storage.files().value(), "project", {}).value()) {
    }

    auto open(int width = 1280, int height = 800) -> Dot::Editor::Workspace {
      Dot::Editor::WorkspaceConfig config;
      config.font = DOT_EDITOR_FONT;
      config.compiler = DOT_TYPESCRIPT_COMPILER;
      config.width = width;
      config.height = height;
      config.play.session.audioMode = Dot::Project::SessionAudio::Offline;
      config.play.session.pollInterval = 0ms;
      auto result = Dot::Editor::Workspace::open(project, config);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }
  };

  auto inspect(Dot::Editor::Workspace& workspace) -> Json {
    auto result = workspace.dispatch("{\"command\":\"inspect\"}");
    REQUIRE(result);
    return Json::parse(*result);
  }

  auto request(Dot::Editor::Workspace& workspace, Json value) -> Json {
    auto result = workspace.dispatch(value.dump());
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return Json::parse(*result);
  }

  auto frames(Dot::Editor::Workspace& workspace) -> void {
    for (int i = 0; i < 3; ++i) {
      auto result = workspace.step(16ms);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    }
  }
}  // namespace

TEST_CASE("workspace.chrome_theme_docking_reset_and_search_preserve_real_scene_pixels") {
  Fixture fixture;
  auto workspace = fixture.open();
  frames(workspace);
  const auto initial = inspect(workspace);
  const auto scene = workspace.snapshot().value();
  const auto image = workspace.captureViewport().value();
  const auto panels = initial["workspace"]["layout"]["panels"];
  REQUIRE(panels.size() == 7);
  CHECK(panels["hierarchy"]["origin"][0] < panels["viewport"]["origin"][0]);
  CHECK(panels["viewport"]["origin"][0] < panels["inspector"]["origin"][0]);
  CHECK(panels["resources"]["origin"][1] > panels["hierarchy"]["origin"][1]);
  CHECK(panels["inspector"]["size"][1] > panels["viewport"]["size"][1]);
  CHECK(panels["viewport"]["visible"] == true);

  for (const auto* theme : {"light", "dark", "light"}) {
    auto result = request(workspace, {{"command", "set-theme"}, {"theme", theme}});
    CHECK(result["workspace"]["theme"] == theme);
    frames(workspace);
    const auto captured = workspace.captureViewport().value();
    CHECK(captured.width == image.width);
    CHECK(captured.height == image.height);
    CHECK((captured.pixels == image.pixels));
    CHECK(workspace.snapshot().value() == scene);
    CHECK(inspect(workspace)["workspace"]["view"]["camera"]
          == initial["workspace"]["view"]["camera"]);
  }

  request(workspace, {{"command", "filter-panel"}, {"panel", "hierarchy"}, {"query", "CUBE"}});
  request(workspace, {{"command", "filter-panel"}, {"panel", "resources"}, {"query", ".ts"}});
  frames(workspace);
  CHECK(inspect(workspace)["workspace"]["filters"]["hierarchy"] == "CUBE");
  CHECK(inspect(workspace)["workspace"]["filters"]["resources"] == ".ts");
  CHECK(inspect(workspace)["workspace"]["displayed"]["hierarchy"] == Json::array({"2"}));
  CHECK(inspect(workspace)["workspace"]["displayed"]["resources"]
        == Json::array({"scripts/mover.ts"}));
  CHECK(workspace.snapshot().value() == scene);
  CHECK((workspace.captureViewport()->pixels == image.pixels));
  auto queued = request(workspace, {{"command", "dock-panel"},
                                    {"panel", "inspector"},
                                    {"target", "viewport"},
                                    {"placement", "down"},
                                    {"ratio", 0.3}});
  CHECK(queued["queued"] == true);
  CHECK(queued["workspace"]["layout"]["pending"] == true);
  CHECK_FALSE(workspace.dispatch("{\"command\":\"reset-layout\"}"));
  frames(workspace);
  const auto moved = inspect(workspace)["workspace"]["layout"];
  CHECK(moved["pending"] == false);
  CHECK(moved["error"] == "");
  CHECK(moved["panels"]["inspector"]["origin"][0] < panels["inspector"]["origin"][0]);
  CHECK(moved["generation"] != initial["workspace"]["layout"]["generation"]);
  request(workspace, {{"command", "reset-layout"}});
  frames(workspace);
  CHECK(inspect(workspace)["workspace"]["layout"]["panels"]["inspector"]["origin"]
        == panels["inspector"]["origin"]);
  CHECK((workspace.captureViewport()->pixels == image.pixels));
  CHECK(workspace.snapshot().value() == scene);
  request(workspace,
          {{"command", "edit"},
           {"revision", inspect(workspace)["revision"]},
           {"operations", Json::array({{{"op", "create"}, {"name", "Group"}},
                                       {{"op", "parent"}, {"entity", "2"}, {"parent", "3"}},
                                       {{"op", "name"}, {"entity", "2"}, {"name", "叶节点"}}})}});
  const auto authored = workspace.snapshot().value();
  request(workspace, {{"command", "filter-panel"}, {"panel", "hierarchy"}, {"query", "叶节点"}});
  frames(workspace);
  CHECK(inspect(workspace)["workspace"]["displayed"]["hierarchy"] == Json::array({"3", "2"}));
  CHECK(workspace.snapshot().value() == authored);
}

TEST_CASE("workspace.chrome_boundaries_minimum_size_and_play_reject_no_false_success") {
  Fixture fixture;
  auto workspace = fixture.open(800, 480);
  CHECK_FALSE(workspace.dispatch(
      "{\"command\":\"dock-panel\",\"panel\":\"inspector\",\"target\":\"viewport\","
      "\"placement\":\"down\",\"ratio\":0.3}"));
  frames(workspace);
  const auto original = workspace.snapshot().value();
  const auto image = workspace.captureViewport().value();
  CHECK(image.width > 100);
  CHECK(image.height > 100);

  for (const auto* bad :
       {"{\"command\":\"set-theme\",\"theme\":\"purple\"}",
        "{\"command\":\"set-theme\",\"theme\":true}", "{\"command\":\"reset-layout\",\"extra\":1}",
        "{\"command\":\"filter-panel\",\"panel\":\"other\",\"query\":\"\"}"}) {
    CHECK_FALSE(workspace.dispatch(bad));
  }

  CHECK_FALSE(workspace.dispatch(
      Json{{"command", "filter-panel"}, {"panel", "hierarchy"}, {"query", std::string(129, 'x')}}
          .dump()));
  CHECK_FALSE(workspace.dispatch(
      Json{{"command", "filter-panel"}, {"panel", "resources"}, {"query", std::string(1, '\0')}}
          .dump()));
  CHECK_FALSE(workspace.dispatch(Json{{"command", "dock-panel"},
                                      {"panel", "inspector"},
                                      {"target", "viewport"},
                                      {"placement", "down"},
                                      {"ratio", 0.99}}
                                     .dump()));
  CHECK(workspace.snapshot().value() == original);
  CHECK(inspect(workspace)["workspace"]["theme"] == "dark");
  CHECK(inspect(workspace)["workspace"]["layout"]["pending"] == false);
  request(workspace, {{"command", "play"}, {"revision", inspect(workspace)["revision"]}});
  request(workspace, {{"command", "set-theme"}, {"theme", "light"}});
  request(workspace, {{"command", "reset-layout"}});
  frames(workspace);
  CHECK(inspect(workspace)["workspace"]["play"]["state"] == "running");
  CHECK(inspect(workspace)["workspace"]["theme"] == "light");
  request(workspace, {{"command", "stop-play"}, {"revision", inspect(workspace)["revision"]}});
  frames(workspace);
  CHECK(workspace.snapshot().value() == original);
}

#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <utility>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Editor.Workspace;
import Dot.Project;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace Dot::Editor {
  namespace {
    using Json = nlohmann::json;
    using namespace std::chrono_literals;

    template <class T>
    auto checked(Core::Result<T> result) -> T {
      if (!result) {
        throw std::runtime_error(result.error().message);
      }

      return std::move(*result);
    }

    auto require(bool value, const char* message) -> void {
      if (!value) {
        throw std::runtime_error(message);
      }
    }
  }  // namespace

  /** Run camera-independent authoring and actual GPU selection on a saved project.
   * 在保存的项目上验收独立编辑相机以及真实 GPU 选择。 */
  auto testViewport(const std::filesystem::path& parent) -> int {
    auto storage = checked(Core::StagedDirectory::create(checked(Core::FileSystem::open(parent))));
    auto project = checked(Project::Project::create(checked(storage.files()), "viewport", {}));
    auto scene = checked(project.loadStartup());
    require(scene.setCamera(checked(scene.primaryCamera()), std::nullopt).has_value(),
            "Remove game camera");
    const auto outside = checked(scene.createEntity("Outside view"));
    require(scene.setMeshRenderer(outside, World::MeshRenderer{}).has_value(), "Add outside mesh");
    require(scene.setTransform(outside, {{1000, 0, 0}}).has_value(), "Place outside mesh");
    const auto peer = checked(scene.createEntity("Instanced peer"));
    require(scene.setMeshRenderer(peer, World::MeshRenderer{}).has_value(), "Add visible instance");
    require(scene.setTransform(peer, {{1.5F, 0, 0}}).has_value(), "Place visible instance");
    require(project.saveStartup(scene).has_value(), "Save camera-free project");
    auto workspace = checked(Workspace::open(project));
    const auto inspect
        = [&] { return Json::parse(checked(workspace.dispatch("{\"command\":\"inspect\"}"))); };
    const auto command = [&](Json request) {
      request["revision"] = inspect()["revision"];
      return Json::parse(checked(workspace.dispatch(request.dump())));
    };
    const auto frame = [&] { require(workspace.step(16ms).has_value(), "Draw native viewport"); };
    const auto original = inspect();
    const Json camera{{"position", {0, 0, 5}},
                      {"pivot", {0, 0, 0}},
                      {"yaw", 0},
                      {"pitch", 0},
                      {"speed", 5},
                      {"orthographic", true},
                      {"orthographicHeight", 4},
                      {"nearPlane", 0.05F},
                      {"farPlane", 10000}};
    checked(workspace.dispatch(
        Json{{"command", "set-view"}, {"mode", "editor"}, {"camera", camera}}.dump()));
    frame();
    frame();
    frame();
    const auto before = checked(workspace.captureViewport());
    require(inspect()["workspace"]["view"]["draws"]["input"] == 3
                && inspect()["workspace"]["view"]["draws"]["visible"] == 2
                && inspect()["workspace"]["view"]["draws"]["culled"] == 1
                && inspect()["workspace"]["view"]["draws"]["colorDraws"] == 1,
            "Native viewport must expose actual mesh submission statistics");
    const auto spatial = inspect()["workspace"]["view"]["draws"]["spatial"];
    require(spatial["indexed"] == 3 && spatial["unindexed"] == 0 && spatial["rebuilt"] == false
                && spatial["changed"] == 0 && spatial["refitted"] == 0,
            "Native viewport must reuse the actual spatial index on unchanged frames");

    for (const auto* theme : {"light", "dark"}) {
      checked(workspace.dispatch(Json{{"command", "set-theme"}, {"theme", theme}}.dump()));
      frame();
      frame();
      require(checked(workspace.captureViewport()).pixels == before.pixels,
              "Chrome themes must preserve actual scene pixels");
      require(inspect()["scene"] == original["scene"], "Theme must preserve authoring");
    }

    const auto layout = inspect()["workspace"]["layout"];
    const auto queued = Json::parse(checked(workspace.dispatch(Json{{"command", "dock-panel"},
                                                                    {"panel", "inspector"},
                                                                    {"target", "viewport"},
                                                                    {"placement", "down"},
                                                                    {"ratio", 0.3}}
                                                                   .dump())));
    require(queued["queued"] == true, "Docking must report queued until its frame");
    frame();
    frame();
    require(inspect()["workspace"]["layout"]["generation"] != layout["generation"],
            "Docking must complete in the native frame");
    require(inspect()["workspace"]["layout"]["error"] == "", "Docking must not hide errors");
    checked(workspace.dispatch("{\"command\":\"reset-layout\"}"));
    frame();
    frame();
    frame();
    require(checked(workspace.captureViewport()).pixels == before.pixels,
            "Reset layout must restore the real viewport dimensions and pixels");
    require(command({{"command", "pick-view"}, {"uv", {0.5F, 0.5F}}})["selected"] == "2",
            "GPU pick must identify the visible cube");
    frame();
    require(command({{"command", "pick-view"}, {"uv", {0.01F, 0.01F}}})["selected"].is_null(),
            "Background pick must clear selection");
    const auto revision = inspect()["revision"];
    command({{"command", "focus-view"}, {"entity", "2"}});
    checked(workspace.dispatch(
        Json{{"command", "navigate-view"}, {"action", "turn"}, {"delta", {20, 10}}, {"orbit", true}}
            .dump()));
    frame();
    require(checked(workspace.captureViewport()).pixels != before.pixels,
            "Navigation must change actual preview pixels");
    require(inspect()["scene"] == original["scene"] && inspect()["revision"] == revision
                && inspect()["historyEntries"] == original["historyEntries"]
                && inspect()["dirty"] == false,
            "Navigation must preserve authoring and history");
    checked(workspace.dispatch("{\"command\":\"set-view\",\"mode\":\"game\"}"));
    frame();
    require(!workspace.captureViewport(), "Game view must expose its missing camera");
    checked(workspace.dispatch("{\"command\":\"set-view\",\"mode\":\"editor\"}"));
    frame();
    require(workspace.captureViewport().has_value(), "Editor view must recover independently");
    checked(workspace.dispatch(
        Json{{"command", "set-view"}, {"mode", "editor"}, {"camera", camera}}.dump()));
    command({{"command", "select"}, {"entity", "2"}});
    checked(workspace.dispatch(
        "{\"command\":\"set-tool\",\"tool\":\"move\",\"space\":\"world\",\"snap\":0.5}"));
    frame();
    const auto gestureStart = inspect();
    const auto gesturePixels = checked(workspace.captureViewport());
    command({{"command", "begin-transform"}, {"axis", 0}, {"uv", {0.5, 0.5}}});
    command({{"command", "update-transform"}, {"uv", {0.7, 0.5}}});
    frame();
    require(inspect()["scene"] == gestureStart["scene"]
                && inspect()["revision"] == gestureStart["revision"],
            "Move preview must not edit document history");
    require(checked(workspace.captureViewport()).pixels != gesturePixels.pixels,
            "Move draft must change actual scene pixels");
    command({{"command", "finish-transform"}, {"commit", true}});
    frame();
    require(inspect()["historyEntries"].get<int>() == gestureStart["historyEntries"].get<int>() + 1,
            "Move release must commit one history entry");
    command({{"command", "undo"}});
    frame();
    require(inspect()["scene"] == gestureStart["scene"]
                && checked(workspace.captureViewport()).pixels == gesturePixels.pixels,
            "Move undo must restore actual scene and pixels");

    for (const auto* tool : {"rotate", "scale"}) {
      const bool rotation = std::string_view(tool) == "rotate";
      checked(workspace.dispatch(Json{{"command", "set-tool"},
                                      {"tool", tool},
                                      {"space", "local"},
                                      {"snap", rotation ? 15.F : 0.1F}}
                                     .dump()));
      frame();
      const auto baseline = inspect();
      const auto pixels = checked(workspace.captureViewport());
      command({{"command", "begin-transform"},
               {"axis", rotation ? 2 : 0},
               {"uv", {rotation ? 0.7 : 0.5, 0.5}}});
      command({{"command", "update-transform"}, {"uv", {0.65, rotation ? 0.35 : 0.5}}});
      frame();
      require(inspect()["scene"] == baseline["scene"]
                  && checked(workspace.captureViewport()).pixels != pixels.pixels,
              "Rotation/scale preview must show real changes without editing the document");
      command({{"command", "finish-transform"}, {"commit", true}});
      frame();
      command({{"command", "undo"}});
      frame();
      require(inspect()["scene"] == baseline["scene"]
                  && checked(workspace.captureViewport()).pixels == pixels.pixels,
              "One undo must restore rotation/scale data and pixels");
    }

    command({{"command", "select-many"}, {"entities", {std::to_string(peer.value), "2"}}});
    checked(workspace.dispatch(
        "{\"command\":\"set-tool\",\"tool\":\"move\",\"space\":\"world\",\"snap\":0.5}"));
    frame();
    const auto group = inspect();
    const auto groupPixels = checked(workspace.captureViewport());
    command({{"command", "begin-transform"}, {"axis", 0}, {"uv", {.5, .5}}});
    require(inspect()["workspace"]["tool"]["roots"] == 2,
            "Group gesture must freeze both selected roots");
    command({{"command", "update-transform"}, {"uv", {.7, .5}}});
    frame();
    require(inspect()["scene"] == group["scene"]
                && checked(workspace.captureViewport()).pixels != groupPixels.pixels,
            "Group preview must change actual pixels without publishing scene edits");
    command({{"command", "finish-transform"}, {"commit", true}});
    const auto movedGroup = inspect();
    require(
        movedGroup["scene"]["entities"][1]["transform"]["position"][0].get<float>() > 0
            && movedGroup["scene"]["entities"][3]["transform"]["position"][0].get<float>() > 1.5F,
        "Group release must move both roots");
    command({{"command", "undo"}});
    frame();
    require(inspect()["scene"] == group["scene"] && inspect()["selection"] == group["selection"]
                && checked(workspace.captureViewport()).pixels == groupPixels.pixels,
            "Group undo must restore scene, selection and pixels");

    command({{"command", "save"}});
    require(Json::parse(checked(World::encodeScene(checked(project.loadStartup()))))
                == original["scene"],
            "Saving must not serialize an editor camera into the game scene");
    checked(workspace.dispatch("{\"command\":\"close\",\"discard\":false}"));
    std::println(
        "Editor viewport acceptance passed: navigation, selection, camera isolation, transactional "
        "move/rotate/scale "
        "movement, group transforms");
    return 0;
  }
}  // namespace Dot::Editor

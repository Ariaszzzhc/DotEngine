#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <print>
#include <stdexcept>

import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Core.Result;
import Dot.Project;
import Dot.Editor.Workspace;

namespace Dot::Editor {
  namespace {
    using Json = nlohmann::json;

    template <class T>
    auto checked(Core::Result<T> result) -> T {
      if (!result) throw std::runtime_error(result.error().message);
      if constexpr (!std::is_void_v<T>) return std::move(*result);
    }

    auto require(bool value, const char* message) -> void {
      if (!value) throw std::runtime_error(message);
    }
  }  // namespace

  auto testModelImport(const std::filesystem::path& parent) -> int {
    auto storage = checked(Core::StagedDirectory::create(checked(Core::FileSystem::open(parent))));
    auto project = checked(Project::Project::create(checked(storage.files()), "game"));
    auto workspace = checked(Workspace::open(project));
    const auto inspect
        = [&] { return Json::parse(checked(workspace.dispatch("{\"command\":\"inspect\"}"))); };
    const auto command = [&](Json request) {
      if (request["command"] == "edit" || request["command"] == "undo"
          || request["command"] == "redo" || request["command"] == "save")
        request["revision"] = inspect()["revision"];
      return Json::parse(checked(workspace.dispatch(request.dump())));
    };
    const auto frame = [&] {
      for (int i = 0; i < 3; ++i) checked(workspace.step(std::chrono::milliseconds(16)));
    };
    frame();
    const auto before = inspect();
    const auto baseline = checked(workspace.captureViewport());
    const auto source
        = (std::filesystem::path(DOT_MODEL_FIXTURES) / "surface.glb").generic_string();
    const auto imported
        = command({{"command", "import-model"}, {"path", "assets/model"}, {"source", source}});
    require(imported["meshes"] == 1 && imported["images"] == 3, "Native model import report");
    const auto animated = command(
        {{"command", "import-model"},
         {"path", "assets/animated"},
         {"source",
          (std::filesystem::path(DOT_MODEL_FIXTURES) / "animated.glb").generic_string()}});
    require(animated["animations"] == 1 && animated["model"] == "assets/animated/model.dotmodel",
            "Native animation import report retains clip and binding descriptor");
    require(inspect()["dirty"] == false, "Asset import must not mutate authoring scene");
    command({{"command", "edit"},
             {"operations", Json::array({{{"op", "remove"}, {"entity", "2"}},
                                         {{"op", "instantiate-model"}, {"path", imported["scene"]}},
                                         {{"op", "create"}, {"name", "Key light"}},
                                         {{"op", "component"},
                                          {"entity", "6"},
                                          {"component", "light"},
                                          {"value",
                                           {{"kind", "directional"},
                                            {"color", {1, 1, 1}},
                                            {"intensity", 3},
                                            {"range", 10},
                                            {"innerConeDegrees", 20},
                                            {"outerConeDegrees", 30},
                                            {"enabled", true},
                                            {"shadows", nullptr}}}}})}});
    frame();
    const auto actual = checked(workspace.captureViewport());
    require(actual.pixels != baseline.pixels,
            "Imported physical model must change real viewport pixels");
    require(inspect()["scene"]["entities"].size() == 5, "Imported hierarchy and authored light");
    command({{"command", "undo"}});
    frame();
    require(inspect()["scene"] == before["scene"], "One undo restores complete model insertion");
    require(checked(workspace.captureViewport()).pixels == baseline.pixels,
            "Undo restores viewport pixels");
    command({{"command", "redo"}});
    frame();
    command({{"command", "save"}});
    require(checked(project.loadStartup()).entities()->size() == 5,
            "Saved model hierarchy reopens");
    command({{"command", "close"}, {"discard", false}});
    checked(workspace.step(std::chrono::milliseconds(16)));
    require(inspect()["workspace"]["running"] == false, "Close imported model workspace");
    std::println(
        "Model acceptance passed: GLB PBR import, hierarchy, actual viewport, undo/redo/save");
    return 0;
  }
}  // namespace Dot::Editor

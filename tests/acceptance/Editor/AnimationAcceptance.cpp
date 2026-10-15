#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <print>
#include <stdexcept>
#include <string>

import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Editor.Workspace;
import Dot.Project;
import Dot.Project.GameSession;
import Dot.World.Scene;

namespace Dot::Editor {
  /** Actual editor import/Inspector/Play/save/duplicate with retained animation.
   * 实际编辑器导入、检查器、播放、保存及复制，保留动画绑定。 */
  auto testSceneAnimation(const std::filesystem::path& compiler,
                          const std::filesystem::path& parent) -> int {
    using Json = nlohmann::json;
    using namespace std::chrono_literals;
    const auto require = [](bool value, const char* message) {
      if (!value) {
        throw std::runtime_error(message);
      }
    };
    auto storage = Core::StagedDirectory::create(Core::FileSystem::open(parent).value()).value();
    auto project = Project::Project::create(storage.files().value(), "game").value();
    WorkspaceConfig config;
    config.compiler = compiler;
    config.play.session.audioMode = Project::SessionAudio::Offline;
    auto workspace = Workspace::open(project, config).value();
    const auto send = [&](Json request) {
      auto result = workspace.dispatch(request.dump());

      if (!result) {
        throw std::runtime_error(result.error().message);
      }

      return Json::parse(*result);
    };
    const auto inspect = [&] { return send({{"command", "inspect"}}); };
    const auto action = [&](Json request) {
      request["revision"] = inspect()["revision"];
      return send(std::move(request));
    };
    const auto frame = [&] {
      for (int i = 0; i < 3; ++i) {
        workspace.step(16ms).value();
      }
    };
    frame();
    send({{"command", "import-model"},
          {"path", "assets/model"},
          {"source",
           (std::filesystem::path(DOT_MODEL_FIXTURES) / "animated.glb").generic_string()}});
    action({{"command", "edit"},
            {"operations",
             Json::array({{{"op", "remove"}, {"entity", "2"}},
                          {{"op", "instantiate-model"}, {"path", "assets/model/model.dotscene"}},
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
    action({{"command", "select"}, {"entity", "3"}});
    frame();
    auto player = inspect()["workspace"]["inspector"]["animation"];
    require(player["targets"][0]["entity"] == "3" && player["targets"][1]["entity"] == "4",
            "Model insertion must remap animation node IDs");
    player["speed"] = .5;
    player["looping"] = false;
    action({{"command", "edit"},
            {"operations", Json::array({{{"op", "component"},
                                         {"entity", "3"},
                                         {"component", "animation"},
                                         {"value", player}}})}});
    frame();
    require(inspect()["workspace"]["inspector"]["animation"] == player,
            "Animation Inspector reflects stored native authoring");
    const auto authored = inspect()["scene"];
    const auto initial = workspace.captureViewport().value();
    action({{"command", "play"}});
    workspace.step(500ms).value();
    const auto played = send({{"command", "inspect-play"}});
    const auto& animated = played["scene"]["entities"][1];
    require(animated["transform"]["position"][0].get<float>() > .2F,
            "Play must advance actual model animation");
    require(workspace.captureViewport()->pixels != initial.pixels,
            "Play must change rendered model pixels");
    require(inspect()["scene"] == authored, "Play must retain unsaved authoring scene");
    action({{"command", "stop-play"}});
    frame();
    require(workspace.captureViewport()->pixels == initial.pixels,
            "Stop restores authored model pixels");
    action({{"command", "save"}});
    auto saved = project.loadStartup().value();
    require(saved.entity(saved.findEntity(3).value())->animation.has_value(),
            "Saved scene retains animation player");
    action({{"command", "duplicate-selection"}});
    frame();
    auto duplicated = inspect()["scene"];
    std::size_t players{};

    for (const auto& entity : duplicated["entities"]) {
      if (!entity["animation"].is_null()) {
        ++players;

        if (entity["id"] != "3") {
          require(entity["animation"]["targets"][0]["entity"] != "3",
                  "Duplicate must own independent animation targets");
        }
      }
    }

    require(players == 2, "Duplicate retains both animation players");
    action({{"command", "undo"}});
    frame();
    require(inspect()["scene"] == authored, "Undo restores animated hierarchy");
    const auto revision = inspect()["revision"];
    auto deleted = workspace.dispatch(
        Json{{"command", "edit"},
             {"revision", revision},
             {"operations", Json::array({{{"op", "remove"}, {"entity", "4"}}})}}
            .dump());
    require(!deleted && inspect()["scene"] == authored,
            "Reject dangling animation target transaction");
    send({{"command", "close"}, {"discard", true}});
    workspace.step(16ms).value();
    std::println(
        "Scene animation acceptance passed: model insertion, Inspector, Play pixels, stop, save, "
        "copy/remap and undo");
    return 0;
  }
}  // namespace Dot::Editor

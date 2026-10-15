#include <algorithm>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <print>
#include <stdexcept>
#include <string>
#include <utility>

import Dot.Assets.Image;
import Dot.Core.Files;
import Dot.Core.Process;
import Dot.Core.StagedDirectory;
import Dot.Editor.Workspace;
import Dot.Project;
import Dot.Project.GameSession;

namespace Dot::Editor {
  /** Actual skinned GLB import/Inspector/history/Play and relocated native export.
   * 实际蒙皮 GLB 导入、检查器、历史、播放及搬移后的原生游戏导出。 */
  auto testModelSkinning(const std::filesystem::path& compiler, const std::filesystem::path& parent)
      -> int {
    using Json = nlohmann::json;
    using namespace std::chrono_literals;
    const auto require = [](bool value, const char* message) {
      if (!value) {
        throw std::runtime_error(message);
      }
    };
    const auto utf = [](const std::filesystem::path& path) {
      const auto bytes = path.generic_u8string();
      return std::string(bytes.begin(), bytes.end());
    };
    auto output = Core::FileSystem::open(parent).value();
    auto storage = Core::StagedDirectory::create(output).value();
    auto project = Project::Project::create(storage.files().value(), "game").value();
    const auto exported = storage.files()->root() / "native";

    {
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
      send({{"command", "set-view"}, {"mode", "game"}});
      const auto imported
          = send({{"command", "import-model"},
                  {"path", "assets/character"},
                  {"source", utf(std::filesystem::path(DOT_MODEL_FIXTURES) / "skinned.glb")}});
      require(
          imported["skins"] == 1 && imported["skinnedMeshes"] == 1 && imported["animations"] == 1,
          "Editor import must expose generated skin assets and owners");
      action({{"command", "edit"},
              {"operations",
               Json::array({{{"op", "remove"}, {"entity", "2"}},
                            {{"op", "instantiate-model"}, {"path", imported["scene"]}}})}});
      const auto light = inspect()["nextEntity"];
      action({{"command", "edit"},
              {"operations", Json::array({{{"op", "create"}, {"name", "Key light"}},
                                          {{"op", "component"},
                                           {"entity", light},
                                           {"component", "light"},
                                           {"value",
                                            {{"kind", "directional"},
                                             {"color", {1, 1, 1}},
                                             {"intensity", 4},
                                             {"range", 10},
                                             {"innerConeDegrees", 20},
                                             {"outerConeDegrees", 30},
                                             {"enabled", true},
                                             {"shadows", nullptr}}}}})}});
      auto entities = inspect()["scene"]["entities"];
      const auto mesh = *std::ranges::find_if(
          entities, [](const auto& entity) { return !entity["skin"].is_null(); });
      const auto actor = *std::ranges::find_if(
          entities, [](const auto& entity) { return !entity["animation"].is_null(); });
      action({{"command", "select"}, {"entity", mesh["id"]}});
      send({{"command", "show-pane"}, {"pane", "inspector"}});
      frame();
      require(inspect()["workspace"]["inspector"]["skin"] == mesh["skin"],
              "Skin Inspector must reflect remapped imported joint IDs");
      require(mesh["skin"]["joints"].size() == 8 && mesh["skin"]["joints"][0] != "4",
              "Model insertion must remap source skin joints past existing entities");
      const auto initial = workspace.captureViewport().value();
      const auto authored = inspect()["scene"];
      auto bad = workspace.dispatch(
          Json{{"command", "duplicate-selection"}, {"revision", inspect()["revision"]}}.dump());
      require(!bad && inspect()["scene"] == authored,
              "Copying only a skin mesh must reject external joint aliasing");
      auto skin = mesh["skin"];
      skin["enabled"] = false;
      action({{"command", "edit"},
              {"operations", Json::array({{{"op", "component"},
                                           {"entity", mesh["id"]},
                                           {"component", "skin"},
                                           {"value", skin}}})}});
      frame();
      require(workspace.captureViewport()->pixels != initial.pixels,
              "Disabled skin must expose authored mesh-node placement");
      action({{"command", "undo"}});
      frame();
      require(workspace.captureViewport()->pixels == initial.pixels,
              "Undo must restore imported skin pixels");
      action({{"command", "select"}, {"entity", actor["id"]}});
      action({{"command", "duplicate-selection"}});
      frame();
      unsigned skins{};

      const auto duplicatedEntities = inspect()["scene"]["entities"];

      for (const auto& entity : duplicatedEntities) {
        skins += !entity["skin"].is_null() ? 1U : 0U;

        if (!entity["skin"].is_null() && entity["id"] != mesh["id"]) {
          for (const auto& joint : entity["skin"]["joints"]) {
            require(
                std::ranges::find(mesh["skin"]["joints"], joint) == mesh["skin"]["joints"].end(),
                "Every duplicated character joint must be independently remapped");
          }
        }
      }

      require(skins == 2, "Character duplicate retains both native skins");
      action({{"command", "undo"}});
      frame();
      require(inspect()["scene"] == authored, "Undo restores complete imported character");
      action({{"command", "play"}});
      workspace.step(500ms).value();
      const auto played = workspace.captureViewport().value();
      require(played.pixels != initial.pixels, "Imported joints must animate actual Play pixels");
      require(inspect()["scene"] == authored, "Play must leave authoring poses unchanged");
      action({{"command", "stop-play"}});
      frame();
      require(workspace.captureViewport()->pixels == initial.pixels,
              "Stop must restore imported skin authoring pixels");
      action({{"command", "save"}});
      action(
          {{"command", "export"}, {"parent", utf(storage.files()->root())}, {"folder", "native"}});
      output.writeAtomic("editor-model-skin-bind.png", Assets::encodeImage(initial).value())
          .value();
      output.writeAtomic("editor-model-skin-play.png", Assets::encodeImage(played).value()).value();
      send({{"command", "close"}, {"discard", true}});
      workspace.step(16ms).value();
    }

    const auto relocated = storage.files()->root() / "relocated";
    std::filesystem::rename(exported, relocated);
    std::filesystem::remove_all(project.files()->root());
#ifdef _WIN32
    const auto executable = relocated / "DotGame.exe";
#else
    const auto executable = relocated / "DotGame";
#endif
    const auto child = Core::runProcess({executable, {"--frames", "8"}, relocated, 30s}).value();
    require(child.exitCode == 0,
            "Relocated native skin game must run without its authoring project");
    std::println(
        "Editor skin acceptance passed: GLB import, Inspector, joint remapping, "
        "history/Play pixels, save and relocated native game");
    return 0;
  }
}  // namespace Dot::Editor

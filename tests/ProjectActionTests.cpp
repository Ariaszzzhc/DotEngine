#include <doctest/doctest.h>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

import Dot.Animation.Files;
import Dot.World.Scene;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Editor.Home;
import Dot.Project;

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
      REQUIRE(players.size() == 2);
      const auto panel = std::ranges::find_if(players, [&](const auto& player) {
        return startup.entity(player.entity)->name == "Animated panel";
      });
      REQUIRE(panel != players.end());
      REQUIRE(panel->properties.clips.size() == 2);
      CHECK(
          std::ranges::count_if(
              players, [](const auto& player) { return player.properties.controller.has_value(); })
          == 1);

      for (const auto& path : panel->properties.clips) {
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

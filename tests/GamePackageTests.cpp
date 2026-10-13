#include <doctest/doctest.h>

#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

#include "SceneAudioFixture.hpp"

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Project;
import Dot.Project.GamePackage;
import Dot.Project.GameSession;
import Dot.Script.Compiler;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace {
  using namespace Dot::Project;
  using Json = nlohmann::json;

  struct Fixture {
    DotTest::Audio::Directory directory;
    Project project;

    Fixture()
        : project(Project::create(directory.files(), "source", {},
                                  Dot::Core::FileSystem::open(DOT_SCRIPT_SDK_DIRECTORY).value())
                      .value()) {
      REQUIRE(project.files()->writeTextAtomic("assets/owned.bin", std::string("A\0B", 3)));
    }

    auto compiler() -> Dot::Script::TypeScriptCompiler {
      return Dot::Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
    }

    auto publish() -> Dot::Core::FileSystem {
      auto scene = project.loadStartup().value();
      auto result = GamePackage::publish(project, scene, compiler(), directory.files(), "game");
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return *result;
    }
  };
}  // namespace

TEST_CASE("game_package.relocation_snapshot_and_precompiled_session_are_independent") {
  Fixture fixture;
  auto scene = fixture.project.loadStartup().value();
  const auto id = scene.entities()->at(1).id;
  auto transform = scene.entity(id)->transform;
  transform.position.x = 0.5;
  REQUIRE(scene.setTransform(id, transform));
  auto published = GamePackage::publish(fixture.project, scene, fixture.compiler(),
                                        fixture.directory.files(), "game")
                       .value();
  CHECK(fixture.project.loadStartup()->entities()->at(1).transform.position.x == 0);
  auto movedRoot = fixture.directory.root / "relocated";
  std::filesystem::rename(published.root(), movedRoot);
  auto package = GamePackage::open(Dot::Core::FileSystem::open(movedRoot).value()).value();
  CHECK_FALSE(package.description()->hotReload);
  CHECK(package.scene()->entities()->at(1).transform.position.x == 0.5);
  CHECK(package.files()->readText("assets/owned.bin").value() == std::string("A\0B", 3));
  std::filesystem::remove_all(fixture.project.files()->root());
  std::filesystem::remove_all(movedRoot);
  auto owned = package.files().value().root();
  {
    auto source = package;
    auto moved = std::move(source);
    CHECK_FALSE(source.files());
    CHECK_FALSE(source.scene());
    auto runtimeScene = moved.scene().value();
    auto session = GameSession::createCompiled(moved.files().value(), moved.description().value(),
                                               runtimeScene, moved.program().value())
                       .value();
    REQUIRE(session.start());
    REQUIRE(session.update(0));
    REQUIRE(session.close());
  }
  CHECK(std::filesystem::exists(owned));
  CHECK_FALSE(std::filesystem::exists(owned / "scripts/mover.ts"));
}

TEST_CASE("game_package.strict_manifests_trees_and_limits_fail_without_leaks") {
  Fixture fixture;
  auto published = fixture.publish();
  const auto original = published.readText("game.json").value();
  const auto generations = Dot::Script::compilerGenerations();
  auto manifest = Json::parse(original);
  auto changed = manifest;
  changed["description"]["hotReload"] = true;
  REQUIRE(published.writeTextAtomic("game.json", changed.dump()));
  CHECK_FALSE(GamePackage::open(published));
  changed = manifest;
  changed["files"][0]["path"] = "../escape";
  REQUIRE(published.writeTextAtomic("game.json", changed.dump()));
  CHECK_FALSE(GamePackage::open(published));
  changed = manifest;
  changed["bytes"] = 1;
  REQUIRE(published.writeTextAtomic("game.json", changed.dump()));
  CHECK_FALSE(GamePackage::open(published));
  REQUIRE(published.writeTextAtomic("game.json", "{\"version\":1,\"version\":1}"));
  CHECK_FALSE(GamePackage::open(published));
  REQUIRE(published.writeTextAtomic("game.json", original));
  REQUIRE(published.writeTextAtomic("unexpected.txt", "extra"));
  CHECK_FALSE(GamePackage::open(published));
  std::filesystem::remove(published.root() / "unexpected.txt");
  REQUIRE(published.writeTextAtomic("content/assets/extra.bin", "extra"));
  CHECK_FALSE(GamePackage::open(published));
  std::filesystem::remove(published.root() / "content/assets/extra.bin");
  PackageLimits limits;
  limits.totalBytes = 1;
  CHECK_FALSE(GamePackage::open(published, limits));
  CHECK(Dot::Script::compilerGenerations() == generations);
}

TEST_CASE("game_package_invalid_scripts_assets_paths_and_destinations_do_not_publish") {
  Fixture fixture;
  auto scene = fixture.project.loadStartup().value();
  REQUIRE(
      fixture.project.files()->writeTextAtomic("scripts/mover.ts", "export const bad:number='x';"));
  CHECK_FALSE(GamePackage::publish(fixture.project, scene, fixture.compiler(),
                                   fixture.directory.files(), "failed"));
  CHECK_FALSE(std::filesystem::exists(fixture.directory.root / "failed"));
  REQUIRE(fixture.project.files()->writeTextAtomic(
      "scripts/mover.ts",
      "import {ScriptComponent} from './dot/engine.js';export class Mover extends "
      "ScriptComponent{}"));
  auto id = scene.entities()->at(1).id;
  Dot::World::AudioSource audio;
  audio.asset = "assets/missing.wav";
  REQUIRE(scene.setAudioSource(id, audio));
  CHECK_FALSE(GamePackage::publish(fixture.project, scene, fixture.compiler(),
                                   fixture.directory.files(), "failed"));
  REQUIRE(scene.setAudioSource(id, std::nullopt));
  auto published = GamePackage::publish(fixture.project, scene, fixture.compiler(),
                                        fixture.directory.files(), "game")
                       .value();
  const auto before = published.readText("game.json").value();
  CHECK_FALSE(GamePackage::publish(fixture.project, scene, fixture.compiler(),
                                   fixture.directory.files(), "game"));
  CHECK(published.readText("game.json").value() == before);
  CHECK_FALSE(GamePackage::publish(fixture.project, scene, fixture.compiler(),
                                   fixture.directory.files(), "../escape"));
  std::filesystem::create_symlink("owned.bin", fixture.project.files()->root() / "assets/link.bin");
  CHECK_FALSE(GamePackage::publish(fixture.project, scene, fixture.compiler(),
                                   fixture.directory.files(), "failed"));
  CHECK_FALSE(std::filesystem::exists(fixture.directory.root / "failed"));
}

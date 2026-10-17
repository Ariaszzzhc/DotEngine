#include <doctest/doctest.h>

#include <chrono>
#include <expected>
#include <filesystem>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <utility>

#include "EnvironmentFixture.hpp"
#include "SceneAudioFixture.hpp"
#include "ScriptCompilation.hpp"

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Editor.Play;
import Dot.Editor.PlayInput;
import Dot.Project;
import Dot.Project.GameSession;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Script.Compiler;
import Dot.Platform.Input;
import Dot.Platform.Events;

namespace {
  using namespace Dot::Editor;
  using namespace std::chrono_literals;

  struct Fixture {
    DotTest::Audio::Directory directory;
    Dot::Project::Project project;

    Fixture() : project(Dot::Project::Project::create(directory.files(), "play", {}).value()) {
    }

    auto compiler() -> Dot::Script::TypeScriptCompiler {
      return Dot::Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
    }

    auto config() -> PlayConfig {
      PlayConfig config;
      config.session.audioMode = Dot::Project::SessionAudio::Offline;
      config.session.pollInterval = 0ms;
      config.session.physics.simulation.gravity = {};
      return config;
    }

    auto controller(int speed = 2, bool fail = false) -> void {
      const auto source = std::string("import {ScriptComponent,input} from 'dot:engine';")
                          + "export class Mover extends ScriptComponent {"
                            "onFixedUpdate(){this.self.setVelocity({x:input.key('D').down?"
                          + std::to_string(speed)
                          + ":0,y:0,z:0});}onUpdate(){"
                          + (fail ? "throw new Error('ordinary play failure');" : "this.self.name='running';")
                          + "}onStop(){this.self.name='stopped';}}";
      REQUIRE(project.files()->writeTextAtomic("scripts/mover.ts", source));
    }
  };

  auto held() -> Dot::Platform::InputState {
    Dot::Platform::InputState input;
    REQUIRE(input.consume({0, 0, Dot::Platform::KeyChanged{Dot::Platform::Key::D, true, false}}));
    return input;
  }
}  // namespace

TEST_CASE("play.independent_unsaved_scene_fixed_physics_and_stop_hooks") {
  Fixture fixture;
  fixture.controller();
  auto authoring = fixture.project.loadStartup().value();
  const auto actor = authoring.entities()->at(1).id;
  auto transform = authoring.entity(actor)->transform;
  transform.position.x = 5;
  REQUIRE(authoring.setTransform(actor, transform));
  REQUIRE(authoring.setRigidBody(actor, Dot::World::RigidBody{}));
  const auto initial = Dot::World::encodeScene(authoring).value();
  auto play = PlaySession::create(fixture.project, fixture.compiler(), authoring, fixture.config())
                  .value();
  REQUIRE(play.scene().value()->id() != authoring.id());
  REQUIRE(play.advance(held(), 40ms));
  CHECK(play.stats()->frames == 1);
  CHECK(play.stats()->fixedSteps == 2);
  CHECK(play.scene().value()->entities()->at(1).transform.position.x
        == doctest::Approx(5 + 4.0 / 60));
  CHECK(play.lastFrame()->reports.size() == 3);
  CHECK(Dot::World::encodeScene(authoring).value() == initial);
  CHECK(fixture.project.loadStartup()->entities()->at(1).transform.position.x == 0);
  auto stopped = play.stop();
  REQUIRE(stopped);
  CHECK(stopped->failures.empty());
  CHECK(play.stats()->state == PlayState::Stopped);
  CHECK(play.scene().value()->entities()->at(1).name == "stopped");
  CHECK_FALSE(play.advance(held(), 1ms));
  REQUIRE(play.stop());
  CHECK(Dot::World::encodeScene(authoring).value() == initial);
}

TEST_CASE("play.pointer_capture_ack_is_published_without_initializing_a_window") {
  Fixture fixture;
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/mover.ts", R"ts(
import {ScriptComponent, input} from 'dot:engine';
export class Mover extends ScriptComponent {
  onStart(): void { input.capturePointer(true); }
  onUpdate(): void { this.self.name = input.pointerCaptured ? 'captured' : 'released'; }
}
)ts"));
  auto authoring = fixture.project.loadStartup().value();
  auto play = PlaySession::create(fixture.project, fixture.compiler(), authoring, fixture.config())
                  .value();
  CHECK(play.pointerCaptureRequested().value());
  REQUIRE(play.advance(held(), 1ms, true));
  CHECK(play.scene().value()->entities()->at(1).name == "captured");
  REQUIRE(play.advance(held(), 1ms));
  CHECK(play.scene().value()->entities()->at(1).name == "released");
  auto moved = std::move(play);
  CHECK_FALSE(play.pointerCaptureRequested());
  REQUIRE(moved.stop());
  CHECK_FALSE(moved.pointerCaptureRequested());
}

TEST_CASE("play.timing_bounds_catchup_reports_and_input_failure_preserve_state") {
  Fixture fixture;
  auto authoring = fixture.project.loadStartup().value();
  auto config = fixture.config();
  config.maxUpdates = 1;
  auto play = PlaySession::create(fixture.project, fixture.compiler(), authoring, config).value();
  CHECK_FALSE(play.advance(Dot::Platform::InputState{}, -1ns));
  CHECK(play.stats()->frames == 0);
  REQUIRE(play.advance(held(), 2s));
  CHECK(play.stats()->fixedSteps == 1);
  CHECK(play.lastFrame()->dropped > 1900ms);
  CHECK(play.scene().value()->entities()->at(1).transform.position.x == doctest::Approx(1.5));
  REQUIRE(play.advance(Dot::Platform::InputState{}, 0ns));
  CHECK(play.stats()->fixedSteps == 1);
  CHECK(play.lastFrame()->reports.size() == 1);
  REQUIRE(play.advance(Dot::Platform::InputState{}, std::chrono::nanoseconds::max()));
  CHECK(play.stats()->fixedSteps == 2);
  CHECK(play.lastFrame()->dropped.count() > 0);
  REQUIRE(play.stop());
  auto narrow = fixture.config();
  narrow.session.input.textBytes = 1;
  auto bounded
      = PlaySession::create(fixture.project, fixture.compiler(), authoring, narrow).value();
  Dot::Platform::InputState invalidInput;
  REQUIRE(invalidInput.consume({0, 0, Dot::Platform::Text{"xx"}}));
  CHECK_FALSE(bounded.advance(invalidInput, 40ms));
  CHECK(bounded.stats()->frames == 0);
  CHECK(bounded.stats()->fixedSteps == 0);
  REQUIRE(bounded.advance(held(), 40ms));
  CHECK(bounded.stats()->fixedSteps == 2);
  REQUIRE(bounded.stop());
  config.maxUpdates = 0;
  CHECK_FALSE(PlaySession::create(fixture.project, fixture.compiler(), authoring, config));
  config.maxUpdates = 65;
  CHECK_FALSE(PlaySession::create(fixture.project, fixture.compiler(), authoring, config));
}

TEST_CASE("play.hot_reload_failure_keeps_running_and_new_source_recovers") {
  Fixture fixture;
  auto authoring = fixture.project.loadStartup().value();
  auto play = PlaySession::create(fixture.project, fixture.compiler(), authoring, fixture.config())
                  .value();
  REQUIRE(play.advance(held(), 100ms));
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/mover.ts",
                                                   "export const x: number = 'bad';"));
  REQUIRE(play.advance(held(), 100ms));
  Dot::Tests::awaitCompilation([&] { return play.pollScripts(); });
  REQUIRE(play.advance(held(), 0ns));
  bool reloadFailed = false;

  for (const auto& report : play.lastFrame()->reports) {
    reloadFailed = reloadFailed || report.source.error.has_value();
  }

  CHECK(reloadFailed);
  CHECK(play.stats()->state == PlayState::Running);
  const auto before = play.scene().value()->entities()->at(1).transform.position.x;
  REQUIRE(fixture.project.files()->writeTextAtomic(
      "scripts/mover.ts",
      "import {ScriptComponent} from 'dot:engine'; export class Mover extends ScriptComponent "
      "{onUpdate(){const t=this.self.transform;t.position.x+=2;this.self.transform=t;}}"));
  auto completed = Dot::Tests::awaitCompilation([&] { return play.pollScripts(); });
  REQUIRE(play.advance(Dot::Platform::InputState{}, 0ns));
  CHECK(completed->committed);
  CHECK(play.scene().value()->entities()->at(1).transform.position.x
        == doctest::Approx(before + 2));
  REQUIRE(play.stop());
}

TEST_CASE("play.component_errors_are_visible_and_realm_fault_requires_stop") {
  Fixture fixture;
  fixture.controller(2, true);
  auto authoring = fixture.project.loadStartup().value();
  const auto actor = authoring.entities()->at(1).id;
  REQUIRE(authoring.setRigidBody(actor, Dot::World::RigidBody{}));
  auto config = fixture.config();
  auto play = PlaySession::create(fixture.project, fixture.compiler(), authoring, config).value();
  REQUIRE(play.advance(Dot::Platform::InputState{}, 0ns));
  REQUIRE(play.lastFrame()->reports.back().frame.failures.size() == 1);
  CHECK(play.stats()->state == PlayState::Running);
  REQUIRE(play.stop());
  REQUIRE(fixture.project.files()->writeTextAtomic(
      "scripts/mover.ts",
      "import {ScriptComponent} from 'dot:engine';export class Mover extends ScriptComponent "
      "{onUpdate(){while(true){}}}"));
  config.session.scripts.runtime.milliseconds = 5;
  auto faulted
      = PlaySession::create(fixture.project, fixture.compiler(), authoring, config).value();
  auto retainedData = faulted.gameData().value();
  auto failure = faulted.advance(Dot::Platform::InputState{}, 0ns);
  REQUIRE_FALSE(failure);
  CHECK(failure.error().message.find("entity " + std::to_string(actor.value)) != std::string::npos);
  REQUIRE(faulted.lastFrame()->reports.back().frame.failures.size() == 1);
  const auto& cause = faulted.lastFrame()->reports.back().frame.failures.front().error.message;
  CHECK(failure.error().message.find(cause) != std::string::npos);
  CHECK(faulted.stats()->state == PlayState::Faulted);
  CHECK(faulted.lastFrame()->reports.back().frame.realmFaulted);
  CHECK_FALSE(retainedData.perform(R"({"command":"set","key":"score","value":0})"));
  CHECK_FALSE(faulted.advance(Dot::Platform::InputState{}, 1ns));
  REQUIRE(faulted.stop());
  CHECK(faulted.stats()->state == PlayState::Stopped);
}

TEST_CASE("play.start_realm_failure_retains_cause_and_releases_temporary_storage") {
  Fixture fixture;
  auto files = fixture.directory.files();
  REQUIRE(files.createDirectories("startup-saves"));
  auto storage = Dot::Core::FileSystem::open(files.root() / "startup-saves").value();
  REQUIRE(fixture.project.files()->writeTextAtomic(
      "scripts/mover.ts",
      "import {ScriptComponent} from 'dot:engine';export class Mover extends ScriptComponent "
      "{onStart(){while(true){}}}"));
  auto authoring = fixture.project.loadStartup().value();
  const auto actor = authoring.entities()->at(1).id;
  REQUIRE(authoring.setRigidBody(actor, Dot::World::RigidBody{}));
  const auto before = Dot::World::encodeScene(authoring).value();
  const auto generations = Dot::Script::compilerGenerations();
  auto config = fixture.config();
  config.temporarySavesParent = storage;
  config.session.scripts.runtime.milliseconds = 5;
  auto rejected = PlaySession::create(fixture.project, fixture.compiler(), authoring, config);
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().code == Dot::Core::ErrorCode::InvalidState);
  CHECK(rejected.error().message.find("Start play script realm faulted") != std::string::npos);
  CHECK(rejected.error().message.find("entity " + std::to_string(actor.value))
        != std::string::npos);
  CHECK(rejected.error().message.find("Script execution time limit exceeded") != std::string::npos);
  CHECK(Dot::Script::compilerGenerations() == generations);
  CHECK(std::filesystem::directory_iterator(storage.root())
        == std::filesystem::directory_iterator{});
  CHECK(Dot::World::encodeScene(authoring).value() == before);
  fixture.controller();
  auto recovered = PlaySession::create(fixture.project, fixture.compiler(), authoring, config);
  REQUIRE(recovered);
  REQUIRE(recovered->stop());
  CHECK(std::filesystem::directory_iterator(storage.root())
        == std::filesystem::directory_iterator{});
}

TEST_CASE("play.moves_source_lifetime_workers_and_failed_open_release_generations") {
  Fixture fixture;
  const auto generations = Dot::Script::compilerGenerations();

  for (int cycle = 0; cycle < 2; ++cycle) {
    auto authoring = fixture.project.loadStartup().value();
    auto source
        = PlaySession::create(fixture.project, fixture.compiler(), authoring, fixture.config())
              .value();
    auto play = std::move(source);
    CHECK_FALSE(source.scene());
    CHECK_FALSE(source.advance(Dot::Platform::InputState{}, 0ns));
    CHECK_FALSE(source.stop());
    CHECK_FALSE(source.stats());
    CHECK_FALSE(source.lastFrame());
    authoring = Dot::World::Scene::create("Gone").value();
    bool rejected = false;
    auto worker = std::thread([&] {
      rejected = !play.advance(Dot::Platform::InputState{}, 0ns) && !play.scene() && !play.stop();
    });
    worker.join();
    CHECK(rejected);
    REQUIRE(play.advance(Dot::Platform::InputState{}, 0ns));
    REQUIRE(play.stop());
  }

  CHECK(Dot::Script::compilerGenerations() == generations);
  REQUIRE(
      fixture.project.files()->writeTextAtomic("scripts/mover.ts", "export const bad:number='x';"));
  auto authoring = fixture.project.loadStartup().value();
  CHECK_FALSE(
      PlaySession::create(fixture.project, fixture.compiler(), authoring, fixture.config()));
  CHECK(Dot::Script::compilerGenerations() == generations);
}

TEST_CASE("play.scene_environment_is_copied_and_isolated_from_authoring_changes") {
  Fixture fixture;
  REQUIRE(DotTest::Environment::write(fixture.project.files().value()));
  auto authoring = fixture.project.loadStartup().value();
  const Dot::World::Environment expected{"assets/studio.hdr", 2, 90, true, {16, 4, 16, 16, 32}};
  REQUIRE(authoring.setEnvironment(expected));
  const Dot::Graphics::OcclusionSettings occlusion{0.75F, 0.025F, 2, 16, 8, 4};
  REQUIRE(authoring.setOcclusion(occlusion));
  auto play = PlaySession::create(fixture.project, fixture.compiler(), authoring, fixture.config())
                  .value();
  CHECK(play.scene().value()->environment().value() == expected);
  REQUIRE(authoring.setEnvironment(std::nullopt));
  REQUIRE(authoring.setOcclusion(std::nullopt));
  CHECK(play.scene().value()->environment().value() == expected);
  CHECK_FALSE(authoring.environment().value());
  CHECK(play.scene().value()->occlusion().value() == occlusion);
  CHECK_FALSE(authoring.occlusion().value());
}

TEST_CASE("play.agent_input_drives_shared_scripts_physics_then_releases_without_authoring_edits") {
  Fixture fixture;
  fixture.controller();
  auto authoring = fixture.project.loadStartup().value();
  const auto actor = authoring.entities()->at(1).id;
  REQUIRE(authoring.setRigidBody(actor, Dot::World::RigidBody{}));
  const auto before = Dot::World::encodeScene(authoring).value();
  auto play = PlaySession::create(fixture.project, fixture.compiler(), authoring, fixture.config())
                  .value();
  PlayInput input;
  const auto request = nlohmann::json{
      {"frames", 3},       {"keys", {"D"}},          {"buttons", nlohmann::json::array()},
      {"pointer", {0, 0}}, {"pointerDelta", {0, 0}}, {"wheel", {0, 0}},
      {"text", ""}};
  REQUIRE(input.submit(request.dump()));
  auto scene = play.scene().value();
  const auto runtime = scene->findEntity(actor.value).value();

  auto previous = scene->entity(runtime)->transform.position.x;

  for (int frame = 0; frame < 3; ++frame) {
    auto next = input.next();
    REQUIRE(next);
    REQUIRE(next->has_value());
    REQUIRE(play.advance(**next, 40ms));
    const auto position = scene->entity(runtime)->transform.position.x;
    CHECK(position > previous);
    previous = position;
  }

  const auto moved = scene->entity(runtime)->transform.position.x;
  CHECK(moved > authoring.entity(actor)->transform.position.x + .1F);
  auto release = input.next();
  REQUIRE(release);
  REQUIRE(release->has_value());
  CHECK((**release).key(Dot::Platform::Key::D).released);
  REQUIRE(play.advance(**release, 40ms));
  CHECK(scene->entity(runtime)->transform.position.x == doctest::Approx(moved));
  CHECK_FALSE(input.next().value());
  CHECK(Dot::World::encodeScene(authoring).value() == before);
  REQUIRE(play.stop());
}

TEST_CASE("play.game_data_and_save_files_are_isolated_and_removed_after_stop") {
  Fixture fixture;
  auto files = fixture.directory.files();
  REQUIRE(files.createDirectories("temporary-saves"));
  auto root = Dot::Core::FileSystem::open(files.root() / "temporary-saves").value();
  auto config = fixture.config();
  config.temporarySavesParent = root;
  auto authoring = fixture.project.loadStartup().value();
  const auto manifest = fixture.project.files()->readText("project.dotproject").value();
  auto play = PlaySession::create(fixture.project, fixture.compiler(), authoring, config).value();
  auto data = play.gameData().value();
  REQUIRE(data.perform(R"({"command":"set","key":"score","value":7})"));
  REQUIRE(data.perform(R"({"command":"save","slot":"test","overwrite":false})"));
  REQUIRE(std::distance(std::filesystem::directory_iterator(root.root()),
                        std::filesystem::directory_iterator{})
          == 1);
  REQUIRE(play.stop());
  CHECK(std::filesystem::directory_iterator(root.root()) == std::filesystem::directory_iterator{});
  CHECK_FALSE(data.perform(R"({"command":"load","slot":"test","revision":"2"})"));
  CHECK(nlohmann::json::parse(data.perform(R"({"command":"snapshot"})").value())["data"]["score"]
        == 7);
  auto restarted
      = PlaySession::create(fixture.project, fixture.compiler(), authoring, config).value();
  auto fresh = restarted.gameData().value();
  CHECK(nlohmann::json::parse(fresh.perform(R"({"command":"snapshot"})").value())["data"].empty());
  CHECK(nlohmann::json::parse(fresh.perform(R"({"command":"list-saves"})").value()).empty());
#if !defined(_WIN32)
  REQUIRE(fresh.perform(R"({"command":"save","slot":"blocked","overwrite":false})"));
  const auto owned = std::filesystem::directory_iterator(root.root())->path();
  const auto permissions = std::filesystem::status(owned).permissions();

  struct Restore {
    std::filesystem::path path;
    std::filesystem::perms permissions;

    ~Restore() {
      std::error_code ignored;
      std::filesystem::permissions(path, permissions, ignored);
    }
  } restore{owned, permissions};

  std::filesystem::permissions(
      owned, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
  auto rejected = restarted.stop();

  if (!rejected) {
    CHECK(rejected.error().code == Dot::Core::ErrorCode::Io);
    CHECK(restarted.stats()->state == PlayState::Stopped);
    CHECK_FALSE(fresh.perform(R"({"command":"set","key":"score","value":0})"));
    std::filesystem::permissions(owned, permissions);
  } else {
    WARN_MESSAGE(false,
                 "Current credentials bypass directory permissions; failure fixture skipped");
  }
#endif
  REQUIRE(restarted.stop());
  CHECK(fixture.project.files()->readText("project.dotproject").value() == manifest);
  config.session.progress.saves = files;
  CHECK_FALSE(PlaySession::create(fixture.project, fixture.compiler(), authoring, config));
  CHECK(std::filesystem::directory_iterator(root.root()) == std::filesystem::directory_iterator{});
}

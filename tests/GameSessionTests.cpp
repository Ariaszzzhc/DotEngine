#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "SceneAudioFixture.hpp"
#include "ScenePreparation.hpp"
#include "ScriptCompilation.hpp"

import Dot.Project;
import Dot.Project.GameSession;
import Dot.Project.PlayerControl;
import Dot.Core.Result;
import Dot.Core.Process;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Platform.Input;
import Dot.Platform.Events;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Audio.Mixer;
import Dot.Game.State;
import Dot.Game.Saves;
import Dot.Physics.Body;
import Dot.Animation.Clip;
import Dot.Animation.Files;
import Dot.Animation.ControllerFiles;
import Dot.Project.ScenePreparation;
import Dot.Assets.Mesh;
import Dot.Graphics.Geometry;

namespace {
  using namespace Dot::Project;

  struct Fixture {
    DotTest::Audio::Directory directory;
    Project project;

    explicit Fixture(bool reload = true, std::uint32_t hz = 60)
        : project(
              Project::create(directory.files(), "game",
                              {"Session", "scenes/main.dotscene", "scripts", 320, 240, hz, reload})
                  .value()) {
    }

    auto compiler() const -> Dot::Script::TypeScriptCompiler {
      return Dot::Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
    }

    auto session(const GameSessionConfig& config = {}) const -> GameSession {
      auto result = GameSession::create(project, compiler(), config);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }

    auto controller(int speed = 2) const -> void {
      auto scene = project.loadStartup().value();
      auto entities = scene.entities().value();
      auto cube = std::ranges::find(entities, "Cube", &Dot::World::EntityInfo::name);
      REQUIRE(cube != entities.end());
      Dot::World::RigidBody body;
      REQUIRE(scene.setRigidBody(cube->id, body));
      Dot::World::AudioSource audio;
      audio.asset = "assets/tone.wav";
      audio.voice.looping = true;
      audio.playOnStart = false;
      REQUIRE(scene.setAudioSource(cube->id, audio));
      REQUIRE(project.saveStartup(scene));
      auto files = project.files().value();
      DotTest::Audio::writeWave(files, "assets/tone.wav", std::vector<float>(1024, 0.25F));
      writeController(speed);
    }

    auto writeController(int speed) const -> void {
      auto source
          = "import {ScriptComponent,input,physics,audio} from 'dot:engine';"
            "export class Mover extends ScriptComponent {#ticks=0;boot=input.index;"
            "onStart(){this.self.name='start'+this.boot;this.self.playAudio();audio.setVolume(.5);}"
            "onFixedUpdate(){++this.#ticks;this.self.setVelocity({x:input.key('D').down?"
            + std::to_string(speed)
            + ":0,y:0,z:0});this.self.name='fixed'+this.#ticks;}"
              "onUpdate(){this.self.name='update'+this.#ticks+(this.self.audioVoice.playing?'play':'pause');}"
              "onSaveState(){return {ticks:this.#ticks};}"
              "onLoadState(state:{ticks:number}){this.#ticks=state.ticks;}"
              "onStop(){this.self.name='stopped';}}";
      REQUIRE(project.files()->writeTextAtomic("scripts/mover.ts", source));
    }
  };

  auto awaitInitial(GameSession& session, SessionState target) -> Dot::Core::Result<SessionState> {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};

    for (;;) {
      auto result = session.pollPreparation();

      if (!result || *result == target) {
        return result;
      }

      REQUIRE((*result == SessionState::Preparing || *result == SessionState::Cancelling));
      REQUIRE(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  }

  auto good(Dot::Core::Result<Dot::Script::ScriptReport> report) -> void {
    REQUIRE_MESSAGE(report, (report ? "" : report.error().message));
    CHECK(report->failures.empty());
    CHECK_FALSE(report->realmFaulted);
  }

  auto good(Dot::Core::Result<Dot::Script::LiveScriptReport> report)
      -> Dot::Script::LiveScriptReport {
    REQUIRE_MESSAGE(report, (report ? "" : report.error().message));
    CHECK(report->frame.failures.empty());
    CHECK_FALSE(report->frame.realmFaulted);
    return std::move(*report);
  }

  auto progressBindings(Dot::Game::State state) -> Dot::Script::HostBindings {
    return {{"progress.get",
             [state](std::string_view arguments) {
               const auto values = nlohmann::json::parse(arguments);
               return state.get(values.at(0).get<std::string>());
             },
             Dot::Script::HostAccess::ReadOnly},
            {"progress.set",
             [state](std::string_view arguments) mutable
                 -> Dot::Core::Result<std::optional<std::string>> {
               const auto values = nlohmann::json::parse(arguments);
               auto changed = state.set(values.at(0).get<std::string>(), values.at(1).dump());
               return changed ? Dot::Core::Result<std::optional<std::string>>{std::nullopt}
                              : Dot::Core::Result<std::optional<std::string>>{
                                    std::unexpected(changed.error())};
             }}};
  }

  auto actor(const GameSession& session) -> Dot::World::EntityInfo {
    auto entities = session.scene().value()->entities().value();
    auto found = std::ranges::find_if(entities,
                                      [](const auto& value) { return value.script.has_value(); });
    REQUIRE(found != entities.end());
    return *found;
  }

  auto held() -> Dot::Platform::InputState {
    Dot::Platform::InputState input;
    REQUIRE(input.consume({0, 0, Dot::Platform::KeyChanged{Dot::Platform::Key::D, true, false}}));
    return input;
  }
}  // namespace

TEST_CASE("game_session_orders_input_scripts_real_physics_audio_and_explicit_shutdown") {
  Fixture fixture(false, 1);
  fixture.controller();
  GameSessionConfig config;
  config.physics.simulation.gravity = {};
  auto session = fixture.session(config);
  CHECK(session.stats()->state == SessionState::Ready);
  CHECK_FALSE(session.fixedUpdate());
  good(session.start());
  CHECK(actor(session).name == "start1");
  REQUIRE(
      fixture.project.files()->writeTextAtomic("scripts/mover.ts", "export const bad:number='x';"));
  REQUIRE(session.publish(held()));
  good(session.fixedUpdate());
  CHECK(actor(session).name == "fixed1");
  CHECK(actor(session).transform.position.x == doctest::Approx(2));
  good(session.update(0));
  CHECK(actor(session).name == "update1play");
  std::vector<float> samples(128);
  REQUIRE(session.renderAudio(samples));
  CHECK(std::ranges::any_of(samples, [](float sample) { return sample > 0.1F; }));
  good(session.start());
  CHECK(actor(session).name == "update1play");
  auto scene = session.scene().value();
  good(session.close());
  CHECK(actor(session).name == "stopped");
  CHECK(session.stats()->state == SessionState::Closed);
  CHECK_FALSE(session.stats()->scripts.has_value());
  good(session.close());
  CHECK_FALSE(session.fixedUpdate());
  CHECK_FALSE(session.update(0));
  CHECK_FALSE(session.publish(held()));
  CHECK_FALSE(session.renderAudio(samples));
  CHECK(scene->entityCount().value() == 2);
}

TEST_CASE("game_session_single_step_pairs_native_fixed_update_animation_without_pause_debt") {
  using Json = nlohmann::json;

  for (const bool live : {false, true}) {
    Fixture fixture(live, 30);
    fixture.controller();
    const auto files = fixture.project.files().value();
    REQUIRE(files.writeTextAtomic("scripts/mover.ts", R"ts(
import {ScriptComponent, gameClock, input} from 'dot:engine';
export class Mover extends ScriptComponent {
  #frames = 0; #fixed = 0; #updates = 0; #fixedDt = 0; #updateDt = 0;
  #wall = 0; #order = ''; #edge = '';
  private publish(): void {
    this.self.name = JSON.stringify({frames: this.#frames, fixed: this.#fixed,
      updates: this.#updates, fixedDt: this.#fixedDt, updateDt: this.#updateDt,
      wall: this.#wall, order: this.#order, paused: gameClock.snapshot().paused});
  }
  onStart(): void { gameClock.setPaused(true); this.publish(); }
  onFrame(seconds: number): void {
    ++this.#frames; this.#wall = seconds;
    if (input.index !== this.#edge && input.key('N').pressed) {
      this.#edge = input.index; gameClock.step();
    }
    this.publish();
  }
  onFixedUpdate(seconds: number): void {
    ++this.#fixed; this.#fixedDt = seconds; this.#order += 'F';
    this.self.setVelocity({x:3,y:0,z:0});
  }
  onUpdate(seconds: number): void {
    ++this.#updates; this.#updateDt = seconds; this.#order += 'U'; this.publish();
  }
}
)ts"));
    auto scene = fixture.project.loadStartup().value();
    const auto animated = scene.createEntity("Animated").value();
    auto clip = Dot::Animation::Clip::create({"Move",
                                              {{0,
                                                Dot::Animation::Path::Translation,
                                                Dot::Animation::Interpolation::Linear,
                                                {{0, {0, 0, 0, 0}}, {2, {2, 0, 0, 0}}}}}})
                    .value();
    REQUIRE(Dot::Animation::saveClip(files, "assets/step.dotanimation", clip));
    REQUIRE(scene.setAnimationPlayer(
        animated,
        Dot::World::AnimationPlayer{{"assets/step.dotanimation"}, {{0, animated.value}}}));
    REQUIRE(fixture.project.saveStartup(scene));
    GameSessionConfig config;
    config.physics.simulation.gravity = {};
    auto session = fixture.session(config);
    good(session.start());
    auto clock = session.gameClock().value();
    const auto initial = actor(session).transform.position;
    good(session.fixedUpdate());
    good(session.update(.25));
    CHECK(actor(session).transform.position == initial);
    REQUIRE(clock.perform(R"({"command":"step"})"));
    CHECK_FALSE(session.update(std::numeric_limits<double>::quiet_NaN()));
    CHECK(clock.snapshot()->step->state == Dot::Script::GameStepState::Pending);
    good(session.fixedUpdate());
    CHECK(session.stats()->simulatedFixedSteps == 0);
    good(session.update(.8));
    auto observed = Json::parse(actor(session).name);
    CHECK(observed["frames"] == 2);
    CHECK(observed["order"] == "FU");
    CHECK(observed["wall"] == .8);
    CHECK(observed["fixedDt"].get<double>() == doctest::Approx(1.0 / 30));
    CHECK(observed["updateDt"] == observed["fixedDt"]);
    CHECK(observed["paused"] == true);
    CHECK(actor(session).transform.position.x == doctest::Approx(initial.x + .1F));
    CHECK(session.stats()->simulatedFixedSteps == 1);
    CHECK(session.stats()->simulatedUpdates == 1);
    CHECK(session.stats()->lifetimeFixedSteps == 1);
    CHECK(clock.snapshot()->step->state == Dot::Script::GameStepState::Completed);
    const auto position = actor(session).transform.position;
    const auto target = session.scene().value()->findEntity(animated.value).value();
    CHECK(session.scene().value()->transform(target)->position.x == doctest::Approx(1.0 / 30));
    good(session.update(.8));
    CHECK(actor(session).transform.position == position);
    CHECK(session.stats()->simulatedUpdates == 1);
    Dot::Platform::InputState input;
    REQUIRE(input.consume({0, 0, Dot::Platform::KeyChanged{Dot::Platform::Key::N, true, false}}));
    REQUIRE(session.publish(input));
    good(session.update(0));
    CHECK(Json::parse(actor(session).name)["order"] == "FUFU");
    CHECK(clock.snapshot()->step->id == 2);
    CHECK(session.stats()->simulatedFixedSteps == 2);
    CHECK(session.stats()->simulatedUpdates == 2);
    CHECK(session.stats()->lifetimeFixedSteps == 2);
    REQUIRE(clock.perform(R"({"command":"step"})"));
    REQUIRE(clock.perform(R"({"command":"set-paused","paused":false})"));
    good(session.update(0));
    CHECK(clock.snapshot()->step->state == Dot::Script::GameStepState::Cancelled);
    CHECK(session.stats()->simulatedFixedSteps == 2);
    CHECK(session.stats()->simulatedUpdates == 3);
    good(session.close());
  }
}

TEST_CASE("game_session_pointer_request_and_native_ack_do_not_require_a_window") {
  Fixture fixture(false);
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/mover.ts", R"ts(
import {ScriptComponent, input} from 'dot:engine';
export class Mover extends ScriptComponent {
  onStart(): void { input.capturePointer(true); }
  onUpdate(): void { this.self.name = input.pointerCaptured ? 'captured' : 'released'; }
  onStop(): void { input.capturePointer(false); }
}
)ts"));
  GameSessionConfig config;
  config.audioMode = SessionAudio::Offline;
  auto session = fixture.session(config);
  CHECK_FALSE(session.pointerCaptureRequested().value());
  good(session.start());
  CHECK(session.pointerCaptureRequested().value());
  good(session.update(0));
  CHECK(actor(session).name == "released");
  REQUIRE(session.publish(held(), true));
  good(session.update(0));
  CHECK(actor(session).name == "captured");
  REQUIRE(session.publish(held()));
  good(session.update(0));
  CHECK(actor(session).name == "released");
  auto moved = std::move(session);
  CHECK_FALSE(session.pointerCaptureRequested());
  bool rejected{};
  std::thread worker([&] { rejected = !moved.pointerCaptureRequested(); });
  worker.join();
  CHECK(rejected);
  good(moved.close());
  CHECK_FALSE(moved.pointerCaptureRequested());
}

TEST_CASE("game_session_start_audio_pose_baseline_preserves_velocity_on_repeated_start") {
  Fixture fixture(false);
  auto scene = fixture.project.loadStartup().value();
  const auto camera = scene.entities()->front().id;
  REQUIRE(scene.setAudioListener(camera, Dot::World::AudioListener{true}));
  REQUIRE(fixture.project.saveStartup(scene));
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/mover.ts", R"ts(
import {audio, scene, ScriptComponent} from 'dot:engine';
export class Mover extends ScriptComponent {
  onStart(): void {
    const camera = scene.primaryCamera!;
    const pose = camera.transform;
    pose.position.x = 1000;
    camera.transform = pose;
  }
  onUpdate(): void {
    this.self.name = audio.listener().velocity.x > 0.5 ? 'moving' : 'stationary';
    const camera = scene.primaryCamera!;
    const pose = camera.transform;
    pose.position.x += 1 / 60;
    camera.transform = pose;
  }
}
)ts"));
  auto session = fixture.session();
  good(session.start());
  good(session.update(1.0 / 60));
  CHECK(actor(session).name == "stationary");
  good(session.start());
  good(session.update(1.0 / 60));
  CHECK(actor(session).name == "moving");
  good(session.close());
}

TEST_CASE("game_session_audio_velocity_uses_elapsed_fixed_steps_before_short_frame") {
  Fixture fixture(false);
  auto scene = fixture.project.loadStartup().value();
  const auto camera = scene.entities()->front().id;
  REQUIRE(scene.setAudioListener(camera, Dot::World::AudioListener{true}));
  REQUIRE(fixture.project.saveStartup(scene));
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/mover.ts", R"ts(
import {audio, scene, ScriptComponent} from 'dot:engine';
export class Mover extends ScriptComponent {
  onFixedUpdate(): void {
    const camera = scene.primaryCamera!;
    const pose = camera.transform;
    pose.position.x += 0.05;
    camera.transform = pose;
  }
  onUpdate(): void {
    this.self.name = audio.listener().velocity.x > 2 ? 'moving' : 'stationary';
  }
}
)ts"));
  auto session = fixture.session();
  good(session.start());
  good(session.fixedUpdate());
  good(session.fixedUpdate());
  good(session.update(0.00001));
  CHECK(session.stats()->state == SessionState::Running);
  good(session.update(0));
  CHECK(actor(session).name == "moving");
  good(session.close());
}

TEST_CASE("game_session_audio_velocity_carries_variable_yaw_until_physics_publishes_it") {
  Fixture fixture(false);
  auto scene = fixture.project.loadStartup().value();
  const auto camera = scene.entities()->front().id;
  const auto entities = scene.entities().value();
  const auto cube = std::ranges::find(entities, "Cube", &Dot::World::EntityInfo::name);
  REQUIRE(cube != entities.end());
  REQUIRE(scene.setCharacterController(cube->id, Dot::World::CharacterController{}));
  REQUIRE(scene.setParent(camera, cube->id));
  REQUIRE(scene.setTransform(camera, {{0.55F, 2.15F, 3.4F}}));
  REQUIRE(scene.setAudioListener(camera, Dot::World::AudioListener{true}));
  REQUIRE(fixture.project.saveStartup(scene));
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/mover.ts", R"ts(
import {audio, ScriptComponent} from 'dot:engine';
export class Mover extends ScriptComponent {
  #yaw = 0;
  onUpdate(seconds: number): void {
    const velocity = audio.listener().velocity;
    this.self.name = Math.hypot(velocity.x, velocity.z) > 1 ? 'moving' : 'stationary';
    this.#yaw += 90 * seconds;
    this.self.setCharacterYaw(this.#yaw);
  }
}
)ts"));
  GameSessionConfig config;
  config.physics.simulation.gravity = {};
  auto session = fixture.session(config);
  good(session.start());
  good(session.update(1));
  good(session.fixedUpdate());
  good(session.update(1.0 / 60));
  CHECK(session.stats()->state == SessionState::Running);
  good(session.update(0));
  CHECK(actor(session).name == "moving");
  good(session.close());
}

TEST_CASE("game_session_hot_reload_migrates_scripts_and_retains_native_motion_audio") {
  Fixture fixture;
  fixture.controller();
  GameSessionConfig config;
  config.pollInterval = std::chrono::milliseconds{0};
  config.physics.simulation.gravity = {};
  auto session = fixture.session(config);
  good(session.start());
  REQUIRE(session.publish(held()));
  good(session.fixedUpdate());
  fixture.writeController(4);
  auto completed = Dot::Tests::awaitCompilation([&] { return session.pollScripts(); });
  good(session.fixedUpdate());
  CHECK(completed->committed);
  CHECK(completed->reload->migrated == 1);
  CHECK(actor(session).name == "fixed2");
  CHECK(actor(session).transform.position.x == doctest::Approx(6.0 / 60));
  REQUIRE(
      fixture.project.files()->writeTextAtomic("scripts/mover.ts", "export const bad:number='x';"));
  Dot::Tests::awaitCompilation([&] { return session.pollScripts(); });
  auto failed = good(session.fixedUpdate());
  CHECK(failed.source.error.has_value());
  CHECK(actor(session).name == "fixed3");
  CHECK(actor(session).transform.position.x == doctest::Approx(10.0 / 60));
  good(session.update(0));
  CHECK(actor(session).name == "update3play");
  CHECK(session.stats()->state == SessionState::Running);
  good(session.close());
}

TEST_CASE("game_session_fixed_rate_boundaries_and_invalid_deltas_have_no_effects") {
  for (auto hz : {1U, 240U}) {
    Fixture fixture(false, hz);
    fixture.controller();
    GameSessionConfig config;
    config.physics.simulation.gravity = {};
    auto session = fixture.session(config);
    good(session.start());
    REQUIRE(session.publish(held()));

    for (auto seconds : {-1., 1.000001, std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()}) {
      CHECK_FALSE(session.update(seconds));
      CHECK(actor(session).name == "start1");
      CHECK(session.stats()->state == SessionState::Running);
    }

    good(session.fixedUpdate());
    CHECK(actor(session).transform.position.x == doctest::Approx(2.0 / hz));
    good(session.update(1));
    good(session.close());
  }
}

TEST_CASE("game_session_resource_ownership_survives_moves_and_releases_on_close") {
  auto generations = Dot::Script::compilerGenerations();
  auto vm = Dot::Script::Runtime::backendMemory();
  auto audio = Dot::Audio::Mixer::backendMemory();
  Fixture fixture;
  fixture.controller();

  for (int index = 0; index < 3; ++index) {
    auto original = fixture.session();
    auto session = std::move(original);
    CHECK_FALSE(original.start());
    CHECK_FALSE(original.close());
    CHECK_FALSE(original.scene());
    bool rejected = false;
    std::thread worker(
        [&] { rejected = !session.start() && !session.close() && !session.scene(); });
    worker.join();
    CHECK(rejected);
    good(session.start());
    auto retained = session.scene().value();
    good(session.close());
    CHECK(Dot::Script::compilerGenerations() == generations);
    CHECK(Dot::Script::Runtime::backendMemory().bytes == vm.bytes);
    CHECK(Dot::Audio::Mixer::backendMemory().bytes == audio.bytes);
    CHECK(retained->entityCount().value() == 2);
  }
}

TEST_CASE("game_session_failed_creation_releases_partial_native_and_compiler_resources") {
  Fixture fixture;
  auto generations = Dot::Script::compilerGenerations();
  auto vm = Dot::Script::Runtime::backendMemory();
  auto audio = Dot::Audio::Mixer::backendMemory();
  GameSessionConfig config;
  config.input.textBytes = 0;
  CHECK_FALSE(GameSession::create(fixture.project, fixture.compiler(), config));
  config = {};
  config.audioMode = static_cast<SessionAudio>(99);
  CHECK_FALSE(GameSession::create(fixture.project, fixture.compiler(), config));
  config = {};
  config.pollInterval = std::chrono::milliseconds{-1};
  CHECK_FALSE(GameSession::create(fixture.project, fixture.compiler(), config));
  REQUIRE(
      fixture.project.files()->writeTextAtomic("scripts/mover.ts", "export const bad:number='x';"));
  CHECK_FALSE(GameSession::create(fixture.project, fixture.compiler()));
  CHECK(Dot::Script::compilerGenerations() == generations);
  CHECK(Dot::Script::Runtime::backendMemory().bytes == vm.bytes);
  CHECK(Dot::Audio::Mixer::backendMemory().bytes == audio.bytes);
}

TEST_CASE("game_session_native_failure_is_terminal_and_close_still_releases_services") {
  Fixture fixture(false);
  auto session = fixture.session();
  good(session.start());
  auto scene = session.scene().value();
  auto entity = actor(session).id;
  Dot::World::AudioSource source;
  source.asset = "assets/missing.wav";
  REQUIRE(scene->setAudioSource(entity, source));
  CHECK_FALSE(session.update(0));
  CHECK(session.stats()->state == SessionState::Faulted);
  REQUIRE(scene->setAudioSource(entity, {}));
  CHECK_FALSE(session.update(0));
  CHECK_FALSE(session.start());
  good(session.close());
  CHECK(session.stats()->state == SessionState::Closed);
}

TEST_CASE("game_session_moved_scene_can_be_closed_and_destructor_never_runs_hooks") {
  Fixture fixture;
  fixture.controller();
  std::shared_ptr<Dot::World::Scene> retained;

  {
    auto session = fixture.session();
    good(session.start());
    retained = session.scene().value();
  }

  auto entities = retained->entities().value();
  CHECK(std::ranges::any_of(entities, [](const auto& value) { return value.name == "start1"; }));
  auto session = fixture.session();
  retained = session.scene().value();
  auto moved = std::move(*retained);
  CHECK_FALSE(session.start());
  CHECK_FALSE(session.scene());
  CHECK_FALSE(session.close());
  CHECK(session.stats()->state == SessionState::Closed);
  good(session.close());
  CHECK(moved.entityCount().value() == 2);
}

TEST_CASE("game_session_ordinary_script_failure_is_reported_and_isolated") {
  Fixture fixture(false);
  REQUIRE(fixture.project.files()->writeTextAtomic(
      "scripts/mover.ts",
      "import {ScriptComponent} from 'dot:engine';export class Mover extends ScriptComponent{"
      "onUpdate(){throw new Error('controller failed');}onStop(){this.self.name='isolated';}}"));
  auto session = fixture.session();
  good(session.start());
  auto report = session.update(0);
  REQUIRE(report);
  REQUIRE(report->frame.failures.size() == 1);
  CHECK(report->frame.failures[0].error.message.find("controller failed") != std::string::npos);
  CHECK_FALSE(report->frame.realmFaulted);
  CHECK(actor(session).name == "isolated");
  CHECK(session.stats()->state == SessionState::Running);
  good(session.update(0));
  good(session.close());
}

TEST_CASE("game_session_realm_timeout_faults_dispatch_but_close_releases_resources") {
  for (const auto* hook : {"onUpdate", "onFrame"}) {
    for (const bool enabled : {false, true}) {
      Fixture fixture(enabled);
      REQUIRE(fixture.project.files()->writeTextAtomic(
          "scripts/mover.ts",
          "import {ScriptComponent} from 'dot:engine';export class Mover extends ScriptComponent{"
              + std::string(hook) + "(){while(true){}}}"));
      GameSessionConfig config;
      config.scripts.runtime.milliseconds = 10;
      auto session = fixture.session(config);
      good(session.start());
      auto clock = session.gameClock().value();

      if (std::string_view(hook) == "onFrame") {
        REQUIRE(clock.perform(R"({"command":"set-paused","paused":true})"));
      }

      const auto resources = Dot::Core::processResources();
      const auto generations = Dot::Script::compilerGenerations();

      if (enabled) {
        CHECK(session.pollScripts(true)->compiling);
      }

      auto report = session.update(0);
      REQUIRE(report);
      CHECK(report->frame.realmFaulted);
      CHECK(session.stats()->state == SessionState::Faulted);
      CHECK_FALSE(session.pollScripts());
      const auto source = session.stats()->source;
      CHECK_FALSE((source && source->compiling));
      CHECK(Dot::Core::processResources().children == resources.children);
      CHECK(Dot::Core::processResources().handles == resources.handles);
      CHECK(Dot::Script::compilerGenerations() == generations);
      CHECK_FALSE(clock.perform(R"({"command":"set-paused","paused":false})"));
      CHECK(session.stats()->simulatedUpdates == 0);
      CHECK_FALSE(session.fixedUpdate());
      auto closed = session.close();
      REQUIRE(closed);
      CHECK(closed->realmFaulted);
      CHECK(session.stats()->state == SessionState::Closed);
    }
  }
}

TEST_CASE("game_session_audio_velocity_uses_all_fixed_steps_once_per_frame") {
  Fixture fixture(false);
  fixture.controller();
  auto scene = fixture.project.loadStartup().value();

  for (auto entity : scene.entities().value()) {
    if (entity.camera) {
      REQUIRE(scene.setAudioListener(entity.id, Dot::World::AudioListener{true}));
    }

    if (entity.audioSource) {
      entity.audioSource->spatial.enabled = true;
      entity.audioSource->spatial.dopplerFactor = 1;
      REQUIRE(scene.setAudioSource(entity.id, entity.audioSource));
    }
  }

  REQUIRE(fixture.project.saveStartup(scene));
  REQUIRE(fixture.project.files()->writeTextAtomic(
      "scripts/mover.ts",
      "import {ScriptComponent} from 'dot:engine';export class Mover extends ScriptComponent{"
      "onFixedUpdate(){this.self.setVelocity({x:2,y:0,z:0});}"
      "onUpdate(){this.self.name='velocity'+this.self.audioVoice.spatial.velocity.x.toFixed(3);}"
      "}"));
  GameSessionConfig config;
  config.physics.simulation.gravity = {};
  auto session = fixture.session(config);
  good(session.start());
  good(session.fixedUpdate());
  good(session.fixedUpdate());
  good(session.update(2.0 / 60));
  good(session.update(0));
  CHECK(actor(session).name == "velocity2.000");
  good(session.update(0));
  CHECK(actor(session).name == "velocity0.000");
  good(session.close());
}

TEST_CASE("game_session_unsaved_scene_clone_is_independent_and_never_persists") {
  Fixture fixture(false);
  auto authoring = fixture.project.loadStartup().value();
  const auto id = authoring.entities()->at(1).id;
  auto transform = authoring.entity(id)->transform;
  transform.position.x = 4;
  REQUIRE(authoring.setTransform(id, transform));
  const auto original = Dot::World::encodeScene(authoring).value();
  auto prepared = GameSession::createFromScene(fixture.project, fixture.compiler(), authoring);
  REQUIRE_MESSAGE(prepared, (prepared ? "" : prepared.error().message));
  auto session = std::move(*prepared);
  REQUIRE(session.scene()->get()->id() != authoring.id());
  CHECK(actor(session).transform.position.x == 4);
  good(session.start());
  REQUIRE(session.publish(held()));
  good(session.update(0.5));
  CHECK(actor(session).transform.position.x > 4);
  CHECK(Dot::World::encodeScene(authoring).value() == original);
  CHECK(fixture.project.loadStartup()->entities()->at(1).transform.position.x == 0);
  good(session.close());
  CHECK(Dot::World::encodeScene(authoring).value() == original);
}

TEST_CASE("game_session_clone_survives_source_replacement_and_factory_failure_cleans_up") {
  Fixture fixture;
  auto authoring = fixture.project.loadStartup().value();
  const auto generations = Dot::Script::compilerGenerations();
  {
    auto session
        = GameSession::createFromScene(fixture.project, fixture.compiler(), authoring).value();
    authoring = Dot::World::Scene::create("Replacement").value();
    good(session.start());
    good(session.update(0.1));
    CHECK(session.scene()->get()->entityCount().value() == 2);
    good(session.close());
  }
  CHECK(Dot::Script::compilerGenerations() == generations);
  auto original = fixture.project.loadStartup().value();
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/mover.ts",
                                                   "export const x: number = 'bad';"));
  CHECK_FALSE(GameSession::createFromScene(fixture.project, fixture.compiler(), original));
  CHECK(Dot::Script::compilerGenerations() == generations);
  GameSessionConfig invalid;
  invalid.pollInterval = std::chrono::milliseconds{-1};
  CHECK_FALSE(GameSession::createFromScene(fixture.project, fixture.compiler(), original, invalid));
  CHECK(Dot::Script::compilerGenerations() == generations);
}

TEST_CASE("game_session_precompiled_pipeline_needs_no_typescript_or_compiler") {
  Fixture fixture(false);
  fixture.controller();
  auto scene = fixture.project.loadStartup().value();
  auto compiled = fixture.compiler().compileProject(fixture.project.files().value()).value();
  auto published = compiled.publish(fixture.directory.files(), "published").value();
  auto program = Dot::Script::CompiledProgram::loadPublished(published).value();
  std::filesystem::remove_all(fixture.project.files()->root() / "scripts");
  GameSessionConfig config;
  config.physics.simulation.gravity = {};
  auto session
      = GameSession::createCompiled(fixture.project.files().value(),
                                    fixture.project.description().value(), scene, program, config)
            .value();
  CHECK_FALSE(session.stats()->hotReload);
  good(session.start());
  REQUIRE(session.publish(held()));
  good(session.fixedUpdate());
  good(session.update(0.1));
  CHECK(actor(session).transform.position.x > 0);
  CHECK(actor(session).name == "update1play");
  std::vector<float> pcm(128);
  REQUIRE(session.renderAudio(pcm));
  CHECK(std::ranges::any_of(pcm, [](float value) { return value > 0.1F; }));
  good(session.close());
  CHECK(actor(session).name == "stopped");
}

TEST_CASE("game_session_precompiled_validation_and_failed_preparation_release_resources") {
  Fixture fixture;
  auto scene = fixture.project.loadStartup().value();
  auto program = fixture.compiler().compileProject(fixture.project.files().value()).value();
  auto description = fixture.project.description().value();
  CHECK_FALSE(
      GameSession::createCompiled(fixture.project.files().value(), description, scene, program));
  description.hotReload = false;
  auto original = Dot::World::encodeScene(scene).value();
  auto id = scene.entities()->at(1).id;
  REQUIRE(scene.setScript(id, Dot::World::ScriptComponent{"missing.ts", "Missing"}));
  const auto memory = Dot::Script::Runtime::backendMemory();
  const auto mixer = Dot::Audio::Mixer::backendMemory();
  CHECK_FALSE(
      GameSession::createCompiled(fixture.project.files().value(), description, scene, program));
  CHECK(Dot::Script::Runtime::backendMemory().bytes == memory.bytes);
  CHECK(Dot::Audio::Mixer::backendMemory().bytes == mixer.bytes);
  auto moved = std::move(program);
  scene = Dot::World::decodeScene(original).value();
  CHECK_FALSE(
      GameSession::createCompiled(fixture.project.files().value(), description, scene, program));
  auto session
      = GameSession::createCompiled(fixture.project.files().value(), description, scene, moved)
            .value();
  good(session.start());
  good(session.close());
}

TEST_CASE("game_state.shared_native_service_survives_scene_realms_and_save_reopen") {
  Fixture fixture(false);
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/mover.ts", R"ts(
import {scene, ScriptComponent} from 'dot:engine';
import {invoke} from 'dot:host';
export class Mover extends ScriptComponent {
  onStart(): void {
    const visits = (invoke('progress.get', ['visits']) as number|undefined) ?? 0;
    invoke('progress.set', ['visits', visits + 1]);
    this.self.name = 'visit-' + (visits + 1);
  }

  onStop(): void {
    invoke('progress.set', ['departed', scene.info().name]);
  }
}
)ts"));
  auto target = fixture.project.loadStartup().value();
  REQUIRE(target.rename("Destination"));
  REQUIRE(Dot::World::saveScene(fixture.project.files().value(), "scenes/next.dotscene", target));
  auto data = Dot::Game::State::create().value();
  GameSessionConfig config;
  config.scripts.services = progressBindings(data);
  auto session = fixture.session(config);
  good(session.start());
  CHECK(actor(session).name == "visit-1");
  auto first = session.scene().value();
  auto request = session.requestScene("scenes/next.dotscene").value();
  good(session.update(0));
  good(Dot::Tests::awaitScene(session, request));
  CHECK(session.sceneRequest(request)->committed);
  CHECK(session.scene().value()->id() != first->id());
  CHECK(actor(session).name == "visit-2");
  CHECK(data.get("visits").value().value() == "2");
  good(session.close());
  CHECK(data.get("departed").value().value() == R"("Destination")");
  auto saves = Dot::Game::Saves::open(fixture.directory.files()).value();
  REQUIRE(saves.write("checkpoint", data.snapshot().value(), false));
  auto loaded = saves.read("checkpoint").value();
  auto restored = Dot::Game::State::create(loaded.data).value();
  CHECK(restored.snapshot()->revision == 1);
  config.scripts.services = progressBindings(restored);
  auto restarted = fixture.session(config);
  good(restarted.start());
  CHECK(actor(restarted).name == "visit-3");
  CHECK(data.get("visits").value().value() == "2");
  good(restarted.close());
}

TEST_CASE("game_state.readonly_preparation_cannot_publish_into_shared_progress") {
  Fixture fixture(false);
  auto data = Dot::Game::State::create(R"({"progress":7})").value();
  GameSessionConfig config;
  config.scripts.services = progressBindings(data);
  auto original = fixture.session(config);
  good(original.start());
  auto old = original.scene().value();
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/mover.ts", R"ts(
import {ScriptComponent} from 'dot:engine';
import {invoke} from 'dot:host';
export class Mover extends ScriptComponent {
  attempted = invoke('progress.set', ['progress', 0]);
}
)ts"));
  auto before = data.snapshot().value();
  auto rejected = GameSession::create(fixture.project, fixture.compiler(), config);
  REQUIRE_FALSE(rejected);
  CHECK(data.snapshot()->data == before.data);
  CHECK(data.snapshot()->revision == before.revision);
  good(original.update(0));
  CHECK(original.scene().value().get() == old.get());
  good(original.close());
}

TEST_CASE("game_data.builtin_sdk_persists_across_scene_changes_close_and_compiled_restart") {
  Fixture fixture(false);
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/mover.ts", R"ts(
import {gameState, saves, type SaveTicket, ScriptComponent} from 'dot:engine';
export class Mover extends ScriptComponent {
  prepared = gameState.snapshot();
  #pending: SaveTicket|null = null;

  private visit(): void {
    const visits = gameState.get('visits') ?? 0;

    if (typeof visits !== 'number') {
      throw new Error('Invalid visits');
    }

    gameState.set('visits', visits + 1);
    this.self.name = 'visit-' + (visits + 1);
    this.#pending = null;
  }

  onStart(): void {
    if (gameState.get('visits') === undefined) {
      this.#pending = saves.list();
    } else {
      this.visit();
    }
  }

  onUpdate(): void {
    if (this.#pending === null) {
      return;
    }

    const ticket = this.#pending;
    const receipt = saves.take(ticket);

    if (receipt.state === 'pending' || receipt.state === 'running') {
      return;
    }

    if (receipt.state !== 'succeeded') {
      throw Error(receipt.error?.message ?? 'Save job cancelled');
    }

    if (ticket.operation === 'list' && (receipt.result as {name: string}[]).some(slot => slot.name === 'checkpoint')) {
      this.#pending = saves.load('checkpoint', gameState.snapshot().revision);
    } else {
      this.visit();
    }
  }

  onStop(): void {
    saves.write('checkpoint', true);
  }
}
)ts"));
  auto target = fixture.project.loadStartup().value();
  REQUIRE(target.rename("Destination"));
  REQUIRE(Dot::World::saveScene(fixture.project.files().value(), "scenes/next.dotscene", target));
  GameSessionConfig config;
  config.progress.saves = fixture.directory.files();
  auto session = fixture.session(config);
  good(session.start());
  const auto awaitVisit = [&](auto& current, const std::string& name) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while (actor(current).name != name) {
      REQUIRE(std::chrono::steady_clock::now() < deadline);
      good(current.update(0));
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  awaitVisit(session, "visit-1");
  auto handle = session.gameData().value();
  CHECK(actor(session).name == "visit-1");
  auto request = session.requestScene("scenes/next.dotscene").value();
  good(session.update(0));
  good(Dot::Tests::awaitScene(session, request));
  REQUIRE(session.sceneRequest(request)->committed);
  CHECK(actor(session).name == "visit-2");
  good(session.close());
  CHECK_FALSE(handle.perform(R"({"command":"set","key":"visits","value":0})"));
  CHECK_FALSE(handle.perform(R"({"command":"list-saves"})"));
  CHECK(nlohmann::json::parse(handle.perform(R"({"command":"snapshot"})").value())["data"]["visits"]
        == 2);
  auto program
      = fixture.compiler().compileProject(fixture.project.files().value(), "scripts").value();
  auto description = fixture.project.description().value();
  auto scene = fixture.project.loadStartup().value();
  auto restarted = GameSession::createCompiled(fixture.project.files().value(), description, scene,
                                               program, config)
                       .value();
  good(restarted.start());
  awaitVisit(restarted, "visit-3");
  CHECK(actor(restarted).name == "visit-3");
  good(restarted.close());
}

TEST_CASE("game_data.builtin_sdk_rejects_preparation_io_and_writes_without_damaging_active_state") {
  for (const auto* attempt : {"gameState.set('score',0)", "saves.list()"}) {
    Fixture fixture(false);
    REQUIRE(fixture.project.files()->writeTextAtomic(
        "scripts/mover.ts",
        std::string("import {gameState,saves,scenes,ScriptComponent} from 'dot:engine';")
            + "export class Mover extends ScriptComponent { attempt="
              "scenes.current().path==='scenes/rejected.dotscene'?"
            + attempt + ":gameState.snapshot();}"));
    auto target = fixture.project.loadStartup().value();
    REQUIRE(
        Dot::World::saveScene(fixture.project.files().value(), "scenes/rejected.dotscene", target));
    GameSessionConfig config;
    config.progress.initial = R"({"score":7})";
    config.progress.saves = fixture.directory.files();
    auto session = fixture.session(config);
    good(session.start());
    auto old = session.scene().value();
    auto handle = session.gameData().value();
    REQUIRE(handle.perform(R"({"command":"set","key":"score","value":8})"));
    const auto before = handle.perform(R"({"command":"snapshot"})").value();
    auto request = session.requestScene("scenes/rejected.dotscene").value();
    good(session.update(0));
    good(Dot::Tests::awaitScene(session, request));
    CHECK(session.sceneRequest(request)->state == Dot::Script::SceneRequestState::Failed);
    CHECK_FALSE(session.sceneRequest(request)->committed);
    CHECK(session.scene().value().get() == old.get());
    CHECK(handle.perform(R"({"command":"snapshot"})").value() == before);
    good(session.update(0));
    good(session.close());
  }
}

TEST_CASE("game_session_source_poll_retry_retains_gameplay_and_rejects_inactive_modes") {
  for (const bool enabled : {false, true}) {
    Fixture fixture(enabled);
    fixture.controller();
    auto session = fixture.session();
    CHECK_FALSE(session.pollScripts(true));
    good(session.start());
    const auto before = actor(session);
    CHECK(session.stats()->source.has_value() == enabled);

    if (enabled) {
      auto scheduled = session.pollScripts(true).value();
      CHECK(scheduled.compiling);
      CHECK_FALSE(scheduled.committed);
      auto completed = Dot::Tests::awaitCompilation([&] { return session.pollScripts(); });
      CHECK(completed->committed);
      CHECK(completed->reload->migrated == 1);
      CHECK(session.stats()->source->compileAttempts == 2);
      CHECK(session.stats()->source->reloads == 1);
      CHECK_FALSE(session.stats()->source->compiling);
    } else {
      CHECK_FALSE(session.pollScripts(true));
    }

    CHECK(actor(session).id == before.id);
    CHECK(actor(session).name == before.name);
    CHECK(actor(session).transform.position == before.transform.position);
    CHECK(actor(session).transform.rotation == before.transform.rotation);
    CHECK(actor(session).transform.scale == before.transform.scale);
    CHECK(session.stats()->state == SessionState::Running);
    good(session.close());
    CHECK_FALSE(session.pollScripts(true));
  }
}

TEST_CASE("game_session_prepared_source_is_verified_and_never_recompiled_on_construction") {
  const auto generations = Dot::Script::compilerGenerations();

  for (const bool reload : {false, true}) {
    Fixture fixture(reload);
    fixture.controller();
    auto authoring = fixture.project.loadStartup().value();
    auto program = fixture.compiler().compileProject(fixture.project.files().value()).value();
    const auto toolPath
        = fixture.directory.files().root() / std::filesystem::path(DOT_COMPILER_FIXTURE).filename();
    REQUIRE(std::filesystem::copy_file(DOT_COMPILER_FIXTURE, toolPath));
    auto tool = Dot::Script::TypeScriptCompiler::create(toolPath).value();
    REQUIRE(std::filesystem::remove(toolPath));
    auto session = GameSession::createFromScene(fixture.project, tool, authoring, {}, std::nullopt,
                                                nullptr, &program);
    REQUIRE_MESSAGE(session, (session ? "" : session.error().message));
    good(session->start());
    good(session->update(0));
    CHECK(session->stats()->hotReload == reload);
    fixture.writeController(5);
    auto stale = GameSession::createFromScene(fixture.project, tool, authoring, {}, std::nullopt,
                                              nullptr, &program);
    REQUIRE_FALSE(stale);
    CHECK(stale.error().code == Dot::Core::ErrorCode::Unavailable);
    auto published = program.publish(fixture.project.files().value(), "published").value();
    auto imported = Dot::Script::CompiledProgram::loadPublished(published).value();
    CHECK_FALSE(GameSession::createFromScene(fixture.project, tool, authoring, {}, std::nullopt,
                                             nullptr, &imported));
    good(session->close());
  }

  CHECK(Dot::Script::compilerGenerations() == generations);
}

TEST_CASE("game_session_prepared_live_generation_does_not_pin_old_scene_transition_source") {
  Fixture fixture;
  fixture.controller(2);
  auto authoring = fixture.project.loadStartup().value();
  auto compiler = fixture.compiler();
  auto program = compiler.compileProject(fixture.project.files().value()).value();
  auto session = GameSession::createFromScene(fixture.project, compiler, authoring, {},
                                              std::nullopt, nullptr, &program)
                     .value();
  good(session.start());
  fixture.writeController(5);
  auto reload = Dot::Tests::awaitCompilation([&] { return session.pollScripts(true); });
  REQUIRE(reload->committed);
  const auto oldScope = session.scene().value()->id();
  auto request = session.requestScene("scenes/main.dotscene").value();
  good(session.update(0));
  good(Dot::Tests::awaitScene(session, request));
  auto result = session.sceneRequest(request).value();
  CHECK_MESSAGE(result.state == Dot::Script::SceneRequestState::Succeeded,
                (result.error ? result.error->message : "Scene replacement did not complete"));
  CHECK(session.scene().value()->id() != oldScope);
  good(session.close());
}

TEST_CASE("game_session_initial_resource_proof_owns_native_data_and_rejects_mismatched_inputs") {
  using namespace Dot;
  Fixture fixture;
  fixture.controller();
  auto files = fixture.project.files().value();
  auto authoring = fixture.project.loadStartup().value();
  const auto entities = authoring.entities().value();
  const auto cube = std::ranges::find(entities, "Cube", &World::EntityInfo::name);
  REQUIRE(cube != entities.end());
  REQUIRE(files.createDirectories("assets"));
  REQUIRE(Assets::saveMesh(files, "assets/initial.dotmesh", Graphics::cube().value()));
  auto body = *cube->rigidBody;
  body.shape = Physics::Shape::Mesh;
  body.motion = Physics::Motion::Static;
  body.meshAsset = "assets/initial.dotmesh";
  REQUIRE(authoring.setRigidBody(cube->id, body));
  const auto owner = authoring.createEntity("Initial animation").value();
  const auto target = authoring.createEntity("Initial target").value();
  auto clip = Animation::Clip::create({"Initial",
                                       {{0,
                                         Animation::Path::Translation,
                                         Animation::Interpolation::Linear,
                                         {{0, {10, 0, 0, 0}}, {1, {12, 0, 0, 0}}}}}})
                  .value();
  const std::string path = "assets/initial.dotanimation";
  const std::string graphPath = "assets/initial.dotcontroller";
  REQUIRE(Animation::saveClip(files, path, clip));
  auto graph = Animation::Controller::create({"Initial", "Idle", {}, {{"Idle", path}}, {}}).value();
  REQUIRE(Animation::saveController(files, graphPath, graph));
  World::AnimationPlayer properties{{path}, {{0, target.value}}};
  properties.controller = graphPath;
  REQUIRE(authoring.setAnimationPlayer(owner, properties));
  const auto baseline = World::encodeScene(authoring).value();
  auto job = Dot::Project::ScenePreparation::begin(files, "scenes/main.dotscene", baseline).value();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};

  while (!job.ready().value()) {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }

  std::optional<Dot::Project::PreparedScene> proof{job.take().value()};
  auto tool = fixture.compiler();
  auto program = tool.compileProject(files).value();

  for (const auto& resource :
       {path, graphPath, std::string("assets/initial.dotmesh"), std::string("assets/tone.wav")}) {
    REQUIRE(files.writeTextAtomic(resource, "unavailable after background preparation"));
  }

  CHECK_FALSE(GameSession::createFromScene(fixture.project, tool, authoring, {}, std::nullopt,
                                           nullptr, &program));
  auto result = GameSession::createFromScene(fixture.project, tool, authoring, {},
                                             "scenes/main.dotscene", nullptr, &program, &*proof);
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  auto session = std::move(*result);
  good(session.start());
  std::vector<float> output(32);
  REQUIRE(session.renderAudio(output));
  CHECK(std::ranges::all_of(output, [](float sample) { return std::abs(sample - .125F) < 1e-5F; }));
  CHECK(session.animationState(session.scene().value()->findEntity(owner.value).value())
            ->controller->playback.state
        == "Idle");
  REQUIRE(authoring.renameEntity(target, "Changed after proof"));
  auto stale = GameSession::createFromScene(fixture.project, tool, authoring, {}, std::nullopt,
                                            nullptr, &program, &*proof);
  REQUIRE_FALSE(stale);
  CHECK(stale.error().code == Core::ErrorCode::InvalidArgument);
  REQUIRE(authoring.renameEntity(target, "Initial target"));
  auto config = GameSessionConfig{};
  --config.audioLimits.clip.sourceBytes;
  CHECK_FALSE(GameSession::createFromScene(fixture.project, tool, authoring, config, std::nullopt,
                                           nullptr, &program, &*proof));
  proof.reset();
  REQUIRE(session.renderAudio(output));
  CHECK(std::ranges::all_of(output, [](float sample) { return std::abs(sample - .125F) < 1e-5F; }));
  CHECK(World::encodeScene(authoring).value() == baseline);
  good(session.close());
}

TEST_CASE("game_session.begin_exposes_actual_pending_services_then_explicit_start") {
  Fixture fixture;
  fixture.controller();
  auto result = GameSession::begin(fixture.project, fixture.compiler());
  REQUIRE(result);
  auto session = std::move(*result);
  auto control = Dot::Project::PlayerControl::create(session).value();
  const auto before = nlohmann::json::parse(control.perform(R"({"command":"status"})").value());
  CHECK(before["player"]["state"] == "preparing");
  CHECK(before["player"]["preparation"] == "resources");
  CHECK(before["player"]["scope"] == "0");
  CHECK(before["player"]["clock"].is_null());
  CHECK(before["player"]["scripts"].is_null());
  CHECK_FALSE(session.scene());
  CHECK_FALSE(session.gameData());
  CHECK_FALSE(session.gameClock());
  CHECK_FALSE(session.hud());
  CHECK_FALSE(session.sceneInstances());
  CHECK_FALSE(session.sceneRequest(1));
  CHECK_FALSE(session.start());
  CHECK_FALSE(session.fixedUpdate());
  CHECK_FALSE(session.update(0));
  CHECK_FALSE(session.stats()->clock);
  CHECK_FALSE(control.perform(R"({"command":"inspect"})"));
  CHECK_FALSE(control.perform(R"({"command":"status","extra":true})"));
  CHECK_FALSE(control.perform(R"({"command":"game-data","value":{"command":"snapshot"}})"));
  REQUIRE(awaitInitial(session, SessionState::Ready));
  CHECK(session.stats()->location.scope != 0);
  REQUIRE(session.stats()->clock);
  CHECK(session.stats()->preparationCompileAttempts == 1);
  CHECK(actor(session).name == "Cube");
  CHECK(session.stats()->simulatedUpdates == 0);
  good(session.start());
  CHECK(actor(session).name.starts_with("start"));
  good(session.close());
}

TEST_CASE("game_session.begin_compiled_owns_generation_and_frozen_initial_scene") {
  Fixture fixture(false);
  fixture.controller();
  auto files = fixture.project.files().value();
  auto program = fixture.compiler().compileProject(files).value();
  auto published = program.publish(files, "compiled").value();
  auto imported = Dot::Script::CompiledProgram::loadPublished(published).value();
  auto description = fixture.project.description().value();
  auto authoring = fixture.project.loadStartup().value();
  auto session = GameSession::beginCompiled(files, description, authoring, imported).value();
  REQUIRE(authoring.renameEntity(authoring.entities()->at(1).id, "Changed after snapshot"));
  REQUIRE(files.writeTextAtomic("scripts/mover.ts", "invalid later TypeScript"));
  REQUIRE(awaitInitial(session, SessionState::Ready));
  CHECK(session.stats()->preparationCompileAttempts == 0);
  CHECK(actor(session).name == "Cube");
  REQUIRE(files.writeTextAtomic("assets/tone.wav", "unavailable after native binding"));
  good(session.start());
  std::vector<float> samples(32);
  REQUIRE(session.renderAudio(samples));
  CHECK(std::ranges::all_of(samples, [](float value) { return std::abs(value - .125F) < 1e-5F; }));
  good(session.close());
}

TEST_CASE("game_session.begin_resource_failure_retains_error_through_owned_teardown") {
  Fixture fixture;
  fixture.controller();
  REQUIRE(fixture.project.files()->writeTextAtomic("assets/tone.wav", "not encoded audio"));
  auto session = GameSession::begin(fixture.project, fixture.compiler()).value();
  auto control = Dot::Project::PlayerControl::create(session).value();
  auto result = awaitInitial(session, SessionState::Ready);
  REQUIRE_FALSE(result);
  CHECK(result.error().message.find("tone.wav") != std::string::npos);
  CHECK(session.stats()->state == SessionState::Cancelling);
  CHECK_FALSE(session.scene());
  REQUIRE(awaitInitial(session, SessionState::Faulted));
  CHECK(session.pollPreparationClosed().value());
  const auto status = nlohmann::json::parse(control.perform(R"({"command":"status"})").value());
  CHECK(status["player"]["state"] == "faulted");
  CHECK(status["player"]["scope"] == "0");
  CHECK(status["player"]["clock"].is_null());
  CHECK(status["player"]["preparationError"]["message"].get<std::string>().find("tone.wav")
        != std::string::npos);
  good(session.close());
  CHECK(session.stats()->state == SessionState::Closed);
  CHECK_FALSE(session.gameData());
}

TEST_CASE("game_session.begin_cancels_live_compiler_after_move_without_publishing_services") {
  Fixture fixture;
  fixture.controller();
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/mover.ts", "sleep"));
  const auto generations = Dot::Script::compilerGenerations();
  const auto processes = Dot::Core::processResources();
  auto tool = Dot::Script::TypeScriptCompiler::create(DOT_COMPILER_FIXTURE).value();
  auto ownership = std::make_shared<int>(1);
  std::weak_ptr<int> released = ownership;
  GameSessionConfig config;
  config.scripts.services.push_back(
      {"lifetime", [ownership](std::string_view) -> Dot::Core::Result<std::optional<std::string>> {
         return std::to_string(*ownership);
       }});
  auto original = GameSession::begin(fixture.project, tool, config).value();
  config.scripts.services.clear();
  ownership.reset();
  CHECK_FALSE(released.expired());
  REQUIRE(original.pollPreparation());
  auto session = std::move(original);
  CHECK_FALSE(original.pollPreparation());
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};

  while (Dot::Core::processResources().children == processes.children) {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    REQUIRE(session.pollPreparation());
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }

  auto control = Dot::Project::PlayerControl::create(session).value();
  REQUIRE(control.perform(R"({"command":"close"})"));
  CHECK(control.closing().value());
  REQUIRE(session.requestPreparationClose());
  REQUIRE(awaitInitial(session, SessionState::Closed));
  CHECK(session.pollPreparationClosed().value());
  CHECK(released.expired());
  CHECK_FALSE(session.scene());
  CHECK(session.stats()->location.scope == 0);
  CHECK(Dot::Core::processResources().children == processes.children);
  CHECK(Dot::Core::processResources().handles == processes.handles);
  CHECK(Dot::Script::compilerGenerations() == generations);
  good(session.close());
}

TEST_CASE("game_session.begin_discards_obsolete_source_before_initial_native_construction") {
  Fixture fixture(true, 1);
  fixture.controller(2);
  auto session = GameSession::begin(fixture.project, fixture.compiler()).value();
  REQUIRE(session.pollPreparation());
  fixture.writeController(7);
  REQUIRE(awaitInitial(session, SessionState::Ready));
  CHECK(session.stats()->preparationCompileAttempts == 2);
  CHECK(session.stats()->discardedScriptCompilations == 1);
  CHECK(actor(session).name == "Cube");
  good(session.start());
  REQUIRE(session.publish(held()));
  good(session.fixedUpdate());
  CHECK(actor(session).transform.position.x == doctest::Approx(7));
  const auto previous = session.stats()->location.scope;
  const auto request = session.requestScene("scenes/main.dotscene").value();
  good(session.update(0));
  good(Dot::Tests::awaitScene(session, request));
  CHECK(session.stats()->location.scope != previous);
  CHECK(session.stats()->preparationCompileAttempts == 2);
  CHECK(session.stats()->discardedScriptCompilations == 1);
  good(session.close());
}

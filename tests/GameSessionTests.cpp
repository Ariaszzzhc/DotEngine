#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <expected>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "SceneAudioFixture.hpp"

import Dot.Project;
import Dot.Project.GameSession;
import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Platform.Input;
import Dot.Platform.Events;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Audio.Mixer;
import Dot.Physics.Body;

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
  auto reloaded = good(session.fixedUpdate());
  CHECK(reloaded.source.committed);
  CHECK(reloaded.source.reload->migrated == 1);
  CHECK(actor(session).name == "fixed2");
  CHECK(actor(session).transform.position.x == doctest::Approx(6.0 / 60));
  REQUIRE(
      fixture.project.files()->writeTextAtomic("scripts/mover.ts", "export const bad:number='x';"));
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
  Fixture fixture(false);
  REQUIRE(fixture.project.files()->writeTextAtomic(
      "scripts/mover.ts",
      "import {ScriptComponent} from 'dot:engine';export class Mover extends ScriptComponent{"
      "onUpdate(){while(true){}}}"));
  GameSessionConfig config;
  config.scripts.runtime.milliseconds = 10;
  auto session = fixture.session(config);
  good(session.start());
  auto report = session.update(0);
  REQUIRE(report);
  CHECK(report->frame.realmFaulted);
  CHECK(session.stats()->state == SessionState::Faulted);
  CHECK_FALSE(session.fixedUpdate());
  auto closed = session.close();
  REQUIRE(closed);
  CHECK(closed->realmFaulted);
  CHECK(session.stats()->state == SessionState::Closed);
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

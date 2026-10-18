#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <future>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "SceneAudioFixture.hpp"
#include "ScenePreparation.hpp"
#include "ScriptCompilation.hpp"

import Dot.Animation.Clip;
import Dot.Animation.Files;
import Dot.Animation.ControllerFiles;
import Dot.Core.Result;
import Dot.Project;
import Dot.Project.GameSession;
import Dot.Project.GamePackage;
import Dot.Editor.Play;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Physics.Body;
import Dot.Audio.Mixer;
import Dot.Assets.Material;
import Dot.Assets.Mesh;
import Dot.Assets.Image;
import Dot.Graphics.Geometry;
import Dot.Graphics.Image;
import Dot.Platform.Input;

namespace {
  using namespace Dot;
  using Json = nlohmann::json;

  struct Fixture {
    DotTest::Audio::Directory directory;
    Project::Project project;
    std::vector<Json> observations;

    explicit Fixture(bool live = false)
        : project(Project::Project::create(
                      directory.files(), "game",
                      {"Transitions", "scenes/main.dotscene", "scripts", 320, 240, 60, live})
                      .value()) {
      auto source = R"ts(
import {ScriptComponent, scenes, physics, audio, input} from 'dot:engine';
import {invoke} from 'dot:host';
export class Travel extends ScriptComponent<{next?: string, requestInPrepare?: boolean,
    failPrepare?: boolean, failStart?: boolean, failStop?: boolean}> {
  boot = scenes.current();
  denied = (() => {
    if (this.properties.failPrepare) {
      throw new Error('candidate rejected');
    }

    if (this.properties.requestInPrepare) {
      try {
        scenes.request('scenes/next.dotscene');
      } catch (error) {
        return (error as {code: string}).code;
      }
    }

    return '';
  })();

  onStart(): void {
    invoke('observe.event', [{phase: 'start', boot: this.boot, where: scenes.current(),
      denied: this.denied, bodies: physics.stats().bodies, audio: audio.stats(), input: input.index}]);

    if (this.properties.next) {
      scenes.request(this.properties.next);
    }

    if (this.properties.failStart) {
      while (true) {}
    }
  }

  onUpdate(): void {
    this.self.name = 'updated';
  }

  onStop(): void {
    invoke('observe.event', [{phase: 'stop', where: scenes.current()}]);

    if (this.properties.failStop) {
      while (true) {}
    }
  }
}
)ts";
      REQUIRE(project.files()->writeTextAtomic("scripts/travel.ts", source));
      DotTest::Audio::writeWave(project.files().value(), "assets/tone.wav",
                                std::vector<float>(1024, .25F));
      auto initial = scene("Start", Json::object(), true);
      REQUIRE(project.saveStartup(initial));
      auto target = scene("Next", Json::object(), false);
      REQUIRE(World::saveScene(project.files().value(), "scenes/next.dotscene", target));
    }

    auto compiler() const -> Script::TypeScriptCompiler {
      return Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
    }

    auto scene(const std::string& name, Json properties = Json::object(), bool sound = false)
        -> World::Scene {
      auto result = World::Scene::create(name).value();
      auto actor = result.createEntity("Actor").value();
      REQUIRE(result.setRigidBody(actor, World::RigidBody{}));
      REQUIRE(result.setScript(actor,
                               World::ScriptComponent{"travel.ts", "Travel", properties.dump()}));

      if (sound) {
        World::AudioSource source;
        source.asset = "assets/tone.wav";
        source.voice.looping = true;
        REQUIRE(result.setAudioSource(actor, source));
      }

      auto camera = result.createEntity("Camera").value();
      World::Camera view;
      view.primary = true;
      REQUIRE(result.setCamera(camera, view));
      return result;
    }

    auto config() -> Project::GameSessionConfig {
      Project::GameSessionConfig result;
      result.pollInterval = std::chrono::milliseconds{0};
      result.scripts.runtime.milliseconds = 30;
      result.scripts.services.push_back(
          {"observe.event",
           [this](std::string_view text) -> Core::Result<std::optional<std::string>> {
             observations.push_back(Json::parse(text).at(0));
             return std::nullopt;
           }});
      return result;
    }

    auto session() -> Project::GameSession {
      auto result = Project::GameSession::create(project, compiler(), config());
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }
  };

  auto good(Core::Result<Script::ScriptReport> report) -> void {
    REQUIRE_MESSAGE(report, (report ? "" : report.error().message));
    REQUIRE_MESSAGE(report->failures.empty(),
                    (report->failures.empty() ? "" : report->failures.front().error.message));
    CHECK_FALSE(report->realmFaulted);
  }

  auto good(Core::Result<Script::LiveScriptReport> report) -> void {
    REQUIRE_MESSAGE(report, (report ? "" : report.error().message));
    good(Core::Result<Script::ScriptReport>{std::move(report->frame)});
  }
}  // namespace

TEST_CASE("scene_transition.prepares_before_stop_rebuilds_services_and_preserves_old_owned_scene") {
  Fixture fixture;
  auto session = fixture.session();
  auto old = session.scene().value();
  auto oldActor = old->entities()->front().id;
  CHECK_FALSE(session.requestScene("scenes/next.dotscene"));
  CHECK_FALSE(session.update(0));
  good(session.start());
  auto id = session.requestScene("scenes/next.dotscene").value();
  CHECK(session.sceneRequest(id)->state == Script::SceneRequestState::Pending);
  good(session.fixedUpdate());
  CHECK(session.scene()->get() == old.get());
  CHECK_FALSE(session.update(-1));
  CHECK(session.sceneRequest(id)->state == Script::SceneRequestState::Pending);
  good(session.update(0));
  auto oldClock = session.gameClock().value();
  REQUIRE(oldClock.perform(R"({"command":"set-paused","paused":true})"));
  REQUIRE(oldClock.perform(R"({"command":"step"})"));
  good(Dot::Tests::awaitScene(session, id));
  CHECK(oldClock.snapshot()->step->state == Script::GameStepState::Cancelled);
  CHECK_FALSE(session.gameClock()->snapshot()->step);
  CHECK(session.stats()->simulatedFixedSteps == 0);
  CHECK(session.stats()->lifetimeFixedSteps == 1);
  CHECK(session.scene().value()->id() != old->id());
  CHECK(session.scene().value()->name().value() == "Next");
  CHECK_FALSE(session.scene().value()->entity(oldActor));
  CHECK(old->entity(oldActor)->name == "updated");
  CHECK(session.stats()->location.path == "scenes/next.dotscene");
  CHECK(session.sceneRequest(id)->state == Script::SceneRequestState::Succeeded);
  CHECK(session.sceneRequest(id)->committed);
  CHECK_FALSE(session.stats()->retiringAudio);
  REQUIRE(fixture.observations.size() == 3);
  CHECK(fixture.observations[0]["phase"] == "start");
  CHECK(fixture.observations[1]["phase"] == "stop");
  const auto& started = fixture.observations.back();
  CHECK(started["where"]["scope"] == std::to_string(session.scene().value()->id()));
  CHECK(started["where"]["path"] == "scenes/next.dotscene");
  CHECK(started["boot"] == started["where"]);
  CHECK(started["audio"]["voices"] == 0);
  CHECK(started["bodies"] == 1);
  CHECK(started["input"] == "1");
  good(session.close());
  CHECK(session.sceneRequest(id)->committed);
}

TEST_CASE("scene_transition.loading_failures_keep_old_realm_world_and_audio_running") {
  Fixture fixture;
  auto session = fixture.session();
  good(session.start());
  auto old = session.scene().value();
  good(session.update(0));
  const auto vm = Script::Runtime::backendMemory();
  const auto audio = Audio::Mixer::backendMemory();
  auto bad = fixture.scene("Rejected", {{"failPrepare", true}});
  REQUIRE(World::saveScene(fixture.project.files().value(), "scenes/rejected.dotscene", bad));
  REQUIRE(fixture.project.files()->writeTextAtomic("scenes/broken.dotscene", "{}"));

  for (const auto* path : {"scenes/missing.dotscene", "scenes/broken.dotscene",
                           "scenes/rejected.dotscene", "scenes/NEXT.dotscene"}) {
    const auto id = session.requestScene(path).value();
    good(session.update(0));
    good(Dot::Tests::awaitScene(session, id));
    auto status = session.sceneRequest(id).value();
    CHECK(status.state == Script::SceneRequestState::Failed);
    CHECK_FALSE(status.committed);
    REQUIRE(status.error);
    CHECK(session.stats()->state == Project::SessionState::Running);
    CHECK(session.scene().value().get() == old.get());
    CHECK(fixture.observations.size() == 1);
    CHECK(Script::Runtime::backendMemory().bytes == vm.bytes);
    CHECK(Audio::Mixer::backendMemory().bytes == audio.bytes);
  }

  std::error_code error;
  std::filesystem::create_symlink("next.dotscene",
                                  fixture.project.files()->root() / "scenes/link.dotscene", error);
  if (!error) {
    auto link = session.requestScene("scenes/link.dotscene").value();
    good(session.update(0));
    good(Dot::Tests::awaitScene(session, link));
    CHECK(session.sceneRequest(link)->state == Script::SceneRequestState::Failed);
    CHECK(session.scene().value().get() == old.get());
  } else {
    WARN_MESSAGE(false, "Symlink fixture unavailable: ", error.message());
  }
  std::vector<float> samples(128);
  REQUIRE(session.renderAudio(samples));
  CHECK(std::ranges::any_of(samples, [](float value) { return value > .1F; }));
  good(session.close());
}

TEST_CASE(
    "scene_transition.script_start_requests_wait_one_frame_and_preparation_writes_are_denied") {
  Fixture fixture;
  auto start
      = fixture.scene("Start", {{"next", "scenes/next.dotscene"}, {"requestInPrepare", true}});
  auto next = fixture.scene("Next", {{"next", "scenes/end.dotscene"}});
  auto end = fixture.scene("End");
  REQUIRE(fixture.project.saveStartup(start));
  REQUIRE(World::saveScene(fixture.project.files().value(), "scenes/next.dotscene", next));
  REQUIRE(World::saveScene(fixture.project.files().value(), "scenes/end.dotscene", end));
  auto session = fixture.session();
  good(session.start());
  CHECK(fixture.observations[0]["denied"] == "InvalidState");
  CHECK(session.scene().value()->name().value() == "Start");
  good(session.update(0));
  good(Dot::Tests::awaitScene(session, 1));
  CHECK(session.scene().value()->name().value() == "Next");
  CHECK(session.stats()->transition->state == Script::SceneRequestState::Pending);
  good(session.update(0));
  good(Dot::Tests::awaitScene(session, 2));
  CHECK(session.scene().value()->name().value() == "End");
  CHECK(session.sceneRequest(1)->committed);
  CHECK(session.sceneRequest(2)->committed);
  good(session.close());
}

TEST_CASE(
    "scene_transition.committed_start_or_stop_faults_report_new_scene_and_release_old_services") {
  for (const bool stop : {false, true}) {
    Fixture fixture;
    auto initial = fixture.scene("Start", stop ? Json{{"failStop", true}} : Json::object());
    auto target = fixture.scene(
        "Target",
        stop ? Json::object() : Json{{"failStart", true}, {"next", "scenes/next.dotscene"}});
    REQUIRE(fixture.project.saveStartup(initial));
    REQUIRE(World::saveScene(fixture.project.files().value(), "scenes/next.dotscene", target));
    auto session = fixture.session();
    good(session.start());
    auto old = session.scene().value();
    auto id = session.requestScene("scenes/next.dotscene").value();
    good(session.update(0));
    CHECK_FALSE(Dot::Tests::awaitScene(session, id));
    CHECK(session.scene().value()->name().value() == "Target");
    CHECK(session.scene().value()->id() != old->id());
    auto result = session.sceneRequest(id).value();
    CHECK(result.state == Script::SceneRequestState::Failed);
    CHECK(result.committed);
    REQUIRE(result.error);
    CHECK(session.stats()->state == Project::SessionState::Faulted);
    CHECK_FALSE(session.requestScene("scenes/main.dotscene"));

    if (!stop) {
      CHECK(session.sceneRequest(id + 1)->state == Script::SceneRequestState::Cancelled);
    }

    REQUIRE(session.close());
  }
}

TEST_CASE(
    "scene_transition.exported_generation_switches_without_sources_or_compiler_and_releases_on_"
    "close") {
  Fixture fixture;
  const auto vm = Script::Runtime::backendMemory();
  const auto mixer = Audio::Mixer::backendMemory();
  auto initial = fixture.project.loadStartup().value();
  auto published = Project::GamePackage::publish(fixture.project, initial, fixture.compiler(),
                                                 fixture.directory.files(), "published")
                       .value();
  auto package = Project::GamePackage::open(published).value();
  auto session = Project::GameSession::createCompiled(
                     package.files().value(), package.description().value(),
                     package.scene().value(), package.program().value(), fixture.config())
                     .value();
  good(session.start());
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/travel.ts", "not TypeScript"));
  auto id = session.requestScene("scenes/next.dotscene").value();
  good(session.update(0));
  good(Dot::Tests::awaitScene(session, id));
  CHECK(session.sceneRequest(id)->committed);
  CHECK(session.scene().value()->name().value() == "Next");
  auto foreign = std::async(std::launch::async, [&] {
    return !session.requestScene("scenes/main.dotscene") && !session.sceneRequest(id);
  });
  CHECK(foreign.get());
  auto pending = session.requestScene("scenes/main.dotscene").value();
  auto moved = std::move(session);
  CHECK_FALSE(session.sceneRequest(id));
  good(moved.close());
  CHECK(moved.sceneRequest(pending)->state == Script::SceneRequestState::Cancelled);
  CHECK(Script::Runtime::backendMemory().bytes == vm.bytes);
  CHECK(Audio::Mixer::backendMemory().bytes == mixer.bytes);
}

TEST_CASE("scene_transition.editor_play_uses_active_file_and_never_mutates_authoring") {
  Fixture fixture;
  auto authoring = fixture.project.loadStartup().value();
  auto before = World::encodeScene(authoring).value();
  Editor::PlayConfig config;
  config.session = fixture.config();
  auto play = Editor::PlaySession::create(fixture.project, fixture.compiler(), authoring, config,
                                          "scenes/editor.dotscene")
                  .value();
  CHECK(play.stats()->location.path == "scenes/editor.dotscene");
  auto id = play.requestScene("scenes/next.dotscene").value();
  REQUIRE(play.advance(Platform::InputState{}, std::chrono::milliseconds{20}));
  Dot::Tests::awaitPlayScene(play, id);
  CHECK(play.sceneRequest(id)->committed);
  CHECK(play.scene().value()->name().value() == "Next");
  CHECK(play.stats()->location.path == "scenes/next.dotscene");
  REQUIRE(play.stop());
  CHECK(World::encodeScene(authoring).value() == before);
  CHECK(World::encodeScene(fixture.project.loadStartup().value()).value() == before);
}

TEST_CASE("scene_transition.hot_reload_compile_failure_is_recoverable_without_retiring_old_scene") {
  Fixture fixture(true);
  auto files = fixture.project.files().value();
  auto source = files.readText("scripts/travel.ts").value();
  auto session = fixture.session();
  good(session.start());
  auto original = session.scene().value();
  REQUIRE(files.writeTextAtomic("scripts/travel.ts", "const broken: number = 'bad';"));
  auto rejected = session.requestScene("scenes/next.dotscene").value();
  auto frame = session.update(0);
  good(Dot::Tests::awaitScene(session, rejected));
  REQUIRE(frame);
  CHECK(Dot::Tests::awaitCompilation([&] { return session.pollScripts(); })->error.has_value());
  CHECK(session.sceneRequest(rejected)->state == Script::SceneRequestState::Failed);
  CHECK_FALSE(session.sceneRequest(rejected)->committed);
  CHECK(session.scene().value().get() == original.get());
  REQUIRE(files.writeTextAtomic("scripts/travel.ts", source));
  auto accepted = session.requestScene("scenes/next.dotscene").value();
  good(session.update(0));
  good(Dot::Tests::awaitScene(session, accepted));
  CHECK(session.sceneRequest(accepted)->committed);
  CHECK(session.scene().value()->name().value() == "Next");
  good(session.close());
}

TEST_CASE("scene_transition.asset_decode_and_native_service_failures_preserve_old_services") {
  Fixture fixture;
  auto files = fixture.project.files().value();
  auto session = fixture.session();
  good(session.start());
  good(session.update(0));
  auto original = session.scene().value();
  const auto vm = Script::Runtime::backendMemory();
  const auto audio = Audio::Mixer::backendMemory();
  REQUIRE(files.writeTextAtomic("assets/corrupt.png", "not a PNG"));
  REQUIRE(files.writeTextAtomic("assets/corrupt.hdr", "not HDR"));
  Assets::MaterialDescription material;
  material.baseColorTexture = Assets::TextureSource{"assets/corrupt.png"};
  REQUIRE(Assets::saveMaterial(files, "assets/corrupt.dotmaterial", material));

  REQUIRE(files.writeTextAtomic("assets/no-normals.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"));
  REQUIRE(Assets::saveMaterial(files, "assets/physical.dotmaterial", {}));
  auto normalFrame = Graphics::sphere(.5F, 8, 4).value();
  REQUIRE(Graphics::meshSurface(normalFrame)->hasNormalMapFrame);
  REQUIRE(Assets::saveMesh(files, "assets/normal-frame.dotmesh", normalFrame));
  auto png = Assets::encodeImage(Graphics::Image{1, 1, {128, 128, 0, 255}}).value();
  REQUIRE(files.writeAtomic("assets/backward-normal.png", png));
  Assets::MaterialDescription backward;
  backward.normalTexture = Assets::TextureSource{"assets/backward-normal.png"};
  backward.normalTexture->sampling.colorSpace = Graphics::ColorSpace::Linear;
  REQUIRE(Assets::saveMaterial(files, "assets/backward.dotmaterial", backward));
  auto corruptNormal = backward;
  corruptNormal.normalTexture->path = "assets/corrupt.png";
  REQUIRE(Assets::saveMaterial(files, "assets/corrupt-normal.dotmaterial", corruptNormal));

  for (const std::string kind :
       {"mesh", "texture", "pbr-frame", "normal-image", "hdr", "audio", "animation"}) {
    auto candidate = fixture.scene("Invalid");
    auto actor = candidate.entities()->front().id;

    if (kind == "mesh") {
      World::MeshRenderer mesh;
      mesh.kind = World::MeshKind::Asset;
      mesh.asset = "assets/missing.dotmesh";
      REQUIRE(candidate.setMeshRenderer(actor, mesh));
    } else if (kind == "texture") {
      REQUIRE(candidate.setMeshRenderer(actor, World::MeshRenderer{}));
      REQUIRE(candidate.setMaterial(actor, World::MaterialBinding{"assets/corrupt.dotmaterial"}));
    } else if (kind == "pbr-frame" || kind == "normal-image") {
      World::MeshRenderer mesh;
      mesh.kind = World::MeshKind::Asset;
      mesh.asset = kind == "pbr-frame" ? "assets/no-normals.obj" : "assets/normal-frame.dotmesh";
      REQUIRE(candidate.setMeshRenderer(actor, mesh));
      REQUIRE(candidate.setMaterial(
          actor,
          World::MaterialBinding{kind == "pbr-frame" ? "assets/physical.dotmaterial"
                                                     : "assets/corrupt-normal.dotmaterial"}));
    } else if (kind == "hdr") {
      World::Environment environment;
      environment.asset = "assets/corrupt.hdr";
      REQUIRE(candidate.setEnvironment(environment));
    } else if (kind == "audio") {
      World::AudioSource source;
      source.asset = "assets/missing.wav";
      REQUIRE(candidate.setAudioSource(actor, source));
    } else {
      REQUIRE(candidate.setRigidBody(actor, std::nullopt));
      World::AnimationPlayer player;
      player.clips = {"assets/missing.dotanimation"};
      player.targets = {{0, actor.value}};
      REQUIRE(candidate.setAnimationPlayer(actor, player));
    }

    REQUIRE(World::saveScene(files, "scenes/invalid.dotscene", candidate));
    auto id = session.requestScene("scenes/invalid.dotscene").value();
    good(session.update(0));
    good(Dot::Tests::awaitScene(session, id));
    auto result = session.sceneRequest(id).value();
    CHECK(result.state == Script::SceneRequestState::Failed);
    CHECK_FALSE(result.committed);
    REQUIRE(result.error);
    if (kind == "pbr-frame") {
      CHECK(result.error->message.find("normals") != std::string::npos);
    }

    if (kind == "normal-image") {
      CHECK(result.error->message.find("corrupt.png") != std::string::npos);
    }

    CHECK(session.scene().value().get() == original.get());
    CHECK(Script::Runtime::backendMemory().bytes == vm.bytes);
    CHECK(Audio::Mixer::backendMemory().bytes == audio.bytes);
  }

  auto candidate = fixture.scene("Signed normal source");
  const auto actor = candidate.entities()->front().id;
  World::MeshRenderer mesh;
  mesh.kind = World::MeshKind::Asset;
  mesh.asset = "assets/normal-frame.dotmesh";
  REQUIRE(candidate.setMeshRenderer(actor, mesh));
  REQUIRE(candidate.setMaterial(actor, World::MaterialBinding{"assets/backward.dotmaterial"}));
  REQUIRE(World::saveScene(files, "scenes/signed-normal.dotscene", candidate));
  const auto accepted = session.requestScene("scenes/signed-normal.dotscene").value();
  good(session.update(0));
  good(Dot::Tests::awaitScene(session, accepted));
  CHECK(session.sceneRequest(accepted)->committed);
  CHECK(session.scene().value()->name().value() == "Signed normal source");
  good(session.close());
}

TEST_CASE("scene_transition.disabled_hot_reload_keeps_the_owned_program_across_scene_changes") {
  Fixture fixture;
  auto session = fixture.session();
  good(session.start());
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/travel.ts", "not TypeScript"));
  auto id = session.requestScene("scenes/next.dotscene").value();
  good(session.update(0));
  good(Dot::Tests::awaitScene(session, id));
  CHECK(session.sceneRequest(id)->committed);
  CHECK(session.scene().value()->name().value() == "Next");
  good(session.close());
}

TEST_CASE("scene_transition.active_scene_export_preserves_original_startup_and_round_trips_paths") {
  Fixture fixture;
  auto files = fixture.project.files().value();
  auto main = files.readText("scenes/main.dotscene").value();
  auto saved = files.readText("scenes/next.dotscene").value();
  auto active = World::loadScene(files, "scenes/next.dotscene").value();
  REQUIRE(active.rename("Unsaved next"));
  auto output = Project::GamePackage::publish(fixture.project, active, fixture.compiler(),
                                              fixture.directory.files(), "active-export", {},
                                              "scenes/next.dotscene");
  REQUIRE_MESSAGE(output, (output ? "" : output.error().message));
  auto package = Project::GamePackage::open(*output).value();
  CHECK(package.description()->startScene == "scenes/next.dotscene");
  CHECK(package.files()->readText("scenes/main.dotscene").value() == main);
  CHECK(package.scene()->name().value() == "Unsaved next");
  CHECK(files.readText("scenes/main.dotscene").value() == main);
  CHECK(files.readText("scenes/next.dotscene").value() == saved);
  auto session = Project::GameSession::createCompiled(
                     package.files().value(), package.description().value(),
                     package.scene().value(), package.program().value(), fixture.config())
                     .value();
  CHECK(session.stats()->location.path == "scenes/next.dotscene");
  good(session.start());
  auto mainRequest = session.requestScene("scenes/main.dotscene").value();
  good(session.update(0));
  good(Dot::Tests::awaitScene(session, mainRequest));
  CHECK(session.sceneRequest(mainRequest)->committed);
  CHECK(session.scene().value()->name().value() == "Start");
  auto back = session.requestScene("scenes/next.dotscene").value();
  good(session.update(0));
  good(Dot::Tests::awaitScene(session, back));
  CHECK(session.sceneRequest(back)->committed);
  CHECK(session.scene().value()->name().value() == "Unsaved next");
  good(session.close());

  for (const auto* path :
       {"../escape.dotscene", "scenes/.dot-stage-1/hidden.dotscene", "scenes/not.json"}) {
    CHECK_FALSE(Project::GamePackage::publish(fixture.project, active, fixture.compiler(),
                                              fixture.directory.files(), "invalid-export", {},
                                              path));
    CHECK_FALSE(std::filesystem::exists(fixture.directory.root / "invalid-export"));
  }
}

TEST_CASE("scene_transition.play_returns_owned_unsaved_origin_instead_of_later_disk_contents") {
  Fixture fixture;
  auto authoring = fixture.project.loadStartup().value();
  REQUIRE(authoring.rename("Unsaved play origin"));
  auto actor = authoring.entities()->front().id;
  auto pose = authoring.transform(actor).value();
  pose.position.x = 7;
  REQUIRE(authoring.setTransform(actor, pose));
  auto snapshot = World::encodeScene(authoring).value();
  Editor::PlayConfig config;
  config.session = fixture.config();
  auto play = Editor::PlaySession::create(fixture.project, fixture.compiler(), authoring, config,
                                          "scenes/main.dotscene")
                  .value();
  auto away = play.requestScene("scenes/next.dotscene").value();
  REQUIRE(play.advance(Platform::InputState{}, std::chrono::nanoseconds{0}));
  Dot::Tests::awaitPlayScene(play, away);
  CHECK(play.sceneRequest(away)->committed);
  CHECK(play.scene().value()->name().value() == "Next");
  REQUIRE(fixture.project.files()->writeTextAtomic("scenes/main.dotscene", "corrupted after Play"));
  auto back = play.requestScene("scenes/main.dotscene").value();
  REQUIRE(play.advance(Platform::InputState{}, std::chrono::nanoseconds{0}));
  Dot::Tests::awaitPlayScene(play, back);
  CHECK(play.sceneRequest(back)->committed);
  CHECK(play.scene().value()->name().value() == "Unsaved play origin");
  auto returned = play.scene().value()->entities()->front();
  CHECK(returned.name == "Actor");
  CHECK(returned.transform.position.x == 7);
  REQUIRE(play.stop());
  CHECK(World::encodeScene(authoring).value() == snapshot);
  CHECK(fixture.project.files()->readText("scenes/main.dotscene").value()
        == "corrupted after Play");
}

TEST_CASE("scene_transition.loading_keeps_old_gameplay_and_close_cancels_without_target_hooks") {
  Fixture fixture;
  auto session = fixture.session();
  good(session.start());
  const auto original = session.scene().value();
  const auto request = session.requestScene("scenes/next.dotscene").value();
  good(session.pollScene());
  CHECK(session.sceneRequest(request)->state == Script::SceneRequestState::Pending);
  good(session.update(0));
  CHECK(session.sceneRequest(request)->state == Script::SceneRequestState::Loading);
  CHECK(session.scene().value().get() == original.get());
  CHECK(fixture.observations.size() == 1);
  CHECK_FALSE(session.requestScene("scenes/main.dotscene"));
  good(session.fixedUpdate());
  CHECK(original->entities()->front().name == "updated");
  good(session.close());
  CHECK(session.sceneRequest(request)->state == Script::SceneRequestState::Cancelled);
  CHECK_FALSE(session.sceneRequest(request)->committed);
  REQUIRE(fixture.observations.size() == 2);
  CHECK(fixture.observations.back()["phase"] == "stop");
  CHECK_FALSE(session.pollScene());
}

TEST_CASE("scene_transition.additive_content_keeps_live_scope_realm_clock_and_native_state") {
  for (const bool live : {false, true}) {
    Fixture fixture(live);
    auto session = fixture.session();
    good(session.start());
    auto scene = session.scene().value();
    const auto actor = scene->entities()->front().id;
    auto clock = session.gameClock().value();
    REQUIRE(clock.perform(R"({"command":"set-paused","paused":true})"));
    const auto pose = scene->worldTransform(actor)->data;
    const auto add = session.appendScene("scenes/next.dotscene").value();
    good(session.update(0));
    good(Dot::Tests::awaitScene(session, add));
    auto record = session.sceneRequest(add).value();
    REQUIRE(record.root);
    CHECK(record.state == Script::SceneRequestState::Succeeded);
    CHECK(record.committed);
    CHECK(session.scene().value() == scene);
    CHECK(session.scene().value()->id() == actor.scene);
    CHECK(scene->worldTransform(actor)->data == pose);
    CHECK(clock.snapshot()->paused);
    CHECK(fixture.observations.size() == 2);
    CHECK(fixture.observations[0]["phase"] == "start");
    CHECK(fixture.observations[1]["phase"] == "start");
    CHECK(fixture.observations[1]["where"]["scope"] == std::to_string(actor.scene));
    REQUIRE(session.sceneInstances()->size() == 1);
    auto camera = scene->primaryCamera().value();
    CHECK(camera.value == 2);
    auto external = scene->createEntity("Player dependency").value();
    REQUIRE(scene->setParent(external, *record.root));
    const auto rejected = session.removeScene(*record.root).value();
    good(session.update(0));
    CHECK(session.sceneRequest(rejected)->state == Script::SceneRequestState::Failed);
    CHECK_FALSE(session.sceneRequest(rejected)->committed);
    CHECK(session.sceneInstances()->size() == 1);
    REQUIRE(scene->setParent(external, std::nullopt));
    const auto remove = session.removeScene(*record.root).value();
    good(session.update(0));
    CHECK(session.sceneRequest(remove)->state == Script::SceneRequestState::Succeeded);
    CHECK(session.sceneRequest(remove)->committed);
    CHECK(session.sceneInstances()->empty());
    CHECK(scene->entity(actor));
    CHECK(scene->entity(external));
    CHECK_FALSE(scene->entity(*record.root));
    CHECK(fixture.observations.size() == 3);
    CHECK(fixture.observations.back()["phase"] == "stop");
    CHECK(clock.snapshot()->paused);
    good(session.close());
  }
}

TEST_CASE("scene_transition.additive_missing_assets_keep_gameplay_and_start_failure_is_committed") {
  Fixture fixture;
  auto session = fixture.session();
  good(session.start());
  auto scene = session.scene().value();
  const auto missing = session.appendScene("scenes/missing.dotscene").value();
  good(session.update(0));
  const auto before = World::encodeScene(*scene).value();
  good(Dot::Tests::awaitScene(session, missing));
  CHECK(session.sceneRequest(missing)->state == Script::SceneRequestState::Failed);
  CHECK_FALSE(session.sceneRequest(missing)->committed);
  CHECK(session.sceneInstances()->empty());
  CHECK(session.stats()->state == Project::SessionState::Running);
  CHECK(World::encodeScene(*scene).value() == before);
  CHECK(scene->entityCount().value() == 2);
  auto bad = fixture.scene("Bad", {{"failPrepare", true}});
  REQUIRE(World::saveScene(fixture.project.files().value(), "scenes/bad.dotscene", bad));
  const auto failed = session.appendScene("scenes/bad.dotscene").value();
  good(session.update(0));
  auto publication = Dot::Tests::awaitScene(session, failed);
  CHECK_FALSE(publication);
  CHECK(session.sceneRequest(failed)->state == Script::SceneRequestState::Failed);
  CHECK(session.sceneRequest(failed)->committed);
  CHECK(session.stats()->state == Project::SessionState::Faulted);
  CHECK(session.scene().value() == scene);
  CHECK(scene->entityCount().value() == 5);
  good(session.close());
}

TEST_CASE("scene_transition.additive_publication_retains_dynamic_body_velocity") {
  Fixture fixture;
  auto content = fixture.scene("Distant room");
  auto node = content.entities()->front().id;
  REQUIRE(content.setTransform(node, {{100, 0, 0}}));
  REQUIRE(World::saveScene(fixture.project.files().value(), "scenes/distant.dotscene", content));
  auto session = fixture.session();
  auto reference = fixture.session();
  good(session.start());
  good(reference.start());

  for (int frame = 0; frame < 10; ++frame) {
    good(session.fixedUpdate());
    good(reference.fixedUpdate());
  }

  const auto id = session.appendScene("scenes/distant.dotscene").value();
  good(session.update(0));
  good(Dot::Tests::awaitScene(session, id));
  REQUIRE(session.sceneRequest(id)->committed);
  good(session.fixedUpdate());
  good(reference.fixedUpdate());
  auto actual = session.scene().value();
  auto expected = reference.scene().value();
  const auto y = actual->transform(actual->findEntity(1).value())->position.y;
  CHECK(y < 0);
  CHECK(y
        == doctest::Approx(expected->transform(expected->findEntity(1).value())->position.y)
               .epsilon(1e-6));
  good(session.close());
  good(reference.close());
}

TEST_CASE("scene_transition.additive_resources_bind_prepared_animation_controller_and_collision") {
  for (const bool live : {false, true}) {
    Fixture fixture(live);
    auto files = fixture.project.files().value();
    auto content = fixture.scene("Animated equipment");
    auto collider = content.entities()->front().id;
    auto body = *content.entity(collider)->rigidBody;
    body.shape = Physics::Shape::Mesh;
    body.motion = Physics::Motion::Static;
    body.meshAsset = "assets/equipment.dotmesh";
    REQUIRE(content.setRigidBody(collider, body));
    REQUIRE(content.setTransform(collider, {{100, 0, 0}}));
    REQUIRE(Assets::saveMesh(files, *body.meshAsset, Graphics::cube().value()));
    const auto owner = content.createEntity("Machine").value();
    const auto target = content.createEntity("Indicator").value();
    auto clip = Animation::Clip::create({"Equipment cycle",
                                         {{0,
                                           Animation::Path::Translation,
                                           Animation::Interpolation::Linear,
                                           {{0, {10, 0, 0, 0}}, {1, {12, 0, 0, 0}}}}}})
                    .value();
    const std::string path = "assets/equipment.dotanimation";
    const std::string graphPath = "assets/equipment.dotcontroller";
    REQUIRE(Animation::saveClip(files, path, clip));
    auto graph
        = Animation::Controller::create({"Equipment", "Cycle", {}, {{"Cycle", path}}, {}}).value();
    REQUIRE(Animation::saveController(files, graphPath, graph));
    World::AnimationPlayer properties{{path}, {{0, target.value}}};
    properties.controller = graphPath;
    REQUIRE(content.setAnimationPlayer(owner, properties));
    REQUIRE(World::saveScene(files, "scenes/equipment.dotscene", content));
    auto session = fixture.session();
    good(session.start());
    const auto scope = session.scene().value()->id();
    const auto request = session.appendScene("scenes/equipment.dotscene").value();
    good(session.update(0));
    good(Dot::Tests::awaitScene(session, request));
    REQUIRE(session.sceneRequest(request)->committed);
    auto scene = session.scene().value();
    CHECK(scene->id() == scope);
    auto nodes = scene->entities().value();
    const auto indicator = std::ranges::find(nodes, "Indicator", &World::EntityInfo::name);
    REQUIRE(indicator != nodes.end());
    const auto x = scene->transform(indicator->id)->position.x;
    CHECK(x >= 10);
    good(session.update(.25));
    CHECK(scene->transform(indicator->id)->position.x > x);
    CHECK(fixture.observations.back()["bodies"] == 2);
    const auto removed = session.removeScene(*session.sceneRequest(request)->root).value();
    good(session.update(0));
    CHECK(session.sceneRequest(removed)->committed);
    CHECK(scene->entityCount().value() == 2);
    good(session.close());
  }
}

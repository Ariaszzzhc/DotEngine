#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ScenePreparation.hpp"
#include "ScriptCompilation.hpp"

import Dot.Animation.Files;
import Dot.Animation.ScenePlayers;
import Dot.Animation.Layers;
import Dot.Animation.TwoBone;
import Dot.Math;
import Dot.Editor.AnimationPreview;
import Dot.Editor.AnimationRetarget;
import Dot.World.SceneFiles;
import Dot.Animation.SkinFiles;
import Dot.Assets.Image;
import Dot.Assets.Material;
import Dot.Assets.Mesh;
import Dot.Graphics.Geometry;
import Dot.Graphics.Image;
import Dot.Graphics.Text;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Project;
import Dot.Project.GameSession;
import Dot.Project.PlayerControl;
import Dot.Script.Compiler;
import Dot.Script.GameData;
import Dot.Script.Runtime;
import Dot.Platform.Input;
import Dot.Platform.Events;
import Dot.World.Scene;

TEST_CASE("example.source_projects_open_and_compile_without_build_generated_sdk") {
  auto compiler = Dot::Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();

  for (const auto* root : {DOT_EXAMPLE_SOURCE, DOT_LAB_SOURCE}) {
    auto project = Dot::Project::Project::open(root);
    REQUIRE_MESSAGE(project, (project ? "" : project.error().message));
    auto files = project->files().value();
    CHECK_FALSE(std::filesystem::exists(files.root() / "scripts/dot"));

    auto scene = project->loadStartup();
    REQUIRE_MESSAGE(scene, (scene ? "" : scene.error().message));
    auto program = compiler.compileProject(files);
    REQUIRE_MESSAGE(program, (program ? "" : program.error().message));
    CHECK_FALSE(program->modules()->empty());
  }
}

namespace {
  using Json = nlohmann::json;
  using namespace Dot;

  struct Fixture {
    Core::StagedDirectory storage;
    Project::Project project;

    Fixture()
        : storage(Core::StagedDirectory::create(
                      Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
                      .value()),
          project([&]() {
            std::filesystem::copy(DOT_EXAMPLE_PROJECT, storage.files()->root() / "project",
                                  std::filesystem::copy_options::recursive);
            auto opened = Project::Project::open(storage.files()->root() / "project");
            REQUIRE_MESSAGE(opened, (opened ? "" : opened.error().message));
            return std::move(*opened);
          }()) {
    }

    auto compiler() -> Script::TypeScriptCompiler {
      return Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
    }

    auto session(bool live = false, std::optional<Core::FileSystem> saves = std::nullopt)
        -> Project::GameSession {
      auto description = project.description().value();
      description.hotReload = live;
      REQUIRE(project.save(description));
      Project::GameSessionConfig config;
      config.progress.saves = std::move(saves);
      config.pollInterval = std::chrono::milliseconds{0};
      auto result = Project::GameSession::create(project, compiler(), config);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }
  };

  template <class T>
  auto good(const Core::Result<T>& result) -> void {
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  }

  auto report(const Core::Result<Script::ScriptReport>& result) -> void {
    good(result);
    CHECK_FALSE(result->realmFaulted);

    for (const auto& failure : result->failures) {
      CHECK_MESSAGE(false, failure.error.message);
    }
  }

  auto report(const Core::Result<Script::LiveScriptReport>& result) -> void {
    good(result);
    CHECK_FALSE(result->frame.realmFaulted);

    for (const auto& failure : result->frame.failures) {
      CHECK_MESSAGE(false, failure.error.message);
    }
  }

  auto entity(Project::GameSession& session, std::string_view name) -> World::EntityInfo {
    auto entities = session.scene().value()->entities().value();
    auto found = std::ranges::find(entities, name, &World::EntityInfo::name);
    REQUIRE(found != entities.end());
    return *found;
  }

  class Driver {
  public:
    explicit Driver(Project::GameSession& session) : m_session(session) {
    }

    auto publish(std::initializer_list<Platform::Key> down = {}, float dx = 0, float dy = 0,
                 bool captured = false) -> void {
      m_input.beginFrame();
      for (const auto& [name, key] : Platform::keyNames()) {
        const bool desired = std::ranges::find(down, key) != down.end();

        if (m_input.key(key).down != desired) {
          REQUIRE(m_input.consume({0, 0, Platform::KeyChanged{key, desired, false}}));
        }
      }

      REQUIRE(m_input.consume({0, 0, Platform::PointerMoved{0, 0, dx, dy}}));
      good(m_session.publish(m_input, captured));
    }

    auto frame(std::initializer_list<Platform::Key> down = {}, float dx = 0, float dy = 0,
               bool captured = false) -> void {
      publish(down, dx, dy, captured);
      report(m_session.fixedUpdate());
      report(m_session.update(1.0 / 60));
    }

  private:
    Project::GameSession& m_session;
    Platform::InputState m_input;
  };

  auto awaitCheckpoint(Project::GameSession& session, Driver& driver) -> void {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    for (;;) {
      auto snapshot = Json::parse(session.gameData()->perform(R"({"command":"snapshot"})").value());
      const auto text = snapshot["data"].value("relayProgressStatus", std::string{});

      if (text != "Saving checkpoint..." && text != "Loading checkpoint...") {
        return;
      }

      REQUIRE(std::chrono::steady_clock::now() < deadline);
      driver.publish();
      report(session.update(0));
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  auto awaitSave(Script::GameData& data, const Json& ticket) -> Json {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto request = ticket;
    request["command"] = "take-save";

    for (;;) {
      auto value = Json::parse(data.perform(request.dump()).value());

      if (value["state"] != "pending" && value["state"] != "running") {
        REQUIRE_MESSAGE(value["state"] == "succeeded", value.dump());
        return value["result"];
      }

      REQUIRE(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  auto observation(Project::GameSession& session) -> Json {
    const auto entities = session.scene().value()->entities().value();
    const auto observer = std::ranges::find_if(entities, [](const auto& entity) {
      return entity.script && entity.script->className == "Observer";
    });
    REQUIRE(observer != entities.end());
    return Json::parse(observer->name);
  }

  auto observe(Fixture& fixture) -> void {
    REQUIRE(fixture.project.files()->writeTextAtomic("scripts/observer.ts", R"ts(
import {scene, ScriptComponent} from 'dot:engine';
export class Observer extends ScriptComponent {
  #model = scene.findEntities('Surveyor')[0]!;
  #player = scene.findEntities('Player')[0]!;
  onUpdate(): void {
    const character = this.#player.characterState;
    this.self.name = JSON.stringify({clip: this.#model.animationState.clip,
      ground: character.ground, vy: character.velocity.y});
  }
}
)ts"));
    auto scene = fixture.project.loadStartup().value();
    const auto id = scene.createEntity("Observer").value();
    REQUIRE(scene.setScript(id, World::ScriptComponent{"observer.ts", "Observer"}));
    REQUIRE(fixture.project.saveStartup(scene));
  }
}  // namespace

TEST_CASE("prism.hud_feedback_is_native_scoped_and_preparation_cannot_publish_labels") {
  Fixture f;
  REQUIRE(f.project.files()->writeTextAtomic("scripts/ui-guard.ts", R"ts(
import {gameClock, hud, ui, input, type HostError, ScriptComponent, type ScriptContext} from 'dot:engine';
function preparing(): void {
  hud.snapshot();
  gameClock.snapshot();
  hud.events('0');
  hud.inputState();
  ui.snapshot();
  ui.events('0');
  for (const attempt of [() => ui.text('checkpoint', 'bad'), () => ui.visible('menu', true), () => ui.enabled('menu', false), () => ui.update([]), () => ui.reset(), () => ui.focus(null), () => hud.set('bad', hud.label()), () => hud.remove('controls'), () => hud.clear(), () => gameClock.setPaused(true), () => gameClock.step(), () => hud.setButton('x', hud.button()), () => hud.focus(null), () => hud.activate('x'), () => input.requestTextInput(true)]) {
    let denied = false;
    try { attempt(); } catch (error) { denied = (error as HostError).code === 'InvalidState'; }
    if (!denied) { throw Error('HUD write escaped preparation guard'); }
  }
}
export class UiGuard extends ScriptComponent {
  constructor(context: ScriptContext) { super(context); preparing(); }
  onStart(): void {
    const label = hud.label({text: 'Owned'});
    const next = hud.label();
    label.position.x = .3;
    if (next.position.x !== .02) { throw Error('HUD label defaults alias'); }
    hud.set('temporary', label);
    hud.remove('temporary');
  }
}
)ts"));
  auto authoring = f.project.loadStartup().value();
  const auto guard = authoring.createEntity("UI guard").value();
  REQUIRE(authoring.setScript(guard, World::ScriptComponent{"ui-guard.ts", "UiGuard"}));
  REQUIRE(f.project.saveStartup(authoring));
  auto session = f.session();
  report(session.start());
  auto old = session.hud().value();
  const auto initial = old.snapshot().value();
  REQUIRE(initial.labels.size() == 5);
  CHECK(initial.labels.front().key == "controls");
  CHECK(std::ranges::count_if(initial.labels, [](const auto& layer) { return layer.value.visible; })
        == 2);
  const auto statusText = [](const auto& snapshot) {
    auto found
        = std::ranges::find(snapshot.labels, std::string("checkpoint"), &Graphics::TextLayer::key);
    REQUIRE(found != snapshot.labels.end());
    return found->value.text;
  };
  const std::filesystem::path fontPath{DOT_EDITOR_FONT};
  auto fontFiles = Core::FileSystem::open(fontPath.parent_path()).value();
  auto fontBytes = fontFiles.readBytes(fontPath.filename(), 32 * 1024 * 1024).value();
  auto font = Graphics::TextFont::create(fontBytes).value();

  for (const auto [width, height] :
       std::array<std::pair<unsigned, unsigned>, 3>{{{800, 480}, {1280, 800}, {2560, 1600}}}) {
    for (const auto size : {20.F, 22.F}) {
      for (const auto& layer : initial.labels) {
        CAPTURE(width);
        CAPTURE(height);
        CAPTURE(size);
        CAPTURE(layer.key);
        auto label = layer.value;
        label.fontSize = size;
        auto raster = font.rasterize(
            label, static_cast<unsigned>(label.size.x * static_cast<float>(width)),
            static_cast<unsigned>(label.size.y * static_cast<float>(height)), height);
        REQUIRE_MESSAGE(raster, (raster ? "" : raster.error().message));
        CHECK_FALSE(raster->overflow);
      }
    }
  }

  Driver driver(session);
  driver.publish({Platform::Key::F7});
  report(session.update(.001));
  CHECK(statusText(old.snapshot().value()).starts_with("Checkpoint unavailable."));
  auto request = session.requestScene("scenes/main.dotscene").value();
  report(session.update(0));
  report(Tests::awaitScene(session, request));
  REQUIRE(session.sceneRequest(request)->committed);
  CHECK(session.hud()->snapshot()->scope != initial.scope);
  CHECK_FALSE(old.perform(R"({"command":"clear"})"));
  CHECK(statusText(old.snapshot().value()).starts_with("Checkpoint unavailable."));
  auto current = session.hud().value();
  REQUIRE(current.snapshot()->labels.size() == 5);
  CHECK(statusText(current.snapshot().value()).starts_with("F6 saves"));
  auto player = Project::PlayerControl::create(session).value();
  CHECK_FALSE(player.perform(Json{
      {"command", "hud"},
      {"scope", std::to_string(initial.scope)},
      {"value",
       {{"command", "clear"}}}}.dump()));
  auto result = player.perform(Json{
      {"command", "hud"},
      {"scope", std::to_string(current.snapshot()->scope)},
      {"value",
       {{"command", "snapshot"}}}}.dump());
  REQUIRE(result);
  CHECK(Json::parse(*result)["result"]["labels"].size() == 5);
  report(session.close());
  CHECK_FALSE(current.perform(R"({"command":"clear"})"));
  CHECK(current.snapshot()->labels.size() == 5);
}

TEST_CASE("prism.pause_menu_freezes_simulation_keeps_frame_ui_and_resets_on_scene_change") {
  Fixture f;
  REQUIRE(f.project.files()->writeTextAtomic("scripts/pause-observer.ts", R"ts(
import {gameState, scene, ScriptComponent} from 'dot:engine';
export class PauseObserver extends ScriptComponent {
  #frames=0; #fixed=0; #updates=0;
  onFrame(): void {
    gameState.set('pauseProbe', {frames: ++this.#frames, fixed: this.#fixed,
      updates: this.#updates, time: scene.findEntities('Surveyor')[0]!.animationState.timeSeconds});
  }
  onUpdate(): void { ++this.#updates; }
  onFixedUpdate(): void { ++this.#fixed; }
}
)ts"));
  auto authoring = f.project.loadStartup().value();
  const auto observer = authoring.createEntity("Pause observer").value();
  REQUIRE(
      authoring.setScript(observer, World::ScriptComponent{"pause-observer.ts", "PauseObserver"}));
  REQUIRE(f.project.saveStartup(authoring));
  auto session = f.session(true, f.storage.files().value());
  report(session.start());
  auto clock = session.gameClock().value();
  Driver driver(session);
  const auto probe = [&] {
    return Json::parse(
        session.gameData()->perform(R"({"command":"snapshot"})").value())["data"]["pauseProbe"];
  };

  for (int i = 0; i < 10; ++i) {
    driver.frame({Platform::Key::W, Platform::Key::Space});
  }

  driver.frame({Platform::Key::M});
  CHECK(clock.snapshot()->paused);
  CHECK_FALSE(session.pointerCaptureRequested().value());
  REQUIRE(session.hud()->snapshot()->labels.size() == 5);
  const auto before = entity(session, "Player").transform;
  CHECK(before.position.y > .1F);
  const auto counters = session.stats().value();
  const auto measured = probe();

  for (int i = 0; i < 60; ++i) {
    driver.frame({Platform::Key::D});
  }

  CHECK(entity(session, "Player").transform.position == before.position);
  CHECK(session.stats()->simulatedFixedSteps == counters.simulatedFixedSteps);
  CHECK(session.stats()->simulatedUpdates == counters.simulatedUpdates);
  CHECK(probe()["frames"].get<int>() > measured["frames"].get<int>());
  CHECK(probe()["fixed"] == measured["fixed"]);
  CHECK(probe()["updates"] == measured["updates"]);
  CHECK(probe()["time"] == measured["time"]);
  const auto files = f.project.files().value();
  const auto source = files.readText("scripts/progress.ts").value();
  REQUIRE(files.writeTextAtomic("scripts/progress.ts", source + "\n"));
  auto reloaded = Tests::awaitCompilation([&] { return session.pollScripts(); });
  REQUIRE(reloaded->committed);
  CHECK(clock.snapshot()->paused);
  const auto uiRevision = session.hud()->snapshot()->revision;
  driver.frame({Platform::Key::D});
  CHECK(entity(session, "Player").transform.position == before.position);
  CHECK(session.hud()->snapshot()->revision == uiRevision);
  CHECK(session.stats()->simulatedFixedSteps == counters.simulatedFixedSteps);

  driver.frame({Platform::Key::F6});
  CHECK(std::ranges::any_of(session.hud()->snapshot()->labels, [](const auto& layer) {
    return layer.key == "checkpoint-error" && layer.value.visible
           && layer.value.text.find("land before saving") != std::string::npos;
  }));
  CHECK(clock.snapshot()->paused);
  driver.frame({Platform::Key::M});
  CHECK_FALSE(clock.snapshot()->paused);
  CHECK(session.pointerCaptureRequested().value());

  for (int i = 0; i < 120; ++i) {
    driver.frame();
  }

  driver.frame({Platform::Key::M});
  driver.frame({Platform::Key::F6});
  awaitCheckpoint(session, driver);
  auto stored = session.gameData()->perform(R"({"command":"read-save","slot":"relay-checkpoint"})");
  REQUIRE_MESSAGE(stored, (stored ? "" : stored.error().message));
  auto progress = session.gameData().value();
  const auto saved = awaitSave(progress, Json::parse(*stored))["data"]["relayCheckpoint"]["feet"];
  auto control = Project::PlayerControl::create(session).value();
  const auto scope = session.stats()->location.scope;
  CHECK_FALSE(control.perform(Json{{"command", "clock"},
                                   {"scope", std::to_string(scope + 1)},
                                   {"value", {{"command", "set-paused"}, {"paused", false}}}}
                                  .dump()));
  REQUIRE(control.perform(Json{{"command", "clock"},
                               {"scope", std::to_string(scope)},
                               {"value", {{"command", "set-paused"}, {"paused", false}}}}
                              .dump()));

  for (int i = 0; i < 30; ++i) {
    driver.frame({Platform::Key::D});
  }

  driver.frame({Platform::Key::M});
  const auto paused = session.stats().value();
  driver.frame({Platform::Key::F7});
  awaitCheckpoint(session, driver);
  CHECK(clock.snapshot()->paused);
  const auto restored = entity(session, "Player").transform.position;
  CHECK(restored.x == doctest::Approx(saved["x"].get<float>()).epsilon(.001));
  CHECK(restored.y == doctest::Approx(saved["y"].get<float>()).epsilon(.001));
  CHECK(restored.z == doctest::Approx(saved["z"].get<float>()).epsilon(.001));
  CHECK(session.stats()->simulatedFixedSteps == paused.simulatedFixedSteps);
  CHECK(session.stats()->simulatedUpdates == paused.simulatedUpdates);
  const auto request = session.requestScene("scenes/main.dotscene").value();
  report(session.update(0));
  report(Tests::awaitScene(session, request));
  CHECK(session.stats()->location.scope != scope);
  CHECK_FALSE(session.gameClock()->snapshot()->paused);
  CHECK(session.stats()->simulatedFixedSteps == 0);
  CHECK_FALSE(clock.perform(R"({"command":"set-paused","paused":false})"));
  auto current = session.gameClock().value();
  REQUIRE(current.perform(R"({"command":"set-paused","paused":true})"));
  report(session.close());
  CHECK(current.snapshot()->paused);
  CHECK_FALSE(current.perform(R"({"command":"set-paused","paused":false})"));
}

TEST_CASE("prism.character_moves_relative_to_acknowledged_camera_and_blocks_at_walls") {
  Fixture fixture;
  auto session = fixture.session();
  report(session.start());
  CHECK(session.pointerCaptureRequested().value());
  Driver driver(session);

  for (int frame = 0; frame < 30; ++frame) {
    driver.frame({Platform::Key::W});
  }

  const auto forward = entity(session, "Player").transform.position;
  CHECK(forward.x == doctest::Approx(-3));
  CHECK(forward.z < 0.8F);
  driver.frame({}, -523.5988F, 0, false);
  const auto unchanged = entity(session, "Camera").transform.rotation;
  CHECK(unchanged.y == doctest::Approx(0));
  driver.frame({}, -523.5988F, 0, true);
  CHECK(entity(session, "Camera").transform.rotation.y > 0.7F);

  for (int frame = 0; frame < 120; ++frame) {
    driver.frame({Platform::Key::W});
  }

  const auto stopped = entity(session, "Player").transform.position;
  CHECK(stopped.x < -6.9F);
  CHECK(stopped.x > -7.1F);
  CHECK(stopped.y == doctest::Approx(0).epsilon(0.02));
  CHECK(fixture.project.loadStartup()->entityCount().value()
        == session.scene().value()->entityCount().value());
  report(session.close());
}

TEST_CASE("prism.checkpoint_restores_native_player_camera_and_survives_a_fresh_game") {
  Fixture f;
  REQUIRE(f.storage.files()->createDirectories("saves"));
  const auto saves = Core::FileSystem::open(f.storage.files()->root() / "saves").value();
  const auto authoring = f.project.files()->readText("scenes/main.dotscene").value();
  auto session = f.session(false, saves);
  report(session.start());
  Driver driver(session);

  for (int frame = 0; frame < 25; ++frame) {
    driver.frame({Platform::Key::W}, 2, -1, true);
  }

  driver.frame();
  const auto feet = entity(session, "Player").transform.position;
  const auto camera = entity(session, "Camera").transform;
  driver.publish({Platform::Key::F6});
  report(session.update(0));
  awaitCheckpoint(session, driver);
  auto data = session.gameData().value();
  auto snapshot = Json::parse(data.perform(R"({"command":"snapshot"})").value());
  CHECK(snapshot["data"]["relayProgressStatus"] == "Checkpoint saved");
  const auto bytes = saves.readText("relay-checkpoint.dotsave").value();
  const auto saved = snapshot["data"]["relayCheckpoint"];
  CHECK(saved["feet"]["z"].get<float>() == feet.z);

  for (int frame = 0; frame < 25; ++frame) {
    driver.frame({Platform::Key::D, Platform::Key::Space}, -1, 1, true);
  }

  CHECK(entity(session, "Player").transform.position != feet);
  driver.publish({Platform::Key::F7});
  report(session.update(.000001));
  awaitCheckpoint(session, driver);
  CHECK(entity(session, "Player").transform.position == feet);
  const auto restored = entity(session, "Camera").transform;
  CHECK(restored.position.x == doctest::Approx(camera.position.x));
  CHECK(restored.position.y == doctest::Approx(camera.position.y));
  CHECK(restored.position.z == doctest::Approx(camera.position.z));
  CHECK(restored.rotation == camera.rotation);
  driver.frame();
  CHECK(entity(session, "Player").transform.position.x == doctest::Approx(feet.x));
  CHECK(entity(session, "Player").transform.position.y == doctest::Approx(feet.y).epsilon(.01));
  CHECK(entity(session, "Player").transform.position.z == doctest::Approx(feet.z));
  CHECK(
      Json::parse(data.perform(R"({"command":"snapshot"})").value())["data"]["relayProgressStatus"]
      == "Checkpoint restored");
  report(session.close());
  auto fresh = f.session(false, saves);
  report(fresh.start());
  CHECK(entity(fresh, "Player").transform.position != feet);
  Driver resumed(fresh);
  resumed.publish({Platform::Key::F7});
  report(fresh.update(0));
  awaitCheckpoint(fresh, resumed);
  CHECK(entity(fresh, "Player").transform.position == feet);
  resumed.frame();
  CHECK(entity(fresh, "Player").transform.position.z == doctest::Approx(feet.z));
  CHECK(saves.readText("relay-checkpoint.dotsave").value() == bytes);
  report(fresh.close());
  CHECK(f.project.files()->readText("scenes/main.dotscene").value() == authoring);
}

TEST_CASE("prism.checkpoint_failures_keep_pose_slot_and_previous_checkpoint_data") {
  Fixture f;
  REQUIRE(f.storage.files()->createDirectories("saves"));
  const auto saves = Core::FileSystem::open(f.storage.files()->root() / "saves").value();
  auto session = f.session(false, saves);
  report(session.start());
  Driver driver(session);
  auto data = session.gameData().value();
  auto press = [&](Platform::Key key) {
    driver.publish();
    report(session.update(0));
    driver.publish({key});
    report(session.update(0));
    awaitCheckpoint(session, driver);
  };
  auto status
      = [&] { return Json::parse(data.perform(R"({"command":"snapshot"})").value())["data"]; };
  press(Platform::Key::F7);
  CHECK(status()["relayProgressStatus"].get<std::string>().starts_with("Checkpoint unavailable:"));
  CHECK(session.stats()->state == Project::SessionState::Running);
  press(Platform::Key::F6);
  const auto original = saves.readText("relay-checkpoint.dotsave").value();
  const auto valid = status()["relayCheckpoint"];
  const auto pose = entity(session, "Player").transform;
  const auto camera = entity(session, "Camera").transform;
  auto samePose = [](const World::Transform& left, const World::Transform& right) {
    return left.position == right.position && left.rotation == right.rotation
           && left.scale == right.scale;
  };
  std::vector<Json> invalids{nullptr, "bad", Json::array()};

  for (const auto& [key, value] : std::vector<std::pair<std::string, Json>>{
           {"format", "other"},
           {"path", "scenes/other.dotscene"},
           {"yawDegrees", "0"},
           {"yawDegrees", 181},
           {"feet", {{"x", 0}, {"y", 0}, {"z", 1000001}}},
           {"camera", {{"yaw", 0}, {"pitch", 2}, {"distance", 3}, {"boom", 3}}},
           {"extra", true}}) {
    auto invalid = valid;
    invalid[key] = value;
    invalids.push_back(std::move(invalid));
  }

  for (const auto& invalid : invalids) {
    REQUIRE(data.perform(
        Json{{"command", "set"}, {"key", "relayCheckpoint"}, {"value", invalid}}.dump()));
    awaitSave(
        data,
        Json::parse(data.perform(R"({"command":"save","slot":"relay-checkpoint","overwrite":true})")
                        .value()));
    press(Platform::Key::F7);
    CHECK(samePose(entity(session, "Player").transform, pose));
    CHECK(samePose(entity(session, "Camera").transform, camera));
    CHECK(status()["relayCheckpoint"] == invalid);
    CHECK(
        status()["relayProgressStatus"].get<std::string>().starts_with("Checkpoint unavailable:"));
  }

  REQUIRE(saves.writeTextAtomic("relay-checkpoint.dotsave", "damaged"));
  press(Platform::Key::F7);
  CHECK(samePose(entity(session, "Player").transform, pose));
  CHECK(saves.readText("relay-checkpoint.dotsave").value() == "damaged");
  REQUIRE(saves.writeTextAtomic("relay-checkpoint.dotsave", original));
  REQUIRE(
      data.perform(Json{{"command", "set"}, {"key", "relayCheckpoint"}, {"value", valid}}.dump()));
  driver.frame({Platform::Key::Space});
  press(Platform::Key::F6);
  CHECK(saves.readText("relay-checkpoint.dotsave").value() == original);
  CHECK(status()["relayProgressStatus"].get<std::string>().find("Land before saving")
        != std::string::npos);
  CHECK(status()["relayCheckpoint"] == valid);
  report(session.close());
  auto unavailable = f.session();
  report(unavailable.start());
  Driver noDisk(unavailable);
  noDisk.publish({Platform::Key::F6});
  report(unavailable.update(0));
  auto absent = Json::parse(unavailable.gameData()->perform(R"({"command":"snapshot"})").value());
  CHECK_FALSE(absent["data"].contains("relayCheckpoint"));
  CHECK(absent["data"]["relayProgressStatus"].get<std::string>().starts_with(
      "Checkpoint unavailable:"));
  REQUIRE(unavailable.gameData()->perform(
      Json{{"command", "set"}, {"key", "relayCheckpoint"}, {"value", valid}}.dump()));
  noDisk.publish();
  report(unavailable.update(0));
  noDisk.publish({Platform::Key::F6});
  report(unavailable.update(0));
  absent = Json::parse(unavailable.gameData()->perform(R"({"command":"snapshot"})").value());
  CHECK(absent["data"]["relayCheckpoint"] == valid);
  report(unavailable.close());
}

TEST_CASE("prism.diagonal_intent_has_the_same_speed_and_native_sprint_is_faster") {
  auto displacement = [](bool diagonal, bool sprint) {
    Fixture fixture;
    auto session = fixture.session();
    report(session.start());
    Driver driver(session);
    const auto origin = entity(session, "Player").transform.position;

    for (int frame = 0; frame < 25; ++frame) {
      if (sprint) {
        driver.frame({Platform::Key::D, Platform::Key::LeftShift});
      } else if (diagonal) {
        driver.frame({Platform::Key::D, Platform::Key::W});
      } else {
        driver.frame({Platform::Key::D});
      }
    }

    const auto final = entity(session, "Player").transform.position;
    report(session.close());
    return std::hypot(final.x - origin.x, final.z - origin.z);
  };
  const auto straight = displacement(false, false);
  CHECK(displacement(true, false) == doctest::Approx(straight).epsilon(0.02));
  CHECK(displacement(false, true) > straight * 1.7F);
}

TEST_CASE("prism.jump_edge_is_consumed_once_and_native_rig_plays_jump_air_land") {
  Fixture fixture;
  observe(fixture);
  auto session = fixture.session();
  report(session.start());
  Driver driver(session);

  for (int frame = 0; frame < 10; ++frame) {
    driver.frame();
  }

  CHECK(observation(session)["ground"] == "grounded");
  CHECK(observation(session)["clip"] == 0);
  driver.publish({Platform::Key::Space});

  for (int step = 0; step < 6; ++step) {
    report(session.fixedUpdate());
  }

  report(session.update(0.1));
  CHECK(entity(session, "Player").transform.position.y > 0.4F);
  std::set<int> clips{observation(session)["clip"].get<int>()};
  float peak{};

  for (int frame = 0; frame < 150; ++frame) {
    driver.frame({Platform::Key::Space});
    peak = std::max(peak, entity(session, "Player").transform.position.y);
    clips.insert(observation(session)["clip"].get<int>());
  }

  CHECK(peak > 1.5F);
  CHECK(peak < 2.0F);
  CHECK(clips.contains(4));
  CHECK(clips.contains(5));
  CHECK(clips.contains(6));
  CHECK(observation(session)["ground"] == "grounded");
  CHECK(entity(session, "Player").transform.position.y == doctest::Approx(0).epsilon(0.02));
  report(session.close());
}

TEST_CASE("prism.jump_press_between_fixed_steps_is_buffered_without_repeating") {
  Fixture fixture;
  auto session = fixture.session();
  report(session.start());
  Driver driver(session);
  driver.frame();
  driver.publish({Platform::Key::Space});
  report(session.update(1.0 / 120));
  driver.publish();
  report(session.fixedUpdate());
  report(session.update(1.0 / 60));
  CHECK(entity(session, "Player").transform.position.y > 0.05F);
  report(session.close());
}

TEST_CASE("prism.hot_reload_keeps_native_rig_character_motion_and_orbit") {
  Fixture fixture;
  auto session = fixture.session(true);
  report(session.start());
  Driver driver(session);
  driver.frame({}, -100, 25, true);

  for (int frame = 0; frame < 10; ++frame) {
    driver.frame({Platform::Key::W});
  }

  const auto before = entity(session, "Player").transform.position;
  const auto rotation = entity(session, "Camera").transform.rotation;
  const auto scope = session.scene().value()->id();
  auto files = fixture.project.files().value();
  auto source = files.readText("scripts/camera.ts").value();
  REQUIRE(files.writeTextAtomic("scripts/camera.ts", source + "\n"));
  auto completed = Dot::Tests::awaitCompilation([&] { return session.pollScripts(); });
  report(session.update(0));
  REQUIRE(completed->committed);
  REQUIRE(completed->reload);
  CHECK(completed->reload->migrated == 1);
  CHECK(session.scene().value()->id() == scope);
  CHECK(entity(session, "Player").transform.position == before);
  CHECK(entity(session, "Camera").transform.rotation == rotation);
  CHECK(session.pointerCaptureRequested().value());
  driver.frame({Platform::Key::W});
  CHECK(entity(session, "Player").transform.position.z < before.z);
  CHECK(entity(session, "Player").character.has_value());
  report(session.close());
}

TEST_CASE("prism.inspection_masks_moving_rig_and_survives_paused_reload") {
  Fixture fixture;
  const auto files = fixture.project.files().value();
  const auto currentSource = files.readText("scripts/player.ts").value();
  const auto uiBytes = files.readText("assets/interface.dotui").value();
  REQUIRE(files.writeTextAtomic("scripts/player.ts",
                                currentSource + "\nexport const reviewGeneration = 1;\n"));
  auto session = fixture.session(true);
  report(session.start());
  Driver driver(session);
  const auto model = entity(session, "Surveyor").id;
  const auto start = entity(session, "Player").transform.position;

  for (int frame = 0; frame < 30; ++frame) {
    driver.frame({Platform::Key::W});
  }

  driver.frame({Platform::Key::W, Platform::Key::E});

  for (int frame = 0; frame < 20; ++frame) {
    driver.frame({Platform::Key::W});
  }

  CHECK(entity(session, "Player").transform.position.z < start.z);
  const auto moving = session.animationState(model).value();
  REQUIRE(moving.layers.size() == 1);
  CHECK(moving.layers[0].mode == Animation::LayerMode::Additive);
  CHECK(moving.layers[0].referenceTime == 0);
  CHECK(moving.layers[0].playback.active);
  CHECK(moving.layers[0].playback.playing);
  CHECK(moving.layers[0].playback.time > .3);
  CHECK(moving.layers[0].playback.weight == 1);
  driver.frame({Platform::Key::M});
  REQUIRE(session.gameClock()->snapshot()->paused);
  const auto paused = session.animationState(model).value();
  const auto nativeUi = session.hud()->perform(R"({"command":"ui-snapshot"})").value();
  const auto pose = Json::parse(World::encodeScene(*session.scene().value()).value());
  REQUIRE(files.writeTextAtomic("scripts/player.ts", currentSource + "\n"));
  auto reloaded = Tests::awaitCompilation([&] { return session.pollScripts(); });
  REQUIRE(reloaded->committed);
  driver.frame({Platform::Key::W});
  const auto labels = session.hud()->snapshot()->labels;
  const auto controls
      = std::ranges::find(labels, std::string{"controls"}, &Graphics::TextLayer::key);
  REQUIRE(controls != labels.end());
  CHECK(controls->value.text.find("E inspect") != std::string::npos);
  CHECK(files.readText("assets/interface.dotui").value() == uiBytes);
  CHECK(Json::parse(session.hud()->perform(R"({"command":"ui-snapshot"})").value())["nodes"]
        == Json::parse(nativeUi)["nodes"]);
  CHECK(session.animationState(model)->layers == paused.layers);
  CHECK(Json::parse(World::encodeScene(*session.scene().value()).value()) == pose);
  REQUIRE(session.controlAnimationLayer(model, "Inspection", Animation::StopLayer{}));
  const auto stopped = Json::parse(World::encodeScene(*session.scene().value()).value());
  std::set<std::string> changed;

  for (std::size_t i = 0; i < pose["entities"].size(); ++i) {
    const auto& before = pose["entities"][i];
    const auto& after = stopped["entities"][i];

    if (before["transform"] != after["transform"]) {
      changed.insert(before["name"].get<std::string>());
      CHECK(before["transform"]["position"] == after["transform"]["position"]);
      CHECK(before["transform"]["scale"] == after["transform"]["scale"]);
    }
  }

  CHECK(changed == std::set<std::string>{"head", "upper_arm.L", "upper_arm.R"});
  driver.frame({Platform::Key::M});

  for (int frame = 0; frame < 180; ++frame) {
    driver.frame();
  }

  driver.frame({Platform::Key::E});

  for (int frame = 0; frame < 180; ++frame) {
    driver.frame();
  }

  const auto finished = session.animationState(model).value();
  CHECK_FALSE(finished.layers[0].playback.active);
  CHECK(finished.layers[0].playback.time == 0);
  report(session.close());
}

TEST_CASE("prism.paused_single_step_advances_airborne_character_and_layer_once_after_reload") {
  Fixture fixture;
  auto session = fixture.session(true);
  report(session.start());
  Driver driver(session);
  driver.frame({Platform::Key::E, Platform::Key::Space});
  driver.frame();
  driver.frame({Platform::Key::M});
  auto clock = session.gameClock().value();
  REQUIRE(clock.snapshot()->paused);
  const auto model = entity(session, "Surveyor").id;
  const auto before = session.animationState(model).value();
  REQUIRE(before.layers.front().playback.active);
  const auto position = entity(session, "Player").transform.position;
  REQUIRE(position.y > 0);
  const auto counters = session.stats().value();
  driver.frame({Platform::Key::N});
  const auto stepped = session.animationState(model).value();
  CHECK(clock.snapshot()->paused);
  CHECK(clock.snapshot()->step->state == Script::GameStepState::Completed);
  CHECK(session.stats()->simulatedFixedSteps == counters.simulatedFixedSteps + 1);
  CHECK(session.stats()->simulatedUpdates == counters.simulatedUpdates + 1);
  CHECK(entity(session, "Player").transform.position.y > position.y);
  CHECK(stepped.layers.front().playback.time
        == doctest::Approx(before.layers.front().playback.time + 1.0 / 60));
  const auto frozen = World::encodeScene(*session.scene().value()).value();
  driver.frame({Platform::Key::N});
  driver.frame();
  CHECK(World::encodeScene(*session.scene().value()).value() == frozen);
  REQUIRE(clock.perform(R"({"command":"step"})"));
  const auto files = fixture.project.files().value();
  REQUIRE(files.writeTextAtomic("scripts/player.ts",
                                files.readText("scripts/player.ts").value() + "\n"));
  REQUIRE(Tests::awaitCompilation([&] { return session.pollScripts(); })->committed);
  CHECK(clock.snapshot()->step->state == Script::GameStepState::Pending);
  driver.frame();
  CHECK(clock.snapshot()->step->state == Script::GameStepState::Completed);
  CHECK(clock.snapshot()->step->id == 2);
  CHECK(session.stats()->simulatedFixedSteps == counters.simulatedFixedSteps + 2);
  CHECK(session.animationState(model)->layers.front().playback.time
        == doctest::Approx(before.layers.front().playback.time + 2.0 / 60));
  report(session.close());
}

TEST_CASE("prism.authored_steps_climb_with_native_support_and_tall_rise_requires_jump") {
  Fixture fixture;
  auto authored = fixture.project.loadStartup().value();
  const auto entities = authored.entities().value();
  const auto actor = std::ranges::find(entities, "Player", &World::EntityInfo::name);
  REQUIRE(actor != entities.end());
  auto pose = actor->transform;
  pose.position = {2, 0, -1.8F};
  REQUIRE(authored.setTransform(actor->id, pose));
  REQUIRE(fixture.project.saveStartup(authored));
  auto session = fixture.session();
  report(session.start());
  Driver driver(session);

  for (int frame = 0; frame < 90; ++frame) {
    driver.frame({Platform::Key::W});
  }

  const auto blocked = entity(session, "Player").transform.position;
  CHECK(blocked.z < -4.9F);
  CHECK(blocked.z > -5.5F);
  CHECK(blocked.y == doctest::Approx(.54).epsilon(.08));
  driver.frame({Platform::Key::W, Platform::Key::Space});

  for (int frame = 0; frame < 8; ++frame) {
    driver.frame({Platform::Key::W});
  }

  for (int frame = 0; frame < 80; ++frame) {
    driver.frame();
  }

  const auto landed = entity(session, "Player").transform.position;
  CHECK(landed.y == doctest::Approx(1.2).epsilon(.05));
  report(session.close());
}

TEST_CASE("prism.authored_surfaces_have_real_normal_frames_and_local_texture_inputs") {
  Fixture fixture;
  auto scene = fixture.project.loadStartup().value();
  auto files = fixture.project.files().value();
  std::map<std::string, Graphics::MeshSurface> surfaces;
  std::map<std::string, Assets::MaterialDescription> materials;
  REQUIRE(scene.environment()->has_value());
  REQUIRE(scene.occlusion()->has_value());
  auto panorama = Assets::loadHdrImage(files, scene.environment()->value().asset);
  REQUIRE_MESSAGE(panorama, (panorama ? "" : panorama.error().message));
  unsigned shadowed{};

  for (const auto& entity : scene.entities().value()) {
    if (entity.light && entity.light->shadows) {
      ++shadowed;
    }

    if (!entity.mesh) {
      continue;
    }

    INFO(entity.name);
    REQUIRE(entity.material);
    REQUIRE(entity.mesh->kind == World::MeshKind::Asset);
    const auto& path = entity.mesh->asset;

    if (!surfaces.contains(path)) {
      auto mesh = Assets::loadMesh(files, path);
      REQUIRE_MESSAGE(mesh, (mesh ? "" : mesh.error().message));
      surfaces.emplace(path, Graphics::meshSurface(mesh->data).value());
    }

    const auto& material = entity.material->asset;

    if (!materials.contains(material)) {
      auto loaded = Assets::loadMaterial(files, material);
      REQUIRE_MESSAGE(loaded, (loaded ? "" : loaded.error().message));
      materials.emplace(material, std::move(*loaded));
    }

    CHECK(surfaces.at(path).hasNormals);

    if (materials.at(material).normalTexture) {
      CHECK_MESSAGE(surfaces.at(path).hasNormalMapFrame, path);
    }
  }

  CHECK(shadowed == 4);
  CHECK(surfaces.size() >= 10);
  CHECK(materials.size() >= 8);
  unsigned textured{};

  for (const auto& [path, material] : materials) {
    if (!material.baseColorTexture) {
      continue;
    }

    INFO(path);
    ++textured;
    REQUIRE(material.metallicRoughnessTexture);
    REQUIRE(material.normalTexture);
    REQUIRE(material.occlusionTexture);
    CHECK(material.baseColorTexture->sampling.colorSpace == Graphics::ColorSpace::Srgb);
    CHECK(material.normalTexture->sampling.colorSpace == Graphics::ColorSpace::Linear);
    CHECK(material.metallicRoughnessTexture->sampling.colorSpace == Graphics::ColorSpace::Linear);

    for (const auto* source : {&*material.baseColorTexture, &*material.metallicRoughnessTexture,
                               &*material.normalTexture, &*material.occlusionTexture}) {
      auto image = Assets::loadImage(files, source->path);
      REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
      const auto size = path.starts_with("assets/surveyor/") ? 2048U : 1024U;
      CHECK(image->width == size);
      CHECK(image->height == size);
    }
  }

  CHECK(textured >= 3);
}

TEST_CASE("prism.surveyor_retains_textured_armor_eight_slot_skinning_and_seven_roles") {
  auto project = Project::Project::open(DOT_EXAMPLE_SOURCE).value();
  auto files = project.files().value();
  auto scene = project.loadStartup().value();
  const auto models = scene.findEntities("Surveyor").value();
  REQUIRE(models.size() == 1);
  auto model = scene.entity(models[0]).value();
  REQUIRE(model.animation);
  CHECK(model.animation->enabled);
  REQUIRE(model.animation->clips.size() == 8);
  CHECK(model.animation->clips.back() == "assets/actions/inspection.dotanimation");
  REQUIRE(model.animation->layers.size() == 1);
  CHECK(model.animation->layers.front().name == "Inspection");
  CHECK(model.animation->layers.front().clip == 7);
  CHECK(model.animation->layers.front().mode == Animation::LayerMode::Additive);
  CHECK(model.animation->layers.front().referenceTime == 0);
  CHECK(model.animation->layers.front().mask.size() == 3);
  CHECK_FALSE(model.animation->layers.front().looping);
  CHECK_FALSE(model.animation->layers.front().playOnStart);

  for (const auto* name : {"head", "upper_arm.L", "upper_arm.R"}) {
    const auto joints = scene.findEntities(name).value();
    REQUIRE(joints.size() == 1);
    const auto binding = std::ranges::find(model.animation->targets, joints.front().value,
                                           &Animation::Target::entity);
    REQUIRE(binding != model.animation->targets.end());
    CHECK(std::ranges::any_of(model.animation->layers.front().mask, [&](const auto& channel) {
      return channel.node == binding->node && channel.path == Animation::Path::Rotation
             && channel.weight == 1;
    }));
  }

  const std::array names{"Idle_Loop",  "Walk_Loop", "Jog_Fwd_Loop", "Sprint_Loop",
                         "Jump_Start", "Jump_Loop", "Jump_Land"};

  for (std::size_t index = 0; index < names.size(); ++index) {
    auto clip = Animation::loadClip(files, model.animation->clips[index]);
    REQUIRE_MESSAGE(clip, (clip ? "" : clip.error().message));
    CHECK(clip->description().value()->name == names[index]);
    CHECK(clip->duration().value() > 0);
    auto sample = clip->sample(clip->duration().value() / 2);
    REQUIRE(sample);
    CHECK_FALSE(sample->empty());
  }

  unsigned skinned{};
  bool extendedWeights{};
  std::set<std::string> textures;

  for (const auto& entity : scene.entities().value()) {
    if (!entity.mesh || !entity.mesh->asset.starts_with("assets/surveyor/")) {
      continue;
    }

    ++skinned;
    REQUIRE(entity.skin);
    REQUIRE(entity.material);
    auto skin = Animation::loadSkin(files, entity.skin->asset);
    auto mesh = Assets::loadMesh(files, entity.mesh->asset);
    REQUIRE(skin);
    REQUIRE(mesh);
    const auto* data = skin->description().value();
    CHECK(data->inverseBindMatrices.size() == 63);
    CHECK(entity.skin->joints.size() == 63);
    CHECK(data->vertices.size() == mesh->data.vertices.size());
    extendedWeights = extendedWeights
                      || std::ranges::any_of(
                          data->vertices, [](const auto& vertex) { return vertex.weights[4] > 0; });
    auto material = Assets::loadMaterial(files, entity.material->asset).value();
    REQUIRE(material.baseColorTexture);
    REQUIRE(material.normalTexture);
    REQUIRE(material.metallicRoughnessTexture);
    REQUIRE(material.occlusionTexture);
    REQUIRE(material.emissiveTexture);
    CHECK(material.occlusionTexture->path == material.metallicRoughnessTexture->path);
    CHECK(material.emissive.x > 1);

    for (const auto* texture : {&*material.baseColorTexture, &*material.normalTexture,
                                &*material.metallicRoughnessTexture, &*material.emissiveTexture}) {
      textures.insert(texture->path);
    }
  }

  CHECK(skinned == 3);
  CHECK(extendedWeights);
  REQUIRE(textures.size() == 4);

  for (const auto& path : textures) {
    auto image = Assets::loadImage(files, path);
    REQUIRE(image);
    CHECK(image->width == 2048);
    CHECK(image->height == 2048);
  }
}

namespace {
  auto control(Project::PlayerControl& player, Json request) -> Json {
    auto result = player.perform(request.dump());
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return Json::parse(*result);
  }

  auto inputRequest(std::uint32_t frames, Json keys = Json::array({"W"})) -> Json {
    return {{"frames", frames},  {"keys", std::move(keys)}, {"buttons", Json::array()},
            {"pointer", {0, 0}}, {"pointerDelta", {0, 0}},  {"wheel", {0, 0}},
            {"text", ""}};
  }
}  // namespace

TEST_CASE("player_control.actual_game_input_has_owned_native_frames_and_release_edges") {
  Fixture fixture;
  auto session = fixture.session();
  auto player = Project::PlayerControl::create(session).value();
  auto initial = control(player, {{"command", "inspect"}});
  CHECK(initial["player"]["state"] == "preparing");
  CHECK_FALSE(player.perform(
      Json{{"command", "input"}, {"scope", initial["player"]["scope"]}, {"value", inputRequest(30)}}
          .dump()));
  CHECK_FALSE(
      player.perform(R"({"command":"game-data","value":{"command":"set","key":"x","value":1}})"));
  CHECK(control(player,
                {{"command", "game-data"}, {"value", {{"command", "snapshot"}}}})["result"]["data"]
        == Json::object());
  report(session.start());
  const auto scope = control(player, {{"command", "inspect"}})["player"]["scope"];
  control(player, {{"command", "input"}, {"scope", scope}, {"value", inputRequest(30)}});
  const auto origin = entity(session, "Player").transform.position;
  const auto camera = entity(session, "Camera").transform.rotation;

  for (int frame = 0; frame < 30; ++frame) {
    auto input = player.next(Platform::InputState{}).value();
    CHECK(input.key(Platform::Key::W).pressed == (frame == 0));
    good(session.publish(input, false));
    report(session.fixedUpdate());
    report(session.update(1.0 / 60));
  }

  CHECK(entity(session, "Player").transform.position.z < origin.z - 1);
  CHECK(entity(session, "Camera").transform.rotation == camera);
  auto status = control(player, {{"command", "inspect"}});
  CHECK(status["player"]["input"]["active"] == true);
  CHECK(status["player"]["input"]["remaining"] == 0);
  CHECK_FALSE(player.perform(
      Json{{"command", "input"}, {"scope", scope}, {"value", inputRequest(1)}}.dump()));
  const auto released = player.next(Platform::InputState{}).value();
  CHECK(released.key(Platform::Key::W).released);
  CHECK_FALSE(released.key(Platform::Key::W).down);
  CHECK(control(player, {{"command", "inspect"}})["player"]["input"]["active"] == false);
  const auto native = player.next(released).value();
  CHECK(native.key(Platform::Key::W).released);
  report(session.close());
}

TEST_CASE(
    "player_control.scene_replacement_invalidates_old_input_scope_and_close_only_requests_exit") {
  Fixture fixture;
  auto session = fixture.session();
  report(session.start());
  auto player = Project::PlayerControl::create(session).value();
  const auto original = control(player, {{"command", "inspect"}});
  const auto scope = original["player"]["scope"];
  control(player, {{"command", "input"}, {"scope", scope}, {"value", inputRequest(600)}});
  control(player, {{"command", "cancel-input"}, {"scope", scope}});
  CHECK_FALSE(player.next(Platform::InputState{})->key(Platform::Key::W).pressed);
  const auto requested = control(
      player, {{"command", "request-scene"}, {"scope", scope}, {"path", "scenes/main.dotscene"}});
  report(session.update(0));
  report(Dot::Tests::awaitScene(session, std::stoull(requested["request"].get<std::string>())));
  const auto after = control(player, {{"command", "inspect"}});
  CHECK(after["player"]["scope"] != scope);
  CHECK(after["player"]["input"]["active"] == false);
  CHECK(after["player"]["retiringAudio"] == false);
  CHECK(control(player, {{"command", "scene-status"},
                         {"request", requested["request"]}})["transition"]["state"]
        == "succeeded");
  CHECK_FALSE(player.perform(
      Json{{"command", "input"}, {"scope", scope}, {"value", inputRequest(1)}}.dump()));
  CHECK_FALSE(player.perform(Json{{"command", "cancel-input"}, {"scope", scope}}.dump()));
  const auto dataBeforeClose = control(
      player, {{"command", "game-data"}, {"value", {{"command", "snapshot"}}}})["result"]["data"];
  CHECK(control(player, {{"command", "close"}})["closing"] == true);
  CHECK(player.closing().value());
  CHECK(session.stats()->state == Project::SessionState::Running);
  CHECK_FALSE(player.next(Platform::InputState{}));
  CHECK_FALSE(player.perform(R"({"command":"game-data","value":{"command":"list-saves"}})"));
  CHECK(control(player,
                {{"command", "game-data"}, {"value", {{"command", "snapshot"}}}})["result"]["data"]
        == dataBeforeClose);
  report(session.close());
  CHECK(control(player, {{"command", "inspect"}})["player"]["state"] == "closed");
}

TEST_CASE("player_control.strict_requests_owner_moves_and_bounded_failure_observations") {
  Fixture fixture;
  auto session = fixture.session();
  report(session.start());
  auto original = Project::PlayerControl::create(session).value();
  auto player = std::move(original);
  CHECK_FALSE(original.perform(R"({"command":"inspect"})"));
  const auto before = control(player, {{"command", "inspect"}});

  for (const auto* request :
       {"[]", R"({"command":"inspect","extra":1})", R"({"command":"inspect","command":"close"})",
        R"({"command":"unknown"})", R"({"command":"input","scope":"1"})"}) {
    CHECK_FALSE(player.perform(request));
    CHECK(control(player, {{"command", "inspect"}}) == before);
  }

  bool rejected{};
  std::thread other([&] {
    rejected = !player.perform(R"({"command":"inspect"})") && !player.next(Platform::InputState{})
               && !player.closing();
  });
  other.join();
  CHECK(rejected);
  Script::ScriptReport failure;
  const auto id = entity(session, "Player").id;
  failure.failures.push_back({id,
                              Script::ScriptPhase::Update,
                              {Core::ErrorCode::Io, std::string(8191, 'x') + "消息", {}}});

  for (int index = 0; index < 35; ++index) {
    good(player.record(failure));
  }

  const auto observed = control(player, {{"command", "inspect"}});
  CHECK(observed["player"]["failureCount"] == "35");
  CHECK(observed["player"]["diagnostics"].size() == 32);
  CHECK(observed["player"]["diagnostics"][0]["sequence"] == "4");
  CHECK(observed["player"]["diagnostics"][0]["truncated"] == true);
  CHECK(observed["player"]["diagnostics"][0]["error"]["message"].get<std::string>().size() == 8191);
  CHECK(before["player"]["diagnostics"].empty());
  report(session.close());
}

TEST_CASE("player_control.game_data_survives_real_session_restart_with_explicit_storage") {
  Fixture fixture;
  good(fixture.storage.files()->createDirectories("user-data"));
  const auto root = Core::FileSystem::open(fixture.storage.files()->root() / "user-data").value();
  const auto create = [&] {
    auto description = fixture.project.description().value();
    description.hotReload = false;
    good(fixture.project.save(description));
    Project::GameSessionConfig config;
    config.progress.saves = root;
    auto session = Project::GameSession::create(fixture.project, fixture.compiler(), config);
    good(session);
    report(session->start());
    return std::move(*session);
  };
  const auto completed = [&](Project::PlayerControl& player, Json response, bool success = true) {
    auto request = response["result"];
    request["command"] = "take-save";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    for (;;) {
      auto value = control(player, {{"command", "game-data"}, {"value", request}})["result"];

      if (value["state"] != "pending" && value["state"] != "running") {
        CHECK((value["state"] == "succeeded") == success);
        return success ? Json{{"result", value["result"]}} : value;
      }

      REQUIRE(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  Json initialData;

  {
    auto session = create();
    auto player = Project::PlayerControl::create(session).value();
    initialData = control(
        player, {{"command", "game-data"}, {"value", {{"command", "snapshot"}}}})["result"]["data"];
    control(player,
            {{"command", "game-data"},
             {"value",
              {{"command", "set"},
               {"key", "checkpoint"},
               {"value", {{"position", {-3, 0, -1}}, {"scene", "scenes/main.dotscene"}}}}}});
    const auto saved = completed(
        player,
        control(player,
                {{"command", "game-data"},
                 {"value", {{"command", "save"}, {"slot", "checkpoint"}, {"overwrite", false}}}}));
    CHECK(saved["result"]["name"] == "checkpoint");
    const auto duplicate = Json::parse(
        player
            .perform(
                R"({"command":"game-data","value":{"command":"save","slot":"checkpoint","overwrite":false}})")
            .value());
    CHECK(completed(player, duplicate, false)["state"] == "failed");
    report(session.close());
  }

  auto restarted = create();
  auto player = Project::PlayerControl::create(restarted).value();
  const auto fresh
      = control(player, {{"command", "game-data"}, {"value", {{"command", "snapshot"}}}});
  CHECK(fresh["result"]["data"] == initialData);
  const auto saved = completed(
      player, control(player, {{"command", "game-data"},
                               {"value", {{"command", "read-save"}, {"slot", "checkpoint"}}}}));
  CHECK(saved["result"]["data"]["checkpoint"]["position"] == Json::array({-3, 0, -1}));
  const auto changed
      = control(player, {{"command", "game-data"},
                         {"value", {{"command", "set"}, {"key", "scratch"}, {"value", 1}}}});
  CHECK_FALSE(player.perform(
      R"({"command":"game-data","value":{"command":"load","slot":"checkpoint","revision":"1"}})"));
  completed(player, control(player, {{"command", "game-data"},
                                     {"value",
                                      {{"command", "load"},
                                       {"slot", "checkpoint"},
                                       {"revision", changed["result"]["revision"]}}}}));
  const auto loaded
      = control(player, {{"command", "game-data"}, {"value", {{"command", "snapshot"}}}});
  CHECK(loaded["result"]["data"] == saved["result"]["data"]);
  CHECK(loaded["result"]["revision"] != saved["result"]["revision"]);
  report(restarted.close());
}

TEST_CASE(
    "prism.native_buttons_route_mouse_keyboard_and_queued_product_actions_without_game_input_"
    "leaks") {
  Fixture f;
  REQUIRE(f.project.files()->writeTextAtomic("scripts/text-request.ts", R"ts(
import {gameState, input, ScriptComponent} from 'dot:engine';
export class TextRequest extends ScriptComponent {
  onFrame(): void { input.requestTextInput(gameState.get('textMode') === true); }
}
)ts"));
  auto authoring = f.project.loadStartup().value();
  const auto requester = authoring.createEntity("Text requester").value();
  REQUIRE(authoring.setScript(requester, World::ScriptComponent{"text-request.ts", "TextRequest"}));
  REQUIRE(f.project.saveStartup(authoring));
  auto session = f.session(false, f.storage.files().value());
  report(session.start());
  auto ui = session.hud().value();
  Platform::InputState native;
  const auto step = [&](std::initializer_list<Platform::Key> keys = {}, bool down = false,
                        Platform::Point pointer = {}, bool enabled = true) {
    native.beginFrame();

    for (const auto& [name, key] : Platform::keyNames()) {
      const bool desired = std::ranges::find(keys, key) != keys.end();

      if (native.key(key).down != desired) {
        REQUIRE(native.consume({0, 0, Platform::KeyChanged{key, desired, false}}));
      }
    }

    if (native.button(Platform::MouseButton::Left).down != down) {
      REQUIRE(native.consume({0, 0,
                              Platform::ButtonChanged{Platform::MouseButton::Left, down,
                                                      std::array{pointer.x, pointer.y}}}));
    }

    REQUIRE(native.consume({0, 0, Platform::PointerMoved{pointer.x, pointer.y, 0, 0}}));
    REQUIRE(session.publish(native, false, Project::GameInputArea{{1000, 500}, enabled}));
    report(session.fixedUpdate());
    report(session.update(1.0 / 60));
  };
  const auto awaitCheckpoint = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    for (;;) {
      const auto value
          = Json::parse(session.gameData()->perform(R"({"command":"snapshot"})").value());
      const auto status = value["data"].value("relayProgressStatus", std::string{});

      if (status != "Saving checkpoint..." && status != "Loading checkpoint...") {
        return;
      }

      REQUIRE(std::chrono::steady_clock::now() < deadline);
      native.beginFrame();
      REQUIRE(session.publish(native, false, Project::GameInputArea{{1000, 500}, true}));
      report(session.update(0));
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  const auto point = [&](std::string_view key) -> Platform::Point {
    const auto state = ui.snapshot().value();
    const auto found = std::ranges::find(state.buttons, key, &Script::HudButtonLayer::key);
    REQUIRE(found != state.buttons.end());
    return {(found->value.position.x + found->value.size.x / 2) * 1000,
            (found->value.position.y + found->value.size.y / 2) * 500};
  };

  for (int i = 0; i < 30; ++i) {
    step();
  }

  step({Platform::Key::M});
  CHECK(session.gameClock()->snapshot()->paused);
  REQUIRE(ui.snapshot()->buttons.size() == 3);
  CHECK(ui.snapshot()->focused == "menu-0-return");
  CHECK_FALSE(session.textInputRequested().value());
  const auto frozen = session.stats().value();
  step({Platform::Key::Tab});
  CHECK(ui.snapshot()->focused == "menu-1-save");
  step({Platform::Key::Enter});
  CHECK(ui.snapshot()->sequence == 1);
  awaitCheckpoint();
  CHECK(Json::parse(session.gameData()
                        ->perform(R"({"command":"snapshot"})")
                        .value())["data"]["relayProgressStatus"]
        == "Checkpoint saved");

  for (int i = 0; i < 10; ++i) {
    step({Platform::Key::Enter});
  }

  CHECK(ui.snapshot()->sequence == 1);
  CHECK(session.stats()->simulatedFixedSteps == frozen.simulatedFixedSteps);
  CHECK(session.stats()->simulatedUpdates == frozen.simulatedUpdates);
  const auto restore = point("menu-2-restore");
  step({}, true, restore);
  CHECK(ui.snapshot()->pressed == "menu-2-restore");
  step({}, false, restore, false);
  CHECK(ui.snapshot()->sequence == 1);
  step({}, true, restore);
  step({}, true, restore);
  step({}, false, restore);
  CHECK(ui.snapshot()->sequence == 2);
  awaitCheckpoint();
  CHECK(Json::parse(session.gameData()
                        ->perform(R"({"command":"snapshot"})")
                        .value())["data"]["relayProgressStatus"]
        == "Checkpoint restored");
  REQUIRE(session.gameData()->perform(R"({"command":"set","key":"textMode","value":true})"));
  step();
  CHECK(session.textInputRequested().value());
  const auto focused = ui.snapshot()->focused;
  step({Platform::Key::Tab, Platform::Key::Space});
  CHECK(ui.snapshot()->focused == focused);
  CHECK(ui.snapshot()->sequence == 2);
  REQUIRE(session.gameData()->perform(R"({"command":"set","key":"textMode","value":false})"));
  step();
  auto control = Project::PlayerControl::create(session).value();
  auto queued = control.perform(Json{{"command", "hud"},
                                     {"scope", std::to_string(session.stats()->location.scope)},
                                     {"value", {{"command", "activate"}, {"key", "menu-0-return"}}}}
                                    .dump());
  REQUIRE(queued);
  CHECK(session.gameClock()->snapshot()->paused);
  CHECK(Json::parse(*queued)["result"]["sequence"] == "3");
  step();
  CHECK_FALSE(session.gameClock()->snapshot()->paused);
  CHECK(std::ranges::none_of(ui.snapshot()->buttons,
                             [](const auto& button) { return button.value.visible; }));
  step({Platform::Key::M});
  step({Platform::Key::Space});
  CHECK_FALSE(session.gameClock()->snapshot()->paused);

  for (int i = 0; i < 10; ++i) {
    step();
  }

  CHECK(std::abs(entity(session, "Player").transform.position.y) < .01F);
  const auto revision = ui.snapshot()->revision;
  CHECK_FALSE(session.publish(native, false, Project::GameInputArea{{0, 500}, true}));
  CHECK(ui.snapshot()->revision == revision);
  REQUIRE(session.publish(native, false, Project::GameInputArea{{0, 0}, false}));
  report(session.close());
  REQUIRE(ui.perform(R"({"command":"events","after":"0"})"));
  CHECK_FALSE(ui.perform(R"({"command":"activate","key":"menu-0-return"})"));
}

TEST_CASE("prism.authored_action_scene_keeps_gameplay_separate_and_animates_real_joints") {
  auto project = Dot::Project::Project::open(DOT_EXAMPLE_SOURCE).value();
  const auto files = project.files().value();
  auto scene = Dot::World::loadScene(files, "scenes/character-action.dotscene").value();
  const auto players = scene.animationPlayers().value();
  REQUIRE(players.size() == 1);
  const auto& player = players.front();
  REQUIRE(player.properties.clip < player.properties.clips.size());
  CHECK(player.properties.clips[player.properties.clip]
        == "assets/actions/inspection.dotanimation");
  CHECK(player.properties.playOnStart);
  CHECK(player.properties.enabled);
  const auto value
      = Dot::Animation::loadClip(files, player.properties.clips[player.properties.clip]).value();
  CHECK(value.duration().value() == 2);
  const auto* description = value.description().value();
  CHECK(description->tracks.size() == 201);
  std::size_t keys{};

  for (const auto& track : description->tracks) {
    keys += track.keys.size();
  }

  CHECK(keys == 405);
  const auto entities = scene.entities().value();
  const auto controller = std::ranges::find_if(
      entities, [](const auto& entity) { return entity.character.has_value(); });
  REQUIRE(controller != entities.end());
  CHECK_FALSE(controller->script);
  const auto main = project.loadStartup().value();
  REQUIRE(main.entity(main.findEntity(controller->id.value).value())->script);
  auto preview = Dot::Editor::AnimationPreview::create(scene, files, player.entity.value).value();

  for (const auto* name : {"head", "upper_arm.L", "upper_arm.R"}) {
    const auto target
        = std::ranges::find_if(entities, [&](const auto& entity) { return entity.name == name; });
    REQUIRE(target != entities.end());
    CHECK(std::ranges::any_of(player.properties.targets, [&](const auto& binding) {
      return binding.entity == target->id.value;
    }));
    REQUIRE(preview.seek(0));
    auto posed = preview.scene().value();
    const auto start = posed->transform(posed->findEntity(target->id.value).value()).value();
    const auto binding = std::ranges::find_if(player.properties.targets, [&](const auto& item) {
      return item.entity == target->id.value;
    });
    REQUIRE(binding != player.properties.targets.end());
    const auto track = std::ranges::find_if(description->tracks, [&](const auto& item) {
      return item.node == binding->node && item.path == Dot::Animation::Path::Rotation;
    });
    REQUIRE(track != description->tracks.end());
    REQUIRE(track->keys.size() == 3);
    CHECK(track->keys[1].time == .8F);

    if (std::string_view(name) == "head") {
      CHECK(track->interpolation == Dot::Animation::Interpolation::CubicSpline);
      CHECK(track->keys.front().outTangent.y == .15F);
      CHECK(track->keys[1].inTangent.y == 0);
      CHECK(track->keys[1].outTangent.y == 0);
    } else {
      CHECK(track->interpolation == Dot::Animation::Interpolation::Linear);
    }

    REQUIRE(preview.seek(.8F));
    posed = preview.scene().value();
    const auto peak = posed->transform(posed->findEntity(target->id.value).value()).value();
    const auto dot = start.rotation.x * peak.rotation.x + start.rotation.y * peak.rotation.y
                     + start.rotation.z * peak.rotation.z + start.rotation.w * peak.rotation.w;
    CHECK(std::abs(dot) < .999F);
    REQUIRE(preview.seek(2));
    posed = preview.scene().value();
    const auto end = posed->transform(posed->findEntity(target->id.value).value()).value();
    CHECK(start.rotation == end.rotation);
  }
}

TEST_CASE("prism.pending_checkpoint_receipts_survive_source_migration_without_duplicate_jobs") {
  Fixture f;
  REQUIRE(f.storage.files()->createDirectories("saves"));
  const auto root = Core::FileSystem::open(f.storage.files()->root() / "saves").value();
  auto session = f.session(true, root);
  report(session.start());
  Driver driver(session);
  driver.frame();
  auto data = session.gameData().value();
  const auto original = f.project.files()->readText("scripts/camera.ts").value();

  for (const bool load : {false, true}) {
    driver.publish({load ? Platform::Key::F7 : Platform::Key::F6});
    report(session.update(0));
    const auto pending = Json::parse(data.perform(R"({"command":"snapshot"})").value());
    CHECK(pending["data"]["relayProgressStatus"]
          == (load ? "Loading checkpoint..." : "Saving checkpoint..."));
    const auto jobs = Json::parse(data.perform(R"({"command":"save-jobs"})").value());
    REQUIRE(jobs.size() == 1);
    CHECK(jobs[0]["operation"] == (load ? "read" : "write"));
    const auto id = jobs[0]["id"];
    REQUIRE(f.project.files()->writeTextAtomic(
        "scripts/camera.ts",
        original + (load ? "\n// pending read migration\n" : "\n// pending write migration\n")));
    auto migrated = Tests::awaitCompilation([&] { return session.pollScripts(); });
    REQUIRE_MESSAGE(migrated, (migrated ? "" : migrated.error().message));
    REQUIRE(migrated->committed);
    const auto retained = Json::parse(data.perform(R"({"command":"save-jobs"})").value());
    REQUIRE(retained.size() == 1);
    CHECK(retained[0]["id"] == id);
    driver.publish();
    report(session.update(0));
    awaitCheckpoint(session, driver);
    const auto completed = Json::parse(data.perform(R"({"command":"snapshot"})").value());
    CHECK(completed["data"]["relayProgressStatus"]
          == (load ? "Checkpoint restored" : "Checkpoint saved"));
    CHECK(Json::parse(data.perform(R"({"command":"save-jobs"})").value()).empty());
    CHECK(root.readText("relay-checkpoint.dotsave"));
  }

  report(session.close());
}

TEST_CASE("player_control.lifecycle_status_is_strict_observational_and_excludes_world_data") {
  Fixture fixture;
  auto session = fixture.session();
  auto player = Project::PlayerControl::create(session).value();
  const auto ready = control(player, {{"command", "status"}});
  CHECK(ready.size() == 2);
  CHECK_FALSE(ready.contains("scene"));
  CHECK(ready["player"]["state"] == "preparing");
  CHECK_FALSE(player.perform(R"({"command":"status","scope":"1"})"));
  report(session.start());
  const auto before = control(player, {{"command", "inspect"}});
  const auto first = control(player, {{"command", "status"}});
  const auto second = control(player, {{"command", "status"}});
  CHECK(first == second);
  CHECK(first["player"] == before["player"]);
  CHECK(control(player, {{"command", "inspect"}})["scene"] == before["scene"]);
  const auto scope = first["player"]["scope"];
  control(player, {{"command", "input"}, {"scope", scope}, {"value", inputRequest(3)}});
  const auto queued = control(player, {{"command", "status"}});
  CHECK(queued["player"]["input"]["active"] == true);
  CHECK(queued["player"]["input"]["remaining"] == 3);
  CHECK(control(player, {{"command", "status"}}) == queued);
  control(player, {{"command", "cancel-input"}, {"scope", scope}});
  const auto cancelled = control(player, {{"command", "status"}});
  CHECK(cancelled["player"]["input"]["active"] == true);
  CHECK(cancelled["player"]["input"]["remaining"] == 0);
  CHECK(control(player, {{"command", "status"}}) == cancelled);
  REQUIRE(player.next(Platform::InputState{}));
  CHECK_FALSE(control(player, {{"command", "status"}})["player"]["input"]["active"].get<bool>());
  report(session.close());
  const auto closed = control(player, {{"command", "status"}});
  CHECK(closed["player"]["state"] == "closed");
  CHECK_FALSE(closed.contains("scene"));
}

TEST_CASE("prism.saved_retarget_profile_rebakes_native_walk_without_changing_gameplay") {
  auto project = Dot::Project::Project::open(DOT_EXAMPLE_SOURCE).value();
  auto files = project.files().value();
  auto profile = Dot::Editor::decodeRetargetProfile(
                     files.readText("assets/actions/walk.dotretarget", 512 * 1024).value())
                     .value();
  CHECK(profile.settings.mappings.size() == 51);
  auto source = Dot::Editor::inspectRetargetReference(files, profile.sourceScene).value();
  auto target = Dot::Editor::inspectRetargetReference(files, profile.targetScene).value();
  REQUIRE(source.size() == 1);
  REQUIRE(target.size() == 1);
  CHECK(source.front().entity == profile.sourceEntity);
  CHECK(target.front().entity == profile.targetEntity);
  CHECK(source.front().rig.nodes.size() == 67);
  CHECK(target.front().rig.nodes.size() == 67);
  auto reference = Dot::World::loadScene(files, profile.targetScene).value();
  const auto definitions = reference.animationPlayers().value();
  REQUIRE(definitions.size() == 1);
  CHECK_FALSE(definitions.front().properties.enabled);
  CHECK_FALSE(definitions.front().properties.playOnStart);
  const auto referenceBytes = Dot::World::encodeScene(reference).value();
  auto referenceScene = std::make_shared<Dot::World::Scene>(std::move(reference));
  auto staticPlayers = Dot::Animation::ScenePlayers::create(referenceScene, files).value();
  REQUIRE(staticPlayers.advance(1));
  CHECK(Dot::World::encodeScene(*referenceScene).value() == referenceBytes);
  auto original = Dot::Animation::loadClip(files, profile.clip).value();
  auto baked = Dot::Animation::retargetClip(source.front().rig, target.front().rig, original,
                                            profile.settings);
  REQUIRE_MESSAGE(baked, (baked ? "" : baked.error().message));
  CHECK(baked->frames == 41);
  CHECK(baked->emittedNodes == 63);
  CHECK(baked->unmappedSource.size() == 16);
  CHECK(baked->unmappedTarget.size() == 16);
  CHECK(baked->clip.description().value()->tracks.size() == 189);
  const auto path = "assets/actions/retargeted-walk.dotanimation";
  CHECK(Dot::Animation::encodeClip(baked->clip).value()
        == files.readText(path, 64 * 1024 * 1024).value());

  for (const auto& track : baked->clip.description().value()->tracks) {
    CHECK(track.node >= 4);
    CHECK(track.node <= 66);
  }

  auto scene = Dot::World::loadScene(files, "scenes/character-action.dotscene").value();
  auto player = scene.animationPlayers()->front();
  REQUIRE(player.properties.clips.size() == 10);
  CHECK(player.properties.clip == 7);
  CHECK(player.properties.clips[8] == path);
  CHECK(Dot::Editor::validateRetargetTarget(scene, player.entity.value, target.front().rig));
  auto anchor = scene.transform(player.entity).value();
  player.properties.clip = 8;
  REQUIRE(scene.setAnimationPlayer(player.entity, player.properties));
  auto preview = Dot::Editor::AnimationPreview::create(scene, files, player.entity.value);
  REQUIRE_MESSAGE(preview, (preview ? "" : preview.error().message));
  REQUIRE(preview->seek(0));
  const auto transform = [](const Dot::World::Scene& value, std::string_view name) {
    auto entries = value.entities().value();
    return std::ranges::find(entries, name, &Dot::World::EntityInfo::name)->transform;
  };
  const auto leg = transform(*preview->scene().value(), "thigh.L");
  REQUIRE(preview->seek(.4));
  auto posed = preview->scene().value();
  const auto posedAnchor = posed->transform(posed->findEntity(player.entity.value).value()).value();
  CHECK(posedAnchor.position == anchor.position);
  CHECK(posedAnchor.rotation == anchor.rotation);
  CHECK(posedAnchor.scale == anchor.scale);
  CHECK(transform(*posed, "thigh.L").rotation != leg.rotation);
  CHECK(scene.transform(player.entity)->rotation == anchor.rotation);
  CHECK(scene.transform(player.entity)->position == anchor.position);
  auto main = project.loadStartup().value();
  CHECK(main.animationPlayers()->front().properties.clips.size() == 8);
  CHECK(main.animationPlayers()->front().properties.controller.has_value());
}

TEST_CASE("prism.saved_two_bone_reach_matches_reference_and_keeps_instance_placement") {
  auto project = Dot::Project::Project::open(DOT_EXAMPLE_SOURCE).value();
  auto files = project.files().value();
  auto base = Dot::World::loadScene(files, "assets/actions/surveyor-reference.dotscene").value();
  auto reference = Dot::World::loadScene(files, "assets/actions/reach-reference.dotscene").value();
  const Dot::Math::Vec3 target{-.35F, 1.22F, -.27F};
  const auto solved = Dot::Animation::solveTwoBone(base, {base.findEntity(15).value(),
                                                          base.findEntity(16).value(),
                                                          base.findEntity(17).value(),
                                                          target,
                                                          {-.65F, 1.12F, .20F}});
  REQUIRE_MESSAGE(solved, (solved ? "" : solved.error().message));
  CHECK_FALSE(solved->clamped);
  CHECK(solved->residual < 1e-5);
  const auto hand = reference.worldTransform(reference.findEntity(17).value()).value();
  CHECK(Dot::Math::length(Dot::Math::Vec3{hand.data[12], hand.data[13], hand.data[14]} - target)
        < 1e-5);

  const auto definitions = reference.animationPlayers().value();
  REQUIRE(definitions.size() == 1);
  CHECK_FALSE(definitions.front().properties.enabled);
  CHECK_FALSE(definitions.front().properties.playOnStart);
  auto frozen = std::make_shared<Dot::World::Scene>(std::move(reference));
  const auto frozenBytes = Dot::World::encodeScene(*frozen).value();
  auto staticPlayers = Dot::Animation::ScenePlayers::create(frozen, files).value();
  REQUIRE(staticPlayers.advance(1));
  CHECK(Dot::World::encodeScene(*frozen).value() == frozenBytes);

  auto scene = Dot::World::loadScene(files, "scenes/character-action.dotscene").value();
  auto player = scene.animationPlayers()->front();
  REQUIRE(player.properties.clips.size() == 10);
  const auto path = "assets/actions/maintenance-reach.dotanimation";
  CHECK(player.properties.clips[9] == path);
  CHECK(player.properties.clip == 7);
  const auto clip = Dot::Animation::loadClip(files, path).value();
  CHECK(clip.description().value()->tracks.size() == 198);
  CHECK(clip.duration().value() == doctest::Approx(1.2));

  for (const auto& track : clip.description().value()->tracks) {
    CHECK(track.node != 0);
  }

  const auto anchor = scene.transform(player.entity).value();
  player.properties.clip = 9;
  REQUIRE(scene.setAnimationPlayer(player.entity, player.properties));
  auto preview = Dot::Editor::AnimationPreview::create(scene, files, player.entity.value).value();
  REQUIRE(preview.seek(.6F));
  const auto posed = preview.scene().value();
  const auto instance = posed->transform(posed->findEntity(player.entity.value).value()).value();
  CHECK(instance.position == anchor.position);
  CHECK(instance.rotation == anchor.rotation);
  CHECK(instance.scale == anchor.scale);
  const auto entries = posed->entities().value();

  for (const auto& update : solved->updates) {
    const auto name = base.entity(update.entity)->name;
    const auto found = std::ranges::find(entries, name, &Dot::World::EntityInfo::name);
    REQUIRE(found != entries.end());
    const auto expected = Dot::Math::compose({}, update.transform.rotation).value();
    const auto actual = Dot::Math::compose({}, found->transform.rotation).value();

    for (std::size_t i = 0; i < expected.data.size(); ++i) {
      CHECK(actual.data[i] == doctest::Approx(expected.data[i]).epsilon(1e-5));
    }

    CHECK(found->transform.position == update.transform.position);
    CHECK(found->transform.scale == update.transform.scale);
  }

  REQUIRE(preview.seek(0));
  const auto start = Dot::World::encodeScene(*preview.scene().value()).value();
  REQUIRE(preview.seek(1.2F));
  CHECK(Dot::World::encodeScene(*preview.scene().value()).value() == start);
  auto main = project.loadStartup().value();
  CHECK(main.animationPlayers()->front().properties.clips.size() == 8);
  CHECK(main.animationPlayers()->front().properties.controller.has_value());
}

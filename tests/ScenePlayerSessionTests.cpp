#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "ScenePreparation.hpp"

import Dot.Animation.Clip;
import Dot.Animation.Files;
import Dot.Animation.ControllerFiles;
import Dot.Animation.ScenePlayers;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Project;
import Dot.Project.GameSession;
import Dot.Project.PlayerControl;
import Dot.Script.Compiler;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace {
  using namespace Dot;

  struct Fixture {
    Core::StagedDirectory storage
        = Core::StagedDirectory::create(
              Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Project::Project project = Project::Project::create(storage.files().value(), "game").value();
    Core::FileSystem files = project.files().value();
    std::shared_ptr<World::Scene> scene
        = std::make_shared<World::Scene>(World::Scene::create().value());
    World::EntityId owner = scene->createEntity("Player").value();
    World::EntityId target = scene->createEntity("Target").value();
    World::AnimationPlayer properties{{"assets/move.dotanimation"}, {{0, target.value}}};

    Fixture() {
      auto clip = Animation::Clip::create({"Move",
                                           {{0,
                                             Animation::Path::Translation,
                                             Animation::Interpolation::Linear,
                                             {{0, {0, 0, 0, 0}}, {2, {2, 0, 0, 0}}}}}})
                      .value();
      Animation::saveClip(files, "assets/move.dotanimation", clip).value();
      scene->setAnimationPlayer(owner, properties).value();
    }
  };
}  // namespace

TEST_CASE("animation.layer_controls_preserve_unprimed_reverse_base_start") {
  Fixture fixture;
  fixture.properties.speed = -1;
  fixture.properties.looping = false;
  fixture.properties.layers
      = {{"Action", 0, {{0, Animation::Path::Translation}}, 0, 1, false, false}};
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, fixture.properties));
  auto authored = fixture.scene->transform(fixture.target).value();
  authored.position.x = 9;
  REQUIRE(fixture.scene->setTransform(fixture.target, authored));
  auto players = Animation::ScenePlayers::create(fixture.scene, fixture.files).value();
  REQUIRE(players.controlLayer(fixture.owner, "Action", Animation::WeightLayer{.5}));
  CHECK(fixture.scene->transform(fixture.target)->position.x == 9);
  REQUIRE(players.advance(.5));
  CHECK(players.state(fixture.owner)->playback.time == 1.5);
  CHECK(fixture.scene->transform(fixture.target)->position.x == 1.5F);
  REQUIRE(players.advance(.5));
  CHECK(players.state(fixture.owner)->playback.time == 1);
}

TEST_CASE("animation.single_step_native_failure_reports_failed_request_and_partial_fixed_work") {
  Fixture fixture;
  auto description = fixture.project.description().value();
  description.fixedHz = 1;
  REQUIRE(fixture.project.save(description));
  constexpr float delta = 1;
  const auto clip = Animation::Clip::create({"Singular step",
                                             {{0,
                                               Animation::Path::Scale,
                                               Animation::Interpolation::Linear,
                                               {{0, {1, 1, 1, 0}}, {2 * delta, {-1, 1, 1, 0}}}}}})
                        .value();
  REQUIRE(Animation::saveClip(fixture.files, "assets/move.dotanimation", clip));
  REQUIRE(fixture.project.saveStartup(*fixture.scene));
  const auto compiler = Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
  auto session = Project::GameSession::create(fixture.project, compiler).value();
  REQUIRE(session.start());
  auto clock = session.gameClock().value();
  REQUIRE(clock.perform(R"({"command":"set-paused","paused":true})"));
  REQUIRE(clock.perform(R"({"command":"step"})"));
  CHECK_FALSE(session.update(.5));
  CHECK(session.stats()->state == Project::SessionState::Faulted);
  CHECK(session.stats()->simulatedFixedSteps == 1);
  CHECK(session.stats()->lifetimeFixedSteps == 1);
  CHECK(session.stats()->simulatedUpdates == 0);
  CHECK(clock.snapshot()->paused);
  CHECK(clock.snapshot()->step->state == Script::GameStepState::Failed);
  CHECK_FALSE(clock.perform(R"({"command":"step"})"));
  const auto target = session.scene().value()->findEntity(fixture.target.value).value();
  CHECK(session.scene().value()->transform(target)->scale.x == 1);
  REQUIRE(session.close());
  CHECK(clock.snapshot()->step->state == Script::GameStepState::Failed);
}

TEST_CASE("animation.product_inspection_reads_actual_layer_clocks_with_strict_running_scope") {
  Fixture fixture;
  fixture.properties.layers
      = {{"Action", 0, {{0, Animation::Path::Translation}}, .5, 2, true, true}};
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, fixture.properties));
  REQUIRE(fixture.project.saveStartup(*fixture.scene));
  const auto compiler = Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
  auto session = Project::GameSession::create(fixture.project, compiler).value();
  auto control = Project::PlayerControl::create(session).value();
  CHECK_FALSE(session.inspectAnimation(fixture.owner));
  REQUIRE(session.start());
  REQUIRE(session.update(.5));
  auto playing = session.scene().value();
  const auto owner = playing->findEntity(fixture.owner.value).value();
  using Json = nlohmann::json;
  const auto before = session.inspectAnimation(owner).value();
  const auto state = Json::parse(before);
  CHECK(state["timeSeconds"] == .5);
  CHECK(state["layers"][0]["timeSeconds"] == 1);
  auto request = Json{{"command", "animation-state"},
                      {"scope", std::to_string(playing->id())},
                      {"entity", std::to_string(owner.value)}};
  const auto reply = control.perform(request.dump());
  REQUIRE(reply);
  CHECK(Json::parse(*reply)["animation"] == state);
  request["scope"] = std::to_string(fixture.scene->id());
  CHECK_FALSE(control.perform(request.dump()));
  request["scope"] = std::to_string(playing->id());
  request["extra"] = 0;
  CHECK_FALSE(control.perform(request.dump()));
  CHECK(session.inspectAnimation(owner).value() == before);
  request = Json{{"command", "animation-layer"},
                 {"scope", std::to_string(playing->id())},
                 {"entity", std::to_string(owner.value)},
                 {"name", "Action"},
                 {"value", {{"action", "stop"}}}};
  REQUIRE(control.perform(request.dump()));
  CHECK_FALSE(session.animationState(owner)->layers.front().playback.active);
  CHECK(session.animationState(owner)->playback.time == .5);
  CHECK(playing->transform(playing->findEntity(fixture.target.value).value())->position.x == .5F);
  const auto stopped = session.inspectAnimation(owner).value();
  request["scope"] = std::to_string(fixture.scene->id());
  CHECK_FALSE(control.perform(request.dump()));
  request["scope"] = std::to_string(playing->id());
  request["value"] = {{"action", "weight"}, {"weight", true}};
  CHECK_FALSE(control.perform(request.dump()));
  CHECK(session.inspectAnimation(owner).value() == stopped);
  REQUIRE(session.close());
  CHECK_FALSE(session.inspectAnimation(owner));
  CHECK_FALSE(control.perform(Json{{"command", "animation-state"},
                                   {"scope", std::to_string(playing->id())},
                                   {"entity", std::to_string(owner.value)}}
                                  .dump()));
}

TEST_CASE("animation.layer_writer_reservations_survive_inactivity_clip_switch_and_stop") {
  using namespace Animation;
  Fixture fixture;
  auto extra = fixture.scene->createEntity("Extra").value();
  auto overlay = Clip::create({"Overlay",
                               {{1,
                                 Path::Translation,
                                 Interpolation::Linear,
                                 {{0, {10, 0, 0, 0}}, {2, {12, 0, 0, 0}}}}}})
                     .value();
  REQUIRE(saveClip(fixture.files, "assets/overlay.dotanimation", overlay));
  fixture.properties.clips.push_back("assets/overlay.dotanimation");
  fixture.properties.targets.push_back({1, extra.value});
  fixture.properties.layers = {{"Action", 1, {{1, Path::Translation}}, 1, 1, false, false}};
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, fixture.properties));
  auto players = ScenePlayers::create(fixture.scene, fixture.files).value();
  REQUIRE(players.advance(0));
  REQUIRE(players.controlLayer(fixture.owner, "Action", PlayLayer{true}));
  REQUIRE(players.advance(.5));
  CHECK(fixture.scene->transform(fixture.target)->position.x == .5F);
  CHECK(fixture.scene->transform(extra)->position.x == 10.5F);
  REQUIRE(players.stop(fixture.owner));
  CHECK_FALSE(players.state(fixture.owner)->layers.front().playback.active);
  auto other = fixture.scene->createEntity("Other player").value();
  World::AnimationPlayer conflict{{"assets/overlay.dotanimation"}, {{1, extra.value}}};
  REQUIRE(fixture.scene->setAnimationPlayer(other, conflict));
  CHECK_FALSE(players.advance(0));
  CHECK_FALSE(ScenePlayers::create(fixture.scene, fixture.files));
  REQUIRE(fixture.scene->setAnimationPlayer(other, std::nullopt));
  REQUIRE(players.play(fixture.owner, 0, true));
  REQUIRE(players.crossFade(fixture.owner, 1, .1));
  REQUIRE(players.advance(.1));
  REQUIRE(players.play(fixture.owner, 0, true));
  REQUIRE(fixture.scene->setRigidBody(extra, World::RigidBody{}));
  CHECK_FALSE(players.advance(0));
  CHECK_FALSE(ScenePlayers::create(fixture.scene, fixture.files));
}

TEST_CASE("animation.layer_masks_validate_actual_library_channels_and_character_authority") {
  using namespace Animation;
  Fixture fixture;
  fixture.properties.layers = {{"Bad", 0, {{0, Path::Scale}}}};
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, fixture.properties));
  CHECK_FALSE(ScenePlayers::create(fixture.scene, fixture.files));
  fixture.properties.layers[0].mask[0].path = Path::Translation;
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, fixture.properties));
  REQUIRE(fixture.scene->setCharacterController(fixture.target, World::CharacterController{}));
  CHECK_FALSE(ScenePlayers::create(fixture.scene, fixture.files));
}

TEST_CASE("animation.character_root_protects_physics_without_blocking_child_bones") {
  Fixture fixture;
  const auto root = fixture.scene->createEntity("Character root").value();
  REQUIRE(fixture.scene->setCharacterController(root, World::CharacterController{}));
  REQUIRE(fixture.scene->setParent(fixture.target, root));
  auto players = Animation::ScenePlayers::create(fixture.scene, fixture.files).value();
  REQUIRE(players.advance(.5));
  CHECK(fixture.scene->transform(fixture.target)->position.x == .5F);
  CHECK(fixture.scene->transform(root)->position.x == 0);
  auto properties = fixture.properties;
  properties.targets[0].entity = root.value;
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, properties));
  CHECK_FALSE(players.advance(.5));
  CHECK(fixture.scene->transform(root)->position.x == 0);
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, fixture.properties));
  REQUIRE(players.advance(.25));
  REQUIRE(fixture.scene->setParent(fixture.target, {}));
  REQUIRE(fixture.scene->setParent(root, fixture.target));
  const auto before = fixture.scene->transform(fixture.target).value();
  CHECK_FALSE(players.advance(.25));
  CHECK(fixture.scene->transform(fixture.target)->position == before.position);
  REQUIRE(fixture.scene->setParent(root, {}));
  REQUIRE(fixture.scene->setParent(fixture.target, root));
  auto clip = Animation::Clip::create({"Root motion",
                                       {{1,
                                         Animation::Path::Translation,
                                         Animation::Interpolation::Linear,
                                         {{0, {0, 0, 0, 0}}, {2, {2, 0, 0, 0}}}}}})
                  .value();
  REQUIRE(Animation::saveClip(fixture.files, "assets/root.dotanimation", clip));
  properties = fixture.properties;
  properties.clips.push_back("assets/root.dotanimation");
  properties.targets.push_back({1, root.value});
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, properties));
  REQUIRE(players.advance(.25));
  CHECK_FALSE(players.crossFade(fixture.owner, 1, .5));
  CHECK(players.state(fixture.owner)->clip == 0);
}

TEST_CASE("animation.scene_players_retain_clocks_reconfigure_pause_reverse_and_remove") {
  Fixture fixture;
  auto players = Animation::ScenePlayers::create(fixture.scene, fixture.files).value();
  REQUIRE(players.advance(.5));
  CHECK(fixture.scene->transform(fixture.target)->position.x == .5F);
  REQUIRE(players.advance(.5));
  CHECK(fixture.scene->transform(fixture.target)->position.x == 1);
  fixture.properties.speed = -1;
  fixture.properties.looping = false;
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, fixture.properties));
  REQUIRE(players.advance(.5));
  CHECK(fixture.scene->transform(fixture.target)->position.x == 1.5F);
  fixture.properties.playOnStart = false;
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, fixture.properties));
  REQUIRE(players.advance(.5));
  CHECK(fixture.scene->transform(fixture.target)->position.x == 1.5F);
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, {}));
  REQUIRE(players.advance(.5));
  CHECK(players.stats()->players == 0);
  CHECK_FALSE(players.advance(-1));
  bool rejected{};
  std::thread worker([&] { rejected = !players.stats(); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(players);
  CHECK_FALSE(players.advance(0));
  CHECK(moved.stats()->players == 0);
}

TEST_CASE("animation.scene_players_failed_rebind_overlap_physics_and_missing_files_preserve_pose") {
  Fixture fixture;
  auto players = Animation::ScenePlayers::create(fixture.scene, fixture.files).value();
  REQUIRE(players.advance(.5));
  auto bad = fixture.properties;
  bad.clips = {"assets/missing.dotanimation"};
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, bad));
  CHECK_FALSE(players.advance(.5));
  CHECK(fixture.scene->transform(fixture.target)->position.x == .5F);
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, fixture.properties));
  REQUIRE(players.advance(.5));
  CHECK(fixture.scene->transform(fixture.target)->position.x == 1);
  const auto other = fixture.scene->createEntity("Other").value();
  REQUIRE(fixture.scene->setAnimationPlayer(other, fixture.properties));
  CHECK_FALSE(players.advance(.5));
  CHECK(fixture.scene->transform(fixture.target)->position.x == 1);
  REQUIRE(fixture.scene->setAnimationPlayer(other, {}));
  REQUIRE(fixture.scene->setRigidBody(fixture.target, World::RigidBody{}));
  CHECK_FALSE(players.advance(.5));
  CHECK(fixture.scene->transform(fixture.target)->position.x == 1);
  REQUIRE(fixture.scene->setRigidBody(fixture.target, {}));
  REQUIRE(fixture.scene->setParent(fixture.target, other));
  REQUIRE(fixture.scene->setRigidBody(other, World::RigidBody{}));
  CHECK_FALSE(Animation::ScenePlayers::create(fixture.scene, fixture.files));
  REQUIRE(fixture.scene->setRigidBody(other, {}));
  REQUIRE(fixture.scene->setParent(fixture.target, fixture.owner));
  REQUIRE(fixture.scene->destroyEntity(fixture.owner));
  REQUIRE(players.advance(.5));
  CHECK(players.stats()->players == 0);
}

TEST_CASE("animation.game_session_saved_clone_fixed_variable_fault_and_close_use_native_players") {
  Fixture fixture;
  const auto compiler = Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
  REQUIRE(fixture.project.saveStartup(*fixture.scene));
  auto session = Project::GameSession::create(fixture.project, compiler).value();
  REQUIRE(session.start());
  const auto playing = session.scene().value();
  const auto target = playing->findEntity(fixture.target.value).value();
  REQUIRE(session.fixedUpdate());
  CHECK(playing->transform(target)->position.x == 0);
  REQUIRE(session.update(.5));
  CHECK(playing->transform(target)->position.x == .5F);
  CHECK(fixture.scene->transform(fixture.target)->position.x == 0);
  auto saved = fixture.project.loadStartup().value();
  CHECK(saved.transform(saved.findEntity(fixture.target.value).value())->position.x == 0);
  REQUIRE(playing->setRigidBody(target, World::RigidBody{}));
  CHECK_FALSE(session.update(.5));
  CHECK(session.stats()->state == Project::SessionState::Faulted);
  REQUIRE(session.close());
  CHECK(session.stats()->state == Project::SessionState::Closed);
}

TEST_CASE(
    "animation.scene_replacement_uses_prepared_clips_then_refreshes_runtime_description_edits") {
  Fixture f;
  REQUIRE(f.project.saveStartup(*f.scene));
  REQUIRE(World::saveScene(f.files, "scenes/next.dotscene", *f.scene));
  const auto compiler = Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
  auto session = Project::GameSession::create(f.project, compiler).value();
  REQUIRE(session.start());
  auto old = session.scene().value();
  auto request = session.requestScene("scenes/next.dotscene").value();
  REQUIRE(session.update(0));
  auto completed = Tests::awaitScene(session, request);
  REQUIRE_MESSAGE(completed, (completed ? "" : completed.error().message));
  CHECK(completed->failures.empty());
  REQUIRE(session.sceneRequest(request)->committed);
  const auto active = session.scene().value();
  CHECK(active->id() != old->id());
  auto target = active->findEntity(f.target.value).value();
  REQUIRE(session.update(.5));
  CHECK(active->transform(target)->position.x == .5F);
  REQUIRE(f.files.writeTextAtomic("assets/move.dotanimation", "invalid after initial publication"));
  REQUIRE(session.update(.25));
  CHECK(active->transform(target)->position.x == .75F);
  auto properties = f.properties;
  properties.speed = 2;
  REQUIRE(active->setAnimationPlayer(active->findEntity(f.owner.value).value(), properties));
  const auto pose = active->transform(target).value();
  CHECK_FALSE(session.update(.25));
  CHECK(session.stats()->state == Project::SessionState::Faulted);
  CHECK(active->transform(target)->position == pose.position);
  REQUIRE(session.close());
  CHECK(f.scene->transform(f.target)->position.x == 0);
}

TEST_CASE("animation.scene_player_manual_library_controls_keep_authoring_and_original_rest") {
  Fixture fixture;
  REQUIRE(fixture.scene->setTransform(fixture.target, {{7, 0, 0}}));
  auto scale = Animation::Clip::create({"Scale",
                                        {{0,
                                          Animation::Path::Scale,
                                          Animation::Interpolation::Linear,
                                          {{0, {2, 2, 2, 0}}, {2, {4, 4, 4, 0}}}}}})
                   .value();
  REQUIRE(Animation::saveClip(fixture.files, "assets/scale.dotanimation", scale));
  fixture.properties.clips.push_back("assets/scale.dotanimation");
  fixture.properties.playOnStart = false;
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, fixture.properties));
  auto players = Animation::ScenePlayers::create(fixture.scene, fixture.files).value();
  REQUIRE(players.play(fixture.owner));
  REQUIRE(players.advance(.5));
  CHECK(fixture.scene->transform(fixture.target)->position.x == .5F);
  REQUIRE(players.pause(fixture.owner));
  REQUIRE(players.advance(.5));
  CHECK(players.state(fixture.owner)->playback.time == .5);
  REQUIRE(std::filesystem::remove(fixture.files.root() / "assets/scale.dotanimation"));
  REQUIRE(players.play(fixture.owner, 1));
  CHECK(players.state(fixture.owner)->name == "Scale");
  CHECK(players.state(fixture.owner)->clip == 1);
  CHECK(fixture.scene->transform(fixture.target)->position.x == 7);
  CHECK(fixture.scene->transform(fixture.target)->scale.x == 2);
  REQUIRE(players.configure(fixture.owner, -1, false));
  REQUIRE(players.play(fixture.owner, {}, true));
  CHECK(players.state(fixture.owner)->playback.time == 2);
  REQUIRE(players.advance(.5));
  CHECK(fixture.scene->transform(fixture.target)->scale.x == 3.5F);
  REQUIRE(players.seek(fixture.owner, 1));
  REQUIRE(players.pause(fixture.owner));
  REQUIRE(players.advance(.1));
  CHECK(fixture.scene->transform(fixture.target)->scale.x == 3);
  CHECK_FALSE(players.play(fixture.owner, 2));
  CHECK_FALSE(players.seek(fixture.owner, 3));
  CHECK_FALSE(players.configure(fixture.owner, 17, true));
  CHECK(players.state(fixture.owner)->playback.time == 1);
  CHECK(players.state(fixture.owner)->clip == 1);
  REQUIRE(players.stop(fixture.owner));
  REQUIRE(players.advance(.5));
  CHECK(fixture.scene->transform(fixture.target)->scale.x == 1);
  REQUIRE(players.play(fixture.owner, 0));
  CHECK(fixture.scene->transform(fixture.target)->position.x == 2);
  REQUIRE(players.stop(fixture.owner));
  CHECK(fixture.scene->transform(fixture.target)->position.x == 7);
  CHECK(fixture.scene->entity(fixture.owner)->animation == fixture.properties);
  auto foreign = World::Scene::create().value();
  CHECK_FALSE(players.play(foreign.createEntity().value()));
  auto moved = std::move(players);
  CHECK_FALSE(players.state(fixture.owner));
  CHECK_FALSE(players.play(fixture.owner));
  CHECK_FALSE(players.pause(fixture.owner));
  CHECK_FALSE(players.stop(fixture.owner));
  CHECK_FALSE(players.seek(fixture.owner, 0));
  CHECK_FALSE(players.configure(fixture.owner, 1, true));
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, {}));
  CHECK_FALSE(moved.state(fixture.owner));
  CHECK_FALSE(moved.play(fixture.owner));
}

TEST_CASE("animation.scene_clip_selection_rejects_writer_and_physics_conflicts_without_effects") {
  Fixture fixture;
  const auto other = fixture.scene->createEntity("Other target").value();
  auto alternate = Animation::Clip::create({"Other",
                                            {{1,
                                              Animation::Path::Translation,
                                              Animation::Interpolation::Linear,
                                              {{0, {3, 0, 0, 0}}, {2, {5, 0, 0, 0}}}}}})
                       .value();
  REQUIRE(Animation::saveClip(fixture.files, "assets/other.dotanimation", alternate));
  fixture.properties.clips.push_back("assets/other.dotanimation");
  fixture.properties.targets.push_back({1, other.value});
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, fixture.properties));
  World::AnimationPlayer another{{"assets/other.dotanimation"}, {{1, other.value}}};
  REQUIRE(fixture.scene->setAnimationPlayer(other, another));
  auto players = Animation::ScenePlayers::create(fixture.scene, fixture.files).value();
  REQUIRE(players.advance(.5));
  CHECK_FALSE(players.crossFade(fixture.owner, 1, 1));
  CHECK_FALSE(players.play(fixture.owner, 1));
  CHECK(players.state(fixture.owner)->clip == 0);
  CHECK(players.state(fixture.owner)->playback.time == .5);
  CHECK(fixture.scene->transform(fixture.target)->position.x == .5F);
  REQUIRE(fixture.scene->setAnimationPlayer(other, {}));
  REQUIRE(fixture.scene->setRigidBody(other, World::RigidBody{}));
  CHECK_FALSE(players.crossFade(fixture.owner, 1, 1));
  CHECK_FALSE(players.play(fixture.owner, 1));
  CHECK(players.state(fixture.owner)->clip == 0);
  REQUIRE(fixture.scene->setRigidBody(other, {}));
  REQUIRE(players.play(fixture.owner, 1));
  CHECK(players.state(fixture.owner)->clip == 1);
  CHECK(fixture.scene->transform(fixture.target)->position.x == 0);
}

TEST_CASE("animation.scene_cross_fades_keep_live_clocks_pause_interrupt_and_authoring") {
  Fixture fixture;
  REQUIRE(fixture.scene->setTransform(fixture.target, {{7, 0, 0}}));
  auto scale = Animation::Clip::create({"Scale",
                                        {{0,
                                          Animation::Path::Scale,
                                          Animation::Interpolation::Linear,
                                          {{0, {2, 2, 2, 0}}, {2, {4, 4, 4, 0}}}}}})
                   .value();
  REQUIRE(Animation::saveClip(fixture.files, "assets/scale.dotanimation", scale));
  fixture.properties.clips.push_back("assets/scale.dotanimation");
  fixture.properties.looping = false;
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, fixture.properties));
  auto players = Animation::ScenePlayers::create(fixture.scene, fixture.files).value();
  REQUIRE(players.advance(.5));
  REQUIRE(std::filesystem::remove(fixture.files.root() / "assets/scale.dotanimation"));
  REQUIRE(players.crossFade(fixture.owner, 1, 1));
  CHECK(fixture.scene->transform(fixture.target)->position.x == .5F);
  CHECK(fixture.scene->transform(fixture.target)->scale.x == 1);
  REQUIRE(players.advance(.25));
  CHECK(fixture.scene->transform(fixture.target)->position.x == doctest::Approx(2.3125));
  CHECK(fixture.scene->transform(fixture.target)->scale.x == doctest::Approx(1.3125));
  CHECK(players.state(fixture.owner)->playback.transition->elapsed == .25);
  REQUIRE(players.pause(fixture.owner));
  REQUIRE(players.advance(.5));
  CHECK(players.state(fixture.owner)->playback.time == .25);
  CHECK(players.state(fixture.owner)->playback.transition->elapsed == .25);
  REQUIRE(players.play(fixture.owner));
  REQUIRE(players.advance(.25));
  CHECK(fixture.scene->transform(fixture.target)->position.x == 4);
  CHECK(fixture.scene->transform(fixture.target)->scale.x == 1.75F);
  REQUIRE(players.crossFade(fixture.owner, 0, .5));
  CHECK(fixture.scene->transform(fixture.target)->position.x == 4);
  CHECK(fixture.scene->transform(fixture.target)->scale.x == 1.75F);
  REQUIRE(players.advance(.25));
  CHECK(fixture.scene->transform(fixture.target)->position.x == 2.125F);
  CHECK(fixture.scene->transform(fixture.target)->scale.x == 1.375F);
  REQUIRE(players.advance(.25));
  CHECK_FALSE(players.state(fixture.owner)->playback.transition);
  CHECK(fixture.scene->transform(fixture.target)->position.x == .5F);
  CHECK(fixture.scene->transform(fixture.target)->scale.x == 1);
  REQUIRE(players.configure(fixture.owner, -1, false));
  REQUIRE(players.crossFade(fixture.owner, 0, 0));
  CHECK(players.state(fixture.owner)->playback.time == 2);
  CHECK(fixture.scene->transform(fixture.target)->position.x == 2);
  REQUIRE(players.crossFade(fixture.owner, 1, 1));
  REQUIRE(players.advance(.25));
  CHECK(players.state(fixture.owner)->playback.time == 1.75);
  REQUIRE(players.seek(fixture.owner, 1));
  CHECK_FALSE(players.state(fixture.owner)->playback.transition);
  CHECK(fixture.scene->transform(fixture.target)->position.x == 7);
  REQUIRE(players.crossFade(fixture.owner, 0, 1));
  REQUIRE(players.stop(fixture.owner));
  CHECK_FALSE(players.state(fixture.owner)->playback.transition);
  CHECK(fixture.scene->transform(fixture.target)->position.x == 7);
  CHECK(fixture.scene->transform(fixture.target)->scale.x == 1);
  CHECK(fixture.scene->entity(fixture.owner)->animation == fixture.properties);
}

TEST_CASE("animation.scene_cross_fade_reserves_interrupted_writers_then_releases_atomically") {
  Fixture fixture;
  const auto second = fixture.scene->createEntity("Second").value();
  const auto third = fixture.scene->createEntity("Third").value();

  for (std::uint32_t node = 1; node <= 2; ++node) {
    const auto path = "assets/" + std::to_string(node) + ".dotanimation";
    auto clip = Animation::Clip::create({"Other",
                                         {{node,
                                           Animation::Path::Translation,
                                           Animation::Interpolation::Linear,
                                           {{0, {3, 0, 0, 0}}, {2, {5, 0, 0, 0}}}}}})
                    .value();
    REQUIRE(Animation::saveClip(fixture.files, path, clip));
    fixture.properties.clips.push_back(path);
  }

  fixture.properties.targets.push_back({1, second.value});
  fixture.properties.targets.push_back({2, third.value});
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, fixture.properties));
  auto players = Animation::ScenePlayers::create(fixture.scene, fixture.files).value();
  REQUIRE(players.advance(.25));
  REQUIRE(players.crossFade(fixture.owner, 1, 1));
  REQUIRE(players.advance(.25));
  REQUIRE(players.crossFade(fixture.owner, 2, 1));
  REQUIRE(players.pause(fixture.owner));

  for (const auto target : {fixture.target, second}) {
    World::AnimationPlayer competing{{"assets/move.dotanimation"}, {{0, target.value}}};
    REQUIRE(fixture.scene->setAnimationPlayer(third, competing));
    CHECK_FALSE(players.advance(.25));
    CHECK(players.state(fixture.owner)->playback.transition->elapsed == 0);
    REQUIRE(fixture.scene->setAnimationPlayer(third, {}));
  }

  REQUIRE(players.play(fixture.owner));
  REQUIRE(players.advance(1));
  CHECK_FALSE(players.state(fixture.owner)->playback.transition);
  World::AnimationPlayer released{{"assets/move.dotanimation"}, {{0, fixture.target.value}}};
  REQUIRE(fixture.scene->setAnimationPlayer(second, released));
  REQUIRE(players.advance(.25));
  CHECK(fixture.scene->transform(fixture.target)->position.x == .25F);
  REQUIRE(players.stop(fixture.owner));
  CHECK(fixture.scene->transform(fixture.target)->position.x == .25F);
  REQUIRE(fixture.scene->setAnimationPlayer(second, {}));
  REQUIRE(players.crossFade(fixture.owner, 0, 1));
  REQUIRE(players.seek(fixture.owner, .5));
  World::AnimationPlayer thirdWriter{{"assets/2.dotanimation"}, {{2, third.value}}};
  REQUIRE(fixture.scene->setAnimationPlayer(second, thirdWriter));
  REQUIRE(players.advance(.25));
}

TEST_CASE("animation.scene_cross_fade_invalid_authority_and_sampling_retain_pose_and_progress") {
  Fixture fixture;
  const auto other = fixture.scene->createEntity("Other").value();
  auto scale = Animation::Clip::create({"Singular middle",
                                        {{1,
                                          Animation::Path::Scale,
                                          Animation::Interpolation::Linear,
                                          {{0, {1, 1, 1, 0}}, {1, {-1, -1, -1, 0}}}}}})
                   .value();
  REQUIRE(Animation::saveClip(fixture.files, "assets/scale.dotanimation", scale));
  fixture.properties.clips.push_back("assets/scale.dotanimation");
  fixture.properties.targets.push_back({1, other.value});
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, fixture.properties));
  auto players = Animation::ScenePlayers::create(fixture.scene, fixture.files).value();
  REQUIRE(players.advance(.25));
  REQUIRE(fixture.scene->setRigidBody(other, World::RigidBody{}));
  CHECK_FALSE(players.crossFade(fixture.owner, 1, 1));
  REQUIRE(fixture.scene->setRigidBody(other, {}));
  REQUIRE(fixture.scene->setParent(other, fixture.target));
  REQUIRE(fixture.scene->setRigidBody(fixture.target, World::RigidBody{}));
  CHECK_FALSE(players.crossFade(fixture.owner, 1, 1));
  REQUIRE(fixture.scene->setRigidBody(fixture.target, {}));
  REQUIRE(fixture.scene->setParent(other, {}));

  for (double seconds : {-1., 86401., std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()}) {
    CHECK_FALSE(players.crossFade(fixture.owner, 1, seconds));
  }

  CHECK_FALSE(players.crossFade(fixture.owner, 2, 1));
  CHECK(players.state(fixture.owner)->clip == 0);
  CHECK(players.state(fixture.owner)->playback.time == .25);
  REQUIRE(players.crossFade(fixture.owner, 1, 1));
  CHECK_FALSE(players.advance(.5));
  CHECK(players.state(fixture.owner)->playback.time == 0);
  CHECK(players.state(fixture.owner)->playback.transition->elapsed == 0);
  CHECK(fixture.scene->transform(fixture.target)->position.x == .25F);
  CHECK(fixture.scene->transform(other)->scale.x == 1);
  World::AnimationPlayer competing{{"assets/move.dotanimation"}, {{0, fixture.target.value}}};
  REQUIRE(fixture.scene->setAnimationPlayer(other, competing));
  CHECK_FALSE(players.advance(.25));
  REQUIRE(fixture.scene->setAnimationPlayer(other, {}));
  REQUIRE(players.advance(.25));
  CHECK(players.state(fixture.owner)->playback.time == .25);
  CHECK(players.state(fixture.owner)->playback.transition->elapsed == .25);
  CHECK(fixture.scene->transform(fixture.target)->position.x == .375F);
  auto foreign = World::Scene::create().value();
  CHECK_FALSE(players.crossFade(foreign.createEntity().value(), 0, 1));
  bool denied{};
  std::thread worker([&] { denied = !players.crossFade(fixture.owner, 0, 1); });
  worker.join();
  CHECK(denied);
  auto moved = std::move(players);
  CHECK_FALSE(players.crossFade(fixture.owner, 0, 1));
  REQUIRE(moved.stop(fixture.owner));
  fixture.properties.enabled = false;
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, fixture.properties));
  CHECK_FALSE(moved.crossFade(fixture.owner, 0, 1));
}

TEST_CASE("animation.scene_cross_fade_cancellation_retires_only_outgoing_writers") {
  Fixture fixture;
  const auto other = fixture.scene->createEntity("Other").value();
  auto clip = Animation::Clip::create({"Other",
                                       {{1,
                                         Animation::Path::Translation,
                                         Animation::Interpolation::Linear,
                                         {{0, {3, 0, 0, 0}}, {2, {5, 0, 0, 0}}}}}})
                  .value();
  REQUIRE(Animation::saveClip(fixture.files, "assets/other.dotanimation", clip));
  fixture.properties.clips.push_back("assets/other.dotanimation");
  fixture.properties.targets.push_back({1, other.value});
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.owner, fixture.properties));
  auto players = Animation::ScenePlayers::create(fixture.scene, fixture.files).value();
  REQUIRE(players.advance(.25));
  REQUIRE(players.crossFade(fixture.owner, 1, 1));
  REQUIRE(players.advance(.25));

  SUBCASE("stop") {
    REQUIRE(players.stop(fixture.owner));
  }

  SUBCASE("seek") {
    REQUIRE(players.seek(fixture.owner, .5));
  }

  SUBCASE("restart") {
    REQUIRE(players.play(fixture.owner, {}, true));
  }

  SUBCASE("immediate same-clip switch") {
    REQUIRE(players.crossFade(fixture.owner, 1, 0));
  }

  CHECK_FALSE(players.state(fixture.owner)->playback.transition);
  World::AnimationPlayer competing{{"assets/move.dotanimation"}, {{0, fixture.target.value}}};
  REQUIRE(fixture.scene->setAnimationPlayer(other, competing));
  REQUIRE(players.advance(.25));
  CHECK(fixture.scene->transform(fixture.target)->position.x == .25F);
  REQUIRE(players.stop(fixture.owner));
  CHECK(fixture.scene->transform(fixture.target)->position.x == .25F);
}

TEST_CASE("animation.controller_native_parameters_pause_resume_reset_and_authority_are_shared") {
  using namespace Animation;
  Fixture f;
  auto spec = ControllerDescription{
      "Locomotion",
      "Idle",
      {{"Speed", 0.0}, {"Grounded", true}},
      {{"Idle", "assets/move.dotanimation", -1, false},
       {"Walk", "assets/move.dotanimation", 2, false}},
      {{"Move", "Idle", "Walk", {{"Speed", ConditionOp::Greater, .1}}, .5}}};
  REQUIRE(saveController(f.files, "assets/motion.dotcontroller", Controller::create(spec).value()));
  f.properties.controller = "assets/motion.dotcontroller";
  f.properties.playOnStart = false;
  REQUIRE(f.scene->setAnimationPlayer(f.owner, f.properties));
  auto players = ScenePlayers::create(f.scene, f.files).value();
  REQUIRE(players.advance(.5));
  CHECK(players.state(f.owner)->controller->playback.elapsedSeconds == 0);
  REQUIRE(players.play(f.owner));
  CHECK(players.state(f.owner)->playback.time == 2);
  REQUIRE(players.parameters(f.owner, std::array{ControllerParameter{"Speed", 1.0}}));
  REQUIRE(players.advance(0));
  CHECK(players.state(f.owner)->controller->playback.state == "Idle");
  REQUIRE(players.advance(.25));
  auto walking = players.state(f.owner).value();
  CHECK(walking.controller->playback.state == "Walk");
  CHECK(walking.controller->playback.transitions == 1);
  CHECK(walking.controller->playback.elapsedSeconds == .25);
  CHECK(walking.playback.time == .5);
  CHECK(walking.playback.speed == 2);
  CHECK(walking.playback.transition->elapsed == .25);
  REQUIRE(players.pause(f.owner));
  REQUIRE(players.advance(1));
  CHECK(players.state(f.owner)->controller->playback.elapsedSeconds == .25);
  CHECK(players.state(f.owner)->playback.time == .5);
  CHECK_FALSE(players.play(f.owner, 0));
  CHECK_FALSE(players.play(f.owner, {}, true));
  CHECK_FALSE(players.crossFade(f.owner, 0, 0));
  CHECK_FALSE(players.configure(f.owner, 1, true));
  CHECK_FALSE(players.seek(f.owner, 0));
  CHECK_FALSE(players.stop(f.owner));
  CHECK_FALSE(players.restorePlayback(f.owner, walking.playback, {}));
  CHECK_FALSE(players.parameters(f.owner, std::array{ControllerParameter{"Speed", true}}));
  CHECK_FALSE(players.parameters(f.owner, std::array{ControllerParameter{"Missing", 0.0}}));
  REQUIRE(players.resetController(f.owner));
  const auto reset = players.state(f.owner).value();
  CHECK(reset.controller->playback.state == "Idle");
  CHECK(reset.controller->playback.elapsedSeconds == 0);
  CHECK(reset.controller->playback.transitions == 0);
  CHECK(std::get<double>(reset.controller->playback.parameters.front().value) == 0);
  CHECK(reset.playback.time == 2);
  CHECK(reset.playback.paused);
  CHECK_FALSE(reset.playback.transition);
  CHECK(f.scene->entity(f.owner)->animation == f.properties);
  REQUIRE(players.play(f.owner));
  REQUIRE(players.advance(.25));
  CHECK(players.state(f.owner)->playback.time == 1.75);
  CHECK(walking.controller->playback.state == "Walk");
  auto moved = std::move(players);
  CHECK_FALSE(players.parameters(f.owner, {}));
  CHECK_FALSE(players.resetController(f.owner));
  CHECK(moved.state(f.owner)->controller->playback.elapsedSeconds == .25);
}

TEST_CASE("animation.controller_failed_pose_retains_state_age_clocks_layers_and_parameter_intent") {
  using namespace Animation;
  Fixture f;
  auto singular
      = Clip::create(
            {"Singular",
             {{0, Path::Scale, Interpolation::Linear, {{0, {1, 1, 1, 0}}, {2, {-1, 1, 1, 0}}}}}})
            .value();
  REQUIRE(saveClip(f.files, "assets/bad.dotanimation", singular));
  f.properties.clips.push_back("assets/bad.dotanimation");
  f.properties.controller = "assets/motion.dotcontroller";
  f.properties.layers = {{"Action", 0, {{0, Path::Translation}}, .5, 1, true, true}};
  auto graph
      = Controller::create(
            {"Atomic",
             "Idle",
             {{"Go", false}},
             {{"Idle", "assets/move.dotanimation"}, {"Bad", "assets/bad.dotanimation", 1, false}},
             {{"GoBad", "Idle", "Bad", {{"Go", ConditionOp::Equal, true}}, 0}}})
            .value();
  REQUIRE(saveController(f.files, *f.properties.controller, graph));
  REQUIRE(f.scene->setAnimationPlayer(f.owner, f.properties));
  auto players = ScenePlayers::create(f.scene, f.files).value();
  REQUIRE(players.advance(.25));
  REQUIRE(players.parameters(f.owner, std::array{ControllerParameter{"Go", true}}));
  const auto before = players.state(f.owner).value();
  const auto pose = f.scene->transform(f.target).value();
  CHECK_FALSE(players.advance(1));
  const auto after = players.state(f.owner).value();
  CHECK(after.clip == before.clip);
  CHECK(after.playback.time == before.playback.time);
  CHECK(after.layers.front().playback.time == before.layers.front().playback.time);
  CHECK(after.controller->playback.state == "Idle");
  CHECK(after.controller->playback.elapsedSeconds == before.controller->playback.elapsedSeconds);
  CHECK(after.controller->playback.transitions == 0);
  CHECK(std::get<bool>(after.controller->playback.parameters.front().value));
  CHECK(f.scene->transform(f.target)->position.x == pose.position.x);
  CHECK(f.scene->transform(f.target)->scale.x == pose.scale.x);
  REQUIRE(players.advance(.25));
  CHECK(players.state(f.owner)->controller->playback.state == "Bad");
}

TEST_CASE("animation.controller_reserves_inactive_state_writers_and_prepared_definitions") {
  using namespace Animation;
  Fixture f;
  auto other = f.scene->createEntity("Later target").value();
  const auto later = Clip::create({"Later",
                                   {{1,
                                     Path::Translation,
                                     Interpolation::Linear,
                                     {{0, {0, 0, 0, 0}}, {2, {4, 0, 0, 0}}}}}})
                         .value();
  REQUIRE(saveClip(f.files, "assets/later.dotanimation", later));
  f.properties.clips.push_back("assets/later.dotanimation");
  f.properties.targets.push_back({1, other.value});
  f.properties.controller = "assets/motion.dotcontroller";
  auto graph = Controller::create(
                   {"Ownership",
                    "Idle",
                    {},
                    {{"Idle", "assets/move.dotanimation"}, {"Later", "assets/later.dotanimation"}},
                    {}})
                   .value();
  REQUIRE(saveController(f.files, *f.properties.controller, graph));
  REQUIRE(f.scene->setAnimationPlayer(f.owner, f.properties));
  const auto clip = loadClip(f.files, "assets/move.dotanimation").value();
  std::vector<ClipAsset> clips{{"assets/move.dotanimation", clip},
                               {"assets/later.dotanimation", later}};
  std::vector<ControllerAsset> controllers{{*f.properties.controller, graph}};
  CHECK_FALSE(ScenePlayers::createPrepared(f.scene, f.files, clips));
  CHECK_FALSE(ScenePlayers::createPrepared(f.scene, f.files, clips,
                                           std::vector{controllers.front(), controllers.front()}));
  REQUIRE(std::filesystem::remove(f.files.root() / *f.properties.controller));
  auto players = ScenePlayers::createPrepared(f.scene, f.files, clips, controllers);
  REQUIRE(players);
  REQUIRE(players->advance(.25));
  CHECK(players->state(f.owner)->controller->playback.state == "Idle");
  auto competitor = f.scene->createEntity("Competitor").value();
  World::AnimationPlayer competition{{"assets/later.dotanimation"}, {{1, other.value}}};
  REQUIRE(f.scene->setAnimationPlayer(competitor, competition));
  CHECK_FALSE(players->advance(.25));
  CHECK(players->state(f.owner)->playback.time == .25);
  REQUIRE(f.scene->setAnimationPlayer(competitor, {}));
  REQUIRE(players->advance(.25));
  auto body = World::RigidBody{};
  REQUIRE(f.scene->setRigidBody(other, body));
  CHECK_FALSE(players->advance(.25));
  CHECK(players->state(f.owner)->playback.time == .5);
}

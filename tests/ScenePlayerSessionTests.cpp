#include <doctest/doctest.h>

#include <filesystem>
#include <limits>
#include <memory>
#include <thread>
#include <utility>

import Dot.Animation.Clip;
import Dot.Animation.Files;
import Dot.Animation.ScenePlayers;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Project;
import Dot.Project.GameSession;
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

#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Animation.Clip;
import Dot.Animation.SceneAnimator;
import Dot.Math;
import Dot.World.Scene;

namespace {
  using namespace Dot;
  using namespace Dot::Animation;

  auto moving() -> Clip {
    return Clip::create({"Move",
                         {{0,
                           Path::Translation,
                           Interpolation::Linear,
                           {{0, {0, 0, 0, 0}}, {2, {4, 0, 0, 0}}}}}})
        .value();
  }
}  // namespace

TEST_CASE("animation.inactive_layer_clock_weight_controls_do_not_prime_base_pose") {
  auto scene = World::Scene::create().value();
  auto entity = scene.createEntity().value();
  REQUIRE(scene.setTransform(entity, {{7, 0, 0}}));
  auto clip
      = Clip::create(
            {"Scale",
             {{0, Path::Scale, Interpolation::Linear, {{0, {2, 1, 1, 0}}, {2, {2, 1, 1, 0}}}}}})
            .value();
  const std::array bindings{NodeBinding{0, entity}};
  const std::array definitions{LayerDefinition{"Action", clip, {{0, Path::Scale}}}};
  auto player = SceneAnimator::bind(scene, moving(), bindings, definitions).value();
  REQUIRE(player.controlLayer(scene, "Action", WeightLayer{.5}));
  REQUIRE(player.controlLayer(scene, "Action", PauseLayer{}));
  REQUIRE(player.controlLayer(scene, "Action", ConfigureLayer{2, false}));
  REQUIRE(player.controlLayer(scene, "Action", StopLayer{}));
  CHECK(scene.transform(entity)->position.x == 7);
  CHECK(scene.transform(entity)->scale.x == 1);
  REQUIRE(player.controlLayer(scene, "Action", PlayLayer{true}));
  CHECK(scene.transform(entity)->position.x == 0);
  CHECK(scene.transform(entity)->scale.x == 1.5F);
}

TEST_CASE("animation.layers_mask_pose_without_feedback_and_preserve_other_channels") {
  auto scene = World::Scene::create().value();
  auto root = scene.createEntity("Root").value();
  auto head = scene.createEntity("Head").value();
  REQUIRE(scene.setTransform(head, {{7, 2, 3}}));
  const auto overlay = Clip::create({"Head",
                                     {{1,
                                       Path::Translation,
                                       Interpolation::Linear,
                                       {{0, {10, 2, 3, 0}}, {2, {14, 2, 3, 0}}}}}})
                           .value();
  const std::array bindings{NodeBinding{0, root}, NodeBinding{1, head}};
  const std::array layers{
      LayerDefinition{"Head", overlay, {{1, Path::Translation, .5}}, 1, 1, true, true}};
  auto player = SceneAnimator::bind(scene, moving(), bindings, layers).value();
  REQUIRE(player.controlLayer(scene, "Head", PauseLayer{}));
  REQUIRE(player.controlLayer(scene, "Head", PlayLayer{}));
  REQUIRE(player.seek(scene, 1));
  CHECK(scene.transform(root)->position.x == 2);
  CHECK(scene.transform(head)->position.x == 8.5F);
  REQUIRE(player.seek(scene, 1));
  CHECK(scene.transform(head)->position.x == 8.5F);
  REQUIRE(player.advance(scene, .5));
  CHECK(scene.transform(root)->position.x == 3);
  CHECK(scene.transform(head)->position.x == 9);
  CHECK(scene.transform(head)->scale.x == 1);
  REQUIRE(player.controlLayer(scene, "Head", StopLayer{}));
  CHECK(scene.transform(head)->position.x == 7);
  CHECK(player.state()->time == 1.5);
}

TEST_CASE("animation.layer_order_and_crossfade_use_unlayered_origins") {
  auto scene = World::Scene::create().value();
  auto entity = scene.createEntity().value();
  auto ten = Clip::create({"Ten",
                           {{0,
                             Path::Translation,
                             Interpolation::Linear,
                             {{0, {10, 0, 0, 0}}, {2, {10, 0, 0, 0}}}}}})
                 .value();
  auto twenty = Clip::create({"Twenty",
                              {{0,
                                Path::Translation,
                                Interpolation::Linear,
                                {{0, {20, 0, 0, 0}}, {2, {20, 0, 0, 0}}}}}})
                    .value();
  const std::array bindings{NodeBinding{0, entity}};
  const std::array definitions{
      LayerDefinition{"First", ten, {{0, Path::Translation}}, .5, 1, true, true},
      LayerDefinition{"Second", twenty, {{0, Path::Translation}}, .5, 1, true, false}};
  auto player = SceneAnimator::bind(scene, moving(), bindings, definitions).value();
  REQUIRE(player.seek(scene, 0));
  CHECK(scene.transform(entity)->position.x == 5);
  REQUIRE(player.crossFade(scene, twenty, 1));
  CHECK(scene.transform(entity)->position.x == 5);
  REQUIRE(player.advance(scene, .5));
  CHECK(scene.transform(entity)->position.x == doctest::Approx(10.25));
  const auto before = scene.transform(entity)->position.x;
  REQUIRE(player.crossFade(scene, ten, 1));
  CHECK(scene.transform(entity)->position.x == before);
  REQUIRE(player.controlLayer(scene, "Second", PlayLayer{true}));
  CHECK(scene.transform(entity)->position.x == doctest::Approx((before + 20) / 2));
  CHECK(player.state()->transition->elapsed == 0);
  REQUIRE(player.controlLayer(scene, "First", StopLayer{}));
  REQUIRE(player.controlLayer(scene, "Second", StopLayer{}));
  CHECK(scene.transform(entity)->position.x == doctest::Approx(10.5));
}

TEST_CASE("animation.layer_failure_preserves_every_clock_weight_pose_and_allows_repair") {
  auto scene = World::Scene::create().value();
  auto entity = scene.createEntity().value();
  auto invalid
      = Clip::create(
            {"Scale",
             {{0, Path::Scale, Interpolation::Linear, {{0, {1, 1, 1, 0}}, {2, {-1, 1, 1, 0}}}}}})
            .value();
  const std::array bindings{NodeBinding{0, entity}};
  const std::array layers{
      LayerDefinition{"Action", invalid, {{0, Path::Scale}}, 1, 1, false, true}};
  auto player = SceneAnimator::bind(scene, moving(), bindings, layers).value();
  REQUIRE(player.seek(scene, 0));
  const auto pose = scene.transform(entity).value();
  const auto clocks = player.layers().value();
  CHECK_FALSE(player.advance(scene, 1));
  CHECK(scene.transform(entity)->position == pose.position);
  CHECK(scene.transform(entity)->rotation == pose.rotation);
  CHECK(scene.transform(entity)->scale == pose.scale);
  CHECK(player.state()->time == 0);
  CHECK(player.layers().value() == clocks);
  CHECK_FALSE(player.controlLayer(scene, "Action", SeekLayer{1}));
  CHECK(player.layers().value() == clocks);
  REQUIRE(player.controlLayer(scene, "Action", WeightLayer{0}));
  REQUIRE(player.advance(scene, 1));
  CHECK(player.layers()->front().playback.time == 1);
  CHECK(scene.transform(entity)->position.x == 2);
  CHECK_FALSE(player.controlLayer(scene, "Action", WeightLayer{1}));
  CHECK(player.layers()->front().playback.weight == 0);
  CHECK(scene.transform(entity)->scale.x == 1);
  REQUIRE(player.controlLayer(scene, "Action", SeekLayer{.5}));
  REQUIRE(player.controlLayer(scene, "Action", WeightLayer{1}));
  CHECK(scene.transform(entity)->scale.x == .5F);
}

TEST_CASE("animation.layers_continue_after_base_end_and_global_pause_stop_freeze_all") {
  auto scene = World::Scene::create().value();
  auto entity = scene.createEntity().value();
  auto overlay
      = Clip::create(
            {"Layer",
             {{0, Path::Scale, Interpolation::Linear, {{0, {1, 1, 1, 0}}, {4, {5, 1, 1, 0}}}}}})
            .value();
  const std::array bindings{NodeBinding{0, entity}};
  const std::array layers{
      LayerDefinition{"Action", overlay, {{0, Path::Scale}}, 1, 1, false, true}};
  auto player = SceneAnimator::bind(scene, moving(), bindings, layers).value();
  REQUIRE(player.configure(1, false));
  REQUIRE(player.advance(scene, 2));
  CHECK_FALSE(player.state()->playing);
  CHECK_FALSE(player.state()->paused);
  REQUIRE(player.advance(scene, .5));
  CHECK(player.state()->time == 2);
  CHECK(player.layers()->front().playback.time == 2.5);
  CHECK(scene.transform(entity)->scale.x == 3.5F);
  REQUIRE(player.pause());
  REQUIRE(player.advance(scene, 1));
  CHECK(player.layers()->front().playback.time == 2.5);
  REQUIRE(player.play());
  REQUIRE(player.configure(0, true));
  REQUIRE(player.advance(scene, .5));
  CHECK(player.state()->time == 2);
  CHECK(player.layers()->front().playback.time == 3);
  REQUIRE(player.stop(scene));
  CHECK(player.state()->paused);
  CHECK_FALSE(player.layers()->front().playback.active);
  CHECK(scene.transform(entity)->position.x == 0);
  CHECK(scene.transform(entity)->scale.x == 1);
}

TEST_CASE("animation.complete_layer_restore_samples_all_contributions_before_publication") {
  auto scene = World::Scene::create().value();
  auto entity = scene.createEntity().value();
  auto negative
      = Clip::create(
            {"Negative",
             {{0, Path::Scale, Interpolation::Linear, {{0, {-1, 1, 1, 0}}, {2, {-1, 1, 1, 0}}}}}})
            .value();
  auto positive
      = Clip::create(
            {"Positive",
             {{0, Path::Scale, Interpolation::Linear, {{0, {1, 1, 1, 0}}, {2, {1, 1, 1, 0}}}}}})
            .value();
  const std::array bindings{NodeBinding{0, entity}};
  const std::array definitions{LayerDefinition{"Negative", negative, {{0, Path::Scale}}, .5},
                               LayerDefinition{"Positive", positive, {{0, Path::Scale}}}};
  auto player = SceneAnimator::bind(scene, moving(), bindings, definitions).value();
  REQUIRE(player.pause());
  CHECK_FALSE(player.controlLayer(scene, "Negative", SeekLayer{0}));
  CHECK_FALSE(player.layers()->front().playback.active);
  const std::array clocks{LayerPlayback{0, .5, 1, false, false, true},
                          LayerPlayback{0, 1, 1, false, false, true}};
  REQUIRE(player.restorePlayback(scene, player.state().value(), clocks));
  CHECK(scene.transform(entity)->scale.x == 1);
  CHECK(player.layers()->front().playback.active);
  CHECK(player.layers()->back().playback.active);
  CHECK_FALSE(player.restorePlayback(scene, player.state().value(), {}));
  CHECK(scene.transform(entity)->scale.x == 1);
}

TEST_CASE("animation.scene_clock_forward_reverse_loop_seek_pause_zero_rate_and_endpoints") {
  auto scene = World::Scene::create().value();
  auto entity = scene.createEntity().value();
  const std::array bindings{NodeBinding{0, entity}};
  auto player = SceneAnimator::bind(scene, moving(), bindings).value();
  REQUIRE(player.advance(scene, .5));
  CHECK(player.state()->time == .5);
  CHECK(scene.entity(entity)->transform.position.x == 1);
  REQUIRE(player.pause());
  REQUIRE(player.advance(scene, 1));
  CHECK(player.state()->time == .5);
  REQUIRE(player.seek(scene, 2));
  REQUIRE(player.play());
  REQUIRE(player.advance(scene, 0));
  CHECK(player.state()->time == 2);
  CHECK(scene.entity(entity)->transform.position.x == 4);
  REQUIRE(player.configure(-1, true));
  REQUIRE(player.advance(scene, 2.5));
  CHECK(player.state()->time == doctest::Approx(1.5));
  CHECK(scene.entity(entity)->transform.position.x == 3);
  REQUIRE(player.configure(1, false));
  REQUIRE(player.advance(scene, 1));
  CHECK(player.state()->time == 2);
  CHECK_FALSE(player.state()->playing);
  REQUIRE(player.configure(-1, false));
  REQUIRE(player.play());
  REQUIRE(player.advance(scene, 3));
  CHECK(player.state()->time == 0);
  CHECK_FALSE(player.state()->playing);
  REQUIRE(player.play(true));
  REQUIRE(player.configure(0, false));
  REQUIRE(player.advance(scene, 5));
  CHECK(player.state()->time == 0);
  CHECK(player.state()->playing);
  const auto state = player.state().value();
  CHECK_FALSE(player.configure(17, true));
  CHECK_FALSE(player.configure(std::numeric_limits<double>::quiet_NaN(), true));
  CHECK_FALSE(player.seek(scene, -1));
  CHECK_FALSE(player.seek(scene, 2.1));
  CHECK_FALSE(player.advance(scene, -1));
  CHECK_FALSE(player.advance(scene, 86401));
  CHECK(player.state()->time == state.time);
  CHECK(player.state()->speed == state.speed);
  CHECK(player.state()->playing == state.playing);
}

TEST_CASE("animation.scene_parent_child_channels_and_unanimated_fields_use_one_local_batch") {
  auto scene = World::Scene::create().value();
  auto parent = scene.createEntity("Parent").value();
  auto child = scene.createEntity("Child").value();
  REQUIRE(scene.setParent(child, parent));
  REQUIRE(scene.setTransform(parent, {{10, 0, 0}}));
  REQUIRE(scene.setTransform(child, {{0, 2, 0}, {}, {2, 3, 4}}));
  auto clip
      = Clip::create(
            {"Hierarchy",
             {{0, Path::Translation, Interpolation::Linear, {{0, {0, 0, 0, 0}}, {2, {4, 0, 0, 0}}}},
              {1,
               Path::Translation,
               Interpolation::Linear,
               {{0, {0, 1, 0, 0}}, {2, {0, 5, 0, 0}}}}}})
            .value();
  const std::array bindings{NodeBinding{0, parent}, NodeBinding{1, child}};
  auto player = SceneAnimator::bind(scene, clip, bindings).value();
  REQUIRE(player.seek(scene, 1));
  const auto world = scene.worldTransform(child).value();
  CHECK(world.data[12] == 2);
  CHECK(world.data[13] == 3);
  CHECK(scene.entity(child)->transform.scale == Math::Vec3{2, 3, 4});
  auto transform = scene.entity(child)->transform;
  transform.scale = {3, 4, 5};
  REQUIRE(scene.setTransform(child, transform));
  REQUIRE(player.seek(scene, 0));
  CHECK(scene.entity(child)->transform.scale == Math::Vec3{3, 4, 5});
  REQUIRE(player.stop(scene));
  CHECK(scene.entity(parent)->transform.position.x == 10);
  CHECK(scene.entity(child)->transform.position.y == 2);
  CHECK(scene.entity(child)->transform.scale == Math::Vec3{3, 4, 5});
  CHECK_FALSE(player.state()->playing);
}

TEST_CASE(
    "animation.scene_failed_channel_or_final_world_validation_rolls_back_clock_and_all_nodes") {
  auto scene = World::Scene::create().value();
  auto first = scene.createEntity().value();
  auto second = scene.createEntity().value();
  auto clip
      = Clip::create(
            {"Invalid midpoint",
             {{0, Path::Translation, Interpolation::Linear, {{0, {0, 0, 0, 0}}, {2, {4, 0, 0, 0}}}},
              {1, Path::Scale, Interpolation::Linear, {{0, {-1, 1, 1, 0}}, {2, {1, 1, 1, 0}}}}}})
            .value();
  const std::array bindings{NodeBinding{0, first}, NodeBinding{1, second}};
  auto player = SceneAnimator::bind(scene, clip, bindings).value();
  REQUIRE(player.seek(scene, .5));
  const auto before = scene.entity(first)->transform;
  const auto scale = scene.entity(second)->transform.scale;
  CHECK_FALSE(player.advance(scene, .5));
  CHECK(player.state()->time == .5);
  CHECK(scene.entity(first)->transform.position == before.position);
  CHECK(scene.entity(second)->transform.scale == scale);
  REQUIRE(scene.destroyEntity(second));
  CHECK_FALSE(player.seek(scene, 0));
  CHECK(player.state()->time == .5);
  CHECK(scene.entity(first)->transform.position == before.position);
}

TEST_CASE("animation.scene_bindings_scope_moves_owner_and_static_clip_lifetime") {
  auto scene = World::Scene::create().value();
  auto entity = scene.createEntity().value();
  const std::array binding{NodeBinding{0, entity}};
  CHECK_FALSE(SceneAnimator::bind(scene, Clip{}, binding));
  CHECK_FALSE(SceneAnimator::bind(scene, moving(), {}));
  const std::array duplicate{binding[0], binding[0]};
  CHECK_FALSE(SceneAnimator::bind(scene, moving(), duplicate));
  const std::array missing{NodeBinding{1, entity}};
  CHECK_FALSE(SceneAnimator::bind(scene, moving(), missing));
  auto player = SceneAnimator::bind(scene, moving(), binding).value();
  auto movedScene = std::move(scene);
  REQUIRE(player.seek(movedScene, 1));
  auto foreign = World::Scene::create().value();
  CHECK_FALSE(player.seek(foreign, 0));
  CHECK(player.state()->time == 1);
  bool rejected{};
  std::thread thread([&] { rejected = !player.state() && !player.pause(); });
  thread.join();
  CHECK(rejected);
  auto moved = std::move(player);
  CHECK_FALSE(player.state());
  CHECK_FALSE(player.configure(1, true));
  CHECK_FALSE(player.play());
  CHECK_FALSE(player.pause());
  CHECK_FALSE(player.seek(movedScene, 0));
  CHECK_FALSE(player.advance(movedScene, 0));
  CHECK_FALSE(player.stop(movedScene));
  REQUIRE(moved.seek(movedScene, 2));
  auto constant = Clip::create({"Constant",
                                {{0, Path::Translation, Interpolation::Step, {{0, {3, 0, 0, 0}}}}}})
                      .value();
  auto still = SceneAnimator::bind(movedScene, constant, binding).value();
  REQUIRE(still.advance(movedScene, 10));
  CHECK(still.state()->time == 0);
  CHECK(still.state()->playing);
  CHECK(movedScene.entity(entity)->transform.position.x == 3);
  REQUIRE(still.configure(1, false));
  REQUIRE(still.advance(movedScene, 1));
  CHECK_FALSE(still.state()->playing);
}

TEST_CASE("animation.scene_final_world_overflow_rolls_back_valid_local_channels") {
  auto scene = World::Scene::create().value();
  auto independent = scene.createEntity().value();
  auto parent = scene.createEntity().value();
  auto child = scene.createEntity().value();
  REQUIRE(scene.setTransform(parent, {{}, {}, {1e20F, 1e20F, 1e20F}}));
  REQUIRE(scene.setParent(child, parent));
  auto clip
      = Clip::create(
            {"Overflow",
             {{0, Path::Translation, Interpolation::Linear, {{0, {0, 0, 0, 0}}, {1, {2, 0, 0, 0}}}},
              {1,
               Path::Translation,
               Interpolation::Linear,
               {{0, {0, 0, 0, 0}}, {1, {1e20F, 0, 0, 0}}}}}})
            .value();
  const std::array bindings{NodeBinding{0, independent}, NodeBinding{1, child}};
  auto player = SceneAnimator::bind(scene, clip, bindings).value();
  CHECK_FALSE(player.advance(scene, .5));
  CHECK(player.state()->time == 0);
  CHECK(player.state()->playing);
  CHECK(scene.entity(independent)->transform.position.x == 0);
  CHECK(scene.entity(child)->transform.position.x == 0);
  CHECK(scene.worldTransform(child)->data[12] == 0);
}

TEST_CASE("animation.scene_clip_switch_restores_prior_fields_preserves_bind_pose_and_is_atomic") {
  auto scene = World::Scene::create().value();
  auto first = scene.createEntity().value();
  auto second = scene.createEntity().value();
  REQUIRE(scene.setTransform(first, {{7, 0, 0}}));
  REQUIRE(scene.setTransform(second, {{0, 3, 0}}));
  const std::array bindings{NodeBinding{0, first}, NodeBinding{1, second}};
  auto player = SceneAnimator::bind(scene, moving(), bindings).value();
  REQUIRE(player.seek(scene, 1));
  auto scale
      = Clip::create(
            {"Scale",
             {{1, Path::Scale, Interpolation::Linear, {{0, {2, 2, 2, 0}}, {2, {4, 4, 4, 0}}}}}})
            .value();
  REQUIRE(player.setClip(scene, scale));
  CHECK(scene.transform(first)->position.x == 7);
  CHECK(scene.transform(second)->scale.x == 2);
  CHECK(player.state()->time == 0);
  REQUIRE(player.advance(scene, .5));
  CHECK(scene.transform(second)->scale.x == 2.5F);
  REQUIRE(player.configure(-1, false));
  REQUIRE(player.setClip(scene, moving()));
  CHECK(scene.transform(second)->scale.x == 1);
  CHECK(scene.transform(first)->position.x == 4);
  CHECK(player.state()->time == 2);
  REQUIRE(player.stop(scene));
  CHECK(scene.transform(first)->position.x == 7);
  CHECK_FALSE(player.state()->playing);
  auto missing = Clip::create({"Unbound",
                               {{2, Path::Translation, Interpolation::Step, {{0, {1, 0, 0, 0}}}}}})
                     .value();
  CHECK_FALSE(player.setClip(scene, missing));
  CHECK_FALSE(player.setClip(scene, Clip{}));
  auto overflowParent = scene.createEntity().value();
  REQUIRE(scene.setTransform(overflowParent, {{}, {}, {1e20F, 1e20F, 1e20F}}));
  REQUIRE(scene.setParent(second, overflowParent));
  auto overflow
      = Clip::create(
            {"Overflow", {{1, Path::Translation, Interpolation::Step, {{0, {1e20F, 0, 0, 0}}}}}})
            .value();
  CHECK_FALSE(player.setClip(scene, overflow));
  CHECK(scene.transform(first)->position.x == 7);
  CHECK(scene.transform(second)->position.y == 3);
  CHECK(player.state()->time == 0);
  CHECK_FALSE(player.state()->playing);
  REQUIRE(player.play());
  REQUIRE(player.seek(scene, 1));
  CHECK(scene.transform(first)->position.x == 2);
  auto moved = std::move(player);
  CHECK_FALSE(player.setClip(scene, moving()));
  bool rejected{};
  std::thread worker([&] { rejected = !moved.setClip(scene, moving()); });
  worker.join();
  CHECK(rejected);
}

TEST_CASE("animation.atomic_state_transition_retains_outgoing_settings_and_advances_once") {
  auto scene = World::Scene::create().value();
  auto entity = scene.createEntity().value();
  const std::array bindings{NodeBinding{0, entity}};
  auto player = SceneAnimator::bind(scene, moving(), bindings).value();
  REQUIRE(player.seek(scene, .5));
  REQUIRE(player.configure(2, true));
  auto incoming = Clip::create({"Incoming",
                                {{0,
                                  Path::Translation,
                                  Interpolation::Linear,
                                  {{0, {10, 0, 0, 0}}, {2, {20, 0, 0, 0}}}}}})
                      .value();
  REQUIRE(player.transitionAndAdvance(scene, {incoming, 1, -1, false}, .25));
  CHECK(player.state()->time == 1.75);
  CHECK(player.state()->speed == -1);
  CHECK_FALSE(player.state()->looping);
  CHECK(player.state()->transition->elapsed == .25);
  CHECK(scene.transform(entity)->position.x == doctest::Approx(2 * .75 + 18.75 * .25));
  REQUIRE(player.configure(0, false));
  const auto before = scene.transform(entity).value();
  REQUIRE(player.advance(scene, .25));
  CHECK(player.state()->time == 1.75);
  CHECK(scene.transform(entity)->position.x
        == doctest::Approx((before.position.x - 18.75 * .25) / .75 * .5 + 18.75 * .5));
}

TEST_CASE("animation.atomic_state_transition_failure_retains_clip_clocks_layers_and_pose") {
  auto scene = World::Scene::create().value();
  auto entity = scene.createEntity().value();
  const std::array bindings{NodeBinding{0, entity}};
  const std::array layers{
      LayerDefinition{"Motion", moving(), {{0, Path::Translation}}, .25, 1, true, true}};
  auto player = SceneAnimator::bind(scene, moving(), bindings, layers).value();
  REQUIRE(player.advance(scene, .25));
  const auto pose = scene.transform(entity).value();
  const auto clock = player.state().value();
  const auto oldLayers = player.layers().value();
  const auto invalid
      = Clip::create(
            {"Singular",
             {{0, Path::Scale, Interpolation::Linear, {{0, {1, 1, 1, 0}}, {2, {-1, 1, 1, 0}}}}}})
            .value();
  CHECK_FALSE(player.transitionAndAdvance(scene, {invalid, 0, 1, false}, 1));
  CHECK(player.state()->time == clock.time);
  CHECK(player.state()->speed == clock.speed);
  CHECK(player.state()->looping == clock.looping);
  CHECK(player.state()->transition == clock.transition);
  CHECK(player.layers().value() == oldLayers);
  CHECK(scene.transform(entity)->position == pose.position);
  CHECK(scene.transform(entity)->scale == pose.scale);
  CHECK_FALSE(player.transitionAndAdvance(scene, {moving(), -1, 1, false}, .1));
  CHECK_FALSE(player.transitionAndAdvance(scene, {moving(), 0, 17, false}, .1));
  CHECK_FALSE(player.transitionAndAdvance(scene, {moving(), 0, 1, false}, -1));
  REQUIRE(player.transitionAndAdvance(scene, {moving(), 0, -1, false, true}, 0));
  CHECK(player.state()->paused);
  CHECK(player.state()->time == 2);
}

TEST_CASE(
    "animation.atomic_state_transition_restores_departed_channels_and_entities_before_advance") {
  auto scene = World::Scene::create().value();
  auto first = scene.createEntity().value();
  auto second = scene.createEntity().value();
  REQUIRE(scene.setTransform(first, {{7, 0, 0}}));
  auto next
      = Clip::create(
            {"Scale",
             {{1, Path::Scale, Interpolation::Linear, {{0, {1, 1, 1, 0}}, {2, {3, 1, 1, 0}}}}}})
            .value();
  auto animator = SceneAnimator::bind(scene, moving(),
                                      std::array{NodeBinding{0, first}, NodeBinding{1, second}})
                      .value();
  REQUIRE(animator.seek(scene, .5));
  CHECK(scene.transform(first)->position.x == 1);
  REQUIRE(animator.transitionAndAdvance(scene, {next, 0}, .5));
  CHECK(scene.transform(first)->position.x == 7);
  CHECK(scene.transform(second)->scale.x == 1.5F);
  REQUIRE(animator.transitionAndAdvance(scene, {moving(), 0}, .25));
  CHECK(scene.transform(first)->position.x == .5F);
  CHECK(scene.transform(second)->scale.x == 1);
  auto same
      = Clip::create(
            {"Same entity",
             {{0, Path::Scale, Interpolation::Linear, {{0, {2, 1, 1, 0}}, {2, {4, 1, 1, 0}}}}}})
            .value();
  REQUIRE(animator.transitionAndAdvance(scene, {same, 0}, .5));
  CHECK(scene.transform(first)->position.x == 7);
  CHECK(scene.transform(first)->scale.x == 2.5F);
}

TEST_CASE("animation.additive_local_trs_uses_reference_weights_and_no_output_feedback") {
  const auto vector = [](Math::Quat q) { return Math::Vec4{q.x, q.y, q.z, q.w}; };
  const auto baseRotation = Math::rotationEulerDegrees({90, 0, 0}).value();
  auto scene = World::Scene::create().value();
  const auto entity = scene.createEntity().value();
  auto base
      = Clip::create({"Base",
                      {{0, Path::Translation, Interpolation::Step, {{0, {10, 2, -1, 0}}}},
                       {0, Path::Scale, Interpolation::Step, {{0, {-2, 3, 4, 0}}}},
                       {0, Path::Rotation, Interpolation::Step, {{0, vector(baseRotation)}}}}})
            .value();
  const auto first = Math::rotationEulerDegrees({0, 30, 0}).value();
  const auto last = Math::rotationEulerDegrees({0, 90, 0}).value();
  const auto action
      = Clip::create(
            {"Action",
             {{0, Path::Translation, Interpolation::Linear, {{0, {4, 2, 1, 0}}, {1, {8, 0, 3, 0}}}},
              {0, Path::Scale, Interpolation::Linear, {{0, {-2, 1, 2, 0}}, {1, {-4, 3, 1, 0}}}},
              {0,
               Path::Rotation,
               Interpolation::Linear,
               {{0, vector(first)}, {1, {-last.x, -last.y, -last.z, -last.w}}}}}})
            .value();
  const std::array bindings{NodeBinding{0, entity}};
  const std::array definitions{
      LayerDefinition{"Offset",
                      action,
                      {{0, Path::Translation, .5}, {0, Path::Rotation}, {0, Path::Scale}},
                      .5,
                      1,
                      false,
                      true,
                      LayerMode::Additive}};
  auto animator = SceneAnimator::bind(scene, base, bindings, definitions).value();
  REQUIRE(animator.seek(scene, 0));
  CHECK(scene.transform(entity)->position == Math::Vec3{10, 2, -1});
  CHECK(scene.transform(entity)->scale == Math::Vec3{-2, 3, 4});
  REQUIRE(animator.controlLayer(scene, "Offset", SeekLayer{1}));
  const auto pose = scene.transform(entity).value();
  CHECK(pose.position == Math::Vec3{11, 1.5F, -.5F});
  CHECK(pose.scale == Math::Vec3{-3, 6, 3});
  const auto expected
      = Math::multiply(Math::compose({}, baseRotation).value(),
                       Math::compose({}, Math::rotationEulerDegrees({0, 30, 0}).value()).value())
            .value();
  const auto actual = Math::compose({}, pose.rotation).value();

  for (std::size_t i = 0; i < actual.data.size(); ++i) {
    CHECK(actual.data[i] == doctest::Approx(expected.data[i]).epsilon(1e-5));
  }

  REQUIRE(animator.controlLayer(scene, "Offset", SeekLayer{1}));
  CHECK(scene.transform(entity)->position == pose.position);
  CHECK(scene.transform(entity)->scale == pose.scale);
  REQUIRE(animator.controlLayer(scene, "Offset", PauseLayer{}));
  REQUIRE(animator.advance(scene, .5));
  CHECK(scene.transform(entity)->rotation == pose.rotation);
  REQUIRE(animator.controlLayer(scene, "Offset", WeightLayer{0}));
  CHECK(scene.transform(entity)->scale == Math::Vec3{-2, 3, 4});
  REQUIRE(animator.controlLayer(scene, "Offset", WeightLayer{1}));
  CHECK(scene.transform(entity)->position == Math::Vec3{12, 1, 0});
  CHECK(scene.transform(entity)->scale == Math::Vec3{-4, 9, 2});
  REQUIRE(animator.controlLayer(scene, "Offset", StopLayer{}));
  CHECK(scene.transform(entity)->position == Math::Vec3{10, 2, -1});
  CHECK(scene.transform(entity)->rotation == baseRotation);
}

TEST_CASE("animation.additive_order_and_crossfade_keep_unlayered_base_origins") {
  auto scene = World::Scene::create().value();
  const auto entity = scene.createEntity().value();
  const auto constant = [](float x) {
    return Clip::create({"Constant",
                         {{0,
                           Path::Translation,
                           Interpolation::Step,
                           {{0, {x, 0, 0, 0}}, {2, {x, 0, 0, 0}}}}}})
        .value();
  };
  auto offset = Clip::create({"Offset",
                              {{0,
                                Path::Translation,
                                Interpolation::Linear,
                                {{0, {1, 0, 0, 0}}, {1, {3, 0, 0, 0}}}}}})
                    .value();
  const std::array bindings{NodeBinding{0, entity}};
  std::array definitions{
      LayerDefinition{
          "Offset", offset, {{0, Path::Translation}}, 1, 0, false, true, LayerMode::Additive},
      LayerDefinition{"Override", constant(20), {{0, Path::Translation}}, .5, 1, true, true}};
  auto animator = SceneAnimator::bind(scene, constant(10), bindings, definitions).value();
  REQUIRE(animator.controlLayer(scene, "Offset", SeekLayer{1}));
  CHECK(scene.transform(entity)->position.x == 16);
  REQUIRE(animator.crossFade(scene, constant(30), 1));
  REQUIRE(animator.advance(scene, .5));
  CHECK(scene.transform(entity)->position.x == 21);
  REQUIRE(animator.crossFade(scene, constant(10), 1));
  CHECK(scene.transform(entity)->position.x == 21);
  REQUIRE(animator.controlLayer(scene, "Override", StopLayer{}));
  CHECK(scene.transform(entity)->position.x == 22);
  REQUIRE(animator.controlLayer(scene, "Offset", StopLayer{}));
  CHECK(scene.transform(entity)->position.x == 20);
  std::swap(definitions[0], definitions[1]);
  auto reversed = SceneAnimator::bind(scene, constant(10), bindings, definitions).value();
  REQUIRE(reversed.controlLayer(scene, "Offset", SeekLayer{1}));
  CHECK(scene.transform(entity)->position.x == 17);
}

TEST_CASE("animation.additive_singular_final_scale_keeps_all_clocks_and_can_be_repaired") {
  auto scene = World::Scene::create().value();
  const auto entity = scene.createEntity().value();
  const auto clip
      = Clip::create(
            {"Offset",
             {{0, Path::Scale, Interpolation::Linear, {{0, {1, 1, 1, 0}}, {1, {-1, 1, 1, 0}}}}}})
            .value();
  const auto repair
      = Clip::create({"Repair", {{0, Path::Scale, Interpolation::Step, {{0, {2, 1, 1, 0}}}}}})
            .value();
  const std::array bindings{NodeBinding{0, entity}};
  std::array definitions{
      LayerDefinition{"Offset", clip, {{0, Path::Scale}}, .5, 1, false, true, LayerMode::Additive},
      LayerDefinition{"Repair", repair, {{0, Path::Scale}}, 1, 0, false, false}};
  auto animator = SceneAnimator::bind(scene, moving(), bindings, definitions).value();
  REQUIRE(animator.seek(scene, 0));
  const auto pose = scene.transform(entity).value();
  const auto clocks = animator.layers().value();
  CHECK_FALSE(animator.controlLayer(scene, "Offset", SeekLayer{1}));
  CHECK(scene.transform(entity)->scale == pose.scale);
  CHECK(scene.transform(entity)->position == pose.position);
  CHECK(animator.layers().value() == clocks);
  CHECK(animator.state()->time == 0);
  REQUIRE(animator.controlLayer(scene, "Repair", PlayLayer{true}));
  REQUIRE(animator.controlLayer(scene, "Offset", SeekLayer{1}));
  CHECK(scene.transform(entity)->scale.x == 2);
  const auto repaired = animator.layers().value();
  CHECK_FALSE(animator.controlLayer(scene, "Repair", StopLayer{}));
  CHECK(animator.layers().value() == repaired);
  REQUIRE(animator.controlLayer(scene, "Offset", WeightLayer{0}));
  REQUIRE(animator.controlLayer(scene, "Repair", StopLayer{}));
  CHECK(scene.transform(entity)->scale.x == 1);
}

TEST_CASE("animation.additive_overflow_preserves_pose_and_all_clocks_before_float_conversion") {
  constexpr float maximum = std::numeric_limits<float>::max();

  for (const auto path : {Path::Translation, Path::Scale}) {
    auto scene = World::Scene::create().value();
    const auto entity = scene.createEntity().value();
    REQUIRE(scene.setTransform(entity, {{maximum / 2, 0, 0}, {}, {2, 1, 1}}));
    const auto base
        = Clip::create({"Base",
                        {{0,
                          path == Path::Scale ? Path::Translation : Path::Scale,
                          Interpolation::Step,
                          {{0, path == Path::Scale ? Math::Vec4{} : Math::Vec4{1, 1, 1, 0}}}}}})
              .value();
    const auto action
        = Clip::create({"Overflow",
                        {{0,
                          path,
                          Interpolation::Linear,
                          {{0, path == Path::Scale ? Math::Vec4{1, 1, 1, 0}
                                                   : Math::Vec4{-maximum / 2, 0, 0, 0}},
                           {1, path == Path::Scale ? Math::Vec4{maximum, 1, 1, 0}
                                                   : Math::Vec4{maximum / 2, 0, 0, 0}}}}}})
              .value();
    const std::array bindings{NodeBinding{0, entity}};
    const std::array definitions{
        LayerDefinition{"Offset", action, {{0, path}}, 1, 1, false, true, LayerMode::Additive}};
    auto animator = SceneAnimator::bind(scene, base, bindings, definitions).value();
    REQUIRE(animator.seek(scene, 0));
    const auto before = scene.transform(entity).value();
    const auto clocks = animator.layers().value();
    const auto failed = animator.controlLayer(scene, "Offset", SeekLayer{1});
    REQUIRE_FALSE(failed);
    CHECK(failed.error().message.find("Animation layer node 0: Additive transform overflow")
          != std::string::npos);
    CHECK(scene.transform(entity)->position == before.position);
    CHECK(scene.transform(entity)->scale == before.scale);
    CHECK(animator.layers().value() == clocks);
    CHECK(animator.state()->time == 0);
    REQUIRE(animator.controlLayer(scene, "Offset", WeightLayer{0}));
    REQUIRE(animator.controlLayer(scene, "Offset", SeekLayer{1}));
    CHECK_FALSE(animator.controlLayer(scene, "Offset", WeightLayer{1}));
    CHECK(animator.layers()->front().playback.weight == 0);
    CHECK(scene.transform(entity)->scale == before.scale);
  }
}

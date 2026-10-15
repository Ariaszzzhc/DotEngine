#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <thread>
#include <utility>

import Dot.Animation.Clip;
import Dot.Animation.SceneAnimator;
import Dot.Math;
import Dot.World.Scene;

namespace {
  using namespace Dot;
  using namespace Dot::Animation;

  auto same(const World::Transform& first, const World::Transform& second) -> bool {
    return first.position == second.position && first.rotation == second.rotation
           && first.scale == second.scale;
  }

  auto move(float first, float last, std::uint32_t node = 0) -> Clip {
    return Clip::create({"Move",
                         {{node,
                           Path::Translation,
                           Interpolation::Linear,
                           {{0, {first, 0, 0, 0}}, {2, {last, 0, 0, 0}}}}}})
        .value();
  }

  auto constant(Path path, Math::Vec4 value, std::uint32_t node = 0) -> Clip {
    return Clip::create({"Constant", {{node, path, Interpolation::Step, {{0, value}}}}}).value();
  }

  struct Fixture {
    World::Scene scene = World::Scene::create().value();
    World::EntityId first = scene.createEntity("First").value();
    World::EntityId second = scene.createEntity("Second").value();
    std::array<NodeBinding, 2> bindings{NodeBinding{0, first}, NodeBinding{1, second}};

    Fixture() {
      scene.setTransform(first, {{10, 0, 0}}).value();
      scene.setTransform(second, {{0, 3, 0}, {}, {2, 3, 4}}).value();
    }

    auto player(Clip clip = move(0, 4)) -> SceneAnimator {
      return SceneAnimator::bind(scene, std::move(clip), bindings).value();
    }

    auto x() const -> float {
      return scene.transform(first)->position.x;
    }
  };
}  // namespace

TEST_CASE("animation.fade_advances_both_clips_freezes_on_pause_and_restores_original_bind_pose") {
  Fixture f;
  auto player = f.player();
  REQUIRE(player.configure(1, false));
  REQUIRE(player.seek(f.scene, .5));
  REQUIRE(player.crossFade(f.scene, move(8, 12), 2));
  CHECK(f.x() == 1);
  CHECK(player.state()->time == 0);
  REQUIRE(player.state()->transition);
  CHECK(player.state()->transition == TransitionProgress{0, 2});
  REQUIRE(player.advance(f.scene, .5));
  CHECK(f.x() == doctest::Approx(3.75));
  CHECK(player.state()->transition->elapsed == .5);
  REQUIRE(player.pause());
  REQUIRE(player.advance(f.scene, 1));
  CHECK(f.x() == doctest::Approx(3.75));
  CHECK(player.state()->transition->elapsed == .5);
  REQUIRE(player.play());
  REQUIRE(player.advance(f.scene, .5));
  CHECK(f.x() == doctest::Approx(6.5));
  REQUIRE(player.advance(f.scene, 1));
  CHECK(f.x() == 12);
  CHECK_FALSE(player.state()->transition);
  CHECK_FALSE(player.state()->playing);
  REQUIRE(player.stop(f.scene));
  CHECK(f.x() == 10);
}

TEST_CASE("animation.fade_partial_channels_use_visible_source_bind_destination_and_unscaled_time") {
  Fixture f;
  auto player = f.player();
  REQUIRE(player.seek(f.scene, .5));
  auto pose = f.scene.transform(f.second).value();
  pose.rotation = Math::rotationEulerDegrees({0, 0, 90}).value();
  pose.scale = {3, 4, 5};
  REQUIRE(f.scene.setTransform(f.second, pose));
  auto target = constant(Path::Rotation, {0, 0, 0, 1}, 1);
  REQUIRE(player.configure(0, false));
  REQUIRE(player.crossFade(f.scene, target, 2));
  CHECK(f.scene.transform(f.second)->rotation == pose.rotation);
  REQUIRE(player.advance(f.scene, 1));
  CHECK(f.x() == doctest::Approx(5.5));
  CHECK(player.state()->time == 0);
  CHECK(player.state()->transition->elapsed == 1);
  CHECK(Math::eulerDegrees(f.scene.transform(f.second)->rotation)->z == doctest::Approx(45));
  CHECK(f.scene.transform(f.second)->scale == Math::Vec3{3, 4, 5});
  REQUIRE(player.advance(f.scene, 1));
  CHECK(f.x() == 10);
  CHECK(f.scene.transform(f.second)->rotation == Math::Quat{});
  CHECK_FALSE(player.state()->transition);
  REQUIRE(f.scene.setTransform(f.first, {{20, 0, 0}}));
  REQUIRE(player.advance(f.scene, .5));
  CHECK(f.x() == 20);
  REQUIRE(player.stop(f.scene));
  CHECK(f.x() == 20);
  CHECK(f.scene.transform(f.second)->scale == Math::Vec3{3, 4, 5});
}

TEST_CASE("animation.fade_interruptions_freeze_the_visible_composite_without_layer_growth") {
  Fixture f;
  auto player = f.player(move(0, 0));
  REQUIRE(player.seek(f.scene, 0));
  REQUIRE(player.crossFade(f.scene, move(10, 10), 2));
  REQUIRE(player.advance(f.scene, 1));
  CHECK(f.x() == 5);
  REQUIRE(player.crossFade(f.scene, move(20, 20), 2));
  CHECK(f.x() == 5);
  REQUIRE(player.advance(f.scene, 1));
  CHECK(f.x() == 12.5F);

  for (int i = 0; i < 32; ++i) {
    const auto visible = f.x();
    REQUIRE(player.crossFade(f.scene, move(30, 30), 2));
    CHECK(f.x() == visible);
    REQUIRE(player.advance(f.scene, .125));
    CHECK(f.x() == doctest::Approx(std::lerp(visible, 30.F, .0625F)));
  }

  REQUIRE(player.advance(f.scene, 2));
  CHECK(f.x() == 30);
  CHECK_FALSE(player.state()->transition);
  REQUIRE(player.stop(f.scene));
  CHECK(f.x() == 10);
}

TEST_CASE("animation.fade_reverse_loop_zero_duration_and_nonlooping_terminal_hold") {
  Fixture f;
  auto player = f.player();
  REQUIRE(player.seek(f.scene, 1));
  REQUIRE(player.configure(-1, true));
  REQUIRE(player.crossFade(f.scene, move(8, 12), 2));
  CHECK(player.state()->time == 2);
  REQUIRE(player.advance(f.scene, .5));
  CHECK(player.state()->time == 1.5);
  CHECK(f.x() == doctest::Approx(3.5));
  REQUIRE(player.crossFade(f.scene, move(20, 24), 0));
  CHECK(f.x() == 24);
  CHECK_FALSE(player.state()->transition);
  REQUIRE(player.configure(1, false));
  REQUIRE(player.crossFade(f.scene, constant(Path::Translation, {6, 0, 0, 0}), 2));
  REQUIRE(player.advance(f.scene, 1));
  CHECK(player.state()->time == 0);
  CHECK(player.state()->playing);
  CHECK(f.x() == 15);
  REQUIRE(player.advance(f.scene, 1));
  CHECK(f.x() == 6);
  CHECK_FALSE(player.state()->playing);
  CHECK_FALSE(player.state()->transition);
}

TEST_CASE("animation.fade_seek_stop_and_immediate_switch_release_every_outgoing_channel") {
  Fixture f;
  auto player = f.player();
  REQUIRE(player.seek(f.scene, 1));
  const auto rotation = constant(Path::Rotation, {0, 0, .70710678F, .70710678F}, 1);
  REQUIRE(player.crossFade(f.scene, rotation, 2));
  REQUIRE(player.advance(f.scene, .5));
  REQUIRE(player.seek(f.scene, 0));
  CHECK_FALSE(player.state()->transition);
  CHECK(f.x() == 10);
  CHECK(f.scene.transform(f.second)->rotation.z == doctest::Approx(.70710678));
  REQUIRE(player.crossFade(f.scene, move(5, 7), 2));
  REQUIRE(player.advance(f.scene, .5));
  REQUIRE(player.stop(f.scene));
  CHECK(f.x() == 10);
  CHECK(f.scene.transform(f.second)->rotation == Math::Quat{});
  REQUIRE(player.crossFade(f.scene, rotation, 2));
  REQUIRE(player.advance(f.scene, .5));
  REQUIRE(player.setClip(f.scene, move(8, 12)));
  CHECK_FALSE(player.state()->transition);
  CHECK(f.x() == 8);
  CHECK(f.scene.transform(f.second)->rotation == Math::Quat{});
}

TEST_CASE("animation.fade_invalid_blends_and_world_overflow_roll_back_all_nodes_and_clocks") {
  Fixture f;
  auto source
      = Clip::create(
            {"Source",
             {{0, Path::Translation, Interpolation::Linear, {{0, {0, 0, 0, 0}}, {2, {4, 0, 0, 0}}}},
              {1, Path::Scale, Interpolation::Step, {{0, {1, 1, 1, 0}}}}}})
            .value();
  auto player = f.player(source);
  REQUIRE(player.seek(f.scene, 0));
  auto target = Clip::create({"Target",
                              {{0,
                                Path::Translation,
                                Interpolation::Linear,
                                {{0, {8, 0, 0, 0}}, {2, {12, 0, 0, 0}}}},
                               {1, Path::Scale, Interpolation::Step, {{0, {-1, 1, 1, 0}}}}}})
                    .value();
  REQUIRE(player.crossFade(f.scene, target, 2));
  const auto first = f.scene.transform(f.first).value();
  const auto second = f.scene.transform(f.second).value();
  const auto before = player.state().value();
  CHECK_FALSE(player.advance(f.scene, 1));
  CHECK(same(f.scene.transform(f.first).value(), first));
  CHECK(same(f.scene.transform(f.second).value(), second));
  CHECK(player.state()->time == before.time);
  CHECK(player.state()->transition == before.transition);
  REQUIRE(player.seek(f.scene, 0));
  CHECK_FALSE(player.state()->transition);
  CHECK(f.x() == 8);
  CHECK(f.scene.transform(f.second)->scale.x == -1);

  Fixture overflow;
  REQUIRE(overflow.scene.setParent(overflow.second, overflow.first));
  REQUIRE(overflow.scene.setTransform(overflow.first, {{}, {}, {1e20F, 1e20F, 1e20F}}));
  auto actor = overflow.player(move(0, 0, 1));
  REQUIRE(actor.seek(overflow.scene, 0));
  REQUIRE(actor.crossFade(overflow.scene, move(0, 1e20F, 1), 2));
  CHECK_FALSE(actor.advance(overflow.scene, 1));
  CHECK(actor.state()->time == 0);
  CHECK(actor.state()->transition->elapsed == 0);
  CHECK(overflow.scene.transform(overflow.second)->position.x == 0);
}

TEST_CASE("animation.fade_failed_start_scope_thread_moves_and_unprimed_visible_pose_are_owned") {
  Fixture f;
  auto player = f.player();
  REQUIRE(player.crossFade(f.scene, move(20, 20), 2));
  CHECK(f.x() == 10);
  REQUIRE(player.advance(f.scene, 1));
  CHECK(f.x() == 15);
  const auto before = player.state().value();

  for (const auto duration : {-1.0, 86401.0, std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()}) {
    CHECK_FALSE(player.crossFade(f.scene, move(0, 4), duration));
  }

  CHECK_FALSE(player.crossFade(f.scene, Clip{}, 1));
  CHECK_FALSE(player.crossFade(f.scene, move(0, 4, 2), 1));
  auto foreign = World::Scene::create().value();
  CHECK_FALSE(player.crossFade(foreign, move(0, 4), 1));
  CHECK(player.state()->transition == before.transition);
  CHECK(f.x() == 15);
  bool rejected{};
  std::thread worker([&] { rejected = !player.crossFade(f.scene, move(0, 4), 1); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(player);
  CHECK_FALSE(player.crossFade(f.scene, move(0, 4), 1));
  auto scene = std::move(f.scene);
  REQUIRE(moved.advance(scene, 1));
  CHECK(scene.transform(f.first)->position.x == 20);
  CHECK_FALSE(moved.state()->transition);
  REQUIRE(moved.crossFade(scene, move(20, 24), 86400));
  REQUIRE(moved.advance(scene, 86400));
  CHECK_FALSE(moved.state()->transition);
  REQUIRE(moved.seek(scene, 1));
  REQUIRE(moved.play(true));
  REQUIRE(moved.crossFade(scene, move(30, 30), 2));
  REQUIRE(moved.advance(scene, .5));
  CHECK(scene.transform(f.first)->position.x == 24);
}

TEST_CASE("animation.fade_rotation_uses_shortest_arc_and_paused_source_remains_frozen") {
  Fixture f;
  const auto from = Math::rotationEulerDegrees({0, 0, 170}).value();
  const auto to = Math::rotationEulerDegrees({0, 0, -170}).value();
  auto player = f.player(constant(Path::Rotation, {from.x, from.y, from.z, from.w}));
  REQUIRE(player.seek(f.scene, 0));
  REQUIRE(player.crossFade(f.scene, constant(Path::Rotation, {to.x, to.y, to.z, to.w}), 2));
  REQUIRE(player.advance(f.scene, 1));
  CHECK(f.scene.worldTransform(f.first)->data[0] == doctest::Approx(-1));
  REQUIRE(player.stop(f.scene));
  REQUIRE(player.setClip(f.scene, move(0, 4)));
  REQUIRE(player.seek(f.scene, 1));
  REQUIRE(player.pause());
  REQUIRE(player.crossFade(f.scene, move(10, 14), 2));
  REQUIRE(player.advance(f.scene, 1));
  CHECK(f.x() == 7);
}

TEST_CASE("animation.fade_invalid_source_or_target_samples_preserve_both_clocks_for_recovery") {
  for (const bool invalidSource : {true, false}) {
    Fixture f;
    auto crossing
        = Clip::create(
              {"Crossing",
               {{0, Path::Scale, Interpolation::Linear, {{0, {-1, 1, 1, 0}}, {2, {1, 1, 1, 0}}}}}})
              .value();
    auto stable = constant(Path::Scale, {2, 1, 1, 0});
    auto player = f.player(invalidSource ? crossing : stable);
    REQUIRE(player.configure(1, false));
    REQUIRE(player.seek(f.scene, 0));
    REQUIRE(player.crossFade(f.scene, invalidSource ? stable : crossing, 4));
    const auto pose = f.scene.transform(f.first).value();
    CHECK_FALSE(player.advance(f.scene, 1));
    CHECK(same(f.scene.transform(f.first).value(), pose));
    CHECK(player.state()->transition->elapsed == 0);
    CHECK(player.state()->time == 0);
    REQUIRE(player.advance(f.scene, .5));
    CHECK(player.state()->transition->elapsed == .5);
    CHECK(f.scene.transform(f.first)->scale.x == doctest::Approx(invalidSource ? -.1875 : 1.6875));
  }
}

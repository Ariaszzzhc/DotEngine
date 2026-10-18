#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <future>
#include <limits>
#include <utility>
#include <vector>

import Dot.Animation.Clip;
import Dot.Animation.Motion;
import Dot.Animation.SceneAnimator;
import Dot.World.Scene;
import Dot.Math;

namespace {
  using namespace Dot;
  using namespace Dot::Animation;

  auto translation(float duration, float first, float last, std::uint32_t node = 0) -> Clip {
    return Clip::create({"Motion",
                         {{node,
                           Path::Translation,
                           Interpolation::Linear,
                           {{0, {first, 0, 0, 0}}, {duration, {last, 0, 0, 0}}}}}})
        .value();
  }

  auto motion(double value = 1) -> Motion {
    return Motion::blend1D({{0, translation(2, 0, 2)}, {2, translation(4, 10, 14)}}, value).value();
  }

  const std::array rest{ChannelSample{0, Path::Translation, {3, 0, 0, 0}}};
}  // namespace

TEST_CASE("animation_motion.weights_cycle_duration_and_shared_phase_follow_the_axis") {
  const auto blend = motion();
  CHECK(blend.duration().value() == 3);
  const auto samples = blend.samples(1.5, true).value();
  REQUIRE(samples.size() == 2);
  CHECK(samples[0].weight == .5);
  CHECK(samples[0].time == 1);
  CHECK(samples[1].time == 2);
  CHECK(blend.sample(1.5, true, rest)->front().value.x == doctest::Approx(6.5));
  const auto low = blend.withValue(-100).value();
  CHECK(low.duration().value() == 2);
  CHECK(low.samples(1, true)->size() == 1);
  CHECK(low.sample(1, true, rest)->front().value.x == 1);
  const auto high = blend.withValue(2).value();
  CHECK(high.duration().value() == 4);
  CHECK(high.samples(2, true)->size() == 1);
  CHECK(high.sample(2, true, rest)->front().value.x == 12);
  CHECK(blend.value().value() == 1);
}

TEST_CASE("animation_motion.offsets_wrap_or_clamp_and_missing_channels_use_bind_values") {
  auto blend
      = Motion::blend1D({{0, translation(2, 0, 2)}, {2, translation(4, 10, 14, 1), .5}}, 1).value();
  const std::array bind{ChannelSample{0, Path::Translation, {3, 0, 0, 0}},
                        ChannelSample{1, Path::Translation, {5, 0, 0, 0}}};
  auto sampled = blend.sample(2.25, true, bind).value();
  CHECK(sampled[0].value.x == doctest::Approx(2.25));
  CHECK(sampled[1].value.x == doctest::Approx(8));
  CHECK(blend.samples(2.25, true)->back().time == 1);
  CHECK(blend.samples(2.25, false)->back().time == 4);
  CHECK_FALSE(blend.sample(0, true, rest));
  auto endpoint = motion(2).samples(4, true).value();
  CHECK(endpoint.front().time == 4);
  CHECK_FALSE(blend.sample(3.01, true, bind));
  const auto again = blend.sample(2.25, true, bind).value();
  CHECK(again[0].value == sampled[0].value);
}

TEST_CASE("animation_motion.invalid_definitions_values_and_bind_channels_are_recoverable") {
  CHECK_FALSE(Motion::single({}));
  CHECK_FALSE(Motion::blend1D({}, 0));
  auto points
      = std::vector{MotionPoint{0, translation(1, 0, 1)}, MotionPoint{0, translation(1, 0, 1)}};
  CHECK_FALSE(Motion::blend1D(points, 0));
  points[1].threshold = 1;
  points[1].phaseOffset = 2;
  CHECK_FALSE(Motion::blend1D(points, 0));
  points[1].phaseOffset = 0;
  CHECK_FALSE(Motion::blend1D(points, std::numeric_limits<double>::quiet_NaN()));
  points.resize(17, points.back());
  CHECK_FALSE(Motion::blend1D(points, 0));
  auto blend = motion();
  CHECK_FALSE(blend.withValue(1000001));
  CHECK_FALSE(blend.sample(-1, true, rest));
  const std::array duplicate{rest[0], rest[0]};
  CHECK_FALSE(blend.sample(0, true, duplicate));
  CHECK(blend.sample(0, true, rest));
  auto moved = std::move(blend);
  CHECK_FALSE(blend.duration());
  CHECK_FALSE(blend.channels());
  CHECK_FALSE(blend.sample(0, true));
  CHECK(moved.sample(0, true, rest));
  CHECK(moved.sameDefinition(moved.withValue(0).value()));
  CHECK_FALSE(moved.sameDefinition(motion()));
}

TEST_CASE("animation_motion.rotation_blends_shortest_arc_and_singular_scales_fail") {
  const auto a
      = Clip::create({"A", {{0, Path::Rotation, Interpolation::Step, {{0, {0, 0, 0, 1}}}}}})
            .value();
  const auto b
      = Clip::create({"B", {{0, Path::Rotation, Interpolation::Step, {{0, {0, 0, 0, -1}}}}}})
            .value();
  const auto rotation = Motion::blend1D({{0, a}, {1, b}}, .5).value();
  const std::array bind{ChannelSample{0, Path::Rotation, {0, 0, 0, 1}}};
  CHECK(std::abs(rotation.sample(0, true, bind)->front().value.w) == doctest::Approx(1));
  const auto scaleA
      = Clip::create({"A", {{0, Path::Scale, Interpolation::Step, {{0, {1, 1, 1, 0}}}}}}).value();
  const auto scaleB
      = Clip::create({"B", {{0, Path::Scale, Interpolation::Step, {{0, {-1, 1, 1, 0}}}}}}).value();
  const auto singular = Motion::blend1D({{0, scaleA}, {1, scaleB}}, .5).value();
  const std::array scaleRest{ChannelSample{0, Path::Scale, {1, 1, 1, 0}}};
  CHECK_FALSE(singular.sample(0, true, scaleRest));
}

TEST_CASE("animation_motion.immutable_sources_allow_independent_parallel_values") {
  const auto original = motion();
  auto job = [original](double value) {
    const auto local = original.withValue(value).value();
    return local.sample(local.duration().value() * .5, true, rest).value().front().value.x;
  };
  auto first = std::async(std::launch::async, job, 0);
  auto second = std::async(std::launch::async, job, 2);
  CHECK(first.get() == 1);
  CHECK(second.get() == 12);
  CHECK(original.value().value() == 1);
}

TEST_CASE("animation_motion.scene_retuning_preserves_phase_and_failed_pose_retains_clocks") {
  auto scene = World::Scene::create().value();
  const auto entity = scene.createEntity("Animated").value();
  const std::array bindings{NodeBinding{0, entity}};
  const auto base = motion(0);
  auto animator = SceneAnimator::bindMotion(scene, base, bindings).value();
  REQUIRE(animator.advance(scene, .5));
  CHECK(animator.state()->time == .5);
  REQUIRE(animator.retuneAndAdvance(scene, base.withValue(2).value(), 0));
  CHECK(animator.state()->time == 1);
  CHECK(scene.transform(entity)->position.x == 11);
  REQUIRE(animator.pause());
  const auto frozen = scene.transform(entity).value();
  REQUIRE(animator.advance(scene, .3));
  CHECK(scene.transform(entity)->position == frozen.position);
  CHECK(scene.transform(entity)->rotation == frozen.rotation);
  CHECK(scene.transform(entity)->scale == frozen.scale);
  REQUIRE(animator.retuneAndAdvance(scene, base.withValue(1).value(), .3));
  CHECK(animator.motion()->value().value() == 2);
  CHECK(scene.transform(entity)->position == frozen.position);
  CHECK_FALSE(animator.retuneAndAdvance(scene, motion(), .1));
  const auto before = animator.state().value();
  CHECK_FALSE(animator.retuneAndAdvance(scene, base.withValue(1).value(), -1));
  CHECK(animator.state()->time == before.time);
  CHECK(animator.motion()->value().value() == 2);
  REQUIRE(animator.play());
  REQUIRE(animator.retuneAndAdvance(scene, base.withValue(1).value(), .25));
  CHECK(animator.state()->time == 1);
  CHECK(scene.transform(entity)->position.x == doctest::Approx(6));
}

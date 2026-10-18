#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <thread>
#include <utility>

import Dot.Animation.TwoBone;
import Dot.Core.Result;
import Dot.Math;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace {
  using namespace Dot;

  struct Fixture {
    World::Scene scene{World::Scene::create().value()};
    World::EntityId placement{scene.createEntity("Placement").value()};
    World::EntityId root{scene.createEntity("Upper arm").value()};
    World::EntityId middle{scene.createEntity("Forearm").value()};
    World::EntityId end{scene.createEntity("Hand").value()};
    World::EntityId finger{scene.createEntity("Finger").value()};

    Fixture() {
      scene.setParent(root, placement).value();
      scene.setParent(middle, root).value();
      scene.setParent(end, middle).value();
      scene.setParent(finger, end).value();
      scene.setTransform(middle, {{1, 0, 0}}).value();
      scene.setTransform(end, {{1, 0, 0}, Math::rotationEulerDegrees({17, 23, -9}).value()})
          .value();
      scene.setTransform(finger, {{.25F, 0, 0}}).value();
    }

    auto solve(Math::Vec3 target, Math::Vec3 pole = {0, 0, 1})
        -> Core::Result<Animation::TwoBonePose> {
      return Animation::solveTwoBone(scene, {root, middle, end, target, pole});
    }
  };

  auto close(Math::Vec3 actual, Math::Vec3 expected, double tolerance = 1e-5) -> void {
    CHECK(Math::length(actual - expected) <= tolerance);
  }

  auto position(const World::Scene& scene, World::EntityId entity) -> Math::Vec3 {
    const auto matrix = scene.worldTransform(entity).value();
    return {matrix.data[12], matrix.data[13], matrix.data[14]};
  }

  auto sameRotation(Math::Quat a, Math::Quat b) -> void {
    const auto first = Math::compose({}, a).value();
    const auto second = Math::compose({}, b).value();

    for (std::size_t i = 0; i < first.data.size(); ++i) {
      CHECK(first.data[i] == doctest::Approx(second.data[i]).epsilon(1e-5));
    }
  }
}  // namespace

TEST_CASE("two_bone.solves_reachable_world_goal_and_pole_without_stretch_or_end_twist") {
  Fixture f;
  const auto before = World::encodeScene(f.scene).value();
  const auto rotation = Math::decomposeTrs(f.scene.worldTransform(f.end).value())->rotation;
  const auto finger = position(f.scene, f.finger) - position(f.scene, f.end);
  auto solved = f.solve({1, 1, 0});
  REQUIRE_MESSAGE(solved, (solved ? "" : solved.error().message));
  CHECK(World::encodeScene(f.scene).value() == before);
  CHECK_FALSE(solved->clamped);
  CHECK(solved->residual < 1e-5);
  REQUIRE(f.scene.setTransforms(solved->updates));
  close(position(f.scene, f.end), {1, 1, 0});
  CHECK(position(f.scene, f.middle).z > .7F);
  CHECK(Math::length(position(f.scene, f.middle)) == doctest::Approx(1));
  CHECK(Math::length(position(f.scene, f.end) - position(f.scene, f.middle)) == doctest::Approx(1));
  CHECK(f.scene.entity(f.middle)->transform.position == Math::Vec3{1, 0, 0});
  CHECK(f.scene.entity(f.end)->transform.position == Math::Vec3{1, 0, 0});
  sameRotation(Math::decomposeTrs(f.scene.worldTransform(f.end).value())->rotation, rotation);
  close(position(f.scene, f.finger) - position(f.scene, f.end), finger);
  auto other = f.solve({1, 1, 0}, {0, 0, -1}).value();
  REQUIRE(f.scene.setTransforms(other.updates));
  close(position(f.scene, f.end), {1, 1, 0});
  CHECK(position(f.scene, f.middle).z < -.7F);
}

TEST_CASE("two_bone.supports_rotated_scaled_ancestors_and_non_axis_local_segments") {
  Fixture f;
  const auto placement = Math::rotationEulerDegrees({31, 52, 19}).value();
  f.scene.setTransform(f.placement, {{4, -2, 3}, placement, {2, 2, 2}}).value();
  f.scene
      .setTransform(
          f.root,
          {{.2F, .3F, -.4F}, Math::rotationEulerDegrees({4, 6, 9}).value(), {.5F, .5F, .5F}})
      .value();
  f.scene.setTransform(f.middle, {{.3F, .8F, -.2F}}).value();
  f.scene.setTransform(f.end, {{-.1F, .6F, .4F}}).value();
  const auto root = position(f.scene, f.root);
  const auto target = root + Math::Vec3{.3F, .8F, .3F};
  const auto endRotation = Math::decomposeTrs(f.scene.worldTransform(f.end).value())->rotation;
  auto solved = f.solve(target, root + Math::Vec3{0, 1, 1});
  REQUIRE_MESSAGE(solved, (solved ? "" : solved.error().message));
  REQUIRE(f.scene.setTransforms(solved->updates));
  close(position(f.scene, f.end), target);
  close(position(f.scene, f.root), root);
  sameRotation(Math::decomposeTrs(f.scene.worldTransform(f.end).value())->rotation, endRotation);
}

TEST_CASE("two_bone.clamps_both_reach_boundaries_and_handles_antiparallel_swing") {
  Fixture f;
  auto outer = f.solve({3, 0, 0}).value();
  CHECK(outer.clamped);
  close(outer.reached, {2, 0, 0});
  CHECK(outer.residual == doctest::Approx(1));
  auto edge = f.solve({2, 0, 0}).value();
  CHECK_FALSE(edge.clamped);
  CHECK(edge.residual < 1e-5);
  auto reverse = f.solve({-2, 0, 0}).value();
  REQUIRE(f.scene.setTransforms(reverse.updates));
  close(position(f.scene, f.end), {-2, 0, 0});
  f.scene.setTransform(f.middle, {{2, 0, 0}}).value();
  auto inner = f.solve({.25F, 0, 0}).value();
  CHECK(inner.clamped);
  close(inner.reached, {1, 0, 0});
  CHECK(inner.residual == doctest::Approx(.75));
  REQUIRE(f.scene.setTransforms(inner.updates));
  close(position(f.scene, f.end), {1, 0, 0});
}

TEST_CASE("two_bone.rejects_degenerate_inputs_scales_and_non_direct_or_foreign_chains") {
  Fixture f;
  const auto before = World::encodeScene(f.scene).value();
  CHECK_FALSE(f.solve({0, 0, 0}));
  CHECK_FALSE(f.solve({1, 0, 0}, {2, 0, 0}));
  CHECK_FALSE(f.solve({1, 0, 0}, {}));
  CHECK_FALSE(f.solve({1, 0, 0}, {1, 0, 1e-7F}));
  CHECK_FALSE(f.solve({std::numeric_limits<float>::infinity(), 0, 0}));
  CHECK_FALSE(f.solve({1, 0, 0}, {0, std::numeric_limits<float>::quiet_NaN(), 1}));
  CHECK_FALSE(Animation::solveTwoBone(f.scene, {f.root, f.middle, f.middle, {1, 1, 0}, {0, 0, 1}}));
  CHECK_FALSE(
      Animation::solveTwoBone(f.scene, {f.placement, f.middle, f.end, {1, 1, 0}, {0, 0, 1}}));
  auto foreign = World::Scene::create().value();
  CHECK_FALSE(Animation::solveTwoBone(foreign, {f.root, f.middle, f.end, {1, 1, 0}, {0, 0, 1}}));
  CHECK(World::encodeScene(f.scene).value() == before);

  for (const auto scale :
       {Math::Vec3{1, 2, 1}, Math::Vec3{-1, 1, 1}, Math::Vec3{1e-7F, 1e-7F, 1e-7F}}) {
    f.scene.setTransform(f.placement, {{}, {}, scale}).value();
    CHECK_FALSE(f.solve({1, 1, 0}));
  }

  f.scene.setTransform(f.placement, {}).value();
  f.scene.setTransform(f.middle, {}).value();
  CHECK_FALSE(f.solve({1, 1, 0}));
  f.scene.setTransform(f.middle, {{1, 0, 0}}).value();
  f.scene.setTransform(f.placement, {{1e8F, 0, 0}}).value();
  CHECK_FALSE(f.solve({1e8F, 1, 0}, {1e8F, 0, 1}));
}

TEST_CASE("two_bone.owned_results_obey_scope_thread_and_source_lifetime") {
  Fixture f;
  auto solved = f.solve({1, 1, 0}).value();
  bool rejected{};
  std::thread worker([&] { rejected = !f.solve({1, 1, 0}); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(f.scene);
  CHECK_FALSE(f.solve({1, 1, 0}));
  REQUIRE(moved.setTransforms(solved.updates));
  close(position(moved, f.end), {1, 1, 0});
  moved = World::Scene::create().value();
  CHECK_FALSE(moved.setTransforms(solved.updates));
  close(solved.reached, {1, 1, 0});
}

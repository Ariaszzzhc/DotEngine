#include <doctest/doctest.h>

#include <array>
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

  auto moving() -> Clip {
    return Clip::create({"Move",
                         {{0,
                           Path::Translation,
                           Interpolation::Linear,
                           {{0, {0, 0, 0, 0}}, {2, {4, 0, 0, 0}}}}}})
        .value();
  }
}  // namespace

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

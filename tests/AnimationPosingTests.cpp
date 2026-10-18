#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

import Dot.Animation.SceneAnimator;
import Dot.Animation.Files;
import Dot.Editor.AnimationPosing;
import Dot.Core.Result;
import Dot.Math;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace {
  using namespace Dot;

  struct Fixture {
    World::Scene scene{World::Scene::create().value()};
    World::EntityId owner{scene.createEntity("Player").value()};
    World::EntityId root{scene.createEntity("Upper arm").value()};
    World::EntityId middle{scene.createEntity("Forearm").value()};
    World::EntityId end{scene.createEntity("Hand").value()};
    World::EntityId other{scene.createEntity("Unrelated").value()};
    World::AnimationPlayer player{
        {"assets/action.dotanimation"},
        {{1, root.value}, {2, middle.value}, {3, end.value}, {9, other.value}}};
    Animation::Clip clip{Animation::Clip::create({"Action",
                                                  {{9,
                                                    Animation::Path::Translation,
                                                    Animation::Interpolation::Linear,
                                                    {{0, {3, 4, 5, 0}}, {1, {6, 7, 8, 0}}}}}})
                             .value()};

    Fixture() {
      scene.setParent(root, owner).value();
      scene.setParent(middle, root).value();
      scene.setParent(end, middle).value();
      scene.setParent(other, owner).value();
      scene.setTransform(middle, {{1, 0, 0}}).value();
      scene.setTransform(end, {{1, 0, 0}}).value();
      scene.setAnimationPlayer(owner, player).value();
    }

    auto prepare(float time = .5F, Math::Vec3 target = {1, 1, 0})
        -> Core::Result<Editor::TwoBoneKeys> {
      return Editor::prepareTwoBoneKeys(scene, owner, clip, time, 3, target, {0, 0, 1});
    }

    auto sample(const Animation::Clip& value, double time) -> void {
      const std::array bindings{Animation::NodeBinding{1, root}, Animation::NodeBinding{2, middle},
                                Animation::NodeBinding{3, end}, Animation::NodeBinding{9, other}};
      auto animator = Animation::SceneAnimator::bind(scene, value, bindings).value();
      REQUIRE(animator.seek(scene, time));
    }
  };
}  // namespace

TEST_CASE("animation_posing.keys_three_bound_rotations_and_preserves_other_channels_without_io") {
  Fixture f;
  const auto source = World::encodeScene(f.scene).value();
  const auto clip = Animation::encodeClip(f.clip).value();
  auto keyed = f.prepare();
  REQUIRE_MESSAGE(keyed, (keyed ? "" : keyed.error().message));
  CHECK(World::encodeScene(f.scene).value() == source);
  CHECK(Animation::encodeClip(f.clip).value() == clip);
  CHECK(keyed->value.description().value()->tracks.size() == 4);
  for (std::size_t i = 0; i < keyed->selection.size(); ++i) {
    CHECK(keyed->selection[i].node == i + 1);
    CHECK(keyed->selection[i].path == Animation::Path::Rotation);
    CHECK(keyed->selection[i].time == .5F);
  }

  const auto unrelated
      = Animation::Clip::create({"Action", {keyed->value.description().value()->tracks.front()}})
            .value();
  CHECK(Animation::encodeClip(unrelated).value() == clip);
  f.sample(keyed->value, .5);
  REQUIRE(Editor::validateTwoBoneKeys(f.scene, *keyed));
  f.sample(keyed->value, 0);
  REQUIRE(Editor::validateTwoBoneKeys(f.scene, *keyed));
  CHECK(f.scene.entity(f.other)->transform.position == Math::Vec3{3, 4, 5});
  auto moved = std::move(f.scene);
  CHECK_FALSE(Editor::validateTwoBoneKeys(f.scene, *keyed));
  REQUIRE(Editor::validateTwoBoneKeys(moved, *keyed));
  moved = World::Scene::create().value();
  CHECK(keyed->value.sample(.5));
}

TEST_CASE("animation_posing.preserves_modes_cubic_tangents_and_authored_quaternion_hemisphere") {
  Fixture f;
  auto description = *f.clip.description().value();
  description.tracks.push_back(
      {1,
       Animation::Path::Rotation,
       Animation::Interpolation::CubicSpline,
       {{0, {0, 0, 0, -1}, {.1F, .2F, 0, 0}, {-.1F, 0, 0, 0}}, {1, {0, 0, 0, -1}, {}, {}}}});
  description.tracks.push_back({2,
                                Animation::Path::Rotation,
                                Animation::Interpolation::Step,
                                {{0, {0, 0, 0, 1}}, {1, {0, 0, 0, 1}}}});
  f.clip = Animation::Clip::create(description).value();
  auto replaced = f.prepare(0).value();
  const auto& root = replaced.value.description().value()->tracks[1];
  CHECK(root.interpolation == Animation::Interpolation::CubicSpline);
  CHECK(root.keys.size() == 2);
  CHECK(root.keys[0].value.w < 0);
  CHECK(root.keys[0].inTangent == description.tracks[1].keys[0].inTangent);
  CHECK(root.keys[0].outTangent == description.tracks[1].keys[0].outTangent);
  CHECK(replaced.value.description().value()->tracks[2].interpolation
        == Animation::Interpolation::Step);
  f.sample(replaced.value, 0);
  REQUIRE(Editor::validateTwoBoneKeys(f.scene, replaced));
  auto inserted = f.prepare().value();
  const auto& middleKey = inserted.value.description().value()->tracks[1].keys[1];
  CHECK(middleKey.time == .5F);
  CHECK(middleKey.value.w < 0);
  CHECK(middleKey.inTangent == Math::Vec4{});
  CHECK(middleKey.outTangent == Math::Vec4{});
  f.sample(inserted.value, .5);
  REQUIRE(Editor::validateTwoBoneKeys(f.scene, inserted));
}

TEST_CASE("animation_posing.rejects_bad_time_binding_ancestry_and_scope_without_mutation") {
  Fixture f;
  const auto before = World::encodeScene(f.scene).value();
  CHECK_FALSE(f.prepare(-1));
  CHECK_FALSE(f.prepare(2));
  CHECK_FALSE(f.prepare(std::numeric_limits<float>::quiet_NaN()));
  CHECK_FALSE(Editor::prepareTwoBoneKeys(f.scene, f.owner, {}, .5F, 3, {1, 1, 0}, {0, 0, 1}));
  CHECK_FALSE(Editor::prepareTwoBoneKeys(f.scene, f.owner, f.clip, .5F, 99, {1, 1, 0}, {0, 0, 1}));
  CHECK_FALSE(Editor::prepareTwoBoneKeys(f.scene, f.owner, f.clip, .5F, 1, {1, 1, 0}, {0, 0, 1}));
  CHECK_FALSE(Editor::prepareTwoBoneKeys(f.scene, f.other, f.clip, .5F, 3, {1, 1, 0}, {0, 0, 1}));
  bool rejected{};
  std::thread worker([&] { rejected = !f.prepare(); });
  worker.join();
  CHECK(rejected);
  CHECK(World::encodeScene(f.scene).value() == before);
  auto other = World::Scene::create().value();
  CHECK_FALSE(Editor::prepareTwoBoneKeys(other, f.owner, f.clip, .5F, 3, {1, 1, 0}, {0, 0, 1}));
  f.player.targets.erase(f.player.targets.begin());
  REQUIRE(f.scene.setAnimationPlayer(f.owner, f.player));
  CHECK_FALSE(f.prepare());
  f.player.targets.push_back({1, f.root.value});
  REQUIRE(f.scene.setAnimationPlayer(f.owner, f.player));
  REQUIRE(f.scene.setParent(f.root, {}));
  CHECK_FALSE(f.prepare());
}

TEST_CASE("animation_posing.validates_real_sample_rotations_translations_and_world_goal") {
  Fixture f;
  auto keyed = f.prepare(.5F, {3, 0, 0}).value();
  CHECK(keyed.pose.clamped);
  CHECK(keyed.pose.residual == doctest::Approx(1));
  f.sample(keyed.value, .5);
  REQUIRE(Editor::validateTwoBoneKeys(f.scene, keyed));
  REQUIRE(f.scene.setTransform(f.middle, {{2, 0, 0}}));
  CHECK_FALSE(Editor::validateTwoBoneKeys(f.scene, keyed));
  REQUIRE(f.scene.setTransforms(keyed.pose.updates));
  REQUIRE(f.scene.setTransform(f.owner, {{5, 0, 0}}));
  CHECK_FALSE(Editor::validateTwoBoneKeys(f.scene, keyed));
}

TEST_CASE("animation_posing.finite_extreme_world_coordinates_keep_double_residuals") {
  Fixture f;
  const auto root = -1.5F * std::ldexp(1.F, 127);
  const auto length = std::ldexp(1.F, 106);
  REQUIRE(f.scene.setTransform(f.owner, {{root, 0, 0}}));
  REQUIRE(f.scene.setTransform(f.middle, {{length, 0, 0}}));
  REQUIRE(f.scene.setTransform(f.end, {{length, 0, 0}}));
  auto keyed = Editor::prepareTwoBoneKeys(f.scene, f.owner, f.clip, .5F, 3, {-root, 0, 0},
                                          {root, 0, std::ldexp(1.F, 127)});
  REQUIRE_MESSAGE(keyed, (keyed ? "" : keyed.error().message));
  REQUIRE(f.scene.setTransforms(keyed->pose.updates));
  auto actual = Editor::validateTwoBoneKeys(f.scene, *keyed);
  REQUIRE(actual);
  CHECK(std::isfinite(actual->residual));
  CHECK(actual->residual > std::numeric_limits<float>::max());
  CHECK(keyed->pose.clamped);
}

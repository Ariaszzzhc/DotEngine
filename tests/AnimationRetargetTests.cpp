#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

import Dot.Animation.Retarget;
import Dot.Core.Result;
import Dot.Animation.Source;
import Dot.Animation.SceneAnimator;
import Dot.Math;
import Dot.World.Scene;

namespace {
  using namespace Dot;
  using namespace Dot::Animation;

  auto rig(bool target = false) -> RetargetRig {
    return {
        {{"Anchor",
          {},
          {{3, 0, 0}, Math::rotationEulerDegrees({0, target ? 180.F : 0.F, 0}).value(), {1, 1, 1}},
          0},
         {target ? "Target hips" : "Source hips",
          0,
          {{0, target ? 2.F : 1.F, 0},
           target ? Math::rotationEulerDegrees({0, 0, 90}).value() : Math::Quat{},
           {1, 1, 1}},
          target ? 7U : 1U},
         {target ? "Target head" : "Source head",
          1,
          {{0, target ? 3.F : 1.F, 0}},
          target ? 17U : 2U},
         {"Mesh sibling", 0, {{4, 0, 0}}, 22}},
        0};
  }

  auto motion(float duration = 1) -> Clip {
    const auto q = Math::rotationEulerDegrees({0, 90, 0}).value();
    return Clip::create({"Source",
                         {{1,
                           Path::Rotation,
                           Interpolation::Linear,
                           {{0, {0, 0, 0, 1}}, {duration, {q.x, q.y, q.z, q.w}}}},
                          {1,
                           Path::Translation,
                           Interpolation::Linear,
                           {{0, {0, 1, 0, 0}}, {duration, {0, 2, 0, 0}}}}}})
        .value();
  }

  auto settings() -> RetargetSettings {
    return {"Transferred", {{1, 7, "Source hips", "Target hips"}}, {2, 1}, {}};
  }

  auto sampled(const Clip& value, std::uint32_t node, Path path, double time) -> Math::Vec4 {
    for (const auto& channel : value.sample(time).value()) {
      if (channel.node == node && channel.path == path) {
        return channel.value;
      }
    }

    FAIL("Missing transferred channel");
    return {};
  }

  auto close(Math::Quat a, Math::Quat b) -> void {
    const auto first = Math::compose({}, a).value();
    const auto last = Math::compose({}, b).value();

    for (std::size_t i = 0; i < first.data.size(); ++i) {
      CHECK(first.data[i] == doctest::Approx(last.data[i]).epsilon(1e-5));
    }
  }
}  // namespace

TEST_CASE(
    "animation_retarget.reference_relative_rotations_keep_proportions_subtrees_and_placement") {
  auto source = rig();
  auto target = rig(true);
  auto value = retargetClip(source, target, motion(), settings()).value();
  CHECK(value.frames == 3);
  CHECK(value.emittedNodes == 2);
  CHECK(value.clip.description().value()->tracks.size() == 6);
  CHECK(value.unmappedSource == std::vector<std::uint32_t>{0, 2, 22});
  CHECK(value.unmappedTarget == std::vector<std::uint32_t>{0, 17, 22});
  const auto start = sampled(value.clip, 7, Path::Rotation, 0);
  close({start.x, start.y, start.z, start.w}, target.nodes[1].reference.rotation);
  const auto peak = sampled(value.clip, 7, Path::Rotation, 1);
  const auto desired = Math::multiplyRotations(Math::rotationEulerDegrees({0, 90, 0}).value(),
                                               target.nodes[1].reference.rotation)
                           .value();
  close({peak.x, peak.y, peak.z, peak.w}, desired);
  CHECK(sampled(value.clip, 7, Path::Translation, 1).y == 2);
  CHECK(sampled(value.clip, 17, Path::Translation, 1).y == 3);
  CHECK(sampled(value.clip, 17, Path::Scale, 1).x == 1);
  CHECK(source.nodes[1].reference.position.y == 1);
  CHECK(target.nodes[1].reference.position.y == 2);

  for (const auto& track : value.clip.description().value()->tracks) {
    CHECK(track.node != 0);
    CHECK(track.node != 22);
  }

  source = {};
  target = {};
  CHECK(sampled(value.clip, 7, Path::Translation, .5).y == 2);
}

TEST_CASE("animation_retarget.world_displacement_scale_respects_target_units_and_mapped_root") {
  auto source = rig();
  auto target = rig(true);
  target.nodes[0].reference.scale = {2, 2, 2};
  auto config = settings();
  config.translation = RetargetTranslation{7, 2};
  const auto value = retargetClip(source, target, motion(), config).value();
  CHECK(sampled(value.clip, 7, Path::Translation, 0).y == 2);
  CHECK(sampled(value.clip, 7, Path::Translation, 1).y == 3);
  CHECK(sampled(value.clip, 17, Path::Translation, 1).y == 3);
  config.mappings.push_back({2, 17, "Source head", "Target head"});
  config.translation = RetargetTranslation{17, 1};
  CHECK_FALSE(retargetClip(source, target, motion(), config));
  config.translation = RetargetTranslation{7, -1};
  CHECK_FALSE(retargetClip(source, target, motion(), config));
}

TEST_CASE(
    "animation_retarget.named_one_to_one_mapping_and_mapped_ancestry_reject_without_partial_"
    "results") {
  auto source = rig();
  auto target = rig(true);
  auto config = settings();
  config.mappings.push_back({2, 17, "Source head", "Target head"});
  REQUIRE(retargetClip(source, target, motion(), config));
  auto bad = config;
  bad.mappings[0].sourceName = "Old name";
  CHECK_FALSE(retargetClip(source, target, motion(), bad));
  bad = config;
  bad.mappings[1].target = 7;
  CHECK_FALSE(retargetClip(source, target, motion(), bad));
  bad = config;
  bad.mappings[1].source = 1;
  CHECK_FALSE(retargetClip(source, target, motion(), bad));
  bad = config;
  bad.mappings[0] = {0, 7, "Anchor", "Target hips"};
  CHECK_FALSE(retargetClip(source, target, motion(), bad));
  target.nodes[2].parent = 0;
  CHECK_FALSE(retargetClip(source, target, motion(), config));
  target.nodes[2].parent = 1;
  auto extra = target.nodes[2];
  extra.parent = 1;
  extra.binding.reset();
  target.nodes.push_back(extra);
  target.nodes[2].parent = 4;
  REQUIRE(retargetClip(source, target, motion(), config));
}

TEST_CASE("animation_retarget.invalid_rigs_scales_bindings_and_rate_budgets_fail_explicitly") {
  auto source = rig();
  const auto target = rig(true);
  auto config = settings();
  auto bad = source;
  bad.nodes[1].parent = 2;
  CHECK_FALSE(retargetClip(bad, target, motion(), config));
  bad = source;
  bad.nodes[1].parent = 100;
  CHECK_FALSE(retargetClip(bad, target, motion(), config));
  bad = source;
  bad.nodes[1].reference.scale = {-1, 1, 1};
  CHECK_FALSE(retargetClip(bad, target, motion(), config));
  bad.nodes[1].reference.scale = {2, 1, 1};
  CHECK_FALSE(retargetClip(bad, target, motion(), config));
  bad = source;
  bad.nodes[2].binding = 1;
  CHECK_FALSE(retargetClip(bad, target, motion(), config));
  config.rate.denominator = 0;
  CHECK_FALSE(retargetClip(source, target, motion(), config));
  config.rate = {241, 1};
  CHECK_FALSE(retargetClip(source, target, motion(), config));
  config = settings();
  config.rate = {30, 1};
  CHECK_FALSE(retargetClip(source, target, motion(300), config));
  const auto still
      = Clip::create({"Still", {{1, Path::Rotation, Interpolation::Step, {{0, {0, 0, 0, 1}}}}}})
            .value();
  CHECK_FALSE(retargetClip(source, target, still, config));
  auto unbound
      = Clip::create(
            {"Unbound",
             {{999, Path::Translation, Interpolation::Linear, {{0, {}}, {1, {1, 0, 0, 0}}}}}})
            .value();
  CHECK_FALSE(retargetClip(source, target, unbound, config));
}

TEST_CASE("animation_retarget.animated_scale_is_rejected_and_float_grid_endpoint_is_emitted_once") {
  const auto source = rig();
  const auto target = rig(true);
  auto config = settings();
  auto scale
      = Clip::create(
            {"Scale",
             {{1, Path::Scale, Interpolation::Linear, {{0, {1, 1, 1, 0}}, {1, {2, 2, 2, 0}}}}}})
            .value();
  const auto rejected = retargetClip(source, target, scale, config);
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().message.find("Animated source scale") != std::string::npos);
  config.rate = {1, 1};
  auto hidden = Clip::create({"Between samples",
                              {{1,
                                Path::Scale,
                                Interpolation::Linear,
                                {{0, {1, 1, 1, 0}}, {.5F, {2, 2, 2, 0}}, {1, {1, 1, 1, 0}}}}}})
                    .value();
  CHECK_FALSE(retargetClip(source, target, hidden, config));
  auto curve
      = Clip::create(
            {"Curve between endpoints",
             {{1,
               Path::Scale,
               Interpolation::CubicSpline,
               {{0, {1, 1, 1, 0}, {}, {1, 1, 1, 0}}, {1, {1, 1, 1, 0}, {-1, -1, -1, 0}, {}}}}}})
            .value();
  CHECK_FALSE(retargetClip(source, target, curve, config));
  auto constant = Clip::create({"Constant scale",
                                {{1,
                                  Path::Scale,
                                  Interpolation::CubicSpline,
                                  {{0, {1, 1, 1, 0}}, {1, {1, 1, 1, 0}}}}}})
                      .value();
  CHECK(retargetClip(source, target, constant, config));
  config.rate = {30, 1};
  auto value = retargetClip(source, target, motion(1.2F), config).value();
  CHECK(value.frames == 37);
  const auto& keys = value.clip.description().value()->tracks.front().keys;
  CHECK(keys.front().time == 0);
  CHECK(keys.back().time == 1.2F);

  for (std::size_t i = 1; i < keys.size(); ++i) {
    CHECK(keys[i - 1].time < keys[i].time);
  }

  std::stop_source stop;
  stop.request_stop();
  auto cancelled = retargetClip(source, target, motion(), config, stop.get_token());
  REQUIRE_FALSE(cancelled);
  CHECK(cancelled.error().code == Core::ErrorCode::Unavailable);
  CHECK(cancelled.error().message == "Animation retarget cancelled");
}

TEST_CASE("animation_retarget.capture_owns_named_subtree_and_rejects_foreign_bindings") {
  auto scene = World::Scene::create().value();
  const auto parent = scene.createEntity("Placement").value();
  const auto owner = scene.createEntity("Rig").value();
  const auto joint = scene.createEntity("Joint").value();
  REQUIRE(scene.setParent(owner, parent));
  REQUIRE(scene.setParent(joint, owner));
  REQUIRE(scene.setAnimationPlayer(owner,
                                   World::AnimationPlayer{{"assets/motion.dotanimation"},
                                                          {{0, owner.value}, {7, joint.value}}}));
  auto captured = captureRetargetRig(scene, owner).value();
  REQUIRE(captured.nodes.size() == 2);
  CHECK_FALSE(captured.nodes[captured.anchor].parent);
  CHECK(captured.nodes[1].name == "Joint");
  REQUIRE(scene.renameEntity(joint, "Changed"));
  CHECK(captured.nodes[1].name == "Joint");
  REQUIRE(scene.setParent(joint, {}));
  CHECK_FALSE(captureRetargetRig(scene, owner));
  CHECK_FALSE(captureRetargetRig(scene, {999, owner.value}));
}

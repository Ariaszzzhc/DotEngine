#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Animation.Clip;
import Dot.Math;

namespace {
  using namespace Dot;
  using namespace Dot::Animation;

  auto translation(Interpolation mode = Interpolation::Linear) -> ClipDescription {
    return {"Moving door", {{0, Path::Translation, mode, {{1, {0, 0, 0, 0}}, {3, {4, 0, 0, 0}}}}}};
  }
}  // namespace

TEST_CASE("animation.clip_step_linear_endpoint_clamps_and_independent_channel_times") {
  auto description = translation();
  description.tracks.push_back({1,
                                Path::Scale,
                                Interpolation::Step,
                                {{0, {1, 1, 1, 0}}, {2, {2, 3, 4, 0}}, {4, {3, 3, 3, 0}}}});
  auto clip = Clip::create(description).value();
  CHECK(clip.duration().value() == 4);
  CHECK(clip.sample(0)->at(0).value.x == 0);
  CHECK(clip.sample(1)->at(0).value.x == 0);
  CHECK(clip.sample(2)->at(0).value.x == doctest::Approx(2));
  CHECK(clip.sample(3)->at(0).value.x == 4);
  CHECK(clip.sample(1000)->at(0).value.x == 4);
  CHECK(clip.sample(1.999)->at(1).value.x == 1);
  CHECK(clip.sample(2)->at(1).value.y == 3);
  description.tracks[0].interpolation = Interpolation::Step;
  auto step = Clip::create(description).value();
  CHECK(step.sample(2.999)->at(0).value.x == 0);
  CHECK(step.sample(3)->at(0).value.x == 4);
  CHECK_FALSE(clip.sample(-1));
  CHECK_FALSE(clip.sample(std::numeric_limits<double>::infinity()));
  CHECK_FALSE(clip.sample(std::numeric_limits<double>::quiet_NaN()));
}

TEST_CASE("animation.clip_cubic_hermite_scales_tangents_by_seconds_and_has_exact_keys") {
  auto description = translation(Interpolation::CubicSpline);
  description.tracks[0].keys[0].outTangent = {2, 0, 0, 0};
  description.tracks[0].keys[1].value = {};
  auto clip = Clip::create(description).value();
  CHECK(clip.sample(2)->at(0).value.x == doctest::Approx(.5));
  CHECK(clip.sample(1)->at(0).value.x == 0);
  CHECK(clip.sample(3)->at(0).value.x == 0);
  CHECK(clip.sample(1.001)->at(0).value.x / .001 == doctest::Approx(2).epsilon(.002));
  description.tracks[0].keys[0].outTangent = {std::numeric_limits<float>::max(), 0, 0, 0};
  description.tracks[0].keys[1].time = 86400;
  auto overflowing = Clip::create(description).value();
  CHECK_FALSE(overflowing.sample(10000));
}

TEST_CASE("animation.clip_rotation_shortest_slerp_and_authored_cubic_normalization") {
  ClipDescription d{"Rotation",
                    {{2,
                      Path::Rotation,
                      Interpolation::Linear,
                      {{0, {0, 0, 0, 1}}, {2, {0, 0, .70710678F, -.70710678F}}}}}};
  auto clip = Clip::create(d).value();
  auto value = clip.sample(1)->at(0).value;
  CHECK(value.z == doctest::Approx(-.38268343));
  CHECK(value.w == doctest::Approx(.92387953));
  d.tracks[0].keys[1].value = {0, 0, 0, -1};
  auto same = Clip::create(d).value();
  CHECK(same.sample(1)->at(0).value.w == doctest::Approx(1));
  d.tracks[0].interpolation = Interpolation::CubicSpline;
  CHECK_FALSE(Clip::create(d)->sample(1));
  d.tracks[0].keys[1].value = {0, 0, 1, 0};
  auto cubic = Clip::create(d).value();
  CHECK(cubic.sample(1)->at(0).value.z == doctest::Approx(.70710678));
  CHECK(cubic.sample(1)->at(0).value.w == doctest::Approx(.70710678));
  CHECK(cubic.sample(2)->at(0).value.z == 1);
}

TEST_CASE("animation.clip_validation_rejects_malformed_channels_keys_and_singular_samples") {
  auto valid = translation();
  std::vector<ClipDescription> malformed;
  auto d = valid;
  d.name.clear();
  malformed.push_back(d);
  d = valid;
  d.name = std::string("a\0b", 3);
  malformed.push_back(d);
  d = valid;
  d.name = "\xc0\x80";
  malformed.push_back(d);
  d = valid;
  d.tracks.push_back(d.tracks[0]);
  malformed.push_back(d);
  d = valid;
  d.tracks[0].node = 10000;
  malformed.push_back(d);
  d = valid;
  d.tracks[0].path = static_cast<Path>(9);
  malformed.push_back(d);
  d = valid;
  d.tracks[0].interpolation = static_cast<Interpolation>(9);
  malformed.push_back(d);
  d = valid;
  d.tracks[0].keys.clear();
  malformed.push_back(d);
  d = valid;
  d.tracks[0].keys[1].time = 1;
  malformed.push_back(d);
  d = valid;
  d.tracks[0].keys[1].time = 0;
  malformed.push_back(d);
  d = valid;
  d.tracks[0].keys[1].time = 86401;
  malformed.push_back(d);
  d = valid;
  d.tracks[0].keys[0].time = -1;
  malformed.push_back(d);
  d = valid;
  d.tracks[0].keys[0].value.w = 1;
  malformed.push_back(d);
  d = valid;
  d.tracks[0].keys[0].outTangent.x = 1;
  malformed.push_back(d);
  d = valid;
  d.tracks[0].interpolation = Interpolation::CubicSpline;
  d.tracks[0].keys.resize(1);
  malformed.push_back(d);
  d = valid;
  d.tracks[0].keys[0].value.x = std::numeric_limits<float>::quiet_NaN();
  malformed.push_back(d);
  d = valid;
  d.tracks[0].path = Path::Rotation;
  malformed.push_back(d);

  for (const auto& bad : malformed) {
    CHECK_FALSE(Clip::create(bad));
  }

  ClipDescription scale{
      "Mirror", {{0, Path::Scale, Interpolation::Linear, {{0, {-1, 1, 1, 0}}, {2, {1, 1, 1, 0}}}}}};
  auto clip = Clip::create(scale).value();
  CHECK_FALSE(clip.sample(1));
  CHECK(clip.sample(0));
  CHECK(clip.sample(2));
}

TEST_CASE("animation.clip_shared_immutable_ownership_moves_and_concurrent_samples") {
  auto description = translation();
  auto clip = Clip::create(description).value();
  auto copy = clip;
  description.tracks.clear();
  auto moved = std::move(clip);
  CHECK_FALSE(clip.description());
  CHECK_FALSE(clip.duration());
  CHECK_FALSE(clip.sample(0));
  CHECK_FALSE(Clip{}.sample(0));
  CHECK(copy.sample(2)->at(0).value.x == 2);
  bool correct{};
  std::thread worker(
      [&] { correct = moved.sample(2)->at(0).value.x == 2 && copy.sample(3)->at(0).value.x == 4; });
  worker.join();
  CHECK(correct);
  auto samples = copy.sample(2).value();
  samples[0].value.x = 100;
  CHECK(copy.sample(2)->at(0).value.x == 2);
}

TEST_CASE("animation.single_track_samples_share_native_step_linear_cubic_and_rotation_evaluation") {
  for (auto mode : {Interpolation::Step, Interpolation::Linear, Interpolation::CubicSpline}) {
    auto description = translation(mode);
    description.tracks.push_back({1, Path::Rotation, mode, {{0, {0, 0, 0, 1}}, {4, {0, 0, 1, 0}}}});
    auto clip = Clip::create(description).value();

    for (double time : {0., 1., 1.25, 2., 3., 4., 100000.}) {
      const auto all = clip.sample(time).value();

      for (std::size_t i = 0; i < all.size(); ++i) {
        auto selected = clip.sampleTrack(i, time).value();
        CHECK(selected.node == all[i].node);
        CHECK(selected.path == all[i].path);
        CHECK(selected.value == all[i].value);
      }
    }
  }
}

TEST_CASE("animation.single_track_errors_and_owned_lifetime_are_independent_of_other_samples") {
  auto description = translation();
  description.tracks.push_back(
      {1, Path::Scale, Interpolation::Linear, {{0, {-1, 1, 1, 0}}, {2, {1, 1, 1, 0}}}});
  auto clip = Clip::create(description).value();
  CHECK_FALSE(clip.sample(1));
  CHECK(clip.sampleTrack(0, 1));
  CHECK_FALSE(clip.sampleTrack(1, 1));
  CHECK_FALSE(clip.sampleTrack(2, 0));
  CHECK_FALSE(clip.sampleTrack(0, -1));
  CHECK_FALSE(clip.sampleTrack(0, std::numeric_limits<double>::infinity()));
  CHECK_FALSE(clip.sampleTrack(0, std::numeric_limits<double>::quiet_NaN()));
  auto moved = std::move(clip);
  CHECK_FALSE(clip.sampleTrack(0, 0));
  CHECK_FALSE(Clip{}.sampleTrack(0, 0));
  description.tracks.clear();
  auto owned = moved.sampleTrack(0, 2).value();
  owned.value.x = 100;
  bool correct{};
  std::thread worker([&] { correct = moved.sampleTrack(0, 2)->value.x == 2; });
  worker.join();
  CHECK(correct);
}

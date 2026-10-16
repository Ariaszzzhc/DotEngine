#include <doctest/doctest.h>

#include <limits>
#include <vector>

import Dot.Animation.Edit;
import Dot.Core.Result;
import Dot.Math;

namespace {
  using namespace Dot;
  using namespace Dot::Animation;

  auto source() -> Clip {
    return Clip::create({"Move",
                         {{0,
                           Path::Translation,
                           Interpolation::Linear,
                           {{0, {0, 0, 0, 0}}, {2, {2, 0, 0, 0}}}}}})
        .value();
  }

  auto edit(const Clip& clip, std::initializer_list<ClipEdit> edits) -> Core::Result<Clip> {
    return editClip(clip, {edits.begin(), edits.size()});
  }
}  // namespace

TEST_CASE("animation_edit.inserts_sorted_replaces_exact_times_and_creates_channels") {
  const auto clip = source();
  auto changed = edit(
      clip, {PutKey{0, Path::Translation, Interpolation::Linear, {1, {4, 0, 0, 0}}},
             PutKey{0, Path::Translation, Interpolation::Linear, {1, {6, 0, 0, 0}}},
             PutKey{1, Path::Scale, Interpolation::Step, {0, {2, 2, 2, 0}}}, RenameClip{"Edited"}});
  REQUIRE(changed);
  const auto& tracks = changed->description().value()->tracks;
  REQUIRE(tracks.size() == 2);
  CHECK(tracks[0].keys.size() == 3);
  CHECK(tracks[0].keys[1].time == 1);
  CHECK(tracks[0].keys[1].value.x == 6);
  CHECK(changed->sample(.5).value()[0].value.x == doctest::Approx(3));
  CHECK(changed->sample(1).value()[1].value == Math::Vec4{2, 2, 2, 0});
  CHECK(changed->description().value()->name == "Edited");
  CHECK(clip.description().value()->name == "Move");
  CHECK(clip.description().value()->tracks[0].keys.size() == 2);
  CHECK(editClip(clip, {})->description().value() == clip.description().value());
}

TEST_CASE("animation_edit.interpolation_conversion_is_explicit_and_tangents_remain_editable") {
  auto clip = edit(source(), {SetInterpolation{0, Path::Translation, Interpolation::CubicSpline}})
                  .value();
  auto key = clip.description().value()->tracks[0].keys[0];
  key.outTangent = {4, 0, 0, 0};
  clip = edit(clip, {PutKey{0, Path::Translation, Interpolation::CubicSpline, key}}).value();
  auto same
      = edit(clip, {SetInterpolation{0, Path::Translation, Interpolation::CubicSpline}}).value();
  CHECK(same.description().value()->tracks[0].keys[0].outTangent.x == 4);
  CHECK(clip.sample(1).value()[0].value.x == doctest::Approx(2));
  CHECK_FALSE(edit(clip, {PutKey{0, Path::Translation, Interpolation::Linear, key}}));
  auto linear = edit(clip, {SetInterpolation{0, Path::Translation, Interpolation::Linear}}).value();
  CHECK(linear.description().value()->tracks[0].keys[0].outTangent == Math::Vec4{});
  CHECK(linear.sample(1).value()[0].value.x == doctest::Approx(1));
  auto step = edit(linear, {SetInterpolation{0, Path::Translation, Interpolation::Step}}).value();
  CHECK(step.sample(1).value()[0].value.x == 0);
}

TEST_CASE("animation_edit.removal_and_batches_validate_final_native_clip_atomically") {
  const auto clip = source();
  auto one = edit(clip, {RemoveKey{0, Path::Translation, 2}}).value();
  CHECK(one.duration().value() == 0);
  CHECK_FALSE(edit(one, {RemoveKey{0, Path::Translation, 0}}));
  CHECK_FALSE(edit(one, {SetInterpolation{0, Path::Translation, Interpolation::CubicSpline}}));
  auto replacement = edit(one, {RemoveTrack{0, Path::Translation},
                                PutKey{1, Path::Rotation, Interpolation::Step, {0, {0, 0, 0, 1}}}});
  REQUIRE(replacement);
  CHECK(replacement->description().value()->tracks[0].node == 1);
  CHECK_FALSE(edit(clip, {RenameClip{"Changed"}, RemoveKey{0, Path::Translation, 1}}));
  CHECK(clip.description().value()->name == "Move");
  CHECK_FALSE(edit(clip, {RemoveTrack{3, Path::Scale}}));
  CHECK_FALSE(edit(clip, {SetInterpolation{3, Path::Scale, Interpolation::Linear}}));
  CHECK_FALSE(edit(clip, {PutKey{1, Path::Scale, Interpolation::Linear, {0, {0, 1, 1, 0}}}}));
  CHECK_FALSE(edit(clip, {PutKey{1, Path::Rotation, Interpolation::Linear, {0, {0, 0, 0, 2}}}}));
  CHECK_FALSE(edit(clip, {RenameClip{""}}));
}

TEST_CASE("animation_edit.rejects_invalid_handles_times_modes_and_batch_limits") {
  const auto clip = source();
  CHECK_FALSE(editClip({}, {}));
  CHECK_FALSE(
      edit(clip, {RemoveKey{0, Path::Translation, std::numeric_limits<float>::quiet_NaN()}}));
  CHECK_FALSE(edit(clip, {PutKey{0, Path::Translation, Interpolation::Linear, {-1, {}}}}));
  CHECK_FALSE(edit(clip, {SetInterpolation{0, Path::Translation, static_cast<Interpolation>(99)}}));
  CHECK_FALSE(edit(clip, {PutKey{10000, Path::Translation, Interpolation::Linear, {0, {}}}}));
  std::vector<ClipEdit> edits(256, RenameClip{"Move"});
  REQUIRE(editClip(clip, edits));
  edits.push_back(RenameClip{"Too many"});
  CHECK_FALSE(editClip(clip, edits));
}

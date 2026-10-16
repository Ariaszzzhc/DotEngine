#include <doctest/doctest.h>

#include <array>
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

TEST_CASE("animation_edit.updates_existing_key_and_reorders_without_inserting_a_copy") {
  const auto clip = source();
  auto key = clip.description().value()->tracks[0].keys[0];
  key.time = 3;
  key.value.x = 7;
  const auto changed = edit(clip, {UpdateKey{0, Path::Translation, 0, key}}).value();
  const auto& keys = changed.description().value()->tracks[0].keys;
  REQUIRE(keys.size() == 2);
  CHECK(keys[0].time == 2);
  CHECK(keys[1].time == 3);
  CHECK(changed.duration().value() == 3);
  CHECK(changed.sample(2.5)->at(0).value.x == doctest::Approx(4.5));
  CHECK(clip.description().value()->tracks[0].keys[0].time == 0);
  key.time = 1;
  const auto back = edit(changed, {UpdateKey{0, Path::Translation, 3, key}}).value();
  CHECK(back.description().value()->tracks[0].keys[0].time == 1);
  CHECK(back.duration().value() == 2);
  const auto same = edit(back, {UpdateKey{0, Path::Translation, 1, key}}).value();
  CHECK(same.sample(1)->at(0).value.x == 7);
}

TEST_CASE("animation_edit.key_update_rejects_collision_missing_and_invalid_keys_atomically") {
  const auto clip = source();
  const auto original = clip.description().value();
  auto key = original->tracks[0].keys[0];
  key.time = 2;
  auto failed = edit(clip, {RenameClip{"Uncommitted"}, UpdateKey{0, Path::Translation, 0, key}});
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("occupied") != std::string::npos);
  CHECK_FALSE(edit(clip, {UpdateKey{0, Path::Translation, 1, key}}));
  CHECK_FALSE(edit(clip, {UpdateKey{3, Path::Translation, 0, key}}));
  key.time = std::numeric_limits<float>::infinity();
  CHECK_FALSE(edit(clip, {UpdateKey{0, Path::Translation, 0, key}}));
  key.time = -1;
  CHECK_FALSE(edit(clip, {UpdateKey{0, Path::Translation, 0, key}}));
  key.time = 86401;
  CHECK_FALSE(edit(clip, {UpdateKey{0, Path::Translation, 0, key}}));
  key.time = 0;
  key.value.w = 1;
  CHECK_FALSE(edit(clip, {UpdateKey{0, Path::Translation, 0, key}}));
  CHECK_FALSE(
      edit(clip, {UpdateKey{0, Path::Translation, std::numeric_limits<float>::quiet_NaN(), key}}));
  CHECK(clip.description().value() == original);
  CHECK(original->name == "Move");
}

TEST_CASE("animation_edit.key_update_preserves_authored_cubic_tangents") {
  auto clip = edit(source(), {SetInterpolation{0, Path::Translation, Interpolation::CubicSpline}})
                  .value();
  auto key = clip.description().value()->tracks[0].keys[0];
  key.time = .5F;
  key.inTangent = {-2, 0, 0, 0};
  key.outTangent = {4, 0, 0, 0};
  const auto changed = edit(clip, {UpdateKey{0, Path::Translation, 0, key}}).value();
  const auto& actual = changed.description().value()->tracks[0].keys[0];
  CHECK(actual.time == .5F);
  CHECK(actual.inTangent == key.inTangent);
  CHECK(actual.outTangent == key.outTangent);
  CHECK(changed.sample(1.25)->at(0).value.x == doctest::Approx(1.75));
}

TEST_CASE("animation_edit.group_retime_resolves_sources_simultaneously_and_retains_payload") {
  auto original = source();
  const std::array<ClipEdit, 1> edits{
      RetimeKeys{{{0, Path::Translation, 0, 2}, {0, Path::Translation, 2, 0}}}};
  auto changed = editClip(original, edits);
  REQUIRE(changed);
  const auto& before = original.description().value()->tracks[0];
  const auto& after = changed->description().value()->tracks[0];
  REQUIRE(after.keys.size() == before.keys.size());
  CHECK(after.keys[0].value == before.keys[1].value);
  CHECK(after.keys[1].value == before.keys[0].value);
  CHECK(after.interpolation == before.interpolation);
}

TEST_CASE(
    "animation_edit.group_retime_rejects_collision_duplicates_and_missing_sources_atomically") {
  auto original = source();

  for (auto changes :
       {RetimeKeys{{}}, RetimeKeys{{{0, Path::Translation, 0, 2}}},
        RetimeKeys{{{0, Path::Translation, 0, .5F}, {0, Path::Translation, 0, .75F}}},
        RetimeKeys{{{0, Path::Translation, 0, .5F}, {0, Path::Translation, 2, .5F}}},
        RetimeKeys{{{0, Path::Translation, 0, -.5F}}}, RetimeKeys{{{0, Path::Translation, .5F, 2}}},
        RetimeKeys{{{9999, Path::Translation, 0, 2}}},
        RetimeKeys{{{0, Path::Translation, 0, std::numeric_limits<float>::infinity()}}}}) {
    const std::array<ClipEdit, 1> edits{changes};
    CHECK_FALSE(editClip(original, edits));
    CHECK(original.description().value()->tracks[0].keys[0].time == 0);
    CHECK(original.description().value()->tracks[0].keys[1].time == 2);
  }
}

TEST_CASE("animation_edit.paste_merges_sorted_keys_and_replacement_is_explicit") {
  const auto original = source();
  const std::array<KeyReference, 2> selection{
      {{0, Path::Translation, 2}, {0, Path::Translation, 0}}};
  const auto group = KeyGroup::create(original, selection).value();
  const auto pasted = edit(original, {PasteKeys{group, 1}}).value();
  const auto& keys = pasted.description().value()->tracks[0].keys;
  REQUIRE(keys.size() == 4);
  CHECK(keys[0].time == 0);
  CHECK(keys[1].time == 1);
  CHECK(keys[2].time == 2);
  CHECK(keys[3].time == 3);
  CHECK_FALSE(edit(original, {PasteKeys{group, 0}}));
  auto changedSource
      = edit(original, {PutKey{0, Path::Translation, Interpolation::Linear, {0, {9, 8, 7, 0}}}})
            .value();
  const auto copied = KeyGroup::create(changedSource, selection).value();
  auto replaced = edit(original, {PasteKeys{copied, 0, KeyCollision::Replace}}).value();
  CHECK(replaced.description().value()->tracks[0].keys.size() == 2);
  CHECK(replaced.description().value()->tracks[0].keys[0].value.x == 9);
  CHECK(original.description().value()->tracks[0].keys[0].value.x == 0);
}

TEST_CASE("animation_edit.paste_creates_matching_channels_and_validates_partial_cubic_results") {
  const auto cubic
      = Clip::create(
            {"Cubic",
             {{3,
               Path::Translation,
               Interpolation::CubicSpline,
               {{0, {0, 0, 0, 0}, {}, {2, 0, 0, 0}}, {2, {2, 0, 0, 0}, {1, 0, 0, 0}, {}}}}}})
            .value();
  const std::array<KeyReference, 1> one{{{3, Path::Translation, 0}}};
  const auto partial = KeyGroup::create(cubic, one).value();
  CHECK_FALSE(edit(source(), {PasteKeys{partial, 1}}));
  const std::array<KeyReference, 2> both{{{3, Path::Translation, 0}, {3, Path::Translation, 2}}};
  const auto group = KeyGroup::create(cubic, both).value();
  auto pasted = edit(source(), {PasteKeys{group, 1}}).value();
  const auto& track = pasted.description().value()->tracks[1];
  CHECK(track.node == 3);
  CHECK(track.interpolation == Interpolation::CubicSpline);
  REQUIRE(track.keys.size() == 2);
  CHECK(track.keys[0].time == 1);
  CHECK(track.keys[1].time == 3);
  CHECK(track.keys[0].outTangent.x == 2);
  CHECK(track.keys[1].inTangent.x == 1);
  CHECK_FALSE(edit(
      source(), {SetInterpolation{0, Path::Translation, Interpolation::Step},
                 PasteKeys{KeyGroup::create(
                               source(), std::array<KeyReference, 1>{{{0, Path::Translation, 0}}})
                               .value(),
                           0, KeyCollision::Replace}}));
}

TEST_CASE("animation_edit.invalid_paste_and_later_batch_failure_preserve_source_and_group") {
  const auto original = source();
  const std::array<KeyReference, 1> selection{{{0, Path::Translation, 0}}};
  const auto group = KeyGroup::create(original, selection).value();
  CHECK_FALSE(edit(original, {PasteKeys{{}, 0}}));
  CHECK_FALSE(edit(original, {PasteKeys{group, -1}}));
  CHECK_FALSE(edit(original, {PasteKeys{group, 1, static_cast<KeyCollision>(99)}}));
  CHECK_FALSE(edit(original, {PasteKeys{group, 1}, RemoveKey{0, Path::Translation, 99}}));
  CHECK(original.description().value()->tracks[0].keys.size() == 2);
  CHECK(group.description().value()->keys[0].key.time == 0);
}

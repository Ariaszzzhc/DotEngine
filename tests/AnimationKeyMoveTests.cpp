#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

import Dot.Editor.AnimationKeyMove;
import Dot.Core.Result;

namespace {
  using namespace Dot;
  using namespace Dot::Animation;

  auto clip() -> Clip {
    return Clip::create({"Move",
                         {{4,
                           Path::Translation,
                           Interpolation::Linear,
                           {{0, {0, 0, 0, 0}}, {1, {1, 0, 0, 0}}, {2, {2, 0, 0, 0}}}}}})
        .value();
  }

  auto beginAt(Clip source, std::uint32_t node, Path path, float time)
      -> Core::Result<Editor::AnimationKeyMove> {
    const std::array<KeyReference, 1> keys{{{node, path, time}}};
    return Editor::AnimationKeyMove::begin(std::move(source), keys);
  }

  auto begin(Clip source = clip()) -> Editor::AnimationKeyMove {
    return beginAt(std::move(source), 4, Path::Translation, 0).value();
  }
}  // namespace

TEST_CASE("animation_key_move.preview_is_absolute_owned_and_never_mutates_source") {
  auto source = clip();
  auto move = begin(source);
  auto initial = move.snapshot().value();
  CHECK_FALSE(initial.changed);
  CHECK_FALSE(initial.occupied);
  REQUIRE(move.update(.75F));
  auto first = move.snapshot().value();
  REQUIRE(move.update(.5F));
  CHECK(move.snapshot()->keys[0].key.time == .5F);
  CHECK(first.keys[0].key.time == .75F);
  CHECK(source.description().value()->tracks[0].keys[0].time == 0);
  CHECK(source.sample(.5)->at(0).value.x == .5F);
  source = {};
  const auto update = move.edit().value();
  CHECK(update.keys[0].time == 0);
  CHECK(update.keys[0].target == .5F);
  const std::array<ClipEdit, 1> edits{update};
  const auto changed = editClip(clip(), edits).value();
  CHECK(changed.description().value()->tracks[0].keys.size() == 3);
  CHECK(changed.description().value()->tracks[0].keys[0].time == .5F);
  CHECK(first.keys[0].key.time == .75F);
}

TEST_CASE("animation_key_move.occupied_time_is_visible_recoverable_and_origin_is_noop") {
  auto move = begin();
  REQUIRE(move.update(1));
  CHECK(move.snapshot()->occupied);
  CHECK(move.snapshot()->changed);
  CHECK_FALSE(move.edit());
  CHECK(move.snapshot()->keys[0].key.time == 1);
  REQUIRE(move.update(.25F));
  CHECK_FALSE(move.snapshot()->occupied);
  REQUIRE(move.edit());
  REQUIRE(move.update(0));
  CHECK_FALSE(move.snapshot()->changed);
  CHECK_FALSE(move.snapshot()->occupied);
  CHECK(move.edit()->keys[0].time == move.edit()->keys[0].target);
  REQUIRE(move.update(-0.F));
  CHECK_FALSE(std::signbit(move.edit()->keys[0].target));
}

TEST_CASE("animation_key_move.invalid_sources_and_samples_retain_the_last_preview") {
  CHECK_FALSE(beginAt({}, 4, Path::Translation, 0));
  CHECK_FALSE(beginAt(clip(), 10000, Path::Translation, 0));
  CHECK_FALSE(beginAt(clip(), 4, static_cast<Path>(99), 0));
  CHECK_FALSE(beginAt(clip(), 9, Path::Translation, 0));
  CHECK_FALSE(beginAt(clip(), 4, Path::Scale, 0));
  CHECK_FALSE(beginAt(clip(), 4, Path::Translation, .0125F));
  auto move = begin();
  REQUIRE(move.update(.0125F));

  for (float time : {-1.F, 86401.F, std::numeric_limits<float>::infinity(),
                     std::numeric_limits<float>::quiet_NaN()}) {
    CHECK_FALSE(beginAt(clip(), 4, Path::Translation, time));
    CHECK_FALSE(move.update(time));
    CHECK(move.snapshot()->keys[0].key.time == .0125F);
  }

  REQUIRE(move.update(86400));
  CHECK(move.edit()->keys[0].target == 86400);
}

TEST_CASE("animation_key_move.cubic_values_tangents_and_mode_survive_time_preview") {
  auto source = clip().description().value()->tracks[0];
  source.interpolation = Interpolation::CubicSpline;
  source.keys[0].inTangent = {-2, 0, 0, 0};
  source.keys[0].outTangent = {4, 0, 0, 0};
  auto value = Clip::create({"Cubic", {source}}).value();
  auto move = begin(value);
  REQUIRE(move.update(.25F));
  const auto edit = move.edit().value();
  const auto ghost = move.snapshot().value().keys[0].key;
  CHECK(ghost.value == source.keys[0].value);
  CHECK(ghost.inTangent == source.keys[0].inTangent);
  CHECK(ghost.outTangent == source.keys[0].outTangent);
  const std::array<ClipEdit, 1> edits{edit};
  auto changed = editClip(value, edits).value();
  CHECK(changed.description().value()->tracks[0].interpolation == Interpolation::CubicSpline);
  CHECK(value.description().value()->tracks[0].keys[0].time == 0);
}

TEST_CASE("animation_key_move.moves_retire_handles_and_independent_previews_share_owned_data") {
  auto source = clip();
  auto first = begin(source);
  auto independent = begin(source);
  REQUIRE(first.update(.25F));
  auto moved = std::move(first);
  CHECK_FALSE(first.snapshot());
  CHECK_FALSE(first.update(1));
  CHECK_FALSE(first.edit());
  CHECK(independent.snapshot()->keys[0].key.time == 0);
  source = {};
  bool updated{};
  std::thread worker([&] { updated = moved.update(.75F).has_value(); });
  worker.join();
  REQUIRE(updated);
  CHECK(moved.snapshot()->keys[0].key.time == .75F);
  independent = std::move(moved);
  CHECK_FALSE(moved.snapshot());
  CHECK(independent.snapshot()->keys[0].key.time == .75F);
  auto& alias = independent;
  independent = std::move(alias);
  CHECK(independent.snapshot()->keys[0].key.time == .75F);
}

TEST_CASE("animation_key_move.selected_sources_vacate_together_and_offsets_use_original_times") {
  const std::array<KeyReference, 2> keys{{{4, Path::Translation, 1}, {4, Path::Translation, 2}}};
  auto source = clip();
  auto move = Editor::AnimationKeyMove::begin(source, keys).value();
  REQUIRE(move.update(3));
  auto state = move.snapshot().value();
  CHECK_FALSE(state.occupied);
  CHECK(state.keys[0].key.time == 2);
  CHECK(state.keys[1].key.time == 3);
  const std::array<ClipEdit, 1> edits{move.edit().value()};
  auto changed = editClip(source, edits).value().description().value()->tracks[0];
  REQUIRE(changed.keys.size() == 3);
  CHECK(changed.keys[1].value.x == 1);
  CHECK(changed.keys[2].value.x == 2);
  REQUIRE(move.update(2.5F));
  CHECK(move.snapshot()->keys[0].key.time == 1.5F);
  REQUIRE(move.update(2));
  CHECK_FALSE(move.snapshot()->changed);
  CHECK(move.snapshot()->keys[0].key.time == 1);
  CHECK(source.description().value()->tracks[0].keys[1].time == 1);
  CHECK_FALSE(move.update(.5F));
  CHECK(move.snapshot()->time == 2);
}

TEST_CASE("animation_key_move.cross_channel_groups_keep_order_payload_and_independent_occupancy") {
  auto value = *clip().description().value();
  value.tracks.push_back(
      {5, Path::Scale, Interpolation::Step, {{0, {1, 2, 3, 0}}, {2, {2, 3, 4, 0}}}});
  auto source = Clip::create(value).value();
  const std::array<KeyReference, 2> keys{{{4, Path::Translation, 1}, {5, Path::Scale, 0}}};
  auto move = Editor::AnimationKeyMove::begin(source, keys).value();
  REQUIRE(move.update(1));
  auto state = move.snapshot().value();
  CHECK(state.occupied);
  CHECK(state.keys[0].occupied);
  CHECK_FALSE(state.keys[1].occupied);
  CHECK_FALSE(move.edit());
  REQUIRE(move.update(.25F));
  CHECK_FALSE(move.snapshot()->occupied);
  const std::array<ClipEdit, 1> edits{move.edit().value()};
  auto changed = editClip(source, edits).value();
  CHECK(changed.description().value()->tracks[0].keys[1].time == 1.25F);
  CHECK(changed.description().value()->tracks[1].keys[0].time == .25F);
  CHECK(changed.description().value()->tracks[1].keys[0].value == value.tracks[1].keys[0].value);
  CHECK(changed.description().value()->tracks[1].interpolation == Interpolation::Step);
  CHECK(move.snapshot()->keys[0].node == 4);
  CHECK(move.snapshot()->keys[1].node == 5);
}

TEST_CASE("animation_key_move.rejects_empty_duplicate_oversized_and_float_collapsed_groups") {
  CHECK_FALSE(Editor::AnimationKeyMove::begin(clip(), {}));
  const std::array<KeyReference, 2> duplicate{
      {{4, Path::Translation, 0}, {4, Path::Translation, -0.F}}};
  CHECK_FALSE(Editor::AnimationKeyMove::begin(clip(), duplicate));
  std::vector<KeyReference> tooMany(MaxSelectedKeys + 1, duplicate[0]);
  CHECK_FALSE(Editor::AnimationKeyMove::begin(clip(), tooMany));
  const auto tiny = std::numeric_limits<float>::denorm_min();
  auto source = Clip::create({"Tiny",
                              {{0,
                                Path::Translation,
                                Interpolation::Linear,
                                {{0, {0, 0, 0, 0}}, {tiny, {1, 0, 0, 0}}}}}})
                    .value();
  const std::array<KeyReference, 2> keys{{{0, Path::Translation, tiny}, {0, Path::Translation, 0}}};
  auto move = Editor::AnimationKeyMove::begin(source, keys).value();
  REQUIRE(move.update(86400));
  auto state = move.snapshot().value();
  CHECK(state.occupied);
  CHECK(state.keys[0].occupied);
  CHECK(state.keys[1].occupied);
  CHECK_FALSE(move.edit());
  REQUIRE(move.update(0));
  CHECK_FALSE(move.snapshot()->occupied);
  CHECK_FALSE(move.snapshot()->changed);
  CHECK(move.snapshot()->keys[0].key.time == tiny);
}

TEST_CASE("animation_key_move.full_bounded_group_has_no_intermediate_collisions_or_source_writes") {
  Track track{0, Path::Translation, Interpolation::Linear, {}};
  std::vector<KeyReference> keys;

  for (std::size_t i = 0; i < MaxSelectedKeys; ++i) {
    const auto time = static_cast<float>(i) * .5F;
    track.keys.push_back({time, {time, 0, 0, 0}});
    keys.push_back({0, Path::Translation, time});
  }

  auto source = Clip::create({"Full group", {track}}).value();
  auto move = Editor::AnimationKeyMove::begin(source, keys).value();
  REQUIRE(move.update(track.keys.back().time + .5F));
  CHECK_FALSE(move.snapshot()->occupied);
  const std::array<ClipEdit, 1> edits{move.edit().value()};
  auto changed = editClip(source, edits).value();
  CHECK(changed.description().value()->tracks[0].keys.size() == MaxSelectedKeys);
  CHECK(changed.description().value()->tracks[0].keys.front().time == .5F);
  CHECK(changed.description().value()->tracks[0].keys.back().value == track.keys.back().value);
  CHECK(source.description().value()->tracks[0].keys.front().time == 0);
}

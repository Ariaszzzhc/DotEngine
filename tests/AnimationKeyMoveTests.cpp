#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <thread>
#include <utility>

import Dot.Editor.AnimationKeyMove;

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

  auto begin(Clip source = clip()) -> Editor::AnimationKeyMove {
    return Editor::AnimationKeyMove::begin(std::move(source), 4, Path::Translation, 0).value();
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
  CHECK(move.snapshot()->key.time == .5F);
  CHECK(first.key.time == .75F);
  CHECK(source.description().value()->tracks[0].keys[0].time == 0);
  CHECK(source.sample(.5)->at(0).value.x == .5F);
  source = {};
  const auto update = move.edit().value();
  CHECK(update.time == 0);
  CHECK(update.key.time == .5F);
  const std::array<ClipEdit, 1> edits{update};
  const auto changed = editClip(clip(), edits).value();
  CHECK(changed.description().value()->tracks[0].keys.size() == 3);
  CHECK(changed.description().value()->tracks[0].keys[0].time == .5F);
  CHECK(first.key.time == .75F);
}

TEST_CASE("animation_key_move.occupied_time_is_visible_recoverable_and_origin_is_noop") {
  auto move = begin();
  REQUIRE(move.update(1));
  CHECK(move.snapshot()->occupied);
  CHECK(move.snapshot()->changed);
  CHECK_FALSE(move.edit());
  CHECK(move.snapshot()->key.time == 1);
  REQUIRE(move.update(.25F));
  CHECK_FALSE(move.snapshot()->occupied);
  REQUIRE(move.edit());
  REQUIRE(move.update(0));
  CHECK_FALSE(move.snapshot()->changed);
  CHECK_FALSE(move.snapshot()->occupied);
  CHECK(move.edit()->time == move.edit()->key.time);
  REQUIRE(move.update(-0.F));
  CHECK_FALSE(std::signbit(move.edit()->key.time));
}

TEST_CASE("animation_key_move.invalid_sources_and_samples_retain_the_last_preview") {
  CHECK_FALSE(Editor::AnimationKeyMove::begin({}, 4, Path::Translation, 0));
  CHECK_FALSE(Editor::AnimationKeyMove::begin(clip(), 10000, Path::Translation, 0));
  CHECK_FALSE(Editor::AnimationKeyMove::begin(clip(), 4, static_cast<Path>(99), 0));
  CHECK_FALSE(Editor::AnimationKeyMove::begin(clip(), 9, Path::Translation, 0));
  CHECK_FALSE(Editor::AnimationKeyMove::begin(clip(), 4, Path::Scale, 0));
  CHECK_FALSE(Editor::AnimationKeyMove::begin(clip(), 4, Path::Translation, .0125F));
  auto move = begin();
  REQUIRE(move.update(.0125F));

  for (float time : {-1.F, 86401.F, std::numeric_limits<float>::infinity(),
                     std::numeric_limits<float>::quiet_NaN()}) {
    CHECK_FALSE(Editor::AnimationKeyMove::begin(clip(), 4, Path::Translation, time));
    CHECK_FALSE(move.update(time));
    CHECK(move.snapshot()->key.time == .0125F);
  }

  REQUIRE(move.update(86400));
  CHECK(move.edit()->key.time == 86400);
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
  CHECK(edit.key.value == source.keys[0].value);
  CHECK(edit.key.inTangent == source.keys[0].inTangent);
  CHECK(edit.key.outTangent == source.keys[0].outTangent);
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
  CHECK(independent.snapshot()->key.time == 0);
  source = {};
  bool updated{};
  std::thread worker([&] { updated = moved.update(.75F).has_value(); });
  worker.join();
  REQUIRE(updated);
  CHECK(moved.snapshot()->key.time == .75F);
  independent = std::move(moved);
  CHECK_FALSE(moved.snapshot());
  CHECK(independent.snapshot()->key.time == .75F);
}

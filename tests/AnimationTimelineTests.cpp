#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

import Dot.Animation.Clip;
import Dot.Core.Result;
import Dot.Editor.AnimationTimeline;
import Dot.Editor.AnimationKeyMove;

namespace {
  using namespace Dot;
  using namespace Dot::Animation;

  auto source() -> Clip {
    return Clip::create(
               {"Timeline",
                {{9, Path::Scale, Interpolation::Linear, {{0, {1, 1, 1, 0}}, {1, {2, 2, 2, 0}}}},
                 {2, Path::Rotation, Interpolation::Linear, {{0, {0, 0, 0, 1}}, {2, {0, 0, 0, 1}}}},
                 {2,
                  Path::Translation,
                  Interpolation::Linear,
                  {{0, {3, 0, 0, 0}}, {1, {4, 0, 0, 0}}, {3, {6, 0, 0, 0}}}}}})
        .value();
  }
}  // namespace

TEST_CASE("animation_timeline.sorted_rows_paging_and_inclusive_native_times") {
  auto clip = source();
  auto timeline = Editor::AnimationTimeline::create(clip).value();
  CHECK(timeline.rows().value() == 3);
  CHECK(timeline.row(2, Path::Translation).value() == 0);
  CHECK(timeline.row(2, Path::Rotation).value() == 1);
  CHECK(timeline.row(9, Path::Scale).value() == 2);
  CHECK_FALSE(timeline.row(9, Path::Translation));
  auto page = timeline.page(0, 1, 0, 2, 32).value();
  CHECK(page.total == 3);
  CHECK(page.offset == 0);
  REQUIRE(page.rows.size() == 2);
  CHECK(page.rows[0].index == 0);
  CHECK(page.rows[0].node == 2);
  CHECK(page.rows[0].path == Path::Translation);
  CHECK(page.rows[0].total == 3);
  CHECK(page.rows[0].visible == 2);
  CHECK(page.rows[0].times == std::vector<float>{0, 1});
  CHECK(page.rows[1].times == std::vector<float>{0});
  CHECK(page.rows[0].bins.empty());
  CHECK(timeline.page(0, 3, 3, 32, 1)->rows.empty());
  auto selected = timeline.select(0, 1, 0, 2).value();
  REQUIRE(selected.size() == 5);
  CHECK(selected[0].node == 2);
  CHECK(selected[0].path == Path::Translation);
  CHECK(selected[1].time == 1);
  CHECK(selected[2].path == Path::Rotation);
  CHECK(selected[3].node == 9);
  CHECK(selected[4].time == 1);
  CHECK(timeline.select(1, 1, 0, 2)->size() == 2);
  CHECK(timeline.select(1.01, 1.99, 0, 2)->empty());
  CHECK(clip.description().value()->tracks[0].node == 9);
  const float nativeTime = .1F;
  auto rounded
      = Clip::create(
            {"Float boundary",
             {{0, Path::Translation, Interpolation::Linear, {{nativeTime, {1, 0, 0, 0}}}}}})
            .value();
  auto exact = Editor::AnimationTimeline::create(rounded).value();
  CHECK(exact.select(nativeTime, nativeTime, 0, 0)->size() == 1);
  CHECK(exact.select(.1, .1, 0, 0)->empty());
}

TEST_CASE("animation_timeline.dense_bins_cover_each_actual_key_once_with_exact_endpoints") {
  ClipDescription description{"Dense", {{7, Path::Translation, Interpolation::Linear, {}}}};

  for (int i = 0; i <= 512; ++i) {
    description.tracks[0].keys.push_back({static_cast<float>(i) / 1024, {1, 0, 0, 0}});
  }

  auto timeline = Editor::AnimationTimeline::create(Clip::create(description).value()).value();
  auto page = timeline.page(0, .5, 0, 1, 7).value();
  const auto& row = page.rows[0];
  CHECK(row.total == 513);
  CHECK(row.visible == 513);
  CHECK(row.times.empty());
  REQUIRE(row.bins.size() == 7);
  CHECK(row.bins.front().first == 0);
  CHECK(row.bins.back().last == .5F);
  std::size_t count{};
  float previous{-1};

  for (const auto& bin : row.bins) {
    CHECK(bin.first > previous);
    CHECK(bin.last >= bin.first);
    CHECK(timeline.select(bin.first, bin.last, 0, 0)->size() == bin.count);
    count += bin.count;
    previous = bin.last;
  }

  CHECK(count == row.visible);
  auto narrow = timeline.page(.25, .375, 0, 1, 128).value();
  CHECK(narrow.rows[0].bins.empty());
  CHECK(narrow.rows[0].times.size() == 129);
  CHECK(narrow.rows[0].times.front() == .25F);
  CHECK(narrow.rows[0].times.back() == .375F);
}

TEST_CASE("animation_timeline.selection_capacity_is_atomic_and_queries_remain_reusable") {
  ClipDescription description{"Many", {{0, Path::Translation, Interpolation::Linear, {}}}};

  for (int i = 0; i < 20000; ++i) {
    description.tracks[0].keys.push_back({static_cast<float>(i), {1, 0, 0, 0}});
  }

  auto timeline
      = Editor::AnimationTimeline::create(Clip::create(std::move(description)).value()).value();
  CHECK_FALSE(timeline.select(0, 19999, 0, 0));
  CHECK(timeline.select(0, 16383, 0, 0)->size() == MaxSelectedKeys);
  CHECK(timeline.select(19999, 19999, 0, 0)->size() == 1);
  CHECK(timeline.page(0, 86400, 0, 1, 128)->rows[0].visible == 20000);
}

TEST_CASE("animation_timeline.invalid_ranges_empty_source_and_zero_duration_are_explicit") {
  CHECK_FALSE(Editor::AnimationTimeline::create({}));
  auto timeline = Editor::AnimationTimeline::create(source()).value();
  CHECK_FALSE(timeline.page(0, 1, 4, 1, 1));
  CHECK_FALSE(timeline.page(0, 1, 0, 0, 1));
  CHECK_FALSE(timeline.page(0, 1, 0, 33, 1));
  CHECK_FALSE(timeline.page(0, 1, 0, 1, 0));
  CHECK_FALSE(timeline.page(0, 1, 0, 1, 129));
  CHECK_FALSE(timeline.page(1, 1, 0, 1, 1));
  CHECK_FALSE(timeline.page(0, 1e-7, 0, 1, 1));
  CHECK_FALSE(timeline.select(0, 1, 2, 1));
  CHECK_FALSE(timeline.select(0, 1, 0, 3));

  for (double value : {-1., 86401., std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
    CHECK_FALSE(timeline.page(0, value, 0, 1, 1));
    CHECK_FALSE(timeline.select(0, value, 0, 1));
  }

  auto zero
      = Clip::create({"Zero", {{0, Path::Translation, Interpolation::Step, {{0, {2, 0, 0, 0}}}}}})
            .value();
  auto zeroTimeline = Editor::AnimationTimeline::create(zero).value();
  CHECK(zeroTimeline.page(0, 1, 0, 1, 1)->rows[0].times == std::vector<float>{0});
  CHECK(zeroTimeline.select(0, 0, 0, 0)->size() == 1);
}

TEST_CASE("animation_timeline.owned_data_moves_and_const_concurrent_queries") {
  auto clip = source();
  auto timeline = Editor::AnimationTimeline::create(clip).value();
  clip = {};
  auto moved = std::move(timeline);
  CHECK_FALSE(timeline.rows());
  CHECK_FALSE(timeline.page(0, 1, 0, 1, 1));
  CHECK_FALSE(timeline.select(0, 1, 0, 1));
  CHECK_FALSE(timeline.row(2, Path::Translation));
  std::array<bool, 4> results{};
  std::vector<std::jthread> workers;

  for (std::size_t i = 0; i < results.size(); ++i) {
    workers.emplace_back([&, i] {
      results[i]
          = moved.page(0, 1, 0, 3, 128)->rows.size() == 3 && moved.select(0, 1, 0, 2)->size() == 5;
    });
  }

  workers.clear();

  for (bool result : results) {
    CHECK(result);
  }
}

TEST_CASE("animation_timeline.rectangle_selection_uses_existing_atomic_group_retime") {
  auto clip = source();
  auto timeline = Editor::AnimationTimeline::create(clip).value();
  auto selected = timeline.select(0, 1, 0, 2).value();
  auto move = Editor::AnimationKeyMove::begin(clip, selected).value();
  REQUIRE(move.update(2.25F));
  std::vector<ClipEdit> changes{move.edit().value()};
  auto edited = editClip(clip, changes).value();
  auto after = Editor::AnimationTimeline::create(edited).value();
  CHECK(after.select(1.25, 2.25, 0, 2)->size() == 6);
  CHECK(after.select(1.25, 1.25, 0, 2)->size() == 3);
  CHECK(after.select(2.25, 2.25, 0, 2)->size() == 2);
  CHECK(timeline.select(0, 1, 0, 2)->size() == 5);
  CHECK(edited.description().value()->tracks[0].keys[0].value.x == 1);
}

TEST_CASE("animation_timeline.bulk_delete_drops_empty_channels_and_preserves_other_keys") {
  auto clip = source();
  auto timeline = Editor::AnimationTimeline::create(clip).value();
  std::vector<ClipEdit> changes{RemoveKeys{timeline.select(0, 1, 0, 2).value()}};
  auto edited = editClip(clip, changes).value();
  CHECK(edited.description().value()->tracks.size() == 2);
  CHECK(edited.description().value()->tracks[0].keys[0].time == 2);
  CHECK(edited.description().value()->tracks[1].keys[0].value.x == 6);
  CHECK(clip.description().value()->tracks.size() == 3);
  changes = {RemoveKeys{{{2, Path::Translation, 3}, {2, Path::Rotation, 2}}}};
  CHECK_FALSE(editClip(edited, changes));
  CHECK(edited.description().value()->tracks.size() == 2);
}

TEST_CASE(
    "animation_timeline.bulk_delete_rejects_duplicate_missing_invalid_and_later_batch_failure") {
  auto clip = source();
  const std::vector<std::vector<KeyReference>> invalid{
      {},
      {{2, Path::Translation, 0}, {2, Path::Translation, 0}},
      {{2, Path::Translation, .5F}},
      {{99, Path::Translation, 0}},
      {{2, Path::Translation, -1}},
      {{2, Path::Translation, std::numeric_limits<float>::quiet_NaN()}},
      std::vector<KeyReference>(MaxSelectedKeys + 1, {2, Path::Translation, 0})};

  for (const auto& keys : invalid) {
    std::vector<ClipEdit> changes{RenameClip{"No partial publication"}, RemoveKeys{keys}};
    CHECK_FALSE(editClip(clip, changes));
    CHECK(clip.description().value()->name == "Timeline");
    CHECK(clip.description().value()->tracks[0].keys.size() == 2);
  }
}

TEST_CASE("animation_timeline.bulk_delete_retains_cubic_minimum_without_invented_keys") {
  auto clip = Clip::create({"Cubic",
                            {{0,
                              Path::Translation,
                              Interpolation::CubicSpline,
                              {{0, {1, 0, 0, 0}}, {1, {2, 0, 0, 0}}}},
                             {1, Path::Translation, Interpolation::Linear, {{0, {4, 0, 0, 0}}}}}})
                  .value();
  std::vector<ClipEdit> changes{RemoveKeys{{{0, Path::Translation, 0}}}};
  CHECK_FALSE(editClip(clip, changes));
  changes = {RemoveKeys{{{0, Path::Translation, 0}, {0, Path::Translation, 1}}}};
  auto edited = editClip(clip, changes).value();
  REQUIRE(edited.description().value()->tracks.size() == 1);
  CHECK(edited.description().value()->tracks[0].node == 1);
  CHECK(edited.description().value()->tracks[0].keys[0].value.x == 4);
}

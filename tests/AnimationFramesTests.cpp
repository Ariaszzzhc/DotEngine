#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

import Dot.Core.Result;
import Dot.Editor.AnimationFrames;
import Dot.Editor.AnimationKeyMove;
import Dot.Animation.Clip;

TEST_CASE("animation_frames.rational_rate_native_samples_and_positive_half_ties") {
  auto grid = Dot::Editor::AnimationFrames::create({60000, 1000}).value();
  CHECK(grid.rate() == Dot::Editor::AnimationRate{60, 1});
  CHECK(grid.time(48).value() == doctest::Approx(.8));
  CHECK(grid.seconds(48).value() == doctest::Approx(.8));
  CHECK(grid.seconds(.5).value() == doctest::Approx(1. / 120));
  CHECK_FALSE(grid.seconds(-1));
  CHECK_FALSE(grid.seconds(std::numeric_limits<double>::infinity()));
  CHECK(grid.sample(48)->native == .8F);
  CHECK(grid.nearest(.75)->index == 45);
  auto ntsc = Dot::Editor::AnimationFrames::create({30000, 1001}).value();
  CHECK(ntsc.time(1000).value() == doctest::Approx(1001. / 30));
  CHECK(ntsc.nearest(ntsc.time(1000).value())->index == 1000);
  auto thirty = Dot::Editor::AnimationFrames::create().value();
  CHECK(thirty.nearest(.75)->index == 23);
  CHECK(thirty.coordinate(.75).value() == 22.5);
  CHECK(thirty.nearest(86400)->index == thirty.maximum());
  CHECK(ntsc.nearest(86400)->index == ntsc.maximum());
}

TEST_CASE("animation_frames.adjacent_steps_native_grid_samples_and_off_grid_times") {
  auto grid = Dot::Editor::AnimationFrames::create().value();
  const auto at = grid.sample(24).value();
  CHECK(grid.adjacent(at.native, -1)->index == 23);
  CHECK(grid.adjacent(at.native, 1)->index == 25);
  CHECK(grid.adjacent(.8, -1)->index == 23);
  CHECK(grid.adjacent(.75, -1)->index == 22);
  CHECK(grid.adjacent(.75, 1)->index == 23);
  CHECK_FALSE(grid.adjacent(0, -1));
  CHECK_FALSE(grid.adjacent(86400, 1));
  CHECK_FALSE(grid.adjacent(0, 0));
  CHECK_FALSE(grid.adjacent(0, 2));
  CHECK(grid.adjacent(0, 1)->index == 1);
}

TEST_CASE("animation_frames.rejects_invalid_rates_times_and_unrepresentable_long_frames") {
  for (auto rate : std::vector<Dot::Editor::AnimationRate>{
           {0, 1}, {30, 0}, {241, 1}, {240001, 1001}, {1, 1001}, {60000, 2000}}) {
    CHECK_FALSE(Dot::Editor::AnimationFrames::create(rate));
  }

  auto grid = Dot::Editor::AnimationFrames::create({240, 1}).value();
  CHECK_FALSE(grid.sample(grid.maximum() - 1));
  CHECK(grid.sample(grid.maximum()));
  CHECK_FALSE(grid.sample(std::numeric_limits<std::uint64_t>::max()));

  for (double time : {-1., 86400.01, std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::infinity()}) {
    CHECK_FALSE(grid.coordinate(time));
    CHECK_FALSE(grid.nearest(time));
    CHECK_FALSE(grid.adjacent(time, 1));
  }

  CHECK(grid.sample(192));
}

TEST_CASE("animation_frames.ruler_ticks_are_bounded_aligned_and_nominal") {
  auto grid = Dot::Editor::AnimationFrames::create().value();
  const auto ticks = grid.ticks(.11, 2, 9).value();
  REQUIRE_FALSE(ticks.empty());
  CHECK(ticks.size() <= 9);
  CHECK(ticks.front().seconds >= .11);
  CHECK(ticks.back().seconds <= 2);

  for (std::size_t i = 0; i < ticks.size(); ++i) {
    CHECK(ticks[i].seconds == grid.time(ticks[i].index).value());

    if (i > 0) {
      CHECK(ticks[i].index > ticks[i - 1].index);
    }
  }

  CHECK(grid.ticks(0, 86400, 64)->size() <= 64);
  CHECK(grid.ticks(.001, .002, 9)->empty());
  CHECK_FALSE(grid.ticks(1, 0, 9));
  CHECK_FALSE(grid.ticks(0, 0, 9));
  CHECK_FALSE(grid.ticks(0, 1, 1));
  CHECK_FALSE(grid.ticks(0, 1, 65));
}

TEST_CASE("animation_frames.anchor_snapping_preserves_relative_group_times_and_collisions") {
  using namespace Dot::Animation;
  auto clip = Clip::create({"Frame edit",
                            {{0,
                              Path::Translation,
                              Interpolation::Linear,
                              {{0, {0, 0, 0, 0}}, {.75F, {1, 0, 0, 0}}, {2, {0, 0, 0, 0}}}},
                             {1,
                              Path::Translation,
                              Interpolation::Linear,
                              {{.25F, {0, 0, 0, 0}}, {.75F, {2, 0, 0, 0}}}}}})
                  .value();
  const std::vector<KeyReference> selected{{1, Path::Translation, .25F},
                                           {0, Path::Translation, .75F}};
  auto move = Dot::Editor::AnimationKeyMove::begin(clip, selected).value();
  auto grid = Dot::Editor::AnimationFrames::create().value();
  REQUIRE(move.update(grid.nearest(.798)->native));
  auto snapshot = move.snapshot().value();
  CHECK(snapshot.time == .8F);
  CHECK(snapshot.keys[0].key.time == doctest::Approx(.3));
  CHECK(snapshot.keys[1].key.value.x == 1);
  CHECK_FALSE(move.update(grid.sample(0)->native));
  const std::vector<KeyReference> one{{0, Path::Translation, .75F}};
  auto collision = Dot::Editor::AnimationKeyMove::begin(clip, one).value();
  REQUIRE(collision.update(grid.sample(0)->native));
  CHECK_FALSE(collision.edit());
  REQUIRE(move.update(.75F));
  CHECK_FALSE(move.snapshot()->changed);
}

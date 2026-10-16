#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

import Dot.Animation.KeyGroup;

namespace {
  using namespace Dot::Animation;

  auto source() -> Clip {
    return Clip::create({"Source",
                         {{1,
                           Path::Translation,
                           Interpolation::Linear,
                           {{0, {0, 0, 0, 0}}, {1, {1, 2, 3, 0}}, {2, {2, 4, 6, 0}}}},
                          {2,
                           Path::Rotation,
                           Interpolation::CubicSpline,
                           {{0, {0, 0, 0, 1}, {1, 0, 0, 0}, {0, 1, 0, 0}},
                            {2, {0, 0, 1, 0}, {0, 0, 1, 0}, {1, 1, 0, 0}}}}}})
        .value();
  }

  auto group(const Clip& clip = source()) -> KeyGroup {
    const std::array<KeyReference, 3> keys{
        {{1, Path::Translation, 2}, {1, Path::Translation, 0}, {2, Path::Rotation, 0}}};
    return KeyGroup::create(clip, keys).value();
  }
}  // namespace

TEST_CASE("animation_keys.owned_ordered_selection_survives_source_retirement_and_moves") {
  auto clip = source();
  auto keys = group(clip);
  auto shared = keys;
  const auto* value = keys.description().value();
  CHECK(value->keys.size() == 3);
  CHECK(value->channels == 2);
  CHECK(value->start == 0);
  CHECK(value->end == 2);
  CHECK(value->keys[0].key.time == 2);
  CHECK(value->keys[2].interpolation == Interpolation::CubicSpline);
  clip = {};
  auto moved = std::move(keys);
  CHECK_FALSE(keys.description());
  CHECK_FALSE(keys.at(0));
  CHECK(moved.description().value() == value);
  moved = {};
  REQUIRE(shared.at(1));
  CHECK(shared.description().value()->keys[0].key.value.x == 2);
  CHECK_FALSE(KeyGroup{}.description());
  CHECK_FALSE(KeyGroup{}.at(0));
}

TEST_CASE("animation_keys.placement_aligns_earliest_time_without_mutating_owned_payloads") {
  auto keys = group();
  auto placed = keys.at(10).value();
  CHECK(placed[0].key.time == 12);
  CHECK(placed[1].key.time == 10);
  CHECK(placed[2].key.time == 10);
  CHECK(placed[0].key.value.x == 2);
  CHECK(placed[2].key.inTangent.x == 1);
  placed[0].key.value.x = 99;
  CHECK(keys.description().value()->keys[0].key.value.x == 2);
  CHECK(keys.description().value()->keys[0].key.time == 2);

  for (const auto time : {-1.F, 86400.F, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
    CHECK_FALSE(keys.at(time));
    CHECK(keys.description().value()->start == 0);
    CHECK(keys.at(0)->at(0).key.time == 2);
  }

  REQUIRE(keys.at(86398));
  CHECK(keys.at(86398)->at(0).key.time == 86400);
}

TEST_CASE("animation_keys.rejects_invalid_duplicate_empty_and_oversized_sources") {
  const auto clip = source();
  CHECK_FALSE(KeyGroup::create(clip, {}));
  const std::array<KeyReference, 2> duplicate{
      {{1, Path::Translation, 0}, {1, Path::Translation, -0.F}}};
  CHECK_FALSE(KeyGroup::create(clip, duplicate));

  for (const auto key :
       {KeyReference{1, Path::Translation, .5F}, KeyReference{9999, Path::Translation, 0},
        KeyReference{1, static_cast<Path>(99), 0}, KeyReference{1, Path::Translation, -1},
        KeyReference{1, Path::Translation, std::numeric_limits<float>::quiet_NaN()}}) {
    const std::array keys{key};
    CHECK_FALSE(KeyGroup::create(clip, keys));
    CHECK_FALSE(KeyGroup::create({}, keys));
  }

  std::vector<KeyReference> tooMany(MaxSelectedKeys + 1, duplicate[0]);
  CHECK_FALSE(KeyGroup::create(clip, tooMany));
  CHECK(clip.description().value()->tracks[0].keys.size() == 3);
}

TEST_CASE("animation_keys.partial_cubic_groups_preserve_tangents_without_becoming_a_clip") {
  const auto clip = source();
  const std::array<KeyReference, 1> selection{{{2, Path::Rotation, 0}}};
  auto keys = KeyGroup::create(clip, selection).value();
  REQUIRE(keys.description().value()->keys.size() == 1);
  auto placed = keys.at(3).value();
  CHECK(placed[0].interpolation == Interpolation::CubicSpline);
  CHECK(placed[0].key.value == clip.description().value()->tracks[1].keys[0].value);
  CHECK(placed[0].key.inTangent == clip.description().value()->tracks[1].keys[0].inTangent);
  CHECK(placed[0].key.outTangent == clip.description().value()->tracks[1].keys[0].outTangent);
  CHECK(placed[0].key.time == 3);
}

TEST_CASE("animation_keys.float_collapse_rejects_placement_and_origin_preserves_subnormal_times") {
  const auto tiny = std::numeric_limits<float>::denorm_min();
  auto clip = Clip::create({"Tiny",
                            {{1,
                              Path::Translation,
                              Interpolation::Linear,
                              {{0, {0, 0, 0, 0}}, {tiny, {1, 0, 0, 0}}}}}})
                  .value();
  const std::array<KeyReference, 2> selection{
      {{1, Path::Translation, 0}, {1, Path::Translation, tiny}}};
  const auto keys = KeyGroup::create(clip, selection).value();
  CHECK_FALSE(keys.at(86400));
  CHECK(keys.at(0)->at(1).key.time == tiny);
  CHECK(keys.description().value()->end == tiny);
}

TEST_CASE("animation_keys.bounded_groups_have_owned_concurrent_placements") {
  Track track{1, Path::Translation, Interpolation::Linear, {}};
  std::vector<KeyReference> selected;

  for (std::size_t i = 0; i < MaxSelectedKeys; ++i) {
    const auto time = static_cast<float>(i) * .5F;
    track.keys.push_back({time, {time, 0, 0, 0}});
    selected.push_back({1, Path::Translation, time});
  }

  const auto clip = Clip::create({"Full", {track}}).value();
  const auto keys = KeyGroup::create(clip, selected).value();
  bool copied{};
  std::thread worker([&] {
    const auto placed = keys.at(.5F);
    copied = placed && placed->size() == MaxSelectedKeys && placed->front().key.time == .5F;
  });
  const auto placed = keys.at(1).value();
  worker.join();
  CHECK(copied);
  CHECK(placed.back().key.time == track.keys.back().time + 1);
  CHECK(keys.description().value()->keys.front().key.time == 0);
}

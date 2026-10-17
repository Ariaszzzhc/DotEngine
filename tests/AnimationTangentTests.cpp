#include <doctest/doctest.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <utility>

import Dot.Editor.AnimationTangent;

namespace {
  auto cubic(Dot::Animation::Path path = Dot::Animation::Path::Translation)
      -> Dot::Animation::Clip {
    return Dot::Animation::Clip::create(
               {"Tangent",
                {{2,
                  path,
                  Dot::Animation::Interpolation::CubicSpline,
                  {{0, {0, 0, 0, path == Dot::Animation::Path::Rotation ? 1.F : 0.F}},
                   {1, {0, 1, 0, 0}, {-0.F, 2, 3, 0}, {4, 5, 6, 0}},
                   {3, {0, 1, 0, 0}}}}}})
        .value();
  }
}  // namespace

TEST_CASE("animation_tangent.edits_one_derivative_without_changing_key_or_other_side") {
  using namespace Dot::Animation;
  auto source = cubic();
  auto tool
      = Dot::Editor::AnimationTangent::begin(source, {2, Path::Translation, 1}, 1, false).value();
  auto before = tool.snapshot().value();
  CHECK(before.offset == doctest::Approx(2. / 3));
  CHECK(before.handleTime == doctest::Approx(5. / 3));
  REQUIRE(tool.update(-4));
  auto state = tool.snapshot().value();
  CHECK(state.changed);
  CHECK(state.handleValue == doctest::Approx(1 - 8. / 3));
  CHECK(before.slope == 5);
  auto edited = tool.value().value();
  const auto key = edited.description().value()->tracks[0].keys[1];
  CHECK(key.time == 1);
  CHECK(key.value.y == 1);
  CHECK(key.inTangent.y == 2);
  CHECK(key.outTangent.x == 4);
  CHECK(key.outTangent.y == -4);
  CHECK(key.outTangent.z == 6);
  CHECK(source.description().value()->tracks[0].keys[1].outTangent.y == 5);
  CHECK(edited.sampleTrack(0, 2)->value.y != source.sampleTrack(0, 2)->value.y);
}

TEST_CASE("animation_tangent.invalid_updates_preserve_ghost_and_origin_restores_float_bits") {
  using namespace Dot::Animation;
  auto tool
      = Dot::Editor::AnimationTangent::begin(cubic(), {2, Path::Translation, 1}, 0, true).value();
  CHECK(tool.snapshot()->offset == doctest::Approx(-1. / 3));
  REQUIRE(tool.update(7));
  CHECK_FALSE(tool.update(std::numeric_limits<float>::infinity()));
  CHECK_FALSE(tool.update(std::numeric_limits<float>::quiet_NaN()));
  CHECK(tool.snapshot()->slope == 7);
  REQUIRE(tool.update(0));
  CHECK_FALSE(tool.snapshot()->changed);
  CHECK(std::bit_cast<std::uint32_t>(tool.snapshot()->slope) == std::bit_cast<std::uint32_t>(-0.F));
  CHECK(std::bit_cast<std::uint32_t>(
            tool.value()->description().value()->tracks[0].keys[1].inTangent.x)
        == std::bit_cast<std::uint32_t>(-0.F));
}

TEST_CASE("animation_tangent.rejects_missing_non_cubic_keys_components_and_unused_sides") {
  using namespace Dot::Animation;
  const auto source = cubic();
  CHECK_FALSE(Dot::Editor::AnimationTangent::begin({}, {2, Path::Translation, 1}, 0, true));
  CHECK_FALSE(Dot::Editor::AnimationTangent::begin(source, {9, Path::Translation, 1}, 0, true));
  CHECK_FALSE(Dot::Editor::AnimationTangent::begin(source, {2, Path::Translation, .5F}, 0, true));
  CHECK_FALSE(Dot::Editor::AnimationTangent::begin(source, {2, Path::Translation, 1}, 3, true));
  CHECK_FALSE(Dot::Editor::AnimationTangent::begin(source, {2, Path::Translation, 0}, 0, true));
  CHECK_FALSE(Dot::Editor::AnimationTangent::begin(source, {2, Path::Translation, 3}, 0, false));
  auto linear = Clip::create(
                    {"Linear",
                     {{2, Path::Translation, Interpolation::Linear, {{0, {}}, {1, {1, 0, 0, 0}}}}}})
                    .value();
  CHECK_FALSE(Dot::Editor::AnimationTangent::begin(linear, {2, Path::Translation, 0}, 0, false));
}

TEST_CASE("animation_tangent.quaternion_derivatives_remain_raw_and_move_retires_source") {
  using namespace Dot::Animation;
  auto tool = Dot::Editor::AnimationTangent::begin(cubic(Path::Rotation), {2, Path::Rotation, 1}, 3,
                                                   false)
                  .value();
  REQUIRE(tool.update(8));
  auto moved = std::move(tool);
  CHECK_FALSE(tool.snapshot());
  CHECK_FALSE(tool.update(2));
  CHECK_FALSE(tool.value());
  auto output = moved.value().value();
  CHECK(output.description().value()->tracks[0].keys[1].outTangent.w == 8);
  auto result = output.sampleTrack(0, 2).value();
  CHECK(result.value.w > 0);
  CHECK(result.value.w < 1);
  auto replacement
      = Dot::Editor::AnimationTangent::begin(cubic(), {2, Path::Translation, 1}, 0, false).value();
  replacement = std::move(moved);
  CHECK_FALSE(moved.snapshot());
  CHECK(replacement.snapshot()->slope == 8);
}

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

import Dot.Graphics.Shadows;
import Dot.Graphics.Geometry;
import Dot.Math;

using namespace Dot::Graphics;

namespace {
  auto camera(Dot::Math::Vec3 position = {0, 0, 5}) -> Dot::Math::Mat4 {
    return Dot::Math::multiply(
               Dot::Math::perspective(60, 1, 0.1F, 100).value(),
               Dot::Math::lookAt(position, position + Dot::Math::Vec3{0, 0, -1}).value())
        .value();
  }
}  // namespace

TEST_CASE("shadows.cpu_settings_frustum_bounds_and_work_are_explicit") {
  ShadowSettings settings;
  CHECK(validateShadowSettings(settings));

  for (const auto resolution : {0U, 127U, 129U, 4096U}) {
    settings.resolution = resolution;
    CHECK_FALSE(validateShadowSettings(settings));
  }

  settings = {};
  settings.cascades = 5;
  CHECK_FALSE(validateShadowSettings(settings));
  settings.cascades = 0;
  CHECK_FALSE(validateShadowSettings(settings));
  settings = {};
  settings.distance = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(validateShadowSettings(settings));
  settings = {};
  settings.filterRadius = 9;
  CHECK_FALSE(validateShadowSettings(settings));
  settings = {};
  settings.normalBias = -1;
  CHECK_FALSE(validateShadowSettings(settings));
  CHECK_FALSE(fitDirectionalShadows(camera(), {0, 0, 5}, {}, {}));
  Dot::Math::Mat4 singular;
  singular.data.fill(0);
  CHECK_FALSE(fitDirectionalShadows(singular, {}, {0, -1, 0}, {}));
  Dot::Math::Mat4 horizon;
  horizon.data[3] = 2;
  CHECK_FALSE(fitDirectionalShadows(horizon, {}, {0, -1, 0}, {}));
  const std::array invalid{Bounds{{1, 0, 0}, {-1, 1, 1}}};
  CHECK_FALSE(fitDirectionalShadows(camera(), {0, 0, 5}, {0, -1, 0}, {}, invalid));
  const std::vector<Bounds> excess(10001);
  CHECK_FALSE(fitDirectionalShadows(camera(), {0, 0, 5}, {0, -1, 0}, {}, excess));
  settings = {};
  settings.distance = 0.1F;
  CHECK_FALSE(fitDirectionalShadows(camera(), {0, 0, 5}, {0, -1, 0}, settings));
}

TEST_CASE("shadows.cpu_cascade_splits_cover_independent_frustum_corners") {
  const auto projection = camera();
  const auto inverse = Dot::Math::inverse(projection).value();
  ShadowSettings settings;
  settings.distance = 50;
  settings.splitLambda = 0;
  const auto fit
      = fitDirectionalShadows(projection, {0, 0, 5}, {0.2F, -1, -0.3F}, settings).value();
  CHECK(fit.count == 4);
  CHECK(fit.forward.z == doctest::Approx(-1));
  float previous = 0.1F;

  for (std::uint32_t cascade = 0; cascade < 4; ++cascade) {
    const auto split = 0.1F + (50 - 0.1F) * static_cast<float>(cascade + 1) / 4;
    CHECK(fit.splits[cascade] == doctest::Approx(split).epsilon(0.001));
    CHECK(fit.texelSize[cascade] > 0);

    for (const auto depth : {previous, split}) {
      for (std::uint32_t corner = 0; corner < 4; ++corner) {
        const Dot::Math::Vec3 clip{corner & 1 ? 1.0F : -1.0F, corner & 2 ? 1.0F : -1.0F, 0};
        const auto near = Dot::Math::transformPoint(inverse, clip).value();
        const auto far = Dot::Math::transformPoint(inverse, {clip.x, clip.y, 1}).value();
        const auto point = near + (far - near) * ((depth - 0.1F) / (100 - 0.1F));
        const auto projected = Dot::Math::transformPoint(fit.matrices[cascade], point).value();
        CHECK(std::abs(projected.x) <= 1.0001F);
        CHECK(std::abs(projected.y) <= 1.0001F);
        CHECK(projected.z >= -0.0001F);
        CHECK(projected.z <= 1.0001F);
      }
    }

    previous = split;
  }

  settings.splitLambda = 1;
  const auto logarithmic
      = fitDirectionalShadows(projection, {0, 0, 5}, {0, -1, 0}, settings).value();
  CHECK(logarithmic.splits[0] == doctest::Approx(0.1 * std::pow(500, 0.25)).epsilon(0.001));
}

TEST_CASE("shadows.cpu_texel_stability_orthographic_camera_and_offscreen_casters") {
  ShadowSettings settings;
  settings.cascades = 1;
  settings.distance = 10;
  const auto view = Dot::Math::multiply(Dot::Math::orthographic(-2, 2, -2, 2, 0.1F, 10).value(),
                                        Dot::Math::lookAt({0, 0, 5}, {}).value())
                        .value();
  const auto original = fitDirectionalShadows(view, {0, 0, 5}, {0, 0, -1}, settings).value();
  const auto shift = original.texelSize[0] * 0.1F;
  const auto moved = Dot::Math::multiply(Dot::Math::orthographic(-2, 2, -2, 2, 0.1F, 10).value(),
                                         Dot::Math::lookAt({shift, 0, 5}, {shift, 0, 0}).value())
                         .value();
  const auto stable = fitDirectionalShadows(moved, {shift, 0, 5}, {0, 0, -1}, settings).value();
  CHECK(stable.matrices[0].data[12] == original.matrices[0].data[12]);
  CHECK(stable.matrices[0].data[0] == original.matrices[0].data[0]);
  const std::array casters{Bounds{{-0.5F, -0.5F, 10}, {0.5F, 0.5F, 12}}};
  const auto expanded
      = fitDirectionalShadows(view, {0, 0, 5}, {0, 0, -1}, settings, casters).value();
  const auto inDepth = Dot::Math::transformPoint(expanded.matrices[0], {0, 0, 11}).value();
  CHECK(inDepth.z >= 0);
  CHECK(inDepth.z <= 1);
  CHECK(expanded.matrices[0].data[10] != original.matrices[0].data[10]);
  const std::array outside{Bounds{{10000, 10000, 10}, {10001, 10001, 12}}};
  const auto excluded
      = fitDirectionalShadows(view, {0, 0, 5}, {0, 0, -1}, settings, outside).value();
  CHECK(excluded.matrices[0] == original.matrices[0]);
}

TEST_CASE("shadows.cpu_local_projections_cover_faces_cones_and_depth_bounds") {
  using namespace Dot::Math;
  ShadowSettings settings;
  settings.distance = 12;
  const Vec3 position{2, 3, -4};
  const auto point = fitPointShadows(position, 20, settings);
  REQUIRE(point);
  CHECK(point->kind == ShadowProjectionKind::Point);
  CHECK(point->count == 6);
  CHECK(point->origin == position);
  CHECK(point->splits[0] == 12);
  const std::array<Vec3, 6> axes{
      {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};

  for (std::size_t face = 0; face < axes.size(); ++face) {
    const auto near
        = transformPoint(point->matrices[face], position + axes[face] * point->nearDepth);
    const auto center = transformPoint(point->matrices[face], position + axes[face] * 2);
    const auto far = transformPoint(point->matrices[face], position + axes[face] * 12);
    REQUIRE(near);
    REQUIRE(center);
    REQUIRE(far);
    CHECK(std::abs(near->z) < .00002F);
    CHECK(center->x == doctest::Approx(0).epsilon(.00001));
    CHECK(center->y == doctest::Approx(0).epsilon(.00001));
    CHECK(center->z > 0);
    CHECK(center->z < 1);
    CHECK(far->z == doctest::Approx(1));
    CHECK(transformPoint(point->matrices[face], position + axes[face] * 24)->z > 1);
    CHECK(Dot::Math::isFinite(point->matrices[face]));
  }

  // A ray on a face seam belongs to both independent projections.
  // 面接缝上的射线同时落在两个独立投影边界。
  const Vec3 seam = position + Vec3{2, 2, 0};
  CHECK(std::abs(transformPoint(point->matrices[0], seam)->y) == doctest::Approx(1));
  CHECK(std::abs(transformPoint(point->matrices[2], seam)->x) == doctest::Approx(1));
  const auto spot = fitSpotShadow(position, {0, -4, 0}, 30, 5, settings);
  REQUIRE(spot);
  CHECK(spot->kind == ShadowProjectionKind::Spot);
  CHECK(spot->count == 1);
  CHECK(spot->forward == Vec3{0, -1, 0});
  CHECK(spot->splits[0] == 5);
  CHECK(transformPoint(spot->matrices[0], position + Vec3{0, -2, 0})->x == doctest::Approx(0));
  const auto edge = transformPoint(spot->matrices[0], position + Vec3{1.15470054F, -2, 0}).value();
  CHECK(std::abs(edge.x) == doctest::Approx(1));
  CHECK(std::abs(transformPoint(spot->matrices[0], position + Vec3{2, -2, 0})->x) > 1);
  CHECK(fitPointShadows({}, .01F, settings)->nearDepth == doctest::Approx(.0001F));
  CHECK(fitSpotShadow({}, {1, .001F, 0}, .1F, .01F, settings));
}

TEST_CASE("shadows.cpu_local_invalid_inputs_fail_without_camera_dependencies") {
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  ShadowSettings settings;

  for (const auto range : {nan, 0.F, -.01F, .009F, 1'000'001.F}) {
    CHECK_FALSE(fitPointShadows({}, range, settings));
    CHECK_FALSE(fitSpotShadow({}, {0, -1, 0}, 30, range, settings));
  }

  for (const auto position : {Dot::Math::Vec3{nan, 0, 0}, Dot::Math::Vec3{0, 1'000'001, 0}}) {
    CHECK_FALSE(fitPointShadows(position, 10, settings));
    CHECK_FALSE(fitSpotShadow(position, {0, -1, 0}, 30, 10, settings));
  }

  for (const auto cone : {nan, -1.F, 0.F, 90.F, 91.F}) {
    CHECK_FALSE(fitSpotShadow({}, {0, -1, 0}, cone, 10, settings));
  }

  CHECK_FALSE(fitSpotShadow({}, {}, 30, 10, settings));
  CHECK_FALSE(fitSpotShadow({}, {nan, 0, 0}, 30, 10, settings));
  settings.resolution = 129;
  CHECK_FALSE(fitPointShadows({}, 10, settings));
  CHECK_FALSE(fitSpotShadow({}, {0, -1, 0}, 30, 10, settings));
}

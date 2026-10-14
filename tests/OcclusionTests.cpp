#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>

import Dot.Graphics.Occlusion;
import Dot.Math;

using namespace Dot::Graphics;

TEST_CASE("occlusion.cpu_settings_and_working_budget_are_bounded_without_a_device") {
  OcclusionSettings settings;
  CHECK(validateOcclusion(settings));
  settings.radius = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(validateOcclusion(settings));
  settings.radius = 0.01F;
  CHECK_FALSE(validateOcclusion(settings));
  settings = {};
  settings.bias = -1;
  CHECK_FALSE(validateOcclusion(settings));
  settings = {};
  settings.strength = 5;
  CHECK_FALSE(validateOcclusion(settings));
  settings = {};
  settings.directions = 7;
  CHECK_FALSE(validateOcclusion(settings));
  settings = {};
  settings.steps = 9;
  CHECK_FALSE(validateOcclusion(settings));
  settings = {};
  settings.filterRadius = 5;
  CHECK_FALSE(validateOcclusion(settings));
  CHECK(occlusionBytes(1920, 1080).value() == 1920ULL * 1080 * 24);
  CHECK(occlusionBytes(3840, 2160));
  CHECK_FALSE(occlusionBytes(4096, 4096));
  CHECK_FALSE(occlusionBytes(0, 32));
  CHECK_FALSE(occlusionBytes(std::numeric_limits<std::uint32_t>::max(), 32));
}

TEST_CASE("occlusion.cpu_perspective_and_orthographic_reconstruction_preserve_world_points") {
  const auto view = Dot::Math::lookAt({2, 3, 5}, {}).value();
  const std::array projections{Dot::Math::perspective(60, 1.5F, 0.1F, 100).value(),
                               Dot::Math::orthographic(-4, 4, -3, 3, 0.1F, 100).value()};

  for (const auto& projection : projections) {
    const auto matrix = Dot::Math::multiply(projection, view).value();
    auto camera = occlusionCamera(matrix).value();
    CHECK(Dot::Math::length(camera.right) == doctest::Approx(1));
    CHECK(Dot::Math::dot(camera.right, camera.up) == doctest::Approx(0).epsilon(0.0001));

    for (const auto point :
         {Dot::Math::Vec3{}, Dot::Math::Vec3{1, 0.2F, 0}, Dot::Math::Vec3{-0.5F, 1, -1}}) {
      const auto clip = Dot::Math::transformPoint(matrix, point).value();
      const auto restored
          = reconstructPosition(camera, {(clip.x + 1) * 0.5F, (1 - clip.y) * 0.5F}, clip.z).value();
      CHECK(restored.x == doctest::Approx(point.x).epsilon(0.0001));
      CHECK(restored.y == doctest::Approx(point.y).epsilon(0.0001));
      CHECK(restored.z == doctest::Approx(point.z).epsilon(0.0001));
    }
  }
}

TEST_CASE("occlusion.cpu_invalid_frustum_coordinates_and_depth_are_explicit_errors") {
  Dot::Math::Mat4 singular;
  singular.data.fill(0);
  CHECK_FALSE(occlusionCamera(singular));
  Dot::Math::Mat4 horizon;
  horizon.data[3] = 2;
  CHECK_FALSE(occlusionCamera(horizon));
  auto camera = occlusionCamera(Dot::Math::perspective(60, 1, 0.1F, 100).value()).value();
  CHECK_FALSE(reconstructPosition(camera, {-0.1F, 0.5F}, 0.5F));
  CHECK_FALSE(reconstructPosition(camera, {0.5F, 0.5F}, 1.1F));
  CHECK_FALSE(reconstructPosition(camera, {0.5F, 0.5F}, std::numeric_limits<float>::infinity()));
  CHECK(reconstructPosition(camera, {0, 0}, 0));
  CHECK(reconstructPosition(camera, {1, 1}, 1));
}

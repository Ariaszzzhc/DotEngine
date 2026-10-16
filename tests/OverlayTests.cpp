#include <doctest/doctest.h>

#include <limits>

import Dot.Graphics.Overlay;
import Dot.Graphics.View;
import Dot.Math;

TEST_CASE("overlay.points_use_top_left_uv_and_reject_invalid_matrices_and_inputs") {
  using namespace Dot;
  Math::Mat4 matrix;
  auto projector = Graphics::OverlayProjector::create(matrix).value();
  matrix.data[0] = 2;
  CHECK(projector.point({-1, 1, 0})->value() == Math::Vec2{0, 0});
  CHECK(projector.point({1, -1, 1})->value() == Math::Vec2{1, 1});
  CHECK(projector.point({0, 0, .5F})->value() == Math::Vec2{.5F, .5F});
  CHECK_FALSE(projector.point({2, 0, .5F})->has_value());
  CHECK_FALSE(projector.point({0, 0, -.1F})->has_value());
  CHECK_FALSE(projector.point({0, 0, 1.1F})->has_value());
  matrix.data.fill(0);
  CHECK_FALSE(Graphics::OverlayProjector::create(matrix));
  matrix = {};
  matrix.data[0] = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(Graphics::OverlayProjector::create(matrix));
  CHECK_FALSE(projector.point({std::numeric_limits<float>::infinity(), 0, 0}));
}

TEST_CASE("overlay.segments_clip_crossing_endpoints_instead_of_dropping_visible_intervals") {
  using namespace Dot;
  const auto projector = Graphics::OverlayProjector::create({}).value();
  auto horizontal = projector.segment({-2, 0, .5F}, {2, 0, .5F})->value();
  CHECK(horizontal.from == Math::Vec2{0, .5F});
  CHECK(horizontal.to == Math::Vec2{1, .5F});
  auto vertical = projector.segment({0, 2, .5F}, {0, -2, .5F})->value();
  CHECK(vertical.from == Math::Vec2{.5F, 0});
  CHECK(vertical.to == Math::Vec2{.5F, 1});
  auto depth = projector.segment({-.5F, 0, -1}, {.5F, 0, 2})->value();
  CHECK(depth.from.x == doctest::Approx(5.0 / 12));
  CHECK(depth.to.x == doctest::Approx(7.0 / 12));
  CHECK_FALSE(projector.segment({-2, 2, .5F}, {2, 2, .5F})->has_value());
  CHECK_FALSE(projector.segment({0, 0, -2}, {0, 0, -1})->has_value());
  CHECK_FALSE(projector.segment({std::numeric_limits<float>::quiet_NaN(), 0, 0}, {}));
}

TEST_CASE("overlay.perspective_near_plane_crossing_is_clipped_before_dividing_by_w") {
  using namespace Dot;
  Graphics::View view;
  view.position = {};
  view.rotation = {};
  view.verticalFovDegrees = 90;
  view.nearPlane = 1;
  view.farPlane = 10;
  auto projector
      = Graphics::OverlayProjector::create(Graphics::viewProjection(view, 1).value()).value();
  CHECK_FALSE(projector.point({0, 0, .5F})->has_value());
  auto line = projector.segment({-.2F, 0, .5F}, {.2F, 0, -2});
  REQUIRE(line);
  REQUIRE(*line);
  CHECK((*line)->from.x == doctest::Approx(.52));
  CHECK((*line)->to.x == doctest::Approx(.55));
  CHECK((*line)->from.y == .5F);
  CHECK_FALSE(projector.segment({0, 0, 1}, {0, 0, 2})->has_value());
}

TEST_CASE("overlay.geometry_limits_colors_and_logical_pixel_sizes_are_strict") {
  using namespace Dot;
  Graphics::ViewportOverlay overlay;
  CHECK(Graphics::validateOverlay(overlay));
  overlay.lines.push_back({{{0, 0}, {1, 1}}});
  overlay.points.push_back({{.5F, .5F}});
  CHECK(Graphics::validateOverlay(overlay));
  overlay.lines[0].thickness = .49F;
  CHECK_FALSE(Graphics::validateOverlay(overlay));
  overlay.lines[0].thickness = 16;
  overlay.points[0].radius = 16;
  CHECK(Graphics::validateOverlay(overlay));
  overlay.points[0].position.x = 1.01F;
  CHECK_FALSE(Graphics::validateOverlay(overlay));
  overlay.points[0].position.x = .5F;
  overlay.points[0].color.w = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(Graphics::validateOverlay(overlay));
  overlay.points[0].color = {0, 1, 0, 1};
  overlay.points.resize(4096, overlay.points[0]);
  CHECK(Graphics::validateOverlay(overlay));
  overlay.points.push_back(overlay.points[0]);
  CHECK_FALSE(Graphics::validateOverlay(overlay));
}

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

import Dot.Editor.TransformTool;
import Dot.Math;

TEST_CASE("editor.axis_drag_orthographic_perspective_absolute_snap_and_parallel_limits") {
  using namespace Dot;
  auto projection = Math::orthographic(-2, 2, -2, 2, 0.1F, 100).value();
  auto view = Math::lookAt({0, 0, 5}, {0, 0, 0}).value();
  auto vp = Math::multiply(projection, view).value();
  auto drag = Editor::AxisDrag::begin(vp, {}, {2, 0, 0}, {0.5F, 0.5F});
  REQUIRE(drag);
  CHECK(drag->delta({0.75F, 0.5F})->x == doctest::Approx(1));
  CHECK(drag->delta({0.25F, 0.5F})->x == doctest::Approx(-1));
  CHECK(drag->delta({0.64F, 0.5F}, 0.5F)->x == doctest::Approx(0.5F));
  CHECK(drag->delta({0.5F, 0.5F})->x == 0);
  CHECK_FALSE(Editor::AxisDrag::begin(vp, {}, {0, 0, 1}, {0.5F, 0.5F}));
  CHECK_FALSE(Editor::AxisDrag::begin(vp, {}, {}, {0.5F, 0.5F}));
  CHECK_FALSE(drag->delta({-0.1F, 0.5F}));
  CHECK_FALSE(drag->delta({0.5F, 0.5F}, -1));
  CHECK_FALSE(drag->delta({0.5F, 0.5F}, std::numeric_limits<float>::infinity()));
  Math::Mat4 singular;
  singular.data.fill(0);
  CHECK_FALSE(Editor::AxisDrag::begin(singular, {}, {1, 0, 0}, {0.5F, 0.5F}));
  projection = Math::perspective(60, 1, 0.1F, 100).value();
  vp = Math::multiply(projection, view).value();
  drag = Editor::AxisDrag::begin(vp, {}, {1, 0, 0}, {0.5F, 0.5F});
  REQUIRE(drag);
  CHECK(drag->delta({0.75F, 0.5F})->x == doctest::Approx(5 * std::tan(3.14159265F / 6) * 0.5F));
  auto vertical = Editor::AxisDrag::begin(vp, {}, {0, 1, 0}, {0.5F, 0.5F});
  REQUIRE(vertical);
  CHECK(vertical->delta({0.5F, 0.25F})->y > 1);
}

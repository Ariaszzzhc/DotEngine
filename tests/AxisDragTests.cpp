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

TEST_CASE("editor.rotation_drag_unwrap_snap_invalid_samples_and_reflected_frame") {
  using namespace Dot;
  const auto view = Math::lookAt({0, 0, 5}, {}).value();
  const auto projection = Math::orthographic(-2, 2, -2, 2, 0.1F, 100).value();
  const auto vp = Math::multiply(projection, view).value();
  auto drag = Editor::RotationDrag::begin(vp, {}, 2, {0.75F, 0.5F});
  REQUIRE(drag);
  CHECK(drag->degrees({0.5F, 0.25F}).value() == doctest::Approx(90));
  CHECK(drag->degrees({0.25F, 0.5F}).value() == doctest::Approx(180));
  CHECK(drag->degrees({0.5F, 0.75F}).value() == doctest::Approx(270));
  CHECK(drag->degrees({0.75F, 0.5F}).value() == doctest::Approx(360));
  CHECK_FALSE(drag->degrees({0.5F, 0.5F}));
  CHECK_FALSE(drag->degrees({0.5F, 0.25F}, -1));
  CHECK(drag->degrees({0.5F, 0.25F}, 45).value() == doctest::Approx(450));
  CHECK_FALSE(Editor::RotationDrag::begin(vp, {}, 0, {0.75F, 0.5F}));
  CHECK_FALSE(Editor::RotationDrag::begin(vp, {}, 3, {0.75F, 0.5F}));
  auto singular = Math::Mat4{};
  singular.data.fill(0);
  CHECK_FALSE(Editor::RotationDrag::begin(vp, singular, 2, {0.75F, 0.5F}));
  CHECK_FALSE(Editor::RotationDrag::begin(vp, Math::compose({0, 0, 10}).value(), 2, {0.75F, 0.5F}));
  auto perspectiveFrame = Math::Mat4{};
  perspectiveFrame.data[3] = 0.5F;
  CHECK_FALSE(Editor::RotationDrag::begin(vp, perspectiveFrame, 2, {0.75F, 0.5F}));
  const auto frame = Math::compose({}, {}, {-0.5F, 1, 1}).value();
  drag = Editor::RotationDrag::begin(vp, frame, 2, {0.375F, 0.5F});
  REQUIRE(drag);
  CHECK(drag->degrees({0.5F, 0.25F}).value() == doctest::Approx(90));
}

TEST_CASE("editor.rotation_drag_perspective_oblique_frame_tracks_actual_local_angle") {
  using namespace Dot;
  const auto vp = Math::multiply(Math::perspective(60, 1.5F, 0.05F, 100).value(),
                                 Math::lookAt({3, 2, 5}, {}).value())
                      .value();
  const auto frame
      = Math::compose({}, Math::rotationEulerDegrees({20, 35, 0}).value(), {-0.5F, 1.3F, 2})
            .value();
  auto uv = [&](Math::Vec3 p) {
    const auto ndc = Math::transformPoint(vp, Math::transformPoint(frame, p).value()).value();
    return Math::Vec2{(ndc.x + 1) * 0.5F, (1 - ndc.y) * 0.5F};
  };
  auto drag = Editor::RotationDrag::begin(vp, frame, 2, uv({0.5F, 0, 0}));
  REQUIRE(drag);
  CHECK(drag->degrees(uv({0, 0.5F, 0})).value() == doctest::Approx(90).epsilon(0.001));
  CHECK(drag->degrees(uv({0.3535534F, 0.3535534F, 0}), 15).value() == doctest::Approx(45));
}

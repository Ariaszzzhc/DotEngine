#include <doctest/doctest.h>

#include <cmath>
#include <limits>

import Dot.Graphics.View;
import Dot.Editor.Camera;
import Dot.Math;

TEST_CASE("view.independent_pose_projection_and_pixel_centers_are_consistent") {
  using namespace Dot::Graphics;
  View view;
  view.position = {0, 0, 5};
  auto vp = viewProjection(view, 1).value();
  CHECK(Dot::Math::transformPoint(vp, {}).value().x == doctest::Approx(0));
  CHECK(Dot::Math::transformPoint(vp, {}).value().y == doctest::Approx(0));
  const auto source = Dot::Math::transformPoint(Dot::Math::inverse(vp).value(),
                                                {(23.5F * 2 / 101) - 1, 1 - (17.5F * 2 / 73), 0.5F})
                          .value();
  auto crop = pixelProjection(vp, 101, 73, {23.1F / 101, 17.8F / 73}).value();
  const auto pixel = Dot::Math::transformPoint(crop, source).value();
  CHECK(pixel.x == doctest::Approx(0).epsilon(0.001));
  CHECK(pixel.y == doctest::Approx(0).epsilon(0.001));
  CHECK(pixel.z == doctest::Approx(0.5F).epsilon(0.001));
  view.orthographic = true;
  vp = viewProjection(view, 2).value();
  CHECK(Dot::Math::transformPoint(vp, {5, 0, 0}).value().x == doctest::Approx(0.5));
  CHECK_FALSE(pixelProjection(vp, 0, 73, {}));
  CHECK_FALSE(pixelProjection(vp, 101, 73, {1, 0}));
  CHECK_FALSE(pixelProjection(vp, 101, 73, {0, -0.1F}));
  view.rotation = {0, 0, 0, 0};
  CHECK_FALSE(validateView(view));
  view = {};
  view.position.x = std::numeric_limits<float>::infinity();
  CHECK_FALSE(viewProjection(view, 1));
  CHECK_FALSE(viewProjection(View{}, 0));
}

TEST_CASE("view.transformed_bounds_include_mirrored_rotated_corners_and_reject_invalid_geometry") {
  using namespace Dot::Graphics;
  Bounds bounds{{-1, -2, -3}, {1, 2, 3}};
  auto transform = Dot::Math::compose({3, 4, 5}, {}, {-2, 1, 1}).value();
  const auto result = transformedBounds(bounds, transform).value();
  CHECK(result.minimum == Dot::Math::Vec3{1, 2, 2});
  CHECK(result.maximum == Dot::Math::Vec3{5, 6, 8});
  CHECK_FALSE(transformedBounds({{1, 0, 0}, {}}, {}));
  transform.data[3] = 1;
  CHECK_FALSE(transformedBounds(bounds, transform));
  CHECK_FALSE(transformedBounds(bounds, Dot::Math::compose({1e6F, 0, 0}).value()));
}

TEST_CASE("editor_camera.navigation_focus_and_projection_preserve_transactional_state") {
  Dot::Editor::Camera camera;
  auto state = camera.state();
  state.position = {0, 0, 5};
  state.yaw = state.pitch = 0;
  REQUIRE(camera.setState(state));
  REQUIRE(camera.move({1, 0, 1}, 1));
  CHECK(Dot::Math::length(camera.state().position - state.position) == doctest::Approx(5));
  auto before = camera.state();
  CHECK_FALSE(camera.move({2, 0, 0}, 1));
  CHECK_FALSE(camera.turn({std::numeric_limits<float>::quiet_NaN(), 0}, true));
  CHECK(camera.state() == before);
  REQUIRE(camera.turn({200, 100}, false));
  CHECK(camera.state().yaw == -160);
  CHECK(camera.state().pitch == 89);
  REQUIRE(camera.focus({{-1, -1, -1}, {1, 1, 1}}, 0.5F));
  CHECK(camera.state().pivot == Dot::Math::Vec3{});
  auto vp = Dot::Graphics::viewProjection(camera.view().value(), 0.5F).value();

  for (int index = 0; index < 8; ++index) {
    const auto clip = Dot::Math::transformPoint(vp, {index & 1 ? 1.F : -1.F, index & 2 ? 1.F : -1.F,
                                                     index & 4 ? 1.F : -1.F})
                          .value();
    CHECK(std::abs(clip.x) < 1);
    CHECK(std::abs(clip.y) < 1);
    CHECK(clip.z > 0);
    CHECK(clip.z < 1);
  }

  const auto distance = Dot::Math::length(camera.state().position - camera.state().pivot);
  REQUIRE(camera.turn({30, -10}, true));
  CHECK(Dot::Math::length(camera.state().position - camera.state().pivot)
        == doctest::Approx(distance));
  REQUIRE(camera.dolly(1));
  CHECK(Dot::Math::length(camera.state().position - camera.state().pivot) < distance);
  const auto previous = camera.state();
  REQUIRE(camera.pan({0.1F, 0.1F}, 1));
  CHECK(Dot::Math::length((camera.state().position - previous.position)
                          - (camera.state().pivot - previous.pivot))
        < 0.00001);
  state = camera.state();
  state.orthographic = true;
  REQUIRE(camera.setState(state));
  REQUIRE(camera.dolly(2));
  CHECK(camera.state().orthographicHeight < state.orthographicHeight);
  CHECK(camera.state().position == state.position);
  before = camera.state();
  state.pitch = 90;
  CHECK_FALSE(camera.setState(state));
  CHECK_FALSE(camera.focus({{}, {std::numeric_limits<float>::infinity(), 1, 1}}, 1));
  CHECK(camera.state() == before);
  Dot::Editor::Camera large;
  REQUIRE(large.focus({{-2000, -2000, -2000}, {2000, 2000, 2000}}, 0.5F));
  CHECK(large.state().farPlane > 10000);
  const auto largeView = Dot::Graphics::viewProjection(large.view().value(), 0.5F).value();

  for (int index = 0; index < 8; ++index) {
    const auto clip = Dot::Math::transformPoint(
                          largeView, {index & 1 ? 2000.F : -2000.F, index & 2 ? 2000.F : -2000.F,
                                      index & 4 ? 2000.F : -2000.F})
                          .value();
    CHECK(std::abs(clip.x) < 1);
    CHECK(std::abs(clip.y) < 1);
    CHECK(clip.z > 0);
    CHECK(clip.z < 1);
  }
}

#include <doctest/doctest.h>

#include <array>
#include <limits>

import Dot.Graphics.Frustum;
import Dot.Math;

TEST_CASE("frustum.identity_six_planes_contact_crossing_and_invalid_boundaries") {
  using namespace Dot;
  auto frustum = Graphics::Frustum::create({}).value();
  const Graphics::Bounds bounds{{-0.1F, -0.1F, 0.4F}, {0.1F, 0.1F, 0.6F}};
  CHECK(frustum.intersects(bounds).value());

  for (const auto delta : std::array<Math::Vec3, 6>{
           {{2, 0, 0}, {-2, 0, 0}, {0, 2, 0}, {0, -2, 0}, {0, 0, -2}, {0, 0, 2}}}) {
    CHECK_FALSE(frustum.intersects(bounds, Math::compose(delta).value()).value());
  }

  CHECK(frustum.intersects({{1, -1, 0}, {1, 1, 1}}).value());
  CHECK(frustum.intersects({{-2, -2, -1}, {2, 2, 2}}).value());
  CHECK_FALSE(frustum.intersects({{1.01F, 0, 0.5F}, {1.1F, 0.1F, 0.6F}}).value());
  CHECK_FALSE(frustum.intersects({{1, 0, 0}, {-1, 0, 0}}));
  const auto infinity = std::numeric_limits<float>::infinity();
  CHECK_FALSE(frustum.intersects({{}, {infinity, 0, 0}}));
  Math::Mat4 invalid;
  invalid.data[0] = infinity;
  CHECK_FALSE(frustum.intersects(bounds, invalid));
  CHECK_FALSE(Graphics::Frustum::create(invalid));
  invalid.data.fill(0);
  CHECK_FALSE(Graphics::Frustum::create(invalid));
}

TEST_CASE("frustum.perspective_orthographic_mirror_shear_behind_eye_and_float_extremes") {
  using namespace Dot;
  const auto camera = Math::lookAt({0, 0, 5}, {}).value();
  const auto box = Graphics::Bounds{{-0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}};

  for (const auto projection : {Math::perspective(60, 1, 0.1F, 100).value(),
                                Math::orthographic(-2, 2, -2, 2, 0.1F, 100).value()}) {
    auto frustum = Graphics::Frustum::create(Math::multiply(projection, camera).value()).value();
    auto mirror
        = Math::compose({}, Math::rotationEulerDegrees({30, 20, 60}).value(), {-2, 1, 3}).value();
    CHECK(frustum.intersects(box, mirror).value());
    mirror.data[4] += 0.8F;
    CHECK(frustum.intersects(box, mirror).value());
    CHECK_FALSE(frustum.intersects(box, Math::compose({0, 0, 6}).value()).value());
    CHECK_FALSE(frustum.intersects(box, Math::compose({1000, 0, 0}).value()).value());
    CHECK(frustum.intersects({{-2000, -2000, -2000}, {2000, 2000, 2000}}).value());
  }

  auto identity = Graphics::Frustum::create({}).value();
  const float maximum = std::numeric_limits<float>::max();
  auto extreme = Math::Mat4{};
  extreme.data[0] = maximum;
  extreme.data[12] = maximum;
  CHECK_FALSE(identity.intersects({{1, 0, 0.5F}, {2, 0, 0.5F}}, extreme).value());
}

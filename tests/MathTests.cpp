#include <doctest/doctest.h>

#include <cmath>
#include <limits>

import Dot.Math;

using namespace Dot::Math;

namespace {
  auto checkVector(Vec3 actual, Vec3 expected) -> void {
    CHECK(actual.x == doctest::Approx(expected.x).epsilon(0.00001));
    CHECK(actual.y == doctest::Approx(expected.y).epsilon(0.00001));
    CHECK(actual.z == doctest::Approx(expected.z).epsilon(0.00001));
  }
  constexpr auto infinity = std::numeric_limits<float>::infinity();
  constexpr auto maximum = std::numeric_limits<float>::max();
}  // namespace

TEST_CASE("Math vectors preserve arithmetic and double range") {
  checkVector(Vec3{1, 2, 3} + Vec3{4, 5, 6}, {5, 7, 9});
  checkVector(Vec3{1, 2, 3} - Vec3{4, 5, 6}, {-3, -3, -3});
  checkVector(Vec3{1, 2, 3} * 2, {2, 4, 6});
  CHECK(dot({1, 2, 3}, {4, 5, 6}) == 32);
  CHECK(length({3, 4, 0}) == 5);
  CHECK(std::isfinite(length({maximum, maximum, maximum})));
  CHECK(std::isfinite(dot({maximum, maximum, maximum}, {maximum, maximum, maximum})));
}

TEST_CASE("Math normalization handles extreme finite inputs") {
  auto normal = normalized(Vec3{3, 4, 0});
  REQUIRE(normal);
  checkVector(*normal, {0.6F, 0.8F, 0});
  for (auto value : {maximum, std::numeric_limits<float>::denorm_min()}) {
    auto vector = normalized(Vec3{value, 0, 0});
    REQUIRE(vector);
    checkVector(*vector, {1, 0, 0});
    auto quaternion = normalized(Quat{value, 0, 0, 0});
    REQUIRE(quaternion);
    CHECK(*quaternion == Quat{1, 0, 0, 0});
  }
}

TEST_CASE("Math normalization rejects degenerate inputs") {
  CHECK_FALSE(normalized(Vec3{}));
  CHECK_FALSE(normalized(Vec3{infinity, 0, 0}));
  CHECK_FALSE(normalized(Quat{0, 0, 0, 0}));
  CHECK_FALSE(normalized(Quat{0, 0, 0, infinity}));
  CHECK_FALSE(eulerDegrees({0, 0, 0, 0}));
  CHECK_FALSE(rotationEulerDegrees({0, infinity, 0}));
}

TEST_CASE("Math matrices compose translation rotation and scale") {
  auto rotation = rotationEulerDegrees({0, 0, 90});
  REQUIRE(rotation);
  auto matrix = compose({2, 3, 4}, *rotation, {2, 3, 4});
  REQUIRE(matrix);
  auto point = transformPoint(*matrix, {1, 0, 0});
  auto vector = transformVector(*matrix, {1, 0, 0});
  REQUIRE(point);
  REQUIRE(vector);
  checkVector(*point, {2, 5, 4});
  checkVector(*vector, {0, 2, 0});
  auto mirrored = compose({}, {}, {-1, 1, 1});
  REQUIRE(mirrored);
  auto reflection = transformPoint(*mirrored, {2, 3, 4});
  REQUIRE(reflection);
  checkVector(*reflection, {-2, 3, 4});
}

TEST_CASE("Math Euler conversion preserves rotation") {
  auto rotation = rotationEulerDegrees({20, 30, 40});
  REQUIRE(rotation);
  auto angles = eulerDegrees(*rotation);
  REQUIRE(angles);
  checkVector(*angles, {20, 30, 40});
  auto wrapped = rotationEulerDegrees({20 + 360, 30 - 720, 40});
  REQUIRE(wrapped);
  CHECK(wrapped->x == doctest::Approx(rotation->x));
  CHECK(wrapped->w == doctest::Approx(rotation->w));
  CHECK(rotationEulerDegrees({maximum, maximum, maximum}));
}

TEST_CASE("Math inverse round trips nonuniform transformed points") {
  auto rotation = rotationEulerDegrees({20, 40, 60});
  REQUIRE(rotation);
  auto matrix = compose({1, 2, 3}, *rotation, {2, 3, 4});
  REQUIRE(matrix);
  auto undo = inverse(*matrix);
  REQUIRE(undo);
  auto transformed = transformPoint(*matrix, {5, 6, 7});
  REQUIRE(transformed);
  auto original = transformPoint(*undo, *transformed);
  REQUIRE(original);
  checkVector(*original, {5, 6, 7});
  auto identity = multiply(*undo, *matrix);
  REQUIRE(identity);
  for (std::size_t index = 0; index < 16; ++index) {
    CHECK(identity->data[index] == doctest::Approx(Mat4{}.data[index]).epsilon(0.00001));
  }
}

TEST_CASE("Math multiplication order matches parent child transforms") {
  auto parent = compose({10, 0, 0}, {}, {2, 2, 2});
  auto child = compose({3, 0, 0});
  REQUIRE(parent);
  REQUIRE(child);
  auto combined = multiply(*parent, *child);
  REQUIRE(combined);
  auto point = transformPoint(*combined, {1, 0, 0});
  REQUIRE(point);
  checkVector(*point, {18, 0, 0});
}

TEST_CASE("Math transforms reject nonfinite singular and overflowing results") {
  CHECK_FALSE(compose({infinity, 0, 0}));
  CHECK_FALSE(compose({}, {}, {0, 1, 1}));
  CHECK_FALSE(compose({}, {0, 0, 0, 0}));
  Mat4 singular;
  singular.data.fill(0);
  CHECK_FALSE(inverse(singular));
  CHECK_FALSE(transformPoint(singular, {}));
  Mat4 invalid;
  invalid.data[0] = infinity;
  CHECK_FALSE(inverse(invalid));
  CHECK_FALSE(multiply(invalid, {}));
  CHECK_FALSE(transformPoint(invalid, {}));
  CHECK_FALSE(transformVector({}, {infinity, 0, 0}));
  auto huge = compose({}, {}, {maximum, 1, 1});
  REQUIRE(huge);
  CHECK_FALSE(multiply(*huge, *huge));
  CHECK_FALSE(transformPoint(*huge, {2, 0, 0}));
  CHECK_FALSE(transformVector(*huge, {2, 0, 0}));
  auto tiny = compose({}, {}, {std::numeric_limits<float>::denorm_min(), 1, 1});
  REQUIRE(tiny);
  CHECK_FALSE(inverse(*tiny));
}

TEST_CASE("Math lookAt maps camera position and forward in right handed space") {
  auto view = lookAt({1, 2, 5}, {1, 2, 0});
  REQUIRE(view);
  auto origin = transformPoint(*view, {1, 2, 5});
  auto ahead = transformPoint(*view, {1, 2, 0});
  REQUIRE(origin);
  REQUIRE(ahead);
  checkVector(*origin, {});
  checkVector(*ahead, {0, 0, -5});
  CHECK_FALSE(lookAt({}, {}));
  CHECK_FALSE(lookAt({}, {0, 1, 0}));
  CHECK_FALSE(lookAt({}, {0, 0, -1}, {}));
  CHECK_FALSE(lookAt({infinity, 0, 0}, {}));
}

TEST_CASE("Math perspective maps clipping planes to WebGPU depth") {
  auto projection = perspective(90, 2, 1, 10);
  REQUIRE(projection);
  auto nearPoint = transformPoint(*projection, {0, 0, -1});
  auto farPoint = transformPoint(*projection, {0, 0, -10});
  auto edge = transformPoint(*projection, {2, 1, -1});
  REQUIRE(nearPoint);
  REQUIRE(farPoint);
  REQUIRE(edge);
  checkVector(*nearPoint, {});
  checkVector(*farPoint, {0, 0, 1});
  checkVector(*edge, {1, 1, 0});
}

TEST_CASE("Math perspective validates field of view and clipping boundaries") {
  for (float fov : {0.0F, -1.0F, 180.0F, infinity}) {
    CHECK_FALSE(perspective(fov, 1, 1, 10));
  }
  CHECK_FALSE(perspective(60, 0, 1, 10));
  CHECK_FALSE(perspective(60, infinity, 1, 10));
  CHECK_FALSE(perspective(60, 1, 0, 10));
  CHECK_FALSE(perspective(60, 1, 10, 10));
  CHECK_FALSE(perspective(60, 1, 10, 1));
  CHECK_FALSE(perspective(60, 1, 1, infinity));
}

TEST_CASE("Math orthographic maps asymmetric bounds and clipping planes") {
  auto projection = orthographic(-2, 6, -3, 1, 0, 10);
  REQUIRE(projection);
  auto nearPoint = transformPoint(*projection, {-2, -3, 0});
  auto farPoint = transformPoint(*projection, {6, 1, -10});
  REQUIRE(nearPoint);
  REQUIRE(farPoint);
  checkVector(*nearPoint, {-1, -1, 0});
  checkVector(*farPoint, {1, 1, 1});
  CHECK_FALSE(orthographic(0, 0, -1, 1, 0, 10));
  CHECK_FALSE(orthographic(-1, 1, 1, -1, 0, 10));
  CHECK_FALSE(orthographic(-1, 1, -1, 1, -1, 10));
  CHECK_FALSE(orthographic(-1, 1, -1, 1, 10, 10));
  CHECK_FALSE(orthographic(-1, infinity, -1, 1, 0, 10));
}

TEST_CASE("Math finite predicates cover every matrix and quaternion component") {
  CHECK(isFinite(Vec3{}));
  CHECK(isFinite(Vec4{}));
  CHECK(isFinite(Quat{}));
  CHECK(isFinite(Mat4{}));
  CHECK_FALSE(isFinite(Vec4{0, 0, 0, infinity}));
  CHECK_FALSE(isFinite(Quat{0, infinity, 0, 1}));
  for (std::size_t index = 0; index < 16; ++index) {
    Mat4 invalid;
    invalid.data[index] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(isFinite(invalid));
  }
}

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <random>

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

TEST_CASE("Math TRS decomposition preserves translation rotation and scale") {
  auto identity = decomposeTrs({});
  REQUIRE(identity);
  CHECK(identity->position == Vec3{});
  CHECK(identity->rotation == Quat{});
  CHECK(identity->scale == Vec3{1, 1, 1});
  auto rotation = rotationEulerDegrees({20, 40, 60});
  REQUIRE(rotation);
  auto matrix = compose({7, 8, 9}, *rotation, {2, 3, 4});
  REQUIRE(matrix);
  auto decomposed = decomposeTrs(*matrix);
  REQUIRE(decomposed);
  checkVector(decomposed->position, {7, 8, 9});
  checkVector(decomposed->scale, {2, 3, 4});
  CHECK(decomposed->rotation.w >= 0);
  auto rebuilt = compose(decomposed->position, decomposed->rotation, decomposed->scale);
  REQUIRE(rebuilt);

  for (std::size_t i = 0; i < 16; ++i) {
    CHECK(rebuilt->data[i] == doctest::Approx(matrix->data[i]).epsilon(0.00001).scale(1));
  }
}

TEST_CASE("Math TRS decomposition canonicalizes all reflection signs") {
  auto rotation = rotationEulerDegrees({-137, 74, 165});
  REQUIRE(rotation);

  for (const float x : {-2.0F, 2.0F}) {
    for (const float y : {-3.0F, 3.0F}) {
      for (const float z : {-4.0F, 4.0F}) {
        auto matrix = compose({-1, 5, 2}, *rotation, {x, y, z});
        REQUIRE(matrix);
        auto parts = decomposeTrs(*matrix);
        REQUIRE(parts);
        CHECK(parts->scale.x == doctest::Approx(x * y * z < 0 ? -2.0F : 2.0F));
        CHECK(parts->scale.y == doctest::Approx(3));
        CHECK(parts->scale.z == doctest::Approx(4));
        CHECK(parts->rotation.w >= 0);
        auto rebuilt = compose(parts->position, parts->rotation, parts->scale);
        REQUIRE(rebuilt);
        auto before = transformPoint(*matrix, {1, 2, 3});
        auto after = transformPoint(*rebuilt, {1, 2, 3});
        REQUIRE(before);
        REQUIRE(after);
        checkVector(*after, *before);
      }
    }
  }
}

TEST_CASE("Math TRS decomposition handles half turn rotations") {
  for (const auto euler : {Vec3{180, 0, 0}, Vec3{0, 180, 0}, Vec3{0, 0, 180}, Vec3{180, 180, 180},
                           Vec3{0, -179.999F, 0}}) {
    auto rotation = rotationEulerDegrees(euler);
    REQUIRE(rotation);
    auto matrix = compose({}, *rotation);
    REQUIRE(matrix);
    auto parts = decomposeTrs(*matrix);
    REQUIRE(parts);
    CHECK(parts->rotation.w >= 0);
    auto rebuilt = compose(parts->position, parts->rotation, parts->scale);
    REQUIRE(rebuilt);

    for (std::size_t i = 0; i < 16; ++i) {
      CHECK(rebuilt->data[i] == doctest::Approx(matrix->data[i]).epsilon(0.00001).scale(1));
    }
  }
}

TEST_CASE("Math TRS decomposition uniform parent hierarchy") {
  auto parentRotation = rotationEulerDegrees({17, 31, 53});
  auto childRotation = rotationEulerDegrees({29, 73, 11});
  REQUIRE(parentRotation);
  REQUIRE(childRotation);
  auto parent = compose({1, 2, 3}, *parentRotation, {2, 2, 2});
  auto child = compose({4, 5, 6}, *childRotation, {2, 3, 4});
  REQUIRE(parent);
  REQUIRE(child);
  auto world = multiply(*parent, *child);
  REQUIRE(world);
  auto parts = decomposeTrs(*world);
  REQUIRE(parts);
  checkVector(parts->scale, {4, 6, 8});
  auto rebuilt = compose(parts->position, parts->rotation, parts->scale);
  REQUIRE(rebuilt);
  auto inverseParent = inverse(*parent);
  REQUIRE(inverseParent);
  auto local = multiply(*inverseParent, *rebuilt);
  REQUIRE(local);
  auto localParts = decomposeTrs(*local);
  REQUIRE(localParts);
  checkVector(localParts->position, {4, 5, 6});
  checkVector(localParts->scale, {2, 3, 4});
}

TEST_CASE("Math TRS decomposition rejects shear perspective and singularity") {
  Mat4 shear;
  shear.data[4] = 0.1F;
  CHECK_FALSE(decomposeTrs(shear));
  auto parent = compose({}, {}, {2, 1, 1});
  auto rotation = rotationEulerDegrees({0, 0, 45});
  REQUIRE(parent);
  REQUIRE(rotation);
  auto child = compose({}, *rotation);
  REQUIRE(child);
  auto world = multiply(*parent, *child);
  REQUIRE(world);
  CHECK_FALSE(decomposeTrs(*world));
  auto projection = perspective(60, 1, 0.1F, 100);
  REQUIRE(projection);
  CHECK_FALSE(decomposeTrs(*projection));
  Mat4 zero;
  zero.data.fill(0);
  zero.data[15] = 1;
  CHECK_FALSE(decomposeTrs(zero));

  for (const auto column : {0, 1, 2}) {
    Mat4 singular;
    singular.data[static_cast<std::size_t>(column * 4 + column)] = 0;
    CHECK_FALSE(decomposeTrs(singular));
  }

  Mat4 dependent;
  dependent.data[4] = 1;
  dependent.data[5] = 0;
  CHECK_FALSE(decomposeTrs(dependent));
}

TEST_CASE("Math TRS decomposition rejects nonfinite and nonaffine elements") {
  for (std::size_t i = 0; i < 16; ++i) {
    for (const auto value : {infinity, std::numeric_limits<float>::quiet_NaN()}) {
      Mat4 invalid;
      invalid.data[i] = value;
      CHECK_FALSE(decomposeTrs(invalid));
    }
  }

  for (const std::size_t i : {3U, 7U, 11U}) {
    Mat4 perspective;
    perspective.data[i] = 1e-30F;
    CHECK_FALSE(decomposeTrs(perspective));
  }

  Mat4 homogeneous;
  homogeneous.data[15] = 0.99999F;
  CHECK_FALSE(decomposeTrs(homogeneous));
}

TEST_CASE("Math TRS decomposition extreme scales and unrepresentable lengths") {
  for (const auto value :
       {maximum, std::numeric_limits<float>::min(), std::numeric_limits<float>::denorm_min()}) {
    auto matrix = compose({1, 2, 3}, {}, {value, value, value});
    REQUIRE(matrix);
    auto parts = decomposeTrs(*matrix);
    REQUIRE_MESSAGE(parts, (parts ? "" : parts.error().message));
    CHECK(parts->scale == Vec3{value, value, value});
    auto rebuilt = compose(parts->position, parts->rotation, parts->scale);
    REQUIRE(rebuilt);
    CHECK(*rebuilt == *matrix);
  }

  Mat4 tooLarge;
  tooLarge.data[0] = maximum;
  tooLarge.data[1] = maximum;
  tooLarge.data[4] = maximum;
  tooLarge.data[5] = -maximum;
  CHECK_FALSE(decomposeTrs(tooLarge));
}

TEST_CASE("Math TRS decomposition orthogonality tolerance is bounded") {
  Mat4 close;
  close.data[4] = 0.000009F;
  auto parts = decomposeTrs(close);
  REQUIRE(parts);
  auto rebuilt = compose(parts->position, parts->rotation, parts->scale);
  REQUIRE(rebuilt);
  CHECK(std::abs(rebuilt->data[4] - close.data[4]) < 0.00001F);
  close.data[4] = 0.00002F;
  CHECK_FALSE(decomposeTrs(close));
}

TEST_CASE("Math TRS decomposition seeded transformed point round trips") {
  std::mt19937 random{8421};
  std::uniform_real_distribution<float> angle{-180, 180};
  std::uniform_real_distribution<float> translation{-100, 100};
  std::uniform_real_distribution<float> scale{0.01F, 1000};

  for (int n = 0; n < 128; ++n) {
    auto rotation = rotationEulerDegrees({angle(random), angle(random), angle(random)});
    REQUIRE(rotation);
    auto matrix = compose({translation(random), translation(random), translation(random)},
                          *rotation, {scale(random), scale(random), scale(random)});
    REQUIRE(matrix);
    auto parts = decomposeTrs(*matrix);
    REQUIRE(parts);
    auto rebuilt = compose(parts->position, parts->rotation, parts->scale);
    REQUIRE(rebuilt);
    auto before = transformPoint(*matrix, {0.5F, -0.25F, 1});
    auto after = transformPoint(*rebuilt, {0.5F, -0.25F, 1});
    REQUIRE(before);
    REQUIRE(after);
    CHECK(after->x == doctest::Approx(before->x).epsilon(0.0001).scale(1));
    CHECK(after->y == doctest::Approx(before->y).epsilon(0.0001).scale(1));
    CHECK(after->z == doctest::Approx(before->z).epsilon(0.0001).scale(1));
  }
}

TEST_CASE("Math spherical blends normalize endpoints and follow the shortest rotation arc") {
  const auto first = rotationEulerDegrees({0, 0, 170}).value();
  const auto last = rotationEulerDegrees({0, 0, -170}).value();
  const auto middle = slerp(first, last, .5).value();
  const auto transform = compose({}, middle).value();
  CHECK(transform.data[0] == doctest::Approx(-1));
  CHECK(transform.data[5] == doctest::Approx(-1));
  CHECK(slerp(first, last, 0).value() == normalized(first).value());
  CHECK(slerp(first, last, 1).value() == normalized(last).value());
  const Quat opposite{-first.x, -first.y, -first.z, -first.w};
  CHECK(compose({}, slerp(first, opposite, .5).value()).value() == compose({}, first).value());
  CHECK(slerp(Quat{0, 0, 0, maximum}, Quat{0, 0, 0, -maximum}, .5).value() == Quat{});
  CHECK(slerp(Quat{}, Quat{0, 0, .000001F, 1}, .5)->z == doctest::Approx(.0000005));

  for (const auto weight : {-1.0, 1.1, std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()}) {
    CHECK_FALSE(slerp(first, last, weight));
  }

  CHECK_FALSE(slerp(Quat{0, 0, 0, 0}, last, 0));
  CHECK_FALSE(slerp(first, Quat{infinity, 0, 0, 1}, 1));
}

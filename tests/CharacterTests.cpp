#include <doctest/doctest.h>

#include <cmath>
#include <limits>

import Dot.Physics.Character;
import Dot.Core.Result;
import Dot.Math;

using namespace Dot::Physics;

TEST_CASE("character.authoring_bounds_and_normalized_yaw_are_device_free") {
  REQUIRE(validateCharacterProperties({}));
  CharacterConfig config;
  config.position = {-1'000'000, 1'000'000, 0};
  config.yawDegrees = 810;
  const auto result = validatedCharacterConfig(config);
  REQUIRE(result);
  CHECK(result->position == config.position);
  CHECK(result->yawDegrees == 90);
  CHECK(config.yawDegrees == 810);

  CharacterProperties low;
  low.radius = low.halfHeight = .05F;
  low.maxSlopeDegrees = low.mass = 1;
  low.stepHeight = low.stickDistance = low.maxStrength = low.jumpSpeed = 0;
  low.maxContacts = 1;
  REQUIRE(validateCharacterProperties(low));
  CharacterProperties high;
  high.radius = high.halfHeight = 5;
  high.maxSlopeDegrees = 85;
  high.stepHeight = high.stickDistance = 1;
  high.mass = 1000;
  high.maxStrength = 10000;
  high.jumpSpeed = 30;
  high.maxContacts = 4096;
  REQUIRE(validateCharacterProperties(high));
  low.radius = std::nextafter(.05F, 0.F);
  CHECK_FALSE(validateCharacterProperties(low));
  high.maxSlopeDegrees = std::nextafter(85.F, 90.F);
  CHECK_FALSE(validateCharacterProperties(high));
  config.position.z = 1'000'001;
  CHECK_FALSE(validatedCharacterConfig(config));
  config.position.z = 0;
  config.yawDegrees = 1'000'001;
  CHECK_FALSE(validatedCharacterConfig(config));
}

TEST_CASE("character.nonfinite_properties_and_pose_fail_at_the_boundary") {
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  const auto infinity = std::numeric_limits<float>::infinity();

  for (const auto member : {&CharacterProperties::radius, &CharacterProperties::halfHeight,
                            &CharacterProperties::maxSlopeDegrees, &CharacterProperties::stepHeight,
                            &CharacterProperties::stickDistance, &CharacterProperties::mass,
                            &CharacterProperties::maxStrength, &CharacterProperties::jumpSpeed}) {
    for (const auto invalid : {nan, infinity, -infinity, -1.F}) {
      CharacterProperties properties;
      properties.*member = invalid;
      const auto result = validateCharacterProperties(properties);
      REQUIRE_FALSE(result);
      CHECK(result.error().code == Dot::Core::ErrorCode::InvalidArgument);
    }
  }

  for (const auto contacts : {0U, 4097U, std::numeric_limits<unsigned>::max()}) {
    CharacterProperties properties;
    properties.maxContacts = contacts;
    CHECK_FALSE(validateCharacterProperties(properties));
  }

  for (const auto invalid : {nan, infinity, -infinity}) {
    CharacterConfig config;
    config.yawDegrees = invalid;
    CHECK_FALSE(validatedCharacterConfig(config));
    config.yawDegrees = 0;

    for (const auto position : {Dot::Math::Vec3{invalid, 0, 0}, Dot::Math::Vec3{0, invalid, 0},
                                Dot::Math::Vec3{0, 0, invalid}}) {
      config.position = position;
      CHECK_FALSE(validatedCharacterConfig(config));
    }
  }
}

TEST_CASE("character.input_has_horizontal_metric_speed_and_explicit_jump_intent") {
  REQUIRE(validateCharacterInput({}));
  REQUIRE(validateCharacterInput({{30, 0, 40}, true}));
  CHECK_FALSE(validateCharacterInput({{50, 0, 1}, false}));
  CHECK_FALSE(validateCharacterInput({{0, std::numeric_limits<float>::denorm_min(), 0}, true}));
  const auto infinity = std::numeric_limits<float>::infinity();
  const auto nan = std::numeric_limits<float>::quiet_NaN();

  for (const auto velocity :
       {Dot::Math::Vec3{nan, 0, 0}, Dot::Math::Vec3{0, 0, infinity}, Dot::Math::Vec3{0, 1, 0}}) {
    CHECK_FALSE(validateCharacterInput({velocity, true}));
  }
}

#include <doctest/doctest.h>

#include <limits>
#include <type_traits>
#include <vector>

import Dot.Physics.Body;
import Dot.Core.Result;
import Dot.Math;

using namespace Dot::Physics;

TEST_CASE("body.properties_default_and_all_motion_shape_variants") {
  static_assert(std::is_trivially_copyable_v<BodyProperties>);

  for (const auto motion : {Motion::Static, Motion::Kinematic, Motion::Dynamic}) {
    for (const auto shape : {Shape::Box, Shape::Sphere, Shape::Capsule}) {
      BodyProperties properties;
      properties.motion = motion;
      properties.shape = shape;
      REQUIRE(validateBodyProperties(properties));
      BodyConfig config;
      static_cast<BodyProperties&>(config) = properties;
      auto validated = validatedBodyConfig(config);
      REQUIRE(validated);
      CHECK(static_cast<const BodyProperties&>(*validated) == properties);
    }
  }
}

TEST_CASE("body.config_normalizes_only_returned_rotation") {
  BodyConfig config;
  config.position = {1, 2, 3};
  config.rotation = {0, 0, 0, 4};
  config.velocity = {300, 400, 0};
  auto validated = validatedBodyConfig(config);
  REQUIRE(validated);
  CHECK(validated->position == config.position);
  CHECK(validated->rotation == Dot::Math::Quat{});
  CHECK(config.rotation.w == 4);
  CHECK(static_cast<const BodyProperties&>(*validated)
        == static_cast<const BodyProperties&>(config));
  config.position.x = 1'000'001;
  CHECK_FALSE(validatedBodyConfig(config));
  config.position.x = -1'000'000;
  REQUIRE(validatedBodyConfig(config));
  config.rotation = {0, 0, 0, 0};
  CHECK_FALSE(validatedBodyConfig(config));
  config.rotation.w = std::numeric_limits<float>::infinity();
  CHECK_FALSE(validatedBodyConfig(config));
}

TEST_CASE("body.property_numeric_boundaries_and_nonfinite_fields") {
  for (const auto member :
       {&BodyProperties::radius, &BodyProperties::halfHeight, &BodyProperties::mass,
        &BodyProperties::friction, &BodyProperties::restitution, &BodyProperties::gravityFactor}) {
    for (const auto invalid :
         {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity()}) {
      BodyProperties properties;
      properties.*member = invalid;
      auto result = validateBodyProperties(properties);
      REQUIRE_FALSE(result);
      CHECK(result.error().code == Dot::Core::ErrorCode::InvalidArgument);
    }
  }

  for (const auto value : {0.001F, 10000.0F}) {
    BodyProperties properties;
    properties.radius = value;
    properties.halfHeight = value;
    properties.halfExtents = {value, value, value};
    REQUIRE(validateBodyProperties(properties));
  }

  for (const auto value : {0.00099F, 10001.0F}) {
    BodyProperties properties;
    properties.radius = value;
    CHECK_FALSE(validateBodyProperties(properties));
    properties.radius = 0.5F;
    properties.halfHeight = value;
    CHECK_FALSE(validateBodyProperties(properties));
    properties.halfHeight = 0.5F;
    properties.halfExtents.z = value;
    CHECK_FALSE(validateBodyProperties(properties));
  }

  BodyProperties minimum;
  minimum.mass = 0.01F;
  minimum.friction = 0;
  minimum.restitution = 0;
  minimum.gravityFactor = 0;
  REQUIRE(validateBodyProperties(minimum));
  BodyProperties maximum;
  maximum.mass = 1'000'000;
  maximum.friction = 1;
  maximum.restitution = 1;
  maximum.gravityFactor = 10;
  maximum.velocity = {300, 400, 0};
  REQUIRE(validateBodyProperties(maximum));
  maximum.velocity.z = 0.1F;
  CHECK_FALSE(validateBodyProperties(maximum));
}

TEST_CASE("body.unknown_enums_static_velocity_and_inactive_dimensions") {
  BodyProperties properties;
  properties.motion = static_cast<Motion>(99);
  CHECK_FALSE(validateBodyProperties(properties));
  properties.motion = Motion::Static;
  properties.shape = static_cast<Shape>(99);
  CHECK_FALSE(validateBodyProperties(properties));
  properties.shape = Shape::Sphere;
  properties.velocity = {std::numeric_limits<float>::denorm_min(), 0, 0};
  CHECK_FALSE(validateBodyProperties(properties));
  properties.velocity = {};
  properties.halfExtents.x = 0;
  CHECK_FALSE(validateBodyProperties(properties));
  properties.halfExtents.x = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(validateBodyProperties(properties));
}

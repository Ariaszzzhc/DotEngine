module;

#include <cmath>
#include <expected>

module Dot.Physics.Character;

import Dot.Core.Result;
import Dot.Math;

namespace Dot::Physics {
  namespace {
    auto inRange(float value, float low, float high) -> bool {
      return std::isfinite(value) && value >= low && value <= high;
    }
  }  // namespace

  auto validateCharacterProperties(const CharacterProperties& properties) -> Core::Result<void> {
    if (!inRange(properties.radius, 0.05F, 5) || !inRange(properties.halfHeight, 0.05F, 5)
        || !inRange(properties.maxSlopeDegrees, 1, 85) || !inRange(properties.stepHeight, 0, 1)
        || !inRange(properties.stickDistance, 0, 1) || !inRange(properties.mass, 1, 1000)
        || !inRange(properties.maxStrength, 0, 10000) || !inRange(properties.jumpSpeed, 0, 30)
        || properties.maxContacts == 0 || properties.maxContacts > 4096) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Character properties exceed finite capsule/movement/contact limits");
    }

    return {};
  }

  auto validatedCharacterConfig(const CharacterConfig& config) -> Core::Result<CharacterConfig> {
    auto valid = validateCharacterProperties(config);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    if (!inRange(config.position.x, -1'000'000, 1'000'000)
        || !inRange(config.position.y, -1'000'000, 1'000'000)
        || !inRange(config.position.z, -1'000'000, 1'000'000)
        || !inRange(config.yawDegrees, -1'000'000, 1'000'000)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid character feet position/yaw");
    }

    auto result = config;
    result.yawDegrees = std::remainder(config.yawDegrees, 360.F);
    return result;
  }

  auto validateCharacterInput(const CharacterInput& input) -> Core::Result<void> {
    if (!Math::isFinite(input.velocity) || input.velocity.y != 0
        || Math::length(input.velocity) > 50) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Character input requires finite horizontal velocity at most 50 m/s");
    }

    return {};
  }
}  // namespace Dot::Physics

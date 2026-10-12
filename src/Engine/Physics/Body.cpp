module;

#include <cmath>
#include <expected>
#include <numbers>

module Dot.Physics.Body;

import Dot.Core.Result;
import Dot.Math;

namespace Dot::Physics {
  namespace {
    auto inRange(float value, float low, float high) -> bool {
      return std::isfinite(value) && value >= low && value <= high;
    }
  }  // namespace

  auto validateBodyProperties(const BodyProperties& properties) -> Core::Result<void> {
    if (properties.motion != Motion::Static && properties.motion != Motion::Kinematic
        && properties.motion != Motion::Dynamic) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown physics motion type");
    }

    if (properties.shape != Shape::Box && properties.shape != Shape::Sphere
        && properties.shape != Shape::Capsule) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown physics shape type");
    }

    if (!inRange(properties.halfExtents.x, 0.001F, 10000)
        || !inRange(properties.halfExtents.y, 0.001F, 10000)
        || !inRange(properties.halfExtents.z, 0.001F, 10000)
        || !inRange(properties.radius, 0.001F, 10000)
        || !inRange(properties.halfHeight, 0.001F, 10000)
        || !inRange(properties.mass, 0.01F, 1'000'000) || !inRange(properties.friction, 0, 1)
        || !inRange(properties.restitution, 0, 1) || !inRange(properties.gravityFactor, 0, 10)
        || !Math::isFinite(properties.velocity) || Math::length(properties.velocity) > 500) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Physics body properties exceed finite physical input limits");
    }

    if (properties.motion == Motion::Static && Math::length(properties.velocity) != 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Static physics bodies require zero velocity");
    }

    return {};
  }

  auto validatedBodyConfig(const BodyConfig& config) -> Core::Result<BodyConfig> {
    auto properties = validateBodyProperties(config);

    if (!properties) {
      return std::unexpected(properties.error());
    }

    if (!inRange(config.position.x, -1'000'000, 1'000'000)
        || !inRange(config.position.y, -1'000'000, 1'000'000)
        || !inRange(config.position.z, -1'000'000, 1'000'000)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid physics position");
    }

    if (!Math::isFinite(config.angularVelocity)
        || Math::length(config.angularVelocity) > 15 * std::numbers::pi_v<float>
        || (config.motion == Motion::Static && Math::length(config.angularVelocity) != 0)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid initial physics angular velocity");
    }

    auto rotation = Math::normalized(config.rotation);

    if (!rotation) {
      return std::unexpected(rotation.error());
    }

    auto normalized = config;
    normalized.rotation = *rotation;
    return normalized;
  }
}  // namespace Dot::Physics

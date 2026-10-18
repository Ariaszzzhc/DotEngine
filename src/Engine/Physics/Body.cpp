module;

#include <algorithm>
#include <cmath>
#include <expected>
#include <numbers>
#include <utility>

module Dot.Physics.Body;

import Dot.Core.Result;
import Dot.Math;
import Dot.Core.Paths;

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
        && properties.shape != Shape::Capsule && properties.shape != Shape::Mesh) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown physics shape type");
    }

    if ((properties.shape == Shape::Mesh
         && (properties.motion != Motion::Static || properties.sensor))
        || (properties.meshAsset
            && (properties.shape != Shape::Mesh || properties.meshAsset->size() > 1024
                || !Core::isPortableRelativePath(*properties.meshAsset)
                || !properties.meshAsset->starts_with("assets/")
                || !properties.meshAsset->ends_with(".dotmesh")))) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid static mesh collider properties/resource");
    }

    if (properties.collisionDetection != CollisionDetection::Discrete
        && properties.collisionDetection != CollisionDetection::Continuous) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown collision detection mode");
    }

    if (properties.collisionDetection == CollisionDetection::Continuous
        && (properties.motion != Motion::Dynamic || properties.sensor)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Continuous collision detection requires a solid dynamic body");
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

    if (config.shape == Shape::Mesh) {
      auto mesh = config.mesh.validateScale(config.meshScale);

      if (!mesh) {
        return std::unexpected(mesh.error());
      }
    } else if (config.mesh.triangles() || config.meshScale != Math::Vec3{1, 1, 1}) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Primitive body cannot carry mesh geometry/scale");
    }

    auto normalized = config;
    normalized.rotation = *rotation;
    return normalized;
  }

  auto scaledBodyConfig(const BodyProperties& description, const Math::TransformParts& pose,
                        TriangleMesh mesh) -> Core::Result<BodyConfig> {
    auto properties = validateBodyProperties(description);

    if (!properties) {
      return std::unexpected(properties.error());
    }

    if (!Math::isFinite(pose.scale) || pose.scale.x == 0 || pose.scale.y == 0
        || pose.scale.z == 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid physics world scale");
    }

    BodyConfig result;
    static_cast<BodyProperties&>(result) = description;
    result.position = pose.position;
    result.rotation = pose.rotation;
    const Math::Vec3 scale{std::abs(pose.scale.x), std::abs(pose.scale.y), std::abs(pose.scale.z)};

    if (description.shape == Shape::Mesh) {
      result.mesh = std::move(mesh);
      result.meshScale = pose.scale;
    } else if (description.shape == Shape::Box) {
      result.halfExtents
          = {description.halfExtents.x * scale.x, description.halfExtents.y * scale.y,
             description.halfExtents.z * scale.z};
    } else {
      const float maximum = std::max({scale.x, scale.y, scale.z});

      if (std::abs(scale.x - scale.y) > 1e-5F * maximum
          || std::abs(scale.x - scale.z) > 1e-5F * maximum) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Sphere/capsule physics requires uniform world scale");
      }

      result.radius *= scale.x;

      if (description.shape == Shape::Capsule) {
        result.halfHeight *= scale.x;
      }
    }

    return validatedBodyConfig(result);
  }
}  // namespace Dot::Physics

module;

#include <cmath>
#include <exception>
#include <expected>
#include <string>

module Dot.Graphics.Lighting;

import Dot.Core.Result;
import Dot.Math;

namespace Dot::Graphics {
  auto validatedLighting(const Lighting& lighting) -> Core::Result<Lighting> {
    const auto positionValid = [](Math::Vec3 value) {
      return Math::isFinite(value) && std::abs(value.x) <= 1e6 && std::abs(value.y) <= 1e6
             && std::abs(value.z) <= 1e6;
    };

    if (!positionValid(lighting.viewPosition) || lighting.lights.size() > 64) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid lighting camera/count");
    }

    try {
      auto result = lighting;

      if (result.viewDirection) {
        auto direction = Math::normalized(*result.viewDirection);

        if (!direction) {
          return Core::fail(direction.error().code,
                            "Orthographic view direction: " + direction.error().message);
        }

        result.viewDirection = *direction;
      }

      for (auto& light : result.lights) {
        if ((light.kind != LightKind::Directional && light.kind != LightKind::Point
             && light.kind != LightKind::Spot)
            || !positionValid(light.position) || !Math::isFinite(light.direction)
            || !Math::isFinite(light.color) || light.color.x < 0 || light.color.x > 1
            || light.color.y < 0 || light.color.y > 1 || light.color.z < 0 || light.color.z > 1
            || !std::isfinite(light.intensity) || light.intensity < 0 || light.intensity > 1e6
            || !std::isfinite(light.range) || light.range < 0.01F || light.range > 1e6
            || !std::isfinite(light.innerConeDegrees) || !std::isfinite(light.outerConeDegrees)
            || light.innerConeDegrees < 0 || light.outerConeDegrees > 90
            || light.outerConeDegrees - light.innerConeDegrees < 0.1F) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid direct light properties");
        }

        if (light.kind != LightKind::Point) {
          auto direction = Math::normalized(light.direction);

          if (!direction) {
            return Core::fail(direction.error().code,
                              "Light direction: " + direction.error().message);
          }

          light.direction = *direction;
        }
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare lighting: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Graphics

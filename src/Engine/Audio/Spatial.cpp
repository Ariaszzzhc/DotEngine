module;

#include <cmath>
#include <expected>

module Dot.Audio.Spatial;

import Dot.Math;
import Dot.Core.Result;

namespace Dot::Audio {
  namespace {
    auto positionValid(Math::Vec3 value) -> bool {
      return Math::isFinite(value) && std::abs(value.x) <= 1000000 && std::abs(value.y) <= 1000000
             && std::abs(value.z) <= 1000000;
    }

    auto velocityValid(Math::Vec3 value) -> bool {
      return Math::isFinite(value) && Math::length(value) <= 100;
    }

    auto range(float value, float low, float high) -> bool {
      return std::isfinite(value) && value >= low && value <= high;
    }
  }  // namespace

  auto validatedListener(const Listener& listener) -> Core::Result<Listener> {
    if (!positionValid(listener.position) || !velocityValid(listener.velocity)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid audio listener position/velocity");
    }

    auto rotation = Math::normalized(listener.rotation);

    if (!rotation) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid audio listener rotation");
    }

    auto result = listener;
    result.rotation = *rotation;
    return result;
  }

  auto validatedSpatial(const SpatialSettings& settings) -> Core::Result<SpatialSettings> {
    if (!positionValid(settings.position) || !velocityValid(settings.velocity)
        || (settings.attenuation != Attenuation::Inverse
            && settings.attenuation != Attenuation::Linear
            && settings.attenuation != Attenuation::Exponential)
        || !range(settings.minDistance, 0.001F, 1000000)
        || !range(settings.maxDistance, 0.001F, 1000000)
        || settings.maxDistance <= settings.minDistance || !range(settings.rolloff, 0, 10)
        || !range(settings.dopplerFactor, 0, 1) || !range(settings.innerConeDegrees, 0, 360)
        || !range(settings.outerConeDegrees, 0, 360)
        || settings.innerConeDegrees > settings.outerConeDegrees
        || !range(settings.outerConeGain, 0, 1)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid spatial audio pose/attenuation/cone settings");
    }

    auto rotation = Math::normalized(settings.rotation);

    if (!rotation) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid spatial audio rotation");
    }

    auto result = settings;
    result.rotation = *rotation;
    return result;
  }
}  // namespace Dot::Audio

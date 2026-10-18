module;

#include <DetourObstacleAvoidance.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <map>
#include <memory>
#include <span>
#include <string>

module Dot.Navigation.Steering;

namespace Dot::Navigation {
  namespace {
    auto point(Math::Vec3 value) -> bool {
      return Math::isFinite(value) && std::abs(value.x) <= 1e6F && std::abs(value.y) <= 1e6F
             && std::abs(value.z) <= 1e6F;
    }

    auto valid(const SteeringBody& body) -> bool {
      return body.id && point(body.position) && Math::isFinite(body.velocity)
             && Math::isFinite(body.desired) && body.velocity.y == 0 && body.desired.y == 0
             && Math::length(body.velocity) <= 1000 && Math::length(body.desired) <= 50
             && std::isfinite(body.radius) && body.radius >= .05F && body.radius <= 5
             && std::isfinite(body.height) && body.height >= .1F && body.height <= 20;
    }

    auto xyz(Math::Vec3 value) -> std::array<float, 3> {
      return {value.x, value.y, value.z};
    }
  }  // namespace

  auto steer(const SteeringBody& body, std::span<const SteeringBody> neighbors,
             std::span<const WallSegment> walls, const SteeringSettings& settings)
      -> Core::Result<SteeringResult> {
    if (!valid(body) || !std::isfinite(settings.maxSpeed) || settings.maxSpeed < .1F
        || settings.maxSpeed > 10 || Math::length(body.desired) > settings.maxSpeed + 1e-5
        || !std::isfinite(settings.neighborDistance) || settings.neighborDistance < .5F
        || settings.neighborDistance > 8 || !std::isfinite(settings.timeHorizon)
        || settings.timeHorizon < .25F || settings.timeHorizon > 5
        || !std::isfinite(settings.margin) || settings.margin < 0 || settings.margin > .2F
        || neighbors.size() > 256 || walls.size() > 512) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid navigation steering input/budget");
    }

    try {
      std::map<std::uint64_t, const SteeringBody*> ordered;
      std::size_t nearby{};

      for (const auto& other : neighbors) {
        if (!valid(other) || !ordered.emplace(other.id, &other).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid/duplicate steering neighbor");
        }
      }

      for (const auto& wall : walls) {
        if (!point(wall.from) || !point(wall.to)
            || (wall.from.x == wall.to.x && wall.from.z == wall.to.z)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid steering wall");
        }
      }

      std::unique_ptr<dtObstacleAvoidanceQuery, decltype(&dtFreeObstacleAvoidanceQuery)> query(
          dtAllocObstacleAvoidanceQuery(), dtFreeObstacleAvoidanceQuery);

      if (!query || !query->init(64, 512)) {
        return Core::fail(Core::ErrorCode::External, "Allocate navigation steering query failed");
      }

      for (const auto& [id, observed] : ordered) {
        const auto& other = *observed;

        if (id == body.id || other.position.y >= body.position.y + body.height
            || body.position.y >= other.position.y + other.height
            || std::hypot(double(other.position.x) - body.position.x,
                          double(other.position.z) - body.position.z)
                   > std::max(settings.neighborDistance,
                              body.radius + other.radius + 2 * settings.margin)) {
          continue;
        }

        if (++nearby > 64) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Nearby steering circle capacity exhausted");
        }

        const auto p = xyz(other.position), velocity = xyz(other.velocity),
                   desired = xyz(other.desired);
        query->addCircle(p.data(), other.radius + settings.margin, velocity.data(), desired.data());
      }

      for (const auto& wall : walls) {
        const auto a = xyz(wall.from), b = xyz(wall.to);
        query->addSegment(a.data(), b.data());
      }

      const auto position = xyz(body.position), velocity = xyz(body.velocity),
                 desired = xyz(body.desired);
      std::array<float, 3> result{};
      const dtObstacleAvoidanceParams parameters{.4F, 2, .75F, .75F, 2.5F, settings.timeHorizon,
                                                 33,  7, 3,    5};
      const auto samples = query->sampleVelocityAdaptive(
          position.data(), body.radius + settings.margin, settings.maxSpeed, velocity.data(),
          desired.data(), result.data(), &parameters);
      Math::Vec3 sampled{result[0], 0, result[2]};

      const auto magnitude = Math::length(sampled);

      if (samples <= 0 || !Math::isFinite(sampled) || magnitude > settings.maxSpeed + .0011F) {
        return Core::fail(Core::ErrorCode::External,
                          "Navigation steering produced invalid velocity/samples");
      }

      /** Detour admits candidates within vmax + 0.001; publish the exact engine speed cap.
       * Detour 接受 vmax + 0.001 内的候选，发布时遵守引擎的精确速度上限。 */
      if (magnitude > settings.maxSpeed) {
        sampled = sampled * static_cast<float>(settings.maxSpeed / magnitude);
      }

      return SteeringResult{sampled, nearby, static_cast<std::size_t>(samples)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Sample navigation velocity: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Navigation

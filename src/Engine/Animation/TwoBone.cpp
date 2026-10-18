module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <string>
#include <utility>

module Dot.Animation.TwoBone;

import Dot.Core.Result;
import Dot.Math;
import Dot.World.Scene;

namespace Dot::Animation {
  namespace {
    auto native(Math::Vec3 value) -> glm::dvec3 {
      return {value.x, value.y, value.z};
    }

    auto native(Math::Quat value) -> glm::dquat {
      return glm::normalize(glm::dquat{value.w, value.x, value.y, value.z});
    }

    auto rotation(glm::dquat value) -> Math::Quat {
      value = glm::normalize(value);
      return {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z),
              static_cast<float>(value.w)};
    }

    auto uniform(Math::Vec3 scale) -> bool {
      return Math::isFinite(scale) && scale.x >= 1e-6F && scale.y >= 1e-6F && scale.z >= 1e-6F
             && std::abs(double(scale.x) - scale.y) <= 1e-5 * scale.x
             && std::abs(double(scale.x) - scale.z) <= 1e-5 * scale.x;
    }

    auto swing(glm::dvec3 from, glm::dvec3 to) -> glm::dquat {
      from = glm::normalize(from);
      to = glm::normalize(to);
      const auto cosine = std::clamp(glm::dot(from, to), -1.0, 1.0);
      const auto cross = glm::cross(from, to);
      const auto sine = glm::length(cross);

      if (sine > 1e-12) {
        const auto angle = std::atan2(sine, cosine);
        return glm::angleAxis(angle, cross / sine);
      }

      if (cosine >= 0) {
        return {1, 0, 0, 0};
      }

      const auto magnitude = glm::abs(from);
      const auto basis = magnitude.x <= magnitude.y && magnitude.x <= magnitude.z
                             ? glm::dvec3{1, 0, 0}
                         : magnitude.y <= magnitude.z ? glm::dvec3{0, 1, 0}
                                                      : glm::dvec3{0, 0, 1};
      const auto axis = glm::normalize(glm::cross(from, basis));
      return {0, axis.x, axis.y, axis.z};
    }
  }  // namespace

  auto solveTwoBone(const World::Scene& scene, const TwoBoneRequest& request)
      -> Core::Result<TwoBonePose> {
    try {
      if (!Math::isFinite(request.target) || !Math::isFinite(request.pole)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Two-bone target/pole must be finite");
      }

      const std::array ids{request.root, request.middle, request.end};
      std::array<World::EntityInfo, 3> entities;
      std::array<Math::TransformParts, 3> world;

      for (std::size_t i = 0; i < ids.size(); ++i) {
        auto entity = scene.entity(ids[i]);
        auto matrix = entity ? scene.worldTransform(ids[i])
                             : Core::Result<Math::Mat4>{std::unexpected(entity.error())};
        auto parts = matrix ? Math::decomposeTrs(*matrix)
                            : Core::Result<Math::TransformParts>{std::unexpected(matrix.error())};

        if (!entity || !parts) {
          const auto& error = !entity ? entity.error() : parts.error();
          return Core::fail(error.code, "Two-bone chain: " + error.message);
        }

        entities[i] = std::move(*entity);
        world[i] = *parts;
      }

      if (request.root == request.middle || request.root == request.end
          || request.middle == request.end || entities[1].parent != request.root
          || entities[2].parent != request.middle) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Two-bone chain requires distinct direct parent/child entities");
      }

      auto ancestor = std::optional{request.end};
      unsigned depth{};

      while (ancestor) {
        auto entity = scene.entity(*ancestor);

        if (!entity) {
          return std::unexpected(entity.error());
        }

        if (++depth > 256 || !uniform(entity->transform.scale)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Two-bone ancestors need positive uniform scales and depth <=256");
        }

        ancestor = entity->parent;
      }

      auto parent = glm::dquat{1, 0, 0, 0};

      if (entities[0].parent) {
        auto matrix = scene.worldTransform(*entities[0].parent);
        auto parts = matrix ? Math::decomposeTrs(*matrix)
                            : Core::Result<Math::TransformParts>{std::unexpected(matrix.error())};

        if (!parts) {
          return std::unexpected(parts.error());
        }

        parent = native(parts->rotation);
      }

      const auto root = native(world[0].position);
      const auto upper = native(world[1].position) - root;
      const auto lower = native(world[2].position) - native(world[1].position);
      const auto goal = native(request.target) - root;
      const auto pole = native(request.pole) - root;
      const auto first = glm::length(upper);
      const auto second = glm::length(lower);
      const auto distance = glm::length(goal);

      if (first < 1e-5 || second < 1e-5 || distance < 1e-5) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Two-bone segments and root-target distance must be >=1e-5");
      }

      const auto direction = goal / distance;
      const auto projected = pole - direction * glm::dot(pole, direction);
      const auto poleDistance = glm::length(projected);

      if (poleDistance <= 1e-5 * glm::length(pole)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Two-bone pole must define a non-collinear bend plane");
      }

      const auto radius = std::clamp(distance, std::abs(first - second), first + second);
      const auto along = (first * first + radius * radius - second * second) / (2 * radius);
      const auto height = std::sqrt(std::max(0.0, first * first - along * along));
      const auto elbow = direction * along + projected * (height / poleDistance);
      const auto tip = direction * radius;
      const auto rootDelta = swing(upper, elbow);
      const auto rootRotation = glm::normalize(rootDelta * native(world[0].rotation));
      const auto middleDelta = swing(rootDelta * lower, tip - elbow);
      const auto middleRotation
          = glm::normalize(middleDelta * rootDelta * native(world[1].rotation));
      TwoBonePose result{{{{request.root, entities[0].transform},
                           {request.middle, entities[1].transform},
                           {request.end, entities[2].transform}}},
                         {},
                         0,
                         radius != distance};
      result.updates[0].transform.rotation = rotation(glm::inverse(parent) * rootRotation);
      result.updates[1].transform.rotation = rotation(glm::inverse(rootRotation) * middleRotation);
      result.updates[2].transform.rotation
          = rotation(glm::inverse(middleRotation) * native(world[2].rotation));

      /** Verify the float output through the actual local TRS hierarchy before publication.
       * 发布前通过真实局部 TRS 层级复核浮点输出。 */
      auto matrix = entities[0].parent ? scene.worldTransform(*entities[0].parent)
                                       : Core::Result<Math::Mat4>{Math::Mat4{}};

      for (const auto& update : result.updates) {
        const auto& pose = update.transform;
        auto local = Math::compose(pose.position, pose.rotation, pose.scale);
        matrix = !matrix ? matrix
                 : local ? Math::multiply(*matrix, *local)
                         : std::unexpected(local.error());
      }

      if (!matrix) {
        return Core::fail(matrix.error().code, "Two-bone output: " + matrix.error().message);
      }

      result.reached = {matrix->data[12], matrix->data[13], matrix->data[14]};
      const auto error = glm::length(native(result.reached) - root - tip);

      if (error > 1e-5 * std::max(1.0, first + second)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Two-bone output loses precision in this world coordinate range");
      }

      result.residual = glm::length(native(result.reached) - native(request.target));
      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Solve two-bone pose: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Animation

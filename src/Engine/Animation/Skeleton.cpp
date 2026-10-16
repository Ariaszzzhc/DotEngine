module;

#include <exception>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <utility>

module Dot.Animation.Skeleton;

import Dot.Animation.SceneSkin;
import Dot.Core.Result;
import Dot.World.Scene;

namespace Dot::Animation {
  auto sampleSkeleton(const World::Scene& scene, World::EntityId owner, Skin skin)
      -> Core::Result<SkeletonPose> {
    try {
      auto info = scene.entity(owner);

      if (!info) {
        return std::unexpected(info.error());
      }

      if (!info->mesh || !info->skin || !info->skin->enabled) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Skeleton requires an enabled mesh skin");
      }

      auto validated = SceneSkin::bind(scene, owner, std::move(skin));

      if (!validated) {
        return std::unexpected(validated.error());
      }

      SkeletonPose result{owner, info->skin->asset, {}};
      const auto& joints = info->skin->joints;
      result.joints.reserve(joints.size());
      std::map<std::uint64_t, std::uint32_t> indices;
      std::map<std::uint64_t, std::optional<World::EntityId>> parents;

      for (std::size_t i = 0; i < joints.size(); ++i) {
        indices.emplace(joints[i], static_cast<std::uint32_t>(i));
      }

      for (const auto value : joints) {
        auto joint = scene.findEntity(value);
        auto entity = joint ? scene.entity(*joint)
                            : Core::Result<World::EntityInfo>{std::unexpected(joint.error())};
        auto world = joint ? scene.worldTransform(*joint)
                           : Core::Result<Math::Mat4>{std::unexpected(joint.error())};

        if (!entity || !world) {
          return std::unexpected(!entity ? entity.error() : world.error());
        }

        parents.emplace(value, entity->parent);
        auto current = entity->parent;
        std::optional<std::uint32_t> parent;
        std::uint32_t links{};

        while (current) {
          if (++links > 256) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Skeleton ancestor chain exceeds limit");
          }

          if (const auto found = indices.find(current->value); found != indices.end()) {
            parent = found->second;
            break;
          }

          auto cached = parents.find(current->value);

          if (cached == parents.end()) {
            auto ancestor = scene.entity(*current);

            if (!ancestor) {
              return std::unexpected(ancestor.error());
            }

            cached = parents.emplace(current->value, ancestor->parent).first;
          }

          current = cached->second;
        }

        result.joints.push_back({*joint,
                                 std::move(entity->name),
                                 {world->data[12], world->data[13], world->data[14]},
                                 parent,
                                 parent ? links : 0});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Inspect skeleton: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Animation

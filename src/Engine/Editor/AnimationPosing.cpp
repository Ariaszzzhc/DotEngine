module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>

module Dot.Editor.AnimationPosing;

import Dot.Core.Result;
import Dot.Math;
import Dot.World.Scene;

namespace Dot::Editor {
  namespace {
    auto distance(Math::Vec3 a, Math::Vec3 b) -> double {
      return std::hypot(double(a.x) - b.x, double(a.y) - b.y, double(a.z) - b.z);
    }
  }  // namespace

  auto prepareTwoBoneKeys(const World::Scene& posed, World::EntityId owner,
                          const Animation::Clip& clip, float time, std::uint32_t end,
                          Math::Vec3 target, Math::Vec3 pole) -> Core::Result<TwoBoneKeys> {
    try {
      auto info = posed.entity(owner);
      auto description = clip.description();
      auto duration = clip.duration();

      if (!info || !description || !duration) {
        return std::unexpected(!info          ? info.error()
                               : !description ? description.error()
                                              : duration.error());
      }

      if (!info->animation || !std::isfinite(time) || time < 0 || time > *duration) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Pose keying needs an animation owner and a time within its clip");
      }

      std::map<std::uint64_t, std::uint32_t> bindings;
      std::optional<World::EntityId> tip;

      for (const auto& binding : info->animation->targets) {
        if (!bindings.emplace(binding.entity, binding.node).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Pose key bindings must be unique");
        }

        if (binding.node == end) {
          auto found = posed.findEntity(binding.entity);

          if (!found || tip) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Pose key end binding is invalid");
          }

          tip = *found;
        }
      }

      auto last = tip ? posed.entity(*tip)
                      : Core::Result<World::EntityInfo>{Core::fail(Core::ErrorCode::InvalidArgument,
                                                                   "Choose a bound pose key end")};
      auto middle = last && last->parent
                        ? posed.entity(*last->parent)
                        : Core::Result<World::EntityInfo>{Core::fail(
                              Core::ErrorCode::InvalidArgument, "Pose key end needs a parent")};

      if (!middle || !middle->parent) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Pose keying needs a direct three-joint chain");
      }

      const std::array chain{*middle->parent, *last->parent, *tip};
      auto ancestor = std::optional{chain[0]};
      unsigned depth{};

      while (ancestor && *ancestor != owner) {
        auto entity = posed.entity(*ancestor);

        if (!entity || ++depth > 256) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Pose key chain ancestry is invalid");
        }

        ancestor = entity->parent;
      }

      if (!ancestor
          || std::ranges::any_of(chain, [&](auto id) { return !bindings.contains(id.value); })) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "All three pose joints must be bound within this owner's subtree");
      }

      auto solved = Animation::solveTwoBone(posed, {chain[0], chain[1], chain[2], target, pole});

      if (!solved) {
        return std::unexpected(solved.error());
      }

      std::array<Animation::ClipEdit, 3> edits;
      std::array<Animation::KeyReference, 3> selection;

      for (std::size_t i = 0; i < chain.size(); ++i) {
        const auto node = bindings.at(chain[i].value);
        auto rotation = solved->updates[i].transform.rotation;
        auto track = std::ranges::find_if((*description)->tracks, [&](const auto& value) {
          return value.node == node && value.path == Animation::Path::Rotation;
        });
        Animation::Key key{time, {rotation.x, rotation.y, rotation.z, rotation.w}};
        auto mode = Animation::Interpolation::Linear;

        if (track != (*description)->tracks.end()) {
          mode = track->interpolation;
          auto next = std::ranges::lower_bound(track->keys, time, {}, &Animation::Key::time);
          const auto& reference = next != track->keys.end() && next->time == time ? *next
                                  : next == track->keys.begin() ? track->keys.front()
                                                                : *std::prev(next);
          const auto sign
              = double(key.value.x) * reference.value.x + double(key.value.y) * reference.value.y
                + double(key.value.z) * reference.value.z + double(key.value.w) * reference.value.w;

          if (sign < 0) {
            key.value = {-key.value.x, -key.value.y, -key.value.z, -key.value.w};
          }

          if (next != track->keys.end() && next->time == time) {
            key.inTangent = next->inTangent;
            key.outTangent = next->outTangent;
          }
        }

        edits[i] = Animation::PutKey{node, Animation::Path::Rotation, mode, key};
        selection[i] = {node, Animation::Path::Rotation, time};
      }

      auto value = Animation::editClip(clip, edits);
      return value ? Core::Result<TwoBoneKeys>{TwoBoneKeys{std::move(*value), std::move(*solved),
                                                           selection, target}}
                   : std::unexpected(value.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare pose keys: " + std::string(error.what()));
    }
  }

  auto validateTwoBoneKeys(const World::Scene& candidate, const TwoBoneKeys& keys)
      -> Core::Result<PoseKeySample> {
    std::array<Math::Vec3, 3> positions;

    for (std::size_t i = 0; i < keys.pose.updates.size(); ++i) {
      const auto& update = keys.pose.updates[i];
      auto found = candidate.findEntity(update.entity.value);
      auto info = found ? candidate.entity(*found)
                        : Core::Result<World::EntityInfo>{std::unexpected(found.error())};
      auto matrix = found ? candidate.worldTransform(*found)
                          : Core::Result<Math::Mat4>{std::unexpected(found.error())};

      if (!info || !matrix) {
        return std::unexpected(!info ? info.error() : matrix.error());
      }

      auto expected = Math::compose({}, update.transform.rotation);
      auto actual = Math::compose({}, info->transform.rotation);

      if (!expected || !actual || info->transform.position != update.transform.position
          || info->transform.scale != update.transform.scale) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Candidate animation does not reproduce the keyed pose transforms");
      }

      for (std::size_t component = 0; component < expected->data.size(); ++component) {
        if (std::abs(double(expected->data[component]) - actual->data[component]) > 1e-5) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Candidate animation blending changes the keyed pose rotation");
        }
      }

      positions[i] = {matrix->data[12], matrix->data[13], matrix->data[14]};
    }

    const auto length = distance(positions[1], positions[0]) + distance(positions[2], positions[1]);

    if (!Math::isFinite(keys.pose.reached) || !Math::isFinite(keys.target)
        || distance(positions[2], keys.pose.reached) > 1e-5 * std::max(1.0, length)) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Candidate animation does not reproduce the solved world target");
    }

    return PoseKeySample{positions[2], distance(positions[2], keys.target)};
  }
}  // namespace Dot::Editor

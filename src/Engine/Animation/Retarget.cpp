module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

module Dot.Animation.Retarget;

import Dot.Core.Text;
import Dot.Math;
import Dot.World.Scene;

namespace Dot::Animation {
  namespace {
    struct Rig {
      std::vector<std::uint32_t> order;
      std::map<std::uint32_t, std::uint32_t> bindings;
      std::vector<Math::TransformParts> local;
      std::vector<Math::Mat4> referenceWorld;
      std::vector<Math::TransformParts> referenceParts;
    };

    auto uniform(Math::Vec3 scale) -> bool {
      return Math::isFinite(scale) && scale.x >= 1e-6F && scale.y >= 1e-6F && scale.z >= 1e-6F
             && std::abs(double(scale.x) - scale.y) <= 1e-5 * scale.x
             && std::abs(double(scale.x) - scale.z) <= 1e-5 * scale.x;
    }

    auto world(const RetargetRig& rig, std::span<const std::uint32_t> order,
               std::span<const Math::TransformParts> poses)
        -> Core::Result<std::vector<Math::Mat4>> {
      std::vector<Math::Mat4> result(rig.nodes.size());

      for (const auto index : order) {
        const auto& node = rig.nodes[index];
        auto local
            = Math::compose(poses[index].position, poses[index].rotation, poses[index].scale);
        auto matrix = !local        ? local
                      : node.parent ? Math::multiply(result[*node.parent], *local)
                                    : local;

        if (!matrix) {
          return Core::fail(matrix.error().code,
                            "Retarget node " + node.name + ": " + matrix.error().message);
        }

        result[index] = *matrix;
      }

      return result;
    }

    auto parts(std::span<const Math::Mat4> matrices)
        -> Core::Result<std::vector<Math::TransformParts>> {
      std::vector<Math::TransformParts> result;
      result.reserve(matrices.size());

      for (const auto& matrix : matrices) {
        auto value = Math::decomposeTrs(matrix);

        if (!value) {
          return std::unexpected(value.error());
        }

        result.push_back(*value);
      }

      return result;
    }

    auto prepare(const RetargetRig& value) -> Core::Result<Rig> {
      if (value.nodes.empty() || value.nodes.size() > 10000 || value.anchor >= value.nodes.size()
          || value.nodes[value.anchor].parent) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid retarget rig capacity/anchor");
      }

      Rig result{};
      std::vector<unsigned> visited(value.nodes.size());
      std::vector<Math::TransformParts> poses;
      poses.reserve(value.nodes.size());
      std::function<Core::Result<void>(std::uint32_t, unsigned)> visit
          = [&](std::uint32_t index, unsigned depth) -> Core::Result<void> {
        if (index >= value.nodes.size() || depth > 256 || visited[index] == 1) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Retarget rig hierarchy is invalid");
        }

        if (visited[index] == 2) {
          return {};
        }

        visited[index] = 1;
        const auto& node = value.nodes[index];

        if (node.parent) {
          auto valid = visit(*node.parent, depth + 1);

          if (!valid) {
            return valid;
          }
        } else if (index != value.anchor) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Retarget rig has an unanchored root");
        }

        visited[index] = 2;
        result.order.push_back(index);
        return {};
      };

      for (std::size_t index = 0; index < value.nodes.size(); ++index) {
        const auto& node = value.nodes[index];

        if (node.name.size() > 1024 || !Core::isValidUtf8(node.name)
            || node.name.find('\0') != std::string::npos || !uniform(node.reference.scale)) {
          return Core::fail(
              Core::ErrorCode::InvalidArgument,
              "Retarget reference needs valid names and positive uniform local scales");
        }

        if (node.binding
            && (*node.binding >= 10000
                || !result.bindings.emplace(*node.binding, static_cast<std::uint32_t>(index))
                        .second)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Duplicate/invalid retarget binding");
        }

        auto valid = visit(static_cast<std::uint32_t>(index), 0);

        if (!valid) {
          return std::unexpected(valid.error());
        }

        auto reference = node.reference;
        auto rotation = Math::normalized(reference.rotation);

        if (!rotation) {
          return std::unexpected(rotation.error());
        }

        reference.rotation = *rotation;
        poses.push_back(reference);
      }

      std::vector<unsigned> depths(value.nodes.size());

      for (const auto index : result.order) {
        depths[index] = value.nodes[index].parent ? depths[*value.nodes[index].parent] + 1 : 0;

        if (depths[index] > 256) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Retarget rig depth exceeds 256");
        }
      }

      if (result.bindings.empty()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Retarget rig has no bindings");
      }

      auto matrices = world(value, result.order, poses);
      auto reference = matrices ? parts(*matrices)
                                : Core::Result<std::vector<Math::TransformParts>>{
                                      std::unexpected(matrices.error())};

      if (!reference) {
        return Core::fail(reference.error().code,
                          "Retarget reference: " + reference.error().message);
      }

      result.local = std::move(poses);
      result.referenceWorld = std::move(*matrices);
      result.referenceParts = std::move(*reference);
      return result;
    }

    auto parentMapping(const RetargetRig& rig, std::uint32_t index,
                       const std::map<std::uint32_t, std::uint32_t>& mapped)
        -> std::optional<std::uint32_t> {
      auto parent = rig.nodes[index].parent;

      while (parent) {
        if (mapped.contains(*parent)) {
          return *parent;
        }

        parent = rig.nodes[*parent].parent;
      }

      return {};
    }

    auto relative(Math::Quat parent, Math::Quat child) -> Core::Result<Math::Quat> {
      auto inverse = Math::inverseRotation(parent);
      return inverse ? Math::multiplyRotations(*inverse, child) : std::unexpected(inverse.error());
    }
  }  // namespace

  auto validateRetargetRig(const RetargetRig& rig) -> Core::Result<void> {
    try {
      auto valid = prepare(rig);
      return valid ? Core::Result<void>{} : std::unexpected(valid.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Validate retarget rig: " + std::string(error.what()));
    }
  }

  auto captureRetargetRig(const World::Scene& scene, World::EntityId owner)
      -> Core::Result<RetargetRig> {
    auto info = scene.entity(owner);

    if (!info || !info->animation) {
      return info ? Core::fail(Core::ErrorCode::InvalidArgument,
                               "Choose an animation owner for retargeting")
                  : std::unexpected(info.error());
    }

    try {
      RetargetRig result;
      std::map<std::uint64_t, std::uint32_t> indices;
      std::function<Core::Result<std::uint32_t>(World::EntityId, unsigned)> add
          = [&](World::EntityId id, unsigned depth) -> Core::Result<std::uint32_t> {
        if (const auto found = indices.find(id.value); found != indices.end()) {
          return found->second;
        }

        if (depth > 256 || result.nodes.size() >= 10000) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Retarget hierarchy exceeds limits");
        }

        auto entity = scene.entity(id);

        if (!entity) {
          return std::unexpected(entity.error());
        }

        std::optional<std::uint32_t> parent;

        if (id != owner) {
          if (!entity->parent) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Retarget binding is outside owner subtree");
          }

          auto added = add(*entity->parent, depth + 1);

          if (!added) {
            return std::unexpected(added.error());
          }

          parent = *added;
        }

        const auto index = static_cast<std::uint32_t>(result.nodes.size());
        result.nodes.push_back(
            {entity->name,
             parent,
             {entity->transform.position, entity->transform.rotation, entity->transform.scale},
             {}});
        indices.emplace(id.value, index);
        return index;
      };
      auto root = add(owner, 0);

      if (!root) {
        return std::unexpected(root.error());
      }

      result.anchor = *root;

      for (const auto& binding : info->animation->targets) {
        auto entity = scene.findEntity(binding.entity);
        auto index = entity ? add(*entity, 0)
                            : Core::Result<std::uint32_t>{std::unexpected(entity.error())};

        if (!index) {
          return std::unexpected(index.error());
        }

        result.nodes[*index].binding = binding.node;
      }

      auto valid = prepare(result);
      return valid ? Core::Result<RetargetRig>{std::move(result)} : std::unexpected(valid.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Capture retarget rig: " + std::string(error.what()));
    }
  }

  auto retargetClip(const RetargetRig& source, const RetargetRig& target, const Clip& clip,
                    const RetargetSettings& settings, std::stop_token stop)
      -> Core::Result<RetargetResult> {
    const double rate = settings.rate.denominator
                            ? double(settings.rate.numerator) / settings.rate.denominator
                            : 0;
    auto duration = clip.duration();

    if (!duration) {
      return std::unexpected(duration.error());
    }

    if (settings.name.empty() || settings.name.size() > 256 || !Core::isValidUtf8(settings.name)
        || settings.name.find('\0') != std::string::npos || settings.mappings.empty()
        || settings.mappings.size() > 10000 || settings.rate.denominator == 0
        || settings.rate.denominator > 10000 || settings.rate.numerator > 240000
        || !std::isfinite(rate) || rate < 1 || rate > 240 || *duration <= 0 || *duration > 86400
        || (settings.translation
            && (!std::isfinite(settings.translation->scale) || settings.translation->scale < 1e-6
                || settings.translation->scale > 1e6))) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid retarget mappings/rate/duration/displacement scale");
    }

    try {
      auto first = prepare(source);
      auto last = prepare(target);

      if (!first || !last) {
        return std::unexpected(!first ? first.error() : last.error());
      }

      std::map<std::uint32_t, std::uint32_t> pairs;
      std::map<std::uint32_t, std::uint32_t> reverse;

      for (const auto& mapping : settings.mappings) {
        const auto from = first->bindings.find(mapping.source);
        const auto to = last->bindings.find(mapping.target);

        if (from == first->bindings.end() || to == last->bindings.end()
            || from->second == source.anchor || to->second == target.anchor
            || source.nodes[from->second].name != mapping.sourceName
            || target.nodes[to->second].name != mapping.targetName
            || !pairs.emplace(to->second, from->second).second
            || !reverse.emplace(from->second, to->second).second) {
          return Core::fail(
              Core::ErrorCode::InvalidArgument,
              "Retarget mapping is missing, duplicate, renamed or addresses an anchor");
        }
      }

      std::set<std::uint32_t> roots;

      for (const auto& [to, from] : pairs) {
        const auto sourceParent = parentMapping(source, from, reverse);
        const auto targetParent = parentMapping(target, to, pairs);

        if (sourceParent.has_value() != targetParent.has_value()
            || (sourceParent && reverse.at(*sourceParent) != *targetParent)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Retarget mapped ancestry differs at " + target.nodes[to].name);
        }

        if (!targetParent) {
          roots.insert(to);
        }
      }

      std::optional<std::uint32_t> translated;

      if (settings.translation) {
        auto bound = last->bindings.find(settings.translation->target);

        if (bound == last->bindings.end() || !roots.contains(bound->second)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Displacement requires a mapped root");
        }

        translated = bound->second;
      }

      std::vector<std::uint32_t> emitted;
      RetargetResult result{{}, 0, 0, {}, {}};

      for (const auto& [binding, index] : last->bindings) {
        auto current = std::optional{index};

        while (current && !roots.contains(*current)) {
          current = target.nodes[*current].parent;
        }

        if (current) {
          emitted.push_back(index);
        }

        if (!pairs.contains(index)) {
          result.unmappedTarget.push_back(binding);
        }
      }

      for (const auto& [binding, index] : first->bindings) {
        if (!reverse.contains(index)) {
          result.unmappedSource.push_back(binding);
        }
      }

      for (const auto& track : clip.description().value()->tracks) {
        if (!first->bindings.contains(track.node)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Source clip channel is outside its rig bindings");
        }

        if (track.path == Path::Scale) {
          const auto reference = first->local[first->bindings.at(track.node)].scale;

          for (const auto& key : track.keys) {
            if (std::abs(double(key.value.x) - reference.x) > 1e-5 * reference.x
                || std::abs(double(key.value.y) - reference.y) > 1e-5 * reference.y
                || std::abs(double(key.value.z) - reference.z) > 1e-5 * reference.z
                || (track.interpolation == Interpolation::CubicSpline
                    && (key.inTangent != Math::Vec4{} || key.outTangent != Math::Vec4{}))) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Animated source scale is not supported by retargeting");
            }
          }
        }
      }

      std::vector<float> times{0};

      for (std::size_t frame = 1;; ++frame) {
        const auto time = static_cast<float>(double(frame) / rate);

        if (time >= *duration) {
          break;
        }

        if (time <= times.back()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Retarget interior sample times collapse");
        }

        times.push_back(time);

        if (times.size() >= 8192) {
          return Core::fail(Core::ErrorCode::Unavailable, "Retarget sample budget exceeded");
        }
      }

      times.push_back(static_cast<float>(*duration));

      if (emitted.empty() || emitted.size() > 10000 || times.size() * emitted.size() * 3 > 1000000
          || times.size() * (source.nodes.size() + target.nodes.size()) > 4000000) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Retarget sampling/key/pose budget exceeded");
      }

      result.frames = times.size();
      result.emittedNodes = emitted.size();
      ClipDescription output{settings.name, {}};

      for (const auto index : emitted) {
        for (const auto path : {Path::Translation, Path::Rotation, Path::Scale}) {
          output.tracks.push_back({*target.nodes[index].binding, path, Interpolation::Linear, {}});
          output.tracks.back().keys.reserve(result.frames);
        }
      }

      const auto sourceAnchor = first->referenceParts[source.anchor].rotation;
      const auto targetAnchor = last->referenceParts[target.anchor].rotation;
      auto sourceInverse = Math::inverseRotation(sourceAnchor).value();
      const auto displacementRotation
          = Math::multiplyRotations(targetAnchor, sourceInverse).value();
      const auto displacementMatrix = Math::compose({}, displacementRotation).value();

      for (std::size_t frame = 0; frame < result.frames; ++frame) {
        if (stop.stop_requested()) {
          return Core::fail(Core::ErrorCode::Unavailable, "Animation retarget cancelled");
        }

        const auto time = times[frame];
        auto sampled = clip.sample(time);

        if (!sampled) {
          return Core::fail(sampled.error().code, "Retarget sample: " + sampled.error().message);
        }

        std::vector<Math::TransformParts> sourcePoses;

        sourcePoses = first->local;

        for (const auto& channel : *sampled) {
          auto& pose = sourcePoses[first->bindings.at(channel.node)];
          const auto value = channel.value;

          if (channel.path == Path::Translation) {
            pose.position = {value.x, value.y, value.z};
          } else if (channel.path == Path::Rotation) {
            pose.rotation = {value.x, value.y, value.z, value.w};
          } else {
            const auto reference = pose.scale;

            if (std::abs(double(value.x) - reference.x) > 1e-5 * reference.x
                || std::abs(double(value.y) - reference.y) > 1e-5 * reference.y
                || std::abs(double(value.z) - reference.z) > 1e-5 * reference.z) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Animated source scale is not supported by retargeting");
            }
          }
        }

        auto sourceMatrices = world(source, first->order, sourcePoses);
        auto sourceParts = sourceMatrices ? parts(*sourceMatrices)
                                          : Core::Result<std::vector<Math::TransformParts>>{
                                                std::unexpected(sourceMatrices.error())};

        if (!sourceParts) {
          return std::unexpected(sourceParts.error());
        }

        std::vector<Math::TransformParts> poses;
        std::vector<Math::Mat4> matrices(target.nodes.size());
        std::vector<Math::Quat> rotations(target.nodes.size());

        poses = last->local;

        for (const auto index : last->order) {
          const auto& node = target.nodes[index];
          auto& pose = poses[index];
          const auto parentRotation = node.parent ? rotations[*node.parent] : Math::Quat{};

          if (const auto found = pairs.find(index); found != pairs.end()) {
            const auto from = found->second;
            auto animated = relative(sourceAnchor, (*sourceParts)[from].rotation).value();
            auto origin = relative(sourceAnchor, first->referenceParts[from].rotation).value();
            auto delta
                = Math::multiplyRotations(animated, Math::inverseRotation(origin).value()).value();
            auto targetOrigin
                = relative(targetAnchor, last->referenceParts[index].rotation).value();
            auto desired = Math::multiplyRotations(
                               targetAnchor, Math::multiplyRotations(delta, targetOrigin).value())
                               .value();
            pose.rotation = relative(parentRotation, desired).value();
          }

          if (translated && index == *translated) {
            const auto from = pairs.at(index);
            const auto a = (*sourceParts)[from].position;
            const auto b = first->referenceParts[from].position;
            const double dx = double(a.x) - b.x;
            const double dy = double(a.y) - b.y;
            const double dz = double(a.z) - b.z;
            constexpr double maximum = std::numeric_limits<float>::max();

            if (std::abs(dx) > maximum || std::abs(dy) > maximum || std::abs(dz) > maximum) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Retarget source displacement overflow");
            }

            auto delta = Math::transformVector(
                displacementMatrix,
                {static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz)});

            if (!delta) {
              return std::unexpected(delta.error());
            }

            const auto base = last->referenceParts[index].position;
            const auto scale = settings.translation->scale;
            const auto finite = [](double x) {
              return std::isfinite(x) && std::abs(x) <= std::numeric_limits<float>::max();
            };
            const double x = base.x + double(delta->x) * scale;
            const double y = base.y + double(delta->y) * scale;
            const double z = base.z + double(delta->z) * scale;

            if (!finite(x) || !finite(y) || !finite(z)) {
              return Core::fail(Core::ErrorCode::InvalidArgument, "Retarget displacement overflow");
            }

            const Math::Vec3 desired{static_cast<float>(x), static_cast<float>(y),
                                     static_cast<float>(z)};
            auto inverse = node.parent ? Math::inverse(matrices[*node.parent])
                                       : Core::Result<Math::Mat4>{Math::Mat4{}};
            auto local = inverse ? Math::transformPoint(*inverse, desired)
                                 : Core::Result<Math::Vec3>{std::unexpected(inverse.error())};

            if (!local) {
              return std::unexpected(local.error());
            }

            pose.position = *local;
          }

          auto local = Math::compose(pose.position, pose.rotation, pose.scale);
          auto matrix = !local        ? local
                        : node.parent ? Math::multiply(matrices[*node.parent], *local)
                                      : local;

          if (!matrix) {
            return std::unexpected(matrix.error());
          }

          matrices[index] = *matrix;
          rotations[index] = Math::multiplyRotations(parentRotation, pose.rotation).value();
        }

        for (std::size_t at = 0; at < emitted.size(); ++at) {
          const auto& pose = poses[emitted[at]];
          std::array<Math::Vec4, 3> values{
              {{pose.position.x, pose.position.y, pose.position.z, 0},
               {pose.rotation.x, pose.rotation.y, pose.rotation.z, pose.rotation.w},
               {pose.scale.x, pose.scale.y, pose.scale.z, 0}}};

          for (std::size_t path = 0; path < 3; ++path) {
            auto& keys = output.tracks[at * 3 + path].keys;
            auto value = values[path];

            if (path == 1 && !keys.empty()) {
              const auto previous = keys.back().value;
              const auto dot = double(previous.x) * value.x + double(previous.y) * value.y
                               + double(previous.z) * value.z + double(previous.w) * value.w;

              if (dot < 0) {
                value = {-value.x, -value.y, -value.z, -value.w};
              }
            }

            keys.push_back({time, value});
          }
        }
      }

      auto baked = Clip::create(std::move(output));

      if (!baked) {
        return std::unexpected(baked.error());
      }

      result.clip = std::move(*baked);
      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Retarget animation: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Animation

module;

#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

module Dot.Editor.AnimationCreation;

import Dot.Animation.Files;
import Dot.Animation.ScenePlayers;
import Dot.Editor.AnimationPreview;
import Dot.Math;
import Dot.World.SceneFiles;

namespace Dot::Editor {
  auto preparePoseAnimation(const World::Scene& scene, const Core::FileSystem& files,
                            std::uint64_t owner, std::string_view path, std::string_view name,
                            double duration) -> Core::Result<AnimationCreation> {
    if (!std::isfinite(duration) || duration <= 0 || duration > 86400
        || static_cast<float>(duration) <= 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation duration must remain positive in native float seconds, <=86400");
    }

    const auto end = static_cast<float>(duration);
    auto entity = scene.findEntity(owner);
    auto info = entity ? scene.entity(*entity)
                       : Core::Result<World::EntityInfo>{std::unexpected(entity.error())};

    if (!info) {
      return std::unexpected(info.error());
    }

    try {
      Animation::PlayerProperties player = info->animation.value_or(Animation::PlayerProperties{});

      if (!info->animation) {
        player.targets.push_back({0, owner});
      }

      player.clip = static_cast<std::uint32_t>(player.clips.size());
      player.clips.emplace_back(path);
      auto valid = Animation::validatePlayerProperties(player);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      Animation::ClipDescription description{std::string(name), {}};
      description.tracks.reserve(player.targets.size() * 3);

      for (const auto& target : player.targets) {
        auto bound = scene.findEntity(target.entity);
        auto pose = bound ? scene.transform(*bound)
                          : Core::Result<World::Transform>{std::unexpected(bound.error())};

        if (!pose) {
          return std::unexpected(pose.error());
        }

        const std::array<Math::Vec4, 3> values{
            Math::Vec4{pose->position.x, pose->position.y, pose->position.z, 0},
            Math::Vec4{pose->rotation.x, pose->rotation.y, pose->rotation.z, pose->rotation.w},
            Math::Vec4{pose->scale.x, pose->scale.y, pose->scale.z, 0}};

        for (std::size_t i = 0; i < values.size(); ++i) {
          description.tracks.push_back({target.node,
                                        static_cast<Animation::Path>(i),
                                        Animation::Interpolation::Linear,
                                        {{0, values[i]}, {end, values[i]}}});
        }
      }

      auto clip = Animation::Clip::create(std::move(description));
      auto text = clip ? Animation::encodeClip(*clip)
                       : Core::Result<std::string>{std::unexpected(clip.error())};

      if (!text) {
        return std::unexpected(text.error());
      }

      auto encoded = World::encodeScene(scene);
      auto candidate = encoded ? World::decodeScene(*encoded)
                               : Core::Result<World::Scene>{std::unexpected(encoded.error())};

      if (!candidate) {
        return std::unexpected(candidate.error());
      }

      auto enabled = player;
      enabled.enabled = true;
      auto assigned = candidate->setAnimationPlayer(candidate->findEntity(owner).value(), enabled);

      if (!assigned) {
        return std::unexpected(assigned.error());
      }

      auto preview = AnimationPreview::create(*candidate, files, owner,
                                              Animation::ClipAsset{std::string(path), *clip});

      if (!preview) {
        return Core::fail(preview.error().code,
                          "Prepare animation from pose: " + preview.error().message);
      }

      return AnimationCreation{std::move(*clip), std::move(player), std::move(*text)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare animation from pose: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Editor

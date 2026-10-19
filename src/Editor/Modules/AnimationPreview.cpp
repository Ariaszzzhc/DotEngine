module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

module Dot.Editor.AnimationPreview;

import Dot.Animation.ScenePlayers;
import Dot.Assets.SceneSkins;
import Dot.Core.Result;
import Dot.World.SceneFiles;
import Dot.World.SceneContent;

namespace Dot::Editor {
  struct AnimationPreview::Impl {
    std::shared_ptr<World::Scene> m_scene;
    Animation::ScenePlayers m_players;
    World::EntityId m_owner;
    std::uint64_t m_source;
    std::uint64_t m_generation{1};
    std::vector<std::string> m_clips;
    std::optional<Core::Error> m_error;
    bool m_bindPose{};
    std::optional<std::string> m_draft;
    std::thread::id m_thread{std::this_thread::get_id()};

    Impl(std::shared_ptr<World::Scene> value, Animation::ScenePlayers playback,
         World::EntityId entity, std::uint64_t origin, std::vector<std::string> paths)
        : m_scene(std::move(value)),
          m_players(std::move(playback)),
          m_owner(entity),
          m_source(origin),
          m_clips(std::move(paths)) {
    }

    auto check(bool mutate = false) const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Animation preview requires owner thread");
      }

      if (mutate && (m_error || m_generation == std::numeric_limits<std::uint64_t>::max())) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Animation preview is faulted or generation exhausted");
      }

      return {};
    }

    template <class Action>
    auto apply(Action&& action) -> Core::Result<void> {
      auto valid = check(true);

      if (!valid) {
        return valid;
      }

      try {
        auto result = action();

        if (result) {
          ++m_generation;
        }

        return result;
      } catch (const std::exception& failure) {
        return Core::fail(Core::ErrorCode::External,
                          "Control animation preview: " + std::string(failure.what()));
      }
    }
  };

  AnimationPreview::AnimationPreview(std::unique_ptr<Impl> impl) noexcept
      : m_impl(std::move(impl)) {
  }

  AnimationPreview::AnimationPreview(AnimationPreview&&) noexcept = default;

  auto AnimationPreview::operator=(AnimationPreview&&) noexcept -> AnimationPreview& = default;

  AnimationPreview::~AnimationPreview() = default;

  auto AnimationPreview::create(const World::Scene& source, const Core::FileSystem& assets,
                                std::uint64_t owner, std::optional<Animation::ClipAsset> draft,
                                std::optional<Animation::ControllerAsset> controller)
      -> Core::Result<AnimationPreview> {
    if (draft && controller) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Choose one clip or controller draft");
    }

    auto entity = source.findEntity(owner);

    if (!entity) {
      return std::unexpected(entity.error());
    }

    auto info = source.entity(*entity);

    if (!info) {
      return std::unexpected(info.error());
    }

    if (!info->animation || !info->animation->enabled) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation preview requires an enabled player");
    }

    try {
      auto encoded = World::encodeScene(source);
      auto clone = encoded ? World::decodeScene(*encoded)
                           : Core::Result<World::Scene>{std::unexpected(encoded.error())};

      if (!clone) {
        return std::unexpected(clone.error());
      }

      auto scene = std::make_shared<World::Scene>(std::move(*clone));
      auto descriptions = scene->animationPlayers();

      if (!descriptions) {
        return std::unexpected(descriptions.error());
      }

      for (auto& description : *descriptions) {
        description.properties.playOnStart = false;

        if (draft && description.entity.value == owner) {
          auto selected = std::ranges::find(description.properties.clips, draft->asset);

          if (selected == description.properties.clips.end()) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Draft is not in the preview player's library");
          }

          /** Clip audition is a separate manual player in the clone only.
           * 单片段试听仅在克隆内显式解除状态机，不修改编辑源。 */
          description.properties.controller.reset();
          description.properties.clip
              = static_cast<std::uint32_t>(selected - description.properties.clips.begin());
        }

        auto changed = scene->setAnimationPlayer(description.entity, description.properties);

        if (!changed) {
          return std::unexpected(changed.error());
        }
      }

      const std::vector<Animation::ClipAsset> decoded
          = draft ? std::vector{*draft} : std::vector<Animation::ClipAsset>{};
      if (controller && info->animation->controller != controller->asset) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Controller draft does not match the selected player");
      }

      const std::vector<Animation::ControllerAsset> controllers
          = controller ? std::vector{*controller} : std::vector<Animation::ControllerAsset>{};
      auto players = Animation::ScenePlayers::create(scene, assets, decoded, controllers);

      if (!players) {
        return std::unexpected(players.error());
      }

      const auto handle = scene->findEntity(owner).value();
      auto state = players->state(handle);

      if (!state) {
        return std::unexpected(state.error());
      }

      auto posed = state->controller
                       ? players->resetController(handle)
                       : players->seek(handle, state->playback.speed < 0 ? state->duration : 0);

      if (!posed) {
        return std::unexpected(posed.error());
      }

      auto skins = Assets::validateSceneSkins(assets, *scene);

      if (!skins) {
        return std::unexpected(skins.error());
      }

      auto impl = std::make_unique<Impl>(std::move(scene), std::move(*players), handle, source.id(),
                                         info->animation->clips);
      impl->m_draft = draft ? std::optional{draft->asset} : std::nullopt;
      return AnimationPreview(std::move(impl));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create animation preview: " + std::string(error.what()));
    }
  }

  auto AnimationPreview::play(std::uint32_t clip, bool restart) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation preview was moved");
    }

    auto applied = m_impl->apply([&]() -> Core::Result<void> {
      auto state = m_impl->m_players.state(m_impl->m_owner);

      if (!state) {
        return Core::Result<void>{std::unexpected(state.error())};
      }

      if (state->controller) {
        if (clip != state->clip || restart) {
          return Core::fail(Core::ErrorCode::InvalidState, "Controller preview owns base playback");
        }

        return m_impl->m_players.play(m_impl->m_owner);
      }

      return m_impl->m_players.play(m_impl->m_owner, clip, restart || m_impl->m_bindPose);
    });

    if (applied) {
      m_impl->m_bindPose = false;
    }

    return applied;
  }

  auto AnimationPreview::pause() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation preview was moved");
    }

    return m_impl->apply([&] { return m_impl->m_players.pause(m_impl->m_owner); });
  }

  auto AnimationPreview::restorePlayback(Animation::PlaybackState state,
                                         std::span<const Animation::LayerPlayback> layers)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation preview was moved");
    }

    auto result = m_impl->apply(
        [&] { return m_impl->m_players.restorePlayback(m_impl->m_owner, state, layers); });

    if (result) {
      m_impl->m_bindPose = false;
    }

    return result;
  }

  auto AnimationPreview::controlLayer(std::string_view name,
                                      const Animation::LayerControl& operation)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation preview was moved");
    }

    auto result = m_impl->apply(
        [&] { return m_impl->m_players.controlLayer(m_impl->m_owner, name, operation); });

    if (result) {
      m_impl->m_bindPose = false;
    }

    return result;
  }

  auto AnimationPreview::parameters(std::span<const Animation::ControllerParameter> values)
      -> Core::Result<void> {
    return m_impl ? m_impl->apply(
                        [&] { return m_impl->m_players.parameters(m_impl->m_owner, values); })
                  : Core::fail(Core::ErrorCode::InvalidState, "Animation preview was moved");
  }

  auto AnimationPreview::resetController() -> Core::Result<void> {
    return m_impl
               ? m_impl->apply([&] { return m_impl->m_players.resetController(m_impl->m_owner); })
               : Core::fail(Core::ErrorCode::InvalidState, "Animation preview was moved");
  }

  auto AnimationPreview::seek(double seconds, bool pause) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation preview was moved");
    }

    auto applied
        = m_impl->apply([&] { return m_impl->m_players.seek(m_impl->m_owner, seconds, pause); });

    if (applied) {
      m_impl->m_bindPose = false;
    }

    return applied;
  }

  auto AnimationPreview::bindPose() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation preview was moved");
    }

    auto applied = m_impl->apply([&] { return m_impl->m_players.stop(m_impl->m_owner); });

    if (applied) {
      m_impl->m_bindPose = true;
    }

    return applied;
  }

  auto AnimationPreview::configure(double speed, bool looping) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation preview was moved");
    }

    return m_impl->apply(
        [&] { return m_impl->m_players.configure(m_impl->m_owner, speed, looping); });
  }

  auto AnimationPreview::advance(double seconds) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation preview was moved");
    }

    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Preview elapsed seconds require [0,1]");
    }

    auto valid = m_impl->check(true);

    if (!valid) {
      return valid;
    }

    auto state = m_impl->m_players.state(m_impl->m_owner);

    if (!state) {
      return std::unexpected(state.error());
    }

    const bool layerPlaying = std::ranges::any_of(state->layers, [](const auto& layer) {
      return layer.playback.active && !layer.playback.paused
             && ((layer.playback.playing && layer.playback.speed != 0) || layer.playback.fade);
    });

    if (state->playback.paused || (!state->controller && !state->playback.playing && !layerPlaying)
        || seconds == 0) {
      return {};
    }

    auto result = m_impl->apply([&] { return m_impl->m_players.advance(seconds); });

    if (!result) {
      m_impl->m_error = result.error();
    }

    return result;
  }

  auto AnimationPreview::capturePose(const Core::FileSystem& assets, std::string_view name,
                                     std::uint64_t generation) const
      -> Core::Result<AnimationPose> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation preview was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto state = m_impl->m_players.state(m_impl->m_owner);

    if (!state) {
      return std::unexpected(state.error());
    }

    if (m_impl->m_error || generation != m_impl->m_generation
        || (!m_impl->m_bindPose && !state->playback.paused)) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Capture requires the current generation of a paused, healthy preview");
    }

    try {
      const std::array selected{m_impl->m_owner};
      auto captured = World::extractSceneContent(*m_impl->m_scene, selected, name);

      if (!captured) {
        return Core::fail(captured.error().code,
                          "Capture animation pose: " + captured.error().message);
      }

      const auto owner = std::ranges::find(captured->entities, m_impl->m_owner,
                                           &World::ContentEntityMapping::source);

      if (owner == captured->entities.end()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Captured animation owner is missing");
      }

      const auto value = owner->target.value;
      auto environment = m_impl->m_scene->environment();
      auto retained = environment ? captured->scene.setEnvironment(*environment)
                                  : Core::Result<void>{std::unexpected(environment.error())};

      if (!retained) {
        return std::unexpected(retained.error());
      }

      auto players = captured->scene.animationPlayers().value();

      for (auto& player : players) {
        player.properties.enabled = false;
        player.properties.playOnStart = false;
        auto frozen = captured->scene.setAnimationPlayer(player.entity, player.properties);

        if (!frozen) {
          return std::unexpected(frozen.error());
        }
      }

      auto valid = Assets::validateSceneSkins(assets, captured->scene);

      if (!valid) {
        return Core::fail(valid.error().code,
                          "Captured pose skin validation: " + valid.error().message);
      }

      return AnimationPose{std::move(captured->scene), value, generation};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Capture animation pose: " + std::string(error.what()));
    }
  }

  auto AnimationPreview::stats() const -> Core::Result<AnimationPreviewStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation preview was moved");
    }

    auto valid = m_impl->check();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    try {
      auto state = m_impl->m_players.state(m_impl->m_owner);
      return state ? Core::Result<AnimationPreviewStats>{{m_impl->m_owner.value, m_impl->m_source,
                                                          m_impl->m_generation, std::move(*state),
                                                          m_impl->m_clips, m_impl->m_error,
                                                          m_impl->m_bindPose, m_impl->m_draft}}
                   : std::unexpected(state.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Read animation preview: " + std::string(error.what()));
    }
  }

  auto AnimationPreview::scene() const -> Core::Result<const World::Scene*> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation preview was moved");
    }

    auto valid = m_impl->check();
    return valid ? Core::Result<const World::Scene*>{m_impl->m_scene.get()}
                 : std::unexpected(valid.error());
  }
}  // namespace Dot::Editor

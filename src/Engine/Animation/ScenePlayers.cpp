module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

module Dot.Animation.ScenePlayers;

import Dot.Animation.Files;
import Dot.Animation.ControllerFiles;
import Dot.Animation.SceneAnimator;
import Dot.Core.Paths;
import Dot.Core.Text;

namespace Dot::Animation {
  namespace {
    template <class T>
    auto take(Core::Result<T> result) -> T {
      if (!result) {
        throw result.error();
      }

      return std::move(*result);
    }
  }  // namespace

  struct ScenePlayers::Impl {
    struct Player {
      PlayerProperties properties;
      SceneAnimator animator;
      std::vector<Clip> library;
      std::uint32_t clip;
      std::vector<std::uint64_t> writers;
      std::size_t channels{}, keys{};
      double duration{};
      bool primed{};
      std::optional<std::vector<std::uint64_t>> m_destinationWriters{};
      std::optional<Controller> m_controller{};
      std::map<std::string, Motion> m_motions{};
    };

    std::shared_ptr<World::Scene> scene;
    Core::FileSystem assets;
    std::uint64_t scope;
    std::thread::id thread{std::this_thread::get_id()};
    std::map<std::uint64_t, std::shared_ptr<Player>> players;
    std::map<std::string, Clip> m_decoded;
    std::map<std::string, Controller> m_controllers;
    std::optional<std::uint64_t> m_synchronizedRevision;

    Impl(std::shared_ptr<World::Scene> value, Core::FileSystem files)
        : scene(std::move(value)), assets(std::move(files)), scope(scene->id()) {
    }

    auto motionFor(const Player& player, const Controller& controller) const
        -> Core::Result<Motion> {
      const auto* state = controller.state().value();
      const auto found = player.m_motions.find(state->name);

      if (found == player.m_motions.end()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Controller motion was not bound: " + state->name);
      }

      if (const auto* blend = std::get_if<ControllerBlend1D>(&state->motion)) {
        auto parameter = controller.parameter(blend->parameter);
        return parameter ? found->second.withValue(std::get<double>(*parameter))
                         : Core::Result<Motion>{std::unexpected(parameter.error())};
      }

      return found->second;
    }

    auto check() const -> Core::Result<void> {
      if (thread != std::this_thread::get_id() || scope != scene->id()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Animation players require owner thread and unchanged scene scope");
      }

      return {};
    }

    auto player(World::EntityId owner) const -> Core::Result<std::shared_ptr<Player>> {
      auto valid = check();

      if (!valid) {
        return std::unexpected(valid.error());
      }

      auto entity = scene->entity(owner);

      if (!entity) {
        return std::unexpected(entity.error());
      }

      const auto found = players.find(owner.value);

      if (!entity->animation || !entity->animation->enabled || found == players.end()) {
        return Core::fail(Core::ErrorCode::NotFound, "Entity has no synchronized animation player");
      }

      if (found->second->properties != *entity->animation) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Animation description needs synchronization");
      }

      return found->second;
    }

    auto writersFor(const Player& player, std::uint32_t clip) const -> std::vector<std::uint64_t> {
      std::map<std::uint32_t, std::uint64_t> targets;

      for (const auto& target : player.properties.targets) {
        targets.emplace(target.node, target.entity);
      }

      std::set<std::uint64_t> unique;

      for (const auto& track : player.library[clip].description().value()->tracks) {
        unique.insert(targets.at(track.node));
      }

      for (const auto& layer : player.properties.layers) {
        for (const auto& channel : layer.mask) {
          unique.insert(targets.at(channel.node));
        }
      }

      return {unique.begin(), unique.end()};
    }

    /** All storage is prepared before pose mutation; retirement only swaps/releases.
     * 姿态变更前准备全部存储，过渡退休只交换/释放，不再分配。 */
    auto retireWriters(Player& player) -> void {
      if (player.m_destinationWriters && !player.animator.state().value().transition) {
        player.writers.swap(*player.m_destinationWriters);
        player.m_destinationWriters.reset();
      }
    }

    /** Physics owns a character's root and ancestor transforms, while bones/meshes
     * below that root remain independently animatable.
     * 物理拥有角色根及祖先变换，根下的骨骼/网格仍可独立动画。 */
    auto characterAuthority() const -> Core::Result<std::set<std::uint64_t>> {
      auto owners = scene->characterControllers();

      if (!owners) {
        return std::unexpected(owners.error());
      }

      std::set<std::uint64_t> result;

      for (auto current : *owners) {
        while (result.insert(current.value).second) {
          auto entity = scene->entity(current);

          if (!entity) {
            return std::unexpected(entity.error());
          }

          if (!entity->parent) {
            break;
          }

          current = *entity->parent;
        }
      }

      return result;
    }

    auto authority(std::uint64_t owner, const std::vector<std::uint64_t>& writers)
        -> Core::Result<void> {
      auto protectedTransforms = characterAuthority();

      if (!protectedTransforms) {
        return std::unexpected(protectedTransforms.error());
      }

      for (const auto id : writers) {
        if (protectedTransforms->contains(id)) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Animation clip conflicts with character controller authority");
        }

        for (const auto& [other, player] : players) {
          if (other != owner && std::ranges::find(player->writers, id) != player->writers.end()) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Animation clip conflicts with another player");
          }
        }

        auto current = scene->findEntity(id);

        while (current) {
          auto entity = scene->entity(*current);

          if (!entity) {
            return std::unexpected(entity.error());
          }

          if (entity->rigidBody) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Animation clip conflicts with rigid-body authority");
          }

          if (!entity->parent) {
            break;
          }

          current = *entity->parent;
        }

        if (!current) {
          return std::unexpected(current.error());
        }
      }

      return {};
    }

    auto controlled(World::EntityId owner) -> Core::Result<std::shared_ptr<Player>> {
      auto valid = check();

      if (!valid) {
        return std::unexpected(valid.error());
      }

      auto entity = scene->entity(owner);

      if (!entity) {
        return std::unexpected(entity.error());
      }

      auto ready = synchronize();
      return ready ? player(owner) : std::unexpected(ready.error());
    }

    auto synchronize(std::span<const ClipAsset> decoded = {},
                     std::span<const ControllerAsset> controllers = {}, bool prepared = false)
        -> Core::Result<void> {
      try {
        if (decoded.size() > 1024 || controllers.size() > 1024) {
          return Core::fail(Core::ErrorCode::Unavailable, "Prepared animation budget exceeded");
        }

        const auto revision = take(scene->animationBindingRevision());

        /** Only the scene's committed binding/authority inputs permit reuse. Runtime clip/fade
         * controls validate their new writers separately; prepared inputs always receive full
         * validation, including duplicate/unused assets, even for an unchanged scene.
         * 仅场景已提交的绑定/写入权输入允许复用；运行期换片/淡化另行校验新增写入者。
         * 准备输入始终完整校验，包括场景未变时的重复/无用资源。 */
        if (!prepared && decoded.empty() && controllers.empty()
            && revision != std::numeric_limits<std::uint64_t>::max()
            && m_synchronizedRevision == revision) {
          return {};
        }

        std::map<std::string, Clip> clips;
        std::map<std::string, Controller> graphs;
        std::set<std::string> usedClips, usedControllers;

        for (const auto& asset : decoded) {
          if (asset.asset.size() > 1024 || !asset.asset.starts_with("assets/")
              || !asset.asset.ends_with(".dotanimation")
              || !Core::isPortableRelativePath(asset.asset) || !Core::isValidUtf8(asset.asset)
              || !asset.value.description() || !clips.emplace(asset.asset, asset.value).second) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Invalid/duplicate prepared animation clip");
          }
        }

        for (const auto& asset : controllers) {
          if (asset.asset.size() > 1024 || !asset.asset.starts_with("assets/")
              || !asset.asset.ends_with(".dotcontroller")
              || !Core::isPortableRelativePath(asset.asset) || !Core::isValidUtf8(asset.asset)
              || !asset.value.description() || !graphs.emplace(asset.asset, asset.value).second) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Invalid/duplicate prepared animation controller");
          }
        }

        const auto descriptions = take(scene->animationPlayers());
        const auto protectedTransforms = take(characterAuthority());
        std::map<std::uint64_t, std::shared_ptr<Player>> candidate;
        std::set<std::uint64_t> writers;
        std::size_t channels{}, keys{}, references{};

        for (const auto& description : descriptions) {
          const auto& properties = description.properties;

          if (!properties.enabled) {
            continue;
          }

          if (properties.clips.size() > 1024 - references) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Scene animation exceeds 1024 clip references");
          }

          references += properties.clips.size();
          std::shared_ptr<Player> player;
          const auto previous = players.find(description.entity.value);

          if (previous != players.end() && previous->second->properties == properties) {
            player = previous->second;
          } else {
            std::vector<NodeBinding> bindings;
            std::map<std::uint32_t, std::uint64_t> targets;

            for (const auto& target : properties.targets) {
              bindings.push_back({target.node, take(scene->findEntity(target.entity))});
              targets.emplace(target.node, target.entity);
            }

            std::vector<Clip> library;
            std::size_t playerChannels{}, playerKeys{};

            for (std::size_t i = 0; i < properties.clips.size(); ++i) {
              const auto& path = properties.clips[i];
              const auto& available = prepared ? clips : m_decoded;
              const auto assetFound = available.find(path);

              if (prepared && assetFound == available.end()) {
                return Core::fail(Core::ErrorCode::NotFound,
                                  "Animation clip is not prepared: " + path);
              }

              if (prepared) {
                usedClips.insert(path);
              }

              auto clip = assetFound != available.end()
                              ? assetFound->second
                              : take(loadClip(assets, std::filesystem::path(std::u8string(
                                                          path.begin(), path.end()))));
              const auto& tracks = take(clip.description())->tracks;
              playerChannels += tracks.size();

              for (const auto& track : tracks) {
                if (!targets.contains(track.node) || track.keys.size() > 1000000 - playerKeys) {
                  return Core::fail(Core::ErrorCode::InvalidArgument,
                                    "Animation library target/key budget invalid");
                }

                playerKeys += track.keys.size();
              }

              if (playerChannels > 30000) {
                return Core::fail(Core::ErrorCode::Unavailable,
                                  "Animation library channel budget exceeded");
              }

              library.push_back(std::move(clip));
            }

            std::optional<Controller> controller;
            auto selectedIndex = properties.clip;
            double speed = properties.speed;
            bool looping = properties.looping;
            std::set<std::uint64_t> selectedWriters;
            std::map<std::string, Motion> motions;

            if (properties.controller) {
              const auto& path = *properties.controller;
              const auto& available = prepared ? graphs : m_controllers;
              const auto assetFound = available.find(path);

              if (prepared && assetFound == available.end()) {
                return Core::fail(Core::ErrorCode::NotFound,
                                  "Animation controller is not prepared: " + path);
              }

              if (prepared) {
                usedControllers.insert(path);
              }

              auto loaded = assetFound != available.end()
                                ? assetFound->second
                                : take(loadController(assets, std::filesystem::path(std::u8string(
                                                                  path.begin(), path.end()))));
              controller = take(loaded.reset());
              const auto* definition = take(controller->description());

              for (const auto& state : definition->states) {
                const auto resolve = [&](const std::string& asset) -> std::size_t {
                  const auto found = std::ranges::find(properties.clips, asset);

                  if (found == properties.clips.end()) {
                    throw Core::fail(Core::ErrorCode::InvalidArgument,
                                     "Controller " + path + " state " + state.name
                                         + " clip is absent from player library: " + asset)
                        .error();
                  }

                  const auto index = static_cast<std::size_t>(found - properties.clips.begin());

                  for (const auto& track : take(library[index].description())->tracks) {
                    selectedWriters.insert(targets.at(track.node));
                  }

                  return index;
                };

                if (const auto* clip = std::get_if<std::string>(&state.motion)) {
                  motions.emplace(state.name, take(Motion::single(library[resolve(*clip)])));
                } else {
                  const auto& blend = std::get<ControllerBlend1D>(state.motion);
                  std::vector<MotionPoint> points;

                  for (const auto& point : blend.points) {
                    points.push_back(
                        {point.threshold, library[resolve(point.clip)], point.phaseOffset});
                  }

                  const auto value = std::get<double>(take(controller->parameter(blend.parameter)));
                  motions.emplace(state.name, take(Motion::blend1D(std::move(points), value)));
                }
              }

              const auto* initial = take(controller->state());
              const auto& firstClip
                  = std::holds_alternative<std::string>(initial->motion)
                        ? std::get<std::string>(initial->motion)
                        : std::get<ControllerBlend1D>(initial->motion).points.front().clip;
              selectedIndex = static_cast<std::uint32_t>(
                  std::ranges::find(properties.clips, firstClip) - properties.clips.begin());
              speed = initial->speed;
              looping = initial->looping;
            }

            const auto& selected = library[selectedIndex];
            auto selectedMotion = controller ? motions.at(controller->state().value()->name)
                                             : take(Motion::single(selected));
            const auto duration = take(selectedMotion.duration());
            std::vector<LayerDefinition> layers;

            for (const auto& layer : properties.layers) {
              layers.push_back({layer.name, library[layer.clip], layer.mask, layer.weight,
                                layer.speed, layer.looping, layer.playOnStart, layer.mode,
                                layer.referenceTime});
            }

            auto animator
                = take(SceneAnimator::bindMotion(*scene, selectedMotion, bindings, layers));
            auto configured = animator.configure(speed, looping);

            if (!configured) {
              return configured;
            }

            if (!properties.playOnStart) {
              auto paused = animator.pause();

              if (!paused) {
                return paused;
              }
            }

            for (const auto& track : take(selected.description())->tracks) {
              selectedWriters.insert(targets.at(track.node));
            }

            for (const auto& layer : properties.layers) {
              for (const auto& channel : layer.mask) {
                selectedWriters.insert(targets.at(channel.node));
              }
            }

            player = std::make_shared<Player>(
                properties, std::move(animator), std::move(library), selectedIndex,
                std::vector<std::uint64_t>(selectedWriters.begin(), selectedWriters.end()),
                playerChannels, playerKeys, duration, false, std::nullopt, std::move(controller),
                std::move(motions));
          }

          if (player->channels > 30000 - channels || player->keys > 1000000 - keys) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Scene animation library budget exceeded");
          }

          channels += player->channels;
          keys += player->keys;

          for (const auto id : player->writers) {
            if (protectedTransforms.contains(id)) {
              return Core::fail(Core::ErrorCode::Unavailable,
                                "Animation conflicts with character controller authority");
            }

            if (!writers.insert(id).second) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Animation players write the same entity");
            }

            auto current = take(scene->findEntity(id));

            while (true) {
              const auto entity = take(scene->entity(current));

              if (entity.rigidBody) {
                return Core::fail(Core::ErrorCode::Unavailable,
                                  "Animation and rigid-body authority conflict at entity "
                                      + std::to_string(entity.id.value));
              }

              if (!entity.parent) {
                break;
              }

              current = *entity.parent;
            }
          }

          candidate.emplace(description.entity.value, std::move(player));
        }

        if (prepared
            && (clips.size() != usedClips.size() || graphs.size() != usedControllers.size())) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Unused prepared animation assets");
        }

        players.swap(candidate);
        m_synchronizedRevision = revision;
        return {};
      } catch (const Core::Error& error) {
        return Core::fail(error.code, "Synchronize animation players: " + error.message);
      } catch (const std::exception& error) {
        return Core::fail(Core::ErrorCode::External,
                          "Synchronize animation players: " + std::string(error.what()));
      }
    }
  };

  ScenePlayers::ScenePlayers(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
  }

  ScenePlayers::ScenePlayers(ScenePlayers&&) noexcept = default;
  auto ScenePlayers::operator=(ScenePlayers&&) noexcept -> ScenePlayers& = default;
  ScenePlayers::~ScenePlayers() = default;

  auto ScenePlayers::create(std::shared_ptr<World::Scene> scene, const Core::FileSystem& assets,
                            std::span<const ClipAsset> decoded,
                            std::span<const ControllerAsset> controllers)
      -> Core::Result<ScenePlayers> {
    return createPlayers(std::move(scene), assets, decoded, controllers, false);
  }

  auto ScenePlayers::createPrepared(std::shared_ptr<World::Scene> scene,
                                    const Core::FileSystem& assets,
                                    std::span<const ClipAsset> decoded,
                                    std::span<const ControllerAsset> controllers)
      -> Core::Result<ScenePlayers> {
    return createPlayers(std::move(scene), assets, decoded, controllers, true);
  }

  auto ScenePlayers::createPlayers(std::shared_ptr<World::Scene> scene,
                                   const Core::FileSystem& assets,
                                   std::span<const ClipAsset> decoded,
                                   std::span<const ControllerAsset> controllers, bool prepared)
      -> Core::Result<ScenePlayers> {
    if (!scene) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Animation players require a scene");
    }

    if (decoded.size() > 1024 || controllers.size() > 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Decoded animation asset budget exceeded");
    }

    try {
      auto descriptions = scene->animationPlayers();

      if (!descriptions) {
        return std::unexpected(descriptions.error());
      }

      auto impl = std::make_unique<Impl>(std::move(scene), assets);
      std::set<std::string> used;
      std::set<std::string> usedControllers;
      std::size_t references{};

      for (const auto& player : *descriptions) {
        if (!player.properties.enabled) {
          continue;
        }

        if (player.properties.clips.size() > 1024 - references) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Scene animation exceeds 1024 clip references");
        }

        if (player.properties.controller) {
          usedControllers.insert(*player.properties.controller);
        }

        references += player.properties.clips.size();
        used.insert(player.properties.clips.begin(), player.properties.clips.end());
      }

      for (const auto& value : decoded) {
        if (!used.contains(value.asset) || value.asset.size() > 1024
            || !value.asset.starts_with("assets/") || !value.asset.ends_with(".dotanimation")
            || !Core::isPortableRelativePath(value.asset) || !Core::isValidUtf8(value.asset)
            || !value.value.description()
            || !impl->m_decoded.emplace(value.asset, value.value).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid, duplicate or unused decoded animation asset");
        }
      }

      for (const auto& value : controllers) {
        if (!usedControllers.contains(value.asset) || !value.value.description()
            || !impl->m_controllers.emplace(value.asset, value.value).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid, duplicate or unused decoded controller asset");
        }
      }

      if (prepared
          && (impl->m_decoded.size() != used.size()
              || impl->m_controllers.size() != usedControllers.size())) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Prepared animation assets do not cover enabled libraries");
      }

      auto ready = impl->synchronize();

      if (ready && prepared) {
        impl->m_decoded.clear();
        impl->m_controllers.clear();
      }

      return ready ? Core::Result<ScenePlayers>{ScenePlayers(std::move(impl))}
                   : std::unexpected(ready.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create animation players: " + std::string(error.what()));
    }
  }

  auto ScenePlayers::advance(double seconds) -> Core::Result<void> {
    return advancePlayers(seconds, {}, {}, false);
  }

  auto ScenePlayers::advancePrepared(double seconds, std::span<const ClipAsset> decoded,
                                     std::span<const ControllerAsset> controllers)
      -> Core::Result<void> {
    return advancePlayers(seconds, decoded, controllers, true);
  }

  auto ScenePlayers::advancePlayers(double seconds, std::span<const ClipAsset> decoded,
                                    std::span<const ControllerAsset> controllers, bool prepared)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation players were moved");
    }

    auto valid = m_impl->check();

    if (!valid) {
      return valid;
    }

    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation delta must be finite seconds [0,1]");
    }

    auto ready = m_impl->synchronize(decoded, controllers, prepared);

    if (!ready) {
      return ready;
    }

    for (auto& [owner, player] : m_impl->players) {
      if (player->m_controller) {
        auto playback = player->animator.state().value();
        auto currentMotion = m_impl->motionFor(*player, *player->m_controller);

        if (!currentMotion) {
          return std::unexpected(currentMotion.error());
        }

        const auto actualDuration = player->animator.motion()->duration().value();
        const auto nextDuration = currentMotion->duration().value();
        const auto time = !player->primed && playback.speed < 0 ? nextDuration
                          : actualDuration > 0 ? playback.time / actualDuration * nextDuration
                                               : 0;
        auto candidate
            = player->m_controller->advanced(playback.paused ? 0 : seconds, time, nextDuration);

        if (!candidate) {
          return std::unexpected(candidate.error());
        }

        const bool changed = candidate->transitionCount().value()
                             != player->m_controller->transitionCount().value();
        Core::Result<void> advanced;

        if (changed || (!player->primed && !playback.paused)) {
          const auto* state = candidate->state().value();
          auto selectedMotion = m_impl->motionFor(*player, *candidate);

          if (!selectedMotion) {
            return std::unexpected(selectedMotion.error());
          }

          const auto fade = changed ? candidate->lastTransition().value()->fadeSeconds : 0;
          advanced = player->animator.transitionMotionAndAdvance(
              *m_impl->scene, {std::move(*selectedMotion), fade, state->speed, state->looping},
              seconds);
        } else if (!playback.paused && seconds > 0) {
          advanced = player->animator.retuneAndAdvance(*m_impl->scene, std::move(*currentMotion),
                                                       seconds);
        } else {
          advanced = player->animator.advance(*m_impl->scene, seconds);
        }

        if (!advanced) {
          return Core::fail(advanced.error().code, "Animation controller player "
                                                       + std::to_string(owner) + ": "
                                                       + advanced.error().message);
        }

        player->m_controller = std::move(*candidate);
        player->duration = player->animator.motion()->duration().value();
        player->primed = player->primed || !playback.paused;
        continue;
      }

      if (!player->primed && player->properties.playOnStart) {
        auto initial = player->animator.seek(
            *m_impl->scene, player->animator.state().value().speed < 0 ? player->duration : 0);

        if (!initial) {
          return initial;
        }
      }

      player->primed = true;
      auto advanced = player->animator.advance(*m_impl->scene, seconds);

      if (!advanced) {
        return Core::fail(advanced.error().code, "Animation player " + std::to_string(owner) + ": "
                                                     + advanced.error().message);
      }

      m_impl->retireWriters(*player);
    }

    return {};
  }

  auto ScenePlayers::stats() const -> Core::Result<ScenePlayerStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation players were moved");
    }

    auto valid = m_impl->check();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    ScenePlayerStats result{m_impl->players.size(), 0, 0};

    for (const auto& [owner, player] : m_impl->players) {
      result.channels += player->channels;
      result.keys += player->keys;
    }

    return result;
  }

  auto ScenePlayers::state(World::EntityId owner) const -> Core::Result<ScenePlayerState> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation players were moved");
    }

    auto result = m_impl->player(owner);

    if (!result) {
      return std::unexpected(result.error());
    }

    try {
      const auto& player = **result;
      auto playback = player.animator.state();
      auto layers = player.animator.layers();

      if (!playback || !layers) {
        return std::unexpected(!playback ? playback.error() : layers.error());
      }

      std::optional<SceneControllerState> controller;
      auto selectedIndex = player.clip;

      if (player.m_controller) {
        auto snapshot = player.m_controller->snapshot();

        if (!snapshot) {
          return std::unexpected(snapshot.error());
        }

        controller = {*player.properties.controller,
                      player.m_controller->description().value()->name, std::move(*snapshot)};
        const auto actual = player.animator.motion().value();
        const auto* state = player.m_controller->state().value();

        if (const auto* blend = std::get_if<ControllerBlend1D>(&state->motion)) {
          const auto duration = actual.duration().value();
          SceneBlendState observed{
              blend->parameter,
              actual.value().value(),
              duration > 0 ? playback->time / duration : 0,
              playback->transition ? playback->transition->elapsed / playback->transition->duration
                                   : 1,
              player.primed,
              {}};
          const auto samples = actual.samples(playback->time, playback->looping).value();
          double dominant = -1;

          for (const auto& sample : samples) {
            const auto& point = blend->points[sample.point];
            const auto index
                = static_cast<std::uint32_t>(std::ranges::find(player.properties.clips, point.clip)
                                             - player.properties.clips.begin());
            observed.samples.push_back({index, point.clip, sample.weight, sample.time,
                                        player.library[index].duration().value(),
                                        point.phaseOffset});

            if (sample.weight > dominant) {
              dominant = sample.weight;
              selectedIndex = index;
            }
          }

          controller->blend = std::move(observed);
        } else {
          selectedIndex = static_cast<std::uint32_t>(
              std::ranges::find(player.properties.clips, std::get<std::string>(state->motion))
              - player.properties.clips.begin());
        }
      }

      return ScenePlayerState{selectedIndex,
                              player.properties.clips[selectedIndex],
                              player.library[selectedIndex].description().value()->name,
                              player.duration,
                              *playback,
                              std::move(*layers),
                              std::move(controller)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Inspect animation player: " + std::string(error.what()));
    }
  }

  auto ScenePlayers::play(World::EntityId owner, std::optional<std::uint32_t> clip, bool restart)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation players were moved");
    }

    auto result = m_impl->controlled(owner);

    if (!result) {
      return std::unexpected(result.error());
    }

    auto& player = **result;
    if (player.m_controller) {
      if (clip || restart) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Controller owns base clip; use resetController");
      }

      if (!player.primed) {
        const auto* state = player.m_controller->state().value();
        auto selectedMotion = m_impl->motionFor(player, *player.m_controller);

        if (!selectedMotion) {
          return std::unexpected(selectedMotion.error());
        }

        auto initial = player.animator.transitionMotionAndAdvance(
            *m_impl->scene, {std::move(*selectedMotion), 0, state->speed, state->looping}, 0);

        if (initial) {
          player.duration = player.animator.motion()->duration().value();
          player.primed = true;
        }

        return initial;
      }

      return player.animator.play();
    }

    const auto selected = clip.value_or(player.clip);

    if (selected >= player.library.size()) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Animation clip index exceeds library");
    }

    if (selected != player.clip) {
      try {
        auto writers = m_impl->writersFor(player, selected);
        auto valid = m_impl->authority(owner.value, writers);

        if (!valid) {
          return valid;
        }

        auto switched = player.animator.setClip(*m_impl->scene, player.library[selected]);

        if (!switched) {
          return switched;
        }

        player.clip = selected;
        player.duration = player.library[selected].duration().value();
        player.writers = std::move(writers);
        player.m_destinationWriters.reset();
      } catch (const std::exception& error) {
        return Core::fail(Core::ErrorCode::External,
                          "Select animation clip: " + std::string(error.what()));
      }
    } else {
      if (restart || !player.primed) {
        const auto state = player.animator.state().value();
        auto rewound = player.animator.seek(*m_impl->scene, state.speed < 0 ? player.duration : 0);

        if (!rewound) {
          return rewound;
        }
      }

      auto played = player.animator.play();

      if (!played) {
        return played;
      }
    }

    player.primed = true;
    m_impl->retireWriters(player);
    return {};
  }

  auto ScenePlayers::crossFade(World::EntityId owner, std::uint32_t clip, double seconds)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation players were moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (!std::isfinite(seconds) || seconds < 0 || seconds > 86400) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation fade seconds require [0,86400]");
    }

    auto controlled = m_impl->controlled(owner);

    if (!controlled) {
      return std::unexpected(controlled.error());
    }

    auto& player = **controlled;

    if (player.m_controller) {
      return Core::fail(Core::ErrorCode::InvalidState, "Controller owns base clip transitions");
    }

    if (clip >= player.library.size()) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Animation clip index exceeds library");
    }

    try {
      auto destination = m_impl->writersFor(player, clip);
      std::vector<std::uint64_t> active = destination;
      std::optional<std::vector<std::uint64_t>> pending;

      if (seconds > 0) {
        active.insert(active.end(), player.writers.begin(), player.writers.end());
        std::ranges::sort(active);
        active.erase(std::unique(active.begin(), active.end()), active.end());
        pending = std::move(destination);
      }

      auto authorized = m_impl->authority(owner.value, active);

      if (!authorized) {
        return authorized;
      }

      const auto duration = player.library[clip].duration().value();
      auto faded = player.animator.crossFade(*m_impl->scene, player.library[clip], seconds);

      if (!faded) {
        return faded;
      }

      player.clip = clip;
      player.duration = duration;
      player.writers = std::move(active);
      player.m_destinationWriters = std::move(pending);
      player.primed = true;
      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Cross-fade animation clip: " + std::string(error.what()));
    }
  }

  auto ScenePlayers::pause(World::EntityId owner) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation players were moved");
    }

    auto player = m_impl->controlled(owner);

    if (!player) {
      return std::unexpected(player.error());
    }

    auto paused = (*player)->animator.pause();

    if (paused && !(*player)->m_controller) {
      (*player)->primed = true;
    }

    return paused;
  }

  auto ScenePlayers::stop(World::EntityId owner) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation players were moved");
    }

    auto player = m_impl->controlled(owner);

    if (!player) {
      return std::unexpected(player.error());
    }

    if ((*player)->m_controller) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Controller owns base playback; use resetController");
    }

    auto stopped = (*player)->animator.stop(*m_impl->scene);

    if (stopped) {
      (*player)->primed = true;
      m_impl->retireWriters(**player);
    }

    return stopped;
  }

  auto ScenePlayers::seek(World::EntityId owner, double seconds, bool pause) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation players were moved");
    }

    auto player = m_impl->controlled(owner);

    if (!player) {
      return std::unexpected(player.error());
    }

    if ((*player)->m_controller) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Controller owns base playback; use resetController");
    }

    auto positioned = (*player)->animator.seek(*m_impl->scene, seconds, pause);

    if (positioned) {
      (*player)->primed = true;
      m_impl->retireWriters(**player);
    }

    return positioned;
  }

  auto ScenePlayers::configure(World::EntityId owner, double speed, bool looping)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation players were moved");
    }

    auto player = m_impl->controlled(owner);
    if (!player) {
      return std::unexpected(player.error());
    }

    if ((*player)->m_controller) {
      return Core::fail(Core::ErrorCode::InvalidState, "Controller owns base speed and looping");
    }

    return (*player)->animator.configure(speed, looping);
  }

  auto ScenePlayers::parameters(World::EntityId owner,
                                std::span<const ControllerParameter> parameters)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation players were moved");
    }

    auto result = m_impl->controlled(owner);

    if (!result) {
      return std::unexpected(result.error());
    }

    auto& player = **result;

    if (!player.m_controller) {
      return Core::fail(Core::ErrorCode::InvalidState, "Player has no animation controller");
    }

    auto candidate = player.m_controller->withParameters(parameters);

    if (!candidate) {
      return std::unexpected(candidate.error());
    }

    player.m_controller = std::move(*candidate);
    return {};
  }

  auto ScenePlayers::resetController(World::EntityId owner) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation players were moved");
    }

    auto result = m_impl->controlled(owner);

    if (!result) {
      return std::unexpected(result.error());
    }

    auto& player = **result;

    if (!player.m_controller) {
      return Core::fail(Core::ErrorCode::InvalidState, "Player has no animation controller");
    }

    auto candidate = player.m_controller->reset();

    if (!candidate) {
      return std::unexpected(candidate.error());
    }

    const auto* state = candidate->state().value();
    auto selectedMotion = m_impl->motionFor(player, *candidate);

    if (!selectedMotion) {
      return std::unexpected(selectedMotion.error());
    }

    auto applied = player.animator.transitionMotionAndAdvance(
        *m_impl->scene,
        {std::move(*selectedMotion), 0, state->speed, state->looping,
         player.animator.state().value().paused},
        0);

    if (!applied) {
      return applied;
    }

    player.duration = player.animator.motion()->duration().value();
    player.m_controller = std::move(*candidate);
    player.primed = true;
    return {};
  }

  auto ScenePlayers::restorePlayback(World::EntityId owner, PlaybackState state,
                                     std::span<const LayerPlayback> layers) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation players were moved");
    }

    auto player = m_impl->controlled(owner);

    if (!player) {
      return std::unexpected(player.error());
    }

    if ((*player)->m_controller) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Controller owns base playback; use resetController");
    }

    auto restored = (*player)->animator.restorePlayback(*m_impl->scene, state, layers);

    if (restored) {
      (*player)->primed = true;
      m_impl->retireWriters(**player);
    }

    return restored;
  }

  auto ScenePlayers::controlLayer(World::EntityId owner, std::string_view name,
                                  const LayerControl& operation) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation players were moved");
    }

    auto player = m_impl->controlled(owner);

    if (!player) {
      return std::unexpected(player.error());
    }

    /** Layer-only controls preserve the pending base directional start.
     * 仅层控制保留尚未应用的基础片段方向起点。 */
    return (*player)->animator.controlLayer(*m_impl->scene, name, operation);
  }
}  // namespace Dot::Animation

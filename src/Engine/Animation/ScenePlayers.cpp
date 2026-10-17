module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

module Dot.Animation.ScenePlayers;

import Dot.Animation.Files;
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
    };

    std::shared_ptr<World::Scene> scene;
    Core::FileSystem assets;
    std::uint64_t scope;
    std::thread::id thread{std::this_thread::get_id()};
    std::map<std::uint64_t, std::shared_ptr<Player>> players;
    std::map<std::string, Clip> m_decoded;

    Impl(std::shared_ptr<World::Scene> value, Core::FileSystem files)
        : scene(std::move(value)), assets(std::move(files)), scope(scene->id()) {
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

    auto synchronize() -> Core::Result<void> {
      try {
        const auto descriptions = take(scene->animationPlayers());
        const auto protectedTransforms = take(characterAuthority());
        std::map<std::uint64_t, std::shared_ptr<Player>> candidate;
        std::set<std::uint64_t> writers;
        std::size_t channels{}, keys{};

        for (const auto& description : descriptions) {
          const auto& properties = description.properties;

          if (!properties.enabled) {
            continue;
          }

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
              const auto decoded = m_decoded.find(path);
              auto clip = decoded != m_decoded.end()
                              ? decoded->second
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

            const auto& selected = library[properties.clip];
            const auto duration = take(selected.duration());
            auto animator = take(SceneAnimator::bind(*scene, selected, bindings));
            auto configured = animator.configure(properties.speed, properties.looping);

            if (!configured) {
              return configured;
            }

            if (!properties.playOnStart) {
              auto paused = animator.pause();

              if (!paused) {
                return paused;
              }
            }

            std::set<std::uint64_t> selectedWriters;

            for (const auto& track : take(selected.description())->tracks) {
              selectedWriters.insert(targets.at(track.node));
            }

            player = std::make_shared<Player>(
                properties, std::move(animator), std::move(library), properties.clip,
                std::vector<std::uint64_t>(selectedWriters.begin(), selectedWriters.end()),
                playerChannels, playerKeys, duration, false);
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

        players.swap(candidate);
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
                            std::span<const ClipAsset> decoded) -> Core::Result<ScenePlayers> {
    return createPlayers(std::move(scene), assets, decoded, false);
  }

  auto ScenePlayers::createPrepared(std::shared_ptr<World::Scene> scene,
                                    const Core::FileSystem& assets,
                                    std::span<const ClipAsset> decoded)
      -> Core::Result<ScenePlayers> {
    return createPlayers(std::move(scene), assets, decoded, true);
  }

  auto ScenePlayers::createPlayers(std::shared_ptr<World::Scene> scene,
                                   const Core::FileSystem& assets,
                                   std::span<const ClipAsset> decoded, bool prepared)
      -> Core::Result<ScenePlayers> {
    if (!scene) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Animation players require a scene");
    }

    if (decoded.size() > 1024) {
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
      std::size_t references{};

      for (const auto& player : *descriptions) {
        if (!player.properties.enabled) {
          continue;
        }

        if (player.properties.clips.size() > 1024 - references) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Scene animation exceeds 1024 clip references");
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

      if (prepared && impl->m_decoded.size() != used.size()) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Prepared animation assets do not cover enabled libraries");
      }

      auto ready = impl->synchronize();

      if (ready && prepared) {
        impl->m_decoded.clear();
      }

      return ready ? Core::Result<ScenePlayers>{ScenePlayers(std::move(impl))}
                   : std::unexpected(ready.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create animation players: " + std::string(error.what()));
    }
  }

  auto ScenePlayers::advance(double seconds) -> Core::Result<void> {
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

    auto ready = m_impl->synchronize();

    if (!ready) {
      return ready;
    }

    for (auto& [owner, player] : m_impl->players) {
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

    const auto& player = **result;
    auto playback = player.animator.state();

    if (!playback) {
      return std::unexpected(playback.error());
    }

    return ScenePlayerState{player.clip, player.properties.clips[player.clip],
                            player.library[player.clip].description().value()->name,
                            player.duration, *playback};
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

    if (paused) {
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

    auto stopped = (*player)->animator.stop(*m_impl->scene);

    if (stopped) {
      (*player)->primed = true;
      m_impl->retireWriters(**player);
    }

    return stopped;
  }

  auto ScenePlayers::seek(World::EntityId owner, double seconds) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation players were moved");
    }

    auto player = m_impl->controlled(owner);

    if (!player) {
      return std::unexpected(player.error());
    }

    auto positioned = (*player)->animator.seek(*m_impl->scene, seconds);

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
    return player ? (*player)->animator.configure(speed, looping) : std::unexpected(player.error());
  }
}  // namespace Dot::Animation

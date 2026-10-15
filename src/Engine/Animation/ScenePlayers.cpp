module;

#include <cmath>
#include <exception>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

module Dot.Animation.ScenePlayers;

import Dot.Animation.Files;
import Dot.Animation.SceneAnimator;

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
      std::vector<std::uint64_t> writers;
      std::size_t channels{}, keys{};
      double duration{};
      bool primed{};
    };

    std::shared_ptr<World::Scene> scene;
    Core::FileSystem assets;
    std::uint64_t scope;
    std::thread::id thread{std::this_thread::get_id()};
    std::map<std::uint64_t, std::shared_ptr<Player>> players;

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

    auto synchronize() -> Core::Result<void> {
      try {
        const auto descriptions = take(scene->animationPlayers());
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

            Clip selected;
            std::size_t playerChannels{}, playerKeys{};

            for (std::size_t i = 0; i < properties.clips.size(); ++i) {
              const auto& path = properties.clips[i];
              auto clip = take(
                  loadClip(assets, std::filesystem::path(std::u8string(path.begin(), path.end()))));
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

              if (i == properties.clip) {
                selected = std::move(clip);
              }
            }

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
                properties, std::move(animator),
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

  auto ScenePlayers::create(std::shared_ptr<World::Scene> scene, const Core::FileSystem& assets)
      -> Core::Result<ScenePlayers> {
    if (!scene) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Animation players require a scene");
    }

    auto impl = std::make_unique<Impl>(std::move(scene), assets);
    auto ready = impl->synchronize();
    return ready ? Core::Result<ScenePlayers>{ScenePlayers(std::move(impl))}
                 : std::unexpected(ready.error());
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
        auto initial = player->animator.seek(*m_impl->scene,
                                             player->properties.speed < 0 ? player->duration : 0);

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
}  // namespace Dot::Animation

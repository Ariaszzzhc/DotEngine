module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

module Dot.Animation.SceneAnimator;

import Dot.Math;
import Dot.World.Scene;

namespace Dot::Animation {
  struct SceneAnimator::Impl {
    struct Target {
      World::EntityId entity;
      World::Transform rest;
    };

    Clip clip;
    std::uint64_t scope{};
    std::map<std::uint32_t, Target> targets;
    PlaybackState playback;
    std::thread::id owner{std::this_thread::get_id()};

    auto check() const -> Core::Result<void> {
      return owner == std::this_thread::get_id()
                 ? Core::Result<void>{}
                 : Core::fail(Core::ErrorCode::InvalidState,
                              "Scene animator requires owner thread");
    }

    auto checkScene(const World::Scene& scene) const -> Core::Result<void> {
      auto checked = check();

      if (!checked) {
        return checked;
      }

      if (scene.id() != scope) {
        return Core::fail(Core::ErrorCode::InvalidState, "Animation scene scope changed");
      }

      for (const auto& [node, target] : targets) {
        auto entity = scene.transform(target.entity);

        if (!entity) {
          return Core::fail(entity.error().code, "Animation node " + std::to_string(node) + ": "
                                                     + entity.error().message);
        }
      }

      return {};
    }

    auto apply(World::Scene& scene, double time, bool restore = false,
               const Clip* replacement = nullptr) -> Core::Result<void> {
      auto checked = checkScene(scene);

      if (!checked) {
        return checked;
      }

      try {
        const auto& next = replacement ? *replacement : clip;
        std::vector<ChannelSample> samples;

        if (!restore) {
          auto sampled = next.sample(time);

          if (!sampled) {
            return std::unexpected(sampled.error());
          }

          samples = std::move(*sampled);
        }

        std::map<std::uint32_t, World::Transform> poses;
        const auto assign = [&](const Track& track, Math::Vec4 value) {
          auto [entry, inserted] = poses.try_emplace(track.node);

          if (inserted) {
            entry->second = scene.transform(targets.at(track.node).entity).value();
          }

          auto& pose = entry->second;

          if (track.path == Path::Translation) {
            pose.position = {value.x, value.y, value.z};
          } else if (track.path == Path::Scale) {
            pose.scale = {value.x, value.y, value.z};
          } else {
            pose.rotation = {value.x, value.y, value.z, value.w};
          }
        };

        if (restore || replacement) {
          for (const auto& track : clip.description().value()->tracks) {
            const auto& rest = targets.at(track.node).rest;
            const auto value
                = track.path == Path::Translation
                      ? Math::Vec4{rest.position.x, rest.position.y, rest.position.z, 0}
                  : track.path == Path::Scale
                      ? Math::Vec4{rest.scale.x, rest.scale.y, rest.scale.z, 0}
                      : Math::Vec4{rest.rotation.x, rest.rotation.y, rest.rotation.z,
                                   rest.rotation.w};
            assign(track, value);
          }
        }

        if (!restore) {
          const auto& tracks = next.description().value()->tracks;

          for (std::size_t i = 0; i < tracks.size(); ++i) {
            assign(tracks[i], samples[i].value);
          }
        }

        std::vector<World::TransformUpdate> updates;
        updates.reserve(poses.size());

        for (const auto& [node, pose] : poses) {
          updates.push_back({targets.at(node).entity, pose});
        }

        return scene.setTransforms(updates);
      } catch (const std::exception& error) {
        return Core::fail(Core::ErrorCode::External,
                          "Apply scene animation: " + std::string(error.what()));
      }
    }
  };

  SceneAnimator::SceneAnimator(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
  }

  SceneAnimator::SceneAnimator(SceneAnimator&&) noexcept = default;
  auto SceneAnimator::operator=(SceneAnimator&&) noexcept -> SceneAnimator& = default;
  SceneAnimator::~SceneAnimator() = default;

  auto SceneAnimator::bind(const World::Scene& scene, Clip clip,
                           std::span<const NodeBinding> bindings) -> Core::Result<SceneAnimator> {
    auto description = clip.description();

    if (!description) {
      return std::unexpected(description.error());
    }

    if (bindings.empty() || bindings.size() > 10000) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation requires 1..10000 node bindings");
    }

    try {
      auto impl = std::make_unique<Impl>();
      impl->scope = scene.id();
      std::unordered_set<std::uint64_t> entities;
      std::map<std::uint32_t, Impl::Target> nodes;

      for (const auto& binding : bindings) {
        auto transform = scene.transform(binding.entity);

        if (!transform) {
          return std::unexpected(transform.error());
        }

        if (binding.node >= 10000 || nodes.contains(binding.node)
            || !entities.insert(binding.entity.value).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Animation bindings require unique node/entity pairs");
        }

        nodes.emplace(binding.node, Impl::Target{binding.entity, *transform});
      }

      for (const auto& track : (*description)->tracks) {
        auto node = nodes.find(track.node);

        if (node == nodes.end()) {
          return Core::fail(Core::ErrorCode::NotFound,
                            "Animation target node is not bound: " + std::to_string(track.node));
        }
      }

      impl->targets = std::move(nodes);
      impl->clip = std::move(clip);
      return SceneAnimator(std::move(impl));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Bind scene animation: " + std::string(error.what()));
    }
  }

  auto SceneAnimator::state() const -> Core::Result<PlaybackState> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene animator was moved");
    }

    auto checked = m_impl->check();
    return checked ? Core::Result<PlaybackState>{m_impl->playback}
                   : std::unexpected(checked.error());
  }

  auto SceneAnimator::configure(double speed, bool looping) -> Core::Result<void> {
    auto checked = state();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!std::isfinite(speed) || std::abs(speed) > 16) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation speed must be within [-16,16]");
    }

    m_impl->playback.speed = speed;
    m_impl->playback.looping = looping;
    return {};
  }

  auto SceneAnimator::play(bool restart) -> Core::Result<void> {
    auto checked = state();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    m_impl->playback.playing = true;

    if (restart) {
      m_impl->playback.time = 0;
    }

    return {};
  }

  auto SceneAnimator::pause() -> Core::Result<void> {
    auto checked = state();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    m_impl->playback.playing = false;
    return {};
  }

  auto SceneAnimator::seek(World::Scene& scene, double time) -> Core::Result<void> {
    auto checked = state();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!std::isfinite(time) || time < 0 || time > m_impl->clip.duration().value()) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Animation seek exceeds clip duration");
    }

    auto applied = m_impl->apply(scene, time);

    if (applied) {
      m_impl->playback.time = time;
    }

    return applied;
  }

  auto SceneAnimator::advance(World::Scene& scene, double seconds) -> Core::Result<void> {
    auto checked = state();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!std::isfinite(seconds) || seconds < 0 || seconds > 86400) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation elapsed seconds must be within [0,86400]");
    }

    auto live = m_impl->checkScene(scene);

    if (!live || !checked->playing) {
      return live;
    }

    if (seconds == 0 || checked->speed == 0) {
      return m_impl->apply(scene, checked->time);
    }

    const auto duration = m_impl->clip.duration().value();
    auto next = *checked;
    next.time += seconds * next.speed;

    if (duration == 0) {
      next.time = 0;
      next.playing = next.looping;
    } else if (next.looping) {
      next.time = std::fmod(next.time, duration);

      if (next.time < 0) {
        next.time += duration;
      }
    } else {
      if (duration == 0 || (next.speed > 0 && next.time >= duration)
          || (next.speed < 0 && next.time <= 0)) {
        next.playing = false;
      }

      next.time = std::clamp(next.time, 0.0, duration);
    }

    auto applied = m_impl->apply(scene, next.time);

    if (applied) {
      m_impl->playback = next;
    }

    return applied;
  }

  auto SceneAnimator::stop(World::Scene& scene) -> Core::Result<void> {
    auto checked = state();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto applied = m_impl->apply(scene, 0, true);

    if (applied) {
      m_impl->playback.time = 0;
      m_impl->playback.playing = false;
    }

    return applied;
  }

  auto SceneAnimator::setClip(World::Scene& scene, Clip clip) -> Core::Result<void> {
    auto current = state();

    if (!current) {
      return std::unexpected(current.error());
    }

    auto description = clip.description();

    if (!description) {
      return std::unexpected(description.error());
    }

    for (const auto& track : (*description)->tracks) {
      if (!m_impl->targets.contains(track.node)) {
        return Core::fail(Core::ErrorCode::NotFound, "Animation replacement target is not bound: "
                                                         + std::to_string(track.node));
      }
    }

    const auto time = current->speed < 0 ? clip.duration().value() : 0;
    auto applied = m_impl->apply(scene, time, false, &clip);

    if (applied) {
      m_impl->clip = std::move(clip);
      m_impl->playback.time = time;
      m_impl->playback.playing = true;
    }

    return applied;
  }

}  // namespace Dot::Animation

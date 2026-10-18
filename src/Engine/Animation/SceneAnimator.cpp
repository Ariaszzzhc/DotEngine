module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

module Dot.Animation.SceneAnimator;

import Dot.Math;
import Dot.World.Scene;

namespace Dot::Animation {
  namespace {
    using Channel = std::pair<std::uint32_t, Path>;
    using Values = std::map<Channel, Math::Vec4>;

    auto channelValue(const World::Transform& pose, Path path) -> Math::Vec4 {
      if (path == Path::Translation) {
        return {pose.position.x, pose.position.y, pose.position.z, 0};
      }

      if (path == Path::Scale) {
        return {pose.scale.x, pose.scale.y, pose.scale.z, 0};
      }

      return {pose.rotation.x, pose.rotation.y, pose.rotation.z, pose.rotation.w};
    }

    auto clockAfter(PlaybackState state, double duration, double seconds) -> PlaybackState {
      if (!state.playing || seconds == 0 || state.speed == 0) {
        return state;
      }

      state.time += seconds * state.speed;

      if (duration == 0) {
        state.time = 0;
        state.playing = state.looping;
      } else if (state.looping) {
        state.time = std::fmod(state.time, duration);

        if (state.time < 0) {
          state.time += duration;
        }
      } else {
        if ((state.speed > 0 && state.time >= duration) || (state.speed < 0 && state.time <= 0)) {
          state.playing = false;
        }

        state.time = std::clamp(state.time, 0.0, duration);
      }

      return state;
    }

    auto mixed(Path path, Math::Vec4 first, Math::Vec4 last, double weight)
        -> Core::Result<Math::Vec4> {
      if (weight == 0 || weight == 1) {
        return weight == 0 ? first : last;
      }

      if (path == Path::Rotation) {
        auto rotation = Math::slerp({first.x, first.y, first.z, first.w},
                                    {last.x, last.y, last.z, last.w}, weight);
        return rotation
                   ? Core::Result<Math::Vec4>{{rotation->x, rotation->y, rotation->z, rotation->w}}
                   : std::unexpected(rotation.error());
      }

      return Math::Vec4{static_cast<float>(std::lerp(double(first.x), double(last.x), weight)),
                        static_cast<float>(std::lerp(double(first.y), double(last.y), weight)),
                        static_cast<float>(std::lerp(double(first.z), double(last.z), weight)), 0};
    }

    /** Add local offsets to the current ordered pose, never to the last output.
     * Rotation is current * slerp(identity, inverse(reference) * sample, weight);
     * scale uses current * lerp(1, sample/reference, weight), preserving signs.
     * Reference sampling validates invertible scales before configuration publication.
     * 向当前有序候选姿态叠加局部偏移，绝不反馈上一输出；旋转后乘最短参考差，缩放乘
     * 加权比例并保留符号。配置发布前采样验证参考缩放可逆；最终场景事务验证合成结果。 */
    auto additive(Path path, Math::Vec4 current, Math::Vec4 sample, Math::Vec4 reference,
                  double weight) -> Core::Result<Math::Vec4> {
      if (path == Path::Rotation) {
        auto delta
            = Math::multiplyRotations({-reference.x, -reference.y, -reference.z, reference.w},
                                      {sample.x, sample.y, sample.z, sample.w});
        auto weighted = delta ? Math::slerp({}, *delta, weight)
                              : Core::Result<Math::Quat>{std::unexpected(delta.error())};
        auto result = weighted ? Math::multiplyRotations(
                                     {current.x, current.y, current.z, current.w}, *weighted)
                               : Core::Result<Math::Quat>{std::unexpected(weighted.error())};
        return result ? Core::Result<Math::Vec4>{{result->x, result->y, result->z, result->w}}
                      : std::unexpected(result.error());
      }

      const auto component = [&](double base, double value, double origin) {
        return path == Path::Translation ? base + weight * (value - origin)
                                         : base * std::lerp(1.0, value / origin, weight);
      };
      const auto x = component(current.x, sample.x, reference.x);
      const auto y = component(current.y, sample.y, reference.y);
      const auto z = component(current.z, sample.z, reference.z);
      constexpr auto limit = std::numeric_limits<float>::max();

      if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || std::abs(x) > limit
          || std::abs(y) > limit || std::abs(z) > limit) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Additive transform overflow");
      }

      return Math::Vec4{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), 0};
    }
  }  // namespace

  struct SceneAnimator::Impl {
    struct Target {
      World::EntityId entity;
      World::Transform rest;
    };

    struct Transition {
      std::optional<Clip> source;
      double sourceTime{};
      bool sourcePlaying{};
      double sourceSpeed{1};
      bool sourceLooping{true};
      Values origin;
    };

    Clip clip;
    Layers m_layers;
    Values m_baseValues;
    std::optional<Transition> m_transition;
    bool m_applied{};
    bool m_staging{};
    std::vector<World::TransformUpdate> m_stagedTransforms;
    std::map<std::uint32_t, World::Transform> m_stagedPoses;
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

    auto restValues() const -> Values {
      Values values;

      for (const auto& track : clip.description().value()->tracks) {
        values[{track.node, track.path}] = channelValue(targets.at(track.node).rest, track.path);
      }

      for (const auto& channel : m_layers.channels().value()) {
        values[{channel.node, channel.path}]
            = channelValue(targets.at(channel.node).rest, channel.path);
      }

      if (m_transition) {
        for (const auto& [channel, value] : m_transition->origin) {
          values[channel] = channelValue(targets.at(channel.first).rest, channel.second);
        }
      }

      return values;
    }

    auto apply(World::Scene& scene, double time, bool restore = false,
               const Clip* replacement = nullptr, const Transition* fade = nullptr,
               double sourceTime = 0, double weight = 0, const Layers* layerCandidate = nullptr)
        -> Core::Result<void> {
      auto checked = checkScene(scene);

      if (!checked) {
        return checked;
      }

      try {
        const auto& next = replacement ? *replacement : clip;
        Values values;

        if (restore || replacement || (m_transition && !fade)) {
          values = restValues();
        }

        for (const auto& channel : m_layers.channels().value()) {
          values.try_emplace(std::pair{channel.node, channel.path},
                             channelValue(targets.at(channel.node).rest, channel.path));
        }

        if (!restore) {
          auto sampled = next.sample(time);

          if (!sampled) {
            return std::unexpected(sampled.error());
          }

          if (fade) {
            values = fade->origin;
            Values destination;

            for (const auto& [channel, value] : values) {
              destination[channel] = channelValue(targets.at(channel.first).rest, channel.second);
            }

            if (fade->source && weight > 0 && weight < 1) {
              auto outgoing = fade->source->sample(sourceTime);

              if (!outgoing) {
                return std::unexpected(outgoing.error());
              }

              for (const auto& sample : *outgoing) {
                values[{sample.node, sample.path}] = sample.value;
              }
            }

            for (const auto& sample : *sampled) {
              destination[{sample.node, sample.path}] = sample.value;
            }

            for (auto& [channel, value] : values) {
              auto result = mixed(channel.second, value, destination.at(channel), weight);

              if (!result) {
                return std::unexpected(result.error());
              }

              value = *result;
            }
          } else {
            for (const auto& sample : *sampled) {
              values[{sample.node, sample.path}] = sample.value;
            }
          }
        }

        Values base;

        if (!m_layers.channels()->empty()) {
          base = values;
        }

        if (!restore) {
          auto overlay = (layerCandidate ? *layerCandidate : m_layers).sample();

          if (!overlay) {
            return std::unexpected(overlay.error());
          }

          for (const auto& layer : *overlay) {
            const auto key = std::pair{layer.channel.node, layer.channel.path};
            auto value = layer.reference
                             ? additive(layer.channel.path, values.at(key), layer.channel.value,
                                        *layer.reference, layer.weight)
                             : mixed(layer.channel.path, values.at(key), layer.channel.value,
                                     layer.weight);

            if (!value) {
              return Core::fail(value.error().code, "Animation layer node "
                                                        + std::to_string(layer.channel.node) + ": "
                                                        + value.error().message);
            }

            values[key] = *value;
          }
        }

        std::map<std::uint32_t, World::Transform> poses;

        for (const auto& [channel, value] : values) {
          const auto [node, path] = channel;
          auto [entry, inserted] = poses.try_emplace(node);

          if (inserted) {
            const auto staged = m_stagedPoses.find(node);
            entry->second = m_staging && staged != m_stagedPoses.end()
                                ? staged->second
                                : scene.transform(targets.at(node).entity).value();
          }

          auto& pose = entry->second;

          if (path == Path::Translation) {
            pose.position = {value.x, value.y, value.z};
          } else if (path == Path::Scale) {
            pose.scale = {value.x, value.y, value.z};
          } else {
            pose.rotation = {value.x, value.y, value.z, value.w};
          }
        }

        /** Later staging passes retain prior departed-channel restores, including
         * entities absent from the incoming clip. No intermediate scene writes.
         * 后续暂存须保留先前退出通道的复位，含新片段未写的实体；不发布中间场景变更。 */
        if (m_staging) {
          for (const auto& [node, pose] : m_stagedPoses) {
            poses.try_emplace(node, pose);
          }
        }

        std::vector<World::TransformUpdate> updates;
        updates.reserve(poses.size());

        for (const auto& [node, pose] : poses) {
          updates.push_back({targets.at(node).entity, pose});
        }

        auto result = m_staging ? Core::Result<void>{} : scene.setTransforms(updates);

        if (result) {
          m_applied = true;
          m_baseValues = std::move(base);

          if (m_staging) {
            m_stagedTransforms = std::move(updates);
            m_stagedPoses = std::move(poses);
          }
        }

        return result;
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
                           std::span<const NodeBinding> bindings,
                           std::span<const LayerDefinition> definitions)
      -> Core::Result<SceneAnimator> {
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

      auto layers = Layers::create(definitions);

      if (!layers) {
        return std::unexpected(layers.error());
      }

      for (const auto& channel : layers->channels().value()) {
        if (!nodes.contains(channel.node)) {
          return Core::fail(Core::ErrorCode::NotFound, "Animation layer target is not bound");
        }
      }

      impl->m_layers = std::move(*layers);
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

  auto SceneAnimator::layers() const -> Core::Result<std::vector<LayerSnapshot>> {
    auto checked = state();
    return checked ? m_impl->m_layers.states() : std::unexpected(checked.error());
  }

  auto SceneAnimator::controlLayer(World::Scene& scene, std::string_view name,
                                   const LayerControl& operation) -> Core::Result<void> {
    auto checked = state();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto candidate = m_impl->m_layers.updated(name, operation);

    if (!candidate) {
      return std::unexpected(candidate.error());
    }

    auto same = m_impl->m_layers.contributesLike(*candidate);

    if (!same) {
      return std::unexpected(same.error());
    }

    if (*same) {
      auto live = m_impl->checkScene(scene);

      if (!live) {
        return live;
      }

      m_impl->m_layers = std::move(*candidate);
      return {};
    }

    const auto* fade = m_impl->m_transition ? &*m_impl->m_transition : nullptr;
    const double weight
        = checked->transition ? checked->transition->elapsed / checked->transition->duration : 0;
    auto applied = m_impl->apply(scene, checked->time, false, nullptr, fade,
                                 fade ? fade->sourceTime : 0, weight, &*candidate);

    if (applied) {
      m_impl->m_layers = std::move(*candidate);
    }

    return applied;
  }

  auto SceneAnimator::restorePlayback(World::Scene& scene, PlaybackState playback,
                                      std::span<const LayerPlayback> clocks) -> Core::Result<void> {
    auto checked = state();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!std::isfinite(playback.time) || playback.time < 0
        || playback.time > m_impl->clip.duration().value() || !std::isfinite(playback.speed)
        || std::abs(playback.speed) > 16 || playback.transition
        || (playback.paused && playback.playing)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid animation playback snapshot");
    }

    auto layers = m_impl->m_layers.restored(clocks);

    if (!layers) {
      return std::unexpected(layers.error());
    }

    auto applied = m_impl->apply(scene, playback.time, false, nullptr, nullptr, 0, 0, &*layers);

    if (applied) {
      m_impl->playback = playback;
      m_impl->m_layers = std::move(*layers);
      m_impl->m_transition.reset();
    }

    return applied;
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

    if (m_impl->m_transition) {
      m_impl->m_transition->sourceSpeed = speed;
      m_impl->m_transition->sourceLooping = looping;
    }

    return {};
  }

  auto SceneAnimator::play(bool restart) -> Core::Result<void> {
    auto checked = state();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    m_impl->playback.playing = true;
    m_impl->playback.paused = false;

    if (restart) {
      m_impl->playback.time = 0;
      m_impl->m_applied = false;
    }

    return {};
  }

  auto SceneAnimator::pause() -> Core::Result<void> {
    auto checked = state();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    m_impl->playback.playing = false;
    m_impl->playback.paused = true;
    return {};
  }

  auto SceneAnimator::seek(World::Scene& scene, double time, bool pause) -> Core::Result<void> {
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

      if (pause) {
        m_impl->playback.playing = false;
        m_impl->playback.paused = true;
      }

      m_impl->playback.transition.reset();
      m_impl->m_transition.reset();
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

    if (!live || checked->paused || (!checked->playing && !m_impl->m_layers.running().value())) {
      return live;
    }

    auto layers = m_impl->m_layers.advanced(seconds);

    if (!layers) {
      return std::unexpected(layers.error());
    }

    auto next = clockAfter(*checked, m_impl->clip.duration().value(), seconds);
    double sourceTime{};
    bool sourcePlaying{};
    double weight{};

    if (m_impl->m_transition) {
      const auto& fade = *m_impl->m_transition;
      next.transition->elapsed
          = std::min(next.transition->duration, next.transition->elapsed + seconds);
      weight = next.transition->elapsed / next.transition->duration;

      if (fade.source) {
        const auto source = clockAfter(
            {fade.sourceTime, fade.sourceSpeed, fade.sourceLooping, fade.sourcePlaying},
            fade.source->duration().value(), seconds);
        sourceTime = source.time;
        sourcePlaying = source.playing;
      }

      next.playing = next.playing || weight < 1;
    }

    auto applied = m_impl->apply(scene, next.time, false, nullptr,
                                 m_impl->m_transition ? &*m_impl->m_transition : nullptr,
                                 sourceTime, weight, &*layers);

    if (applied) {
      if (m_impl->m_transition && weight < 1) {
        m_impl->m_transition->sourceTime = sourceTime;
        m_impl->m_transition->sourcePlaying = sourcePlaying;
      } else {
        m_impl->m_transition.reset();
        next.transition.reset();
      }

      m_impl->playback = next;
      m_impl->m_layers = std::move(*layers);
    }

    return applied;
  }

  auto SceneAnimator::transitionAndAdvance(World::Scene& scene, PlaybackTransition transition,
                                           double seconds) -> Core::Result<void> {
    auto current = state();

    if (!current) {
      return std::unexpected(current.error());
    }

    if (!std::isfinite(seconds) || seconds < 0 || seconds > 86400) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid animation transition elapsed time");
    }

    try {
      SceneAnimator candidate(std::make_unique<Impl>(*m_impl));
      candidate.m_impl->m_staging = true;
      auto configured = candidate.configure(transition.speed, transition.looping);

      if (!configured) {
        return configured;
      }

      auto entered = candidate.crossFade(scene, std::move(transition.clip), transition.fadeSeconds);

      if (!entered) {
        return entered;
      }

      if (candidate.m_impl->m_transition) {
        candidate.m_impl->m_transition->sourceSpeed = current->speed;
        candidate.m_impl->m_transition->sourceLooping = current->looping;
      }

      auto advanced = candidate.advance(scene, seconds);

      if (!advanced) {
        return advanced;
      }

      if (transition.paused) {
        auto paused = candidate.pause();

        if (!paused) {
          return paused;
        }
      }

      auto published = scene.setTransforms(candidate.m_impl->m_stagedTransforms);

      if (published) {
        candidate.m_impl->m_staging = false;
        candidate.m_impl->m_stagedTransforms.clear();
        candidate.m_impl->m_stagedPoses.clear();
        m_impl.swap(candidate.m_impl);
      }

      return published;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare animation transition: " + std::string(error.what()));
    }
  }

  auto SceneAnimator::stop(World::Scene& scene) -> Core::Result<void> {
    auto checked = state();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto layers = m_impl->m_layers.stopped();

    if (!layers) {
      return std::unexpected(layers.error());
    }

    auto applied = m_impl->apply(scene, 0, true);

    if (applied) {
      m_impl->m_layers = std::move(*layers);
      m_impl->playback.paused = true;
      m_impl->playback.time = 0;
      m_impl->playback.playing = false;
      m_impl->playback.transition.reset();
      m_impl->m_transition.reset();
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
      m_impl->playback.paused = false;
      m_impl->playback.transition.reset();
      m_impl->m_transition.reset();
    }

    return applied;
  }

  auto SceneAnimator::crossFade(World::Scene& scene, Clip clip, double seconds)
      -> Core::Result<void> {
    auto current = state();

    if (!current) {
      return std::unexpected(current.error());
    }

    if (!std::isfinite(seconds) || seconds < 0 || seconds > 86400) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation fade seconds require [0,86400]");
    }

    if (seconds == 0) {
      return setClip(scene, std::move(clip));
    }

    auto description = clip.description();
    auto live = m_impl->checkScene(scene);

    if (!description || !live) {
      return !description ? Core::Result<void>{std::unexpected(description.error())} : live;
    }

    try {
      Impl::Transition fade;
      fade.origin = m_impl->restValues();

      for (const auto& track : (*description)->tracks) {
        if (!m_impl->targets.contains(track.node)) {
          return Core::fail(Core::ErrorCode::NotFound, "Animation fade target is not bound");
        }

        fade.origin[{track.node, track.path}] = {};
      }

      for (auto& [channel, value] : fade.origin) {
        const auto base = m_impl->m_baseValues.find(channel);
        value
            = !m_impl->m_layers.channels()->empty() && base != m_impl->m_baseValues.end()
                  ? base->second
                  : channelValue(scene.transform(m_impl->targets.at(channel.first).entity).value(),
                                 channel.second);
      }

      if (!m_impl->m_transition && m_impl->m_applied && current->playing) {
        fade.source = m_impl->clip;
        fade.sourceTime = current->time;
        fade.sourcePlaying = true;
        fade.sourceSpeed = current->speed;
        fade.sourceLooping = current->looping;
      }

      const auto time = current->speed < 0 ? clip.duration().value() : 0;
      auto result = m_impl->apply(scene, time, false, &clip, &fade, fade.sourceTime, 0);

      if (result) {
        m_impl->clip = std::move(clip);
        m_impl->m_transition = std::move(fade);
        m_impl->playback.time = time;
        m_impl->playback.playing = true;
        m_impl->playback.paused = false;
        m_impl->playback.transition = TransitionProgress{0, seconds};
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Start animation fade: " + std::string(error.what()));
    }
  }

}  // namespace Dot::Animation

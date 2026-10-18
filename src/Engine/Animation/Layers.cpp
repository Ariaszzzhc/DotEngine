module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.Animation.Layers;

import Dot.Core.Text;
import Dot.Math;

namespace Dot::Animation {
  auto layerModeName(LayerMode mode) noexcept -> std::string_view {
    switch (mode) {
      case LayerMode::Override:
        return "override";
      case LayerMode::Additive:
        return "additive";
    }

    return "unknown";
  }

  auto decodeLayerControl(std::string_view source) -> Core::Result<LayerControl> {
    namespace Codec = Serialization::Detail;

    if (source.empty() || source.size() > 4096 || !Core::isValidUtf8(source)
        || source.find('\0') != std::string_view::npos) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid layer control bytes");
    }

    try {
      const auto value = Codec::parseJson(source, 8);
      const auto action = value.at("action").get<std::string>();

      if (action == "play") {
        Codec::requireFields(value, {"action", "restart"});
        return PlayLayer{value["restart"].get<bool>()};
      }

      if (action == "pause" || action == "stop") {
        Codec::requireFields(value, {"action"});
        return action == "pause" ? LayerControl{PauseLayer{}} : LayerControl{StopLayer{}};
      }

      if (action == "fade") {
        Codec::requireFields(value, {"action", "weight", "seconds", "stop"});

        if (!value["weight"].is_number() || !value["seconds"].is_number()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Layer fade requires numeric weight/seconds");
        }

        return FadeLayer{value["weight"].get<double>(), value["seconds"].get<double>(),
                         value["stop"].get<bool>()};
      }

      const auto field = action == "seek" ? "seconds" : action == "weight" ? "weight" : "speed";
      Codec::requireFields(
          value, action == "configure"
                     ? std::initializer_list<std::string_view>{"action", "speed", "looping"}
                     : std::initializer_list<std::string_view>{"action", field});

      if (!value[field].is_number()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Layer control requires a number");
      }

      const auto number = value[field].get<double>();

      if (!std::isfinite(number)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Layer control requires a finite number");
      }

      if (action == "seek") {
        return SeekLayer{number};
      }

      if (action == "weight") {
        return WeightLayer{number};
      }

      if (action == "configure") {
        return ConfigureLayer{number, value["looping"].get<bool>()};
      }

      return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown animation layer control");
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Decode layer control: " + std::string(error.what()));
    }
  }

  auto validLayerName(std::string_view name) -> bool {
    const auto letter = [](char value) {
      return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
    };
    return !name.empty() && name.size() <= 64 && letter(name.front())
           && std::ranges::all_of(name, [&](char value) {
                return letter(value) || (value >= '0' && value <= '9') || value == '_'
                       || value == '-';
              });
  }

  struct Layers::Data {
    struct Track {
      std::size_t index;
      double weight;
      std::optional<Math::Vec4> reference;
    };

    struct Entry {
      std::string name;
      Clip clip;
      std::vector<Track> tracks;
      double duration;
      LayerMode mode;
      double referenceTime;
    };

    std::vector<Entry> entries;
    std::vector<LayerChannel> channels;
  };

  Layers::Layers(std::shared_ptr<const Data> data, std::vector<LayerPlayback> states)
      : m_data(std::move(data)), m_states(std::move(states)) {
  }

  Layers::Layers(const Layers&) = default;

  auto Layers::operator=(const Layers& other) -> Layers& {
    if (this != &other) {
      Layers candidate(other);
      m_data.swap(candidate.m_data);
      m_states.swap(candidate.m_states);
    }

    return *this;
  }

  Layers::Layers(Layers&&) noexcept = default;

  auto Layers::operator=(Layers&& other) noexcept -> Layers& {
    if (this != &other) {
      m_data = std::move(other.m_data);
      m_states = std::move(other.m_states);
    }

    return *this;
  }

  Layers::~Layers() = default;

  auto Layers::create(std::span<const LayerDefinition> definitions) -> Core::Result<Layers> {
    if (definitions.size() > 8) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation supports at most eight layers");
    }

    try {
      auto data = std::make_shared<Data>();
      std::vector<LayerPlayback> states;
      std::set<std::string> names;
      std::map<std::pair<std::uint32_t, Path>, double> channels;
      std::size_t count{};

      for (const auto& definition : definitions) {
        if (!validLayerName(definition.name) || !names.insert(definition.name).second
            || definition.mask.empty() || definition.mask.size() > 30000 - count
            || !std::isfinite(definition.weight) || definition.weight < 0 || definition.weight > 1
            || !std::isfinite(definition.speed) || std::abs(definition.speed) > 16
            || (definition.mode != LayerMode::Override && definition.mode != LayerMode::Additive)
            || !std::isfinite(definition.referenceTime) || definition.referenceTime < 0
            || (definition.mode == LayerMode::Override && definition.referenceTime != 0)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid animation layer definition");
        }

        auto description = definition.clip.description();

        if (!description) {
          return std::unexpected(description.error());
        }

        Data::Entry entry{
            definition.name, definition.clip,         {}, definition.clip.duration().value(),
            definition.mode, definition.referenceTime};

        if (definition.referenceTime > entry.duration) {
          return Core::fail(
              Core::ErrorCode::InvalidArgument,
              "Animation layer " + definition.name + " reference exceeds clip duration");
        }

        std::map<std::pair<std::uint32_t, Path>, std::size_t> tracks;

        for (std::size_t i = 0; i < (*description)->tracks.size(); ++i) {
          const auto& track = (*description)->tracks[i];
          tracks.emplace(std::pair{track.node, track.path}, i);
        }

        std::set<std::pair<std::uint32_t, Path>> unique;

        for (const auto& channel : definition.mask) {
          const auto key = std::pair{channel.node, channel.path};
          const auto track = tracks.find(key);

          if (!std::isfinite(channel.weight) || channel.weight < 0 || channel.weight > 1
              || track == tracks.end() || !unique.insert(key).second) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Animation layer " + definition.name + " mask at node "
                                  + std::to_string(channel.node)
                                  + " requires a unique existing channel and weight [0,1]");
          }

          std::optional<Math::Vec4> reference;

          if (definition.mode == LayerMode::Additive) {
            auto sampled = definition.clip.sampleTrack(track->second, definition.referenceTime);

            if (!sampled) {
              return Core::fail(
                  sampled.error().code,
                  "Animation layer " + definition.name + " reference: " + sampled.error().message);
            }

            reference = sampled->value;
          }

          entry.tracks.push_back({track->second, channel.weight, reference});
          channels.emplace(key, 1);
        }

        count += definition.mask.size();
        states.push_back({definition.speed < 0 ? entry.duration : 0, definition.weight,
                          definition.speed, definition.looping, definition.playOnStart,
                          definition.playOnStart});
        data->entries.push_back(std::move(entry));
      }

      for (const auto& [key, weight] : channels) {
        data->channels.push_back({key.first, key.second, weight});
      }

      return Layers(std::move(data), std::move(states));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create animation layers: " + std::string(error.what()));
    }
  }

  auto Layers::channels() const -> Core::Result<std::span<const LayerChannel>> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation layers are empty or moved");
    }

    return m_data->channels;
  }

  auto Layers::states() const -> Core::Result<std::vector<LayerSnapshot>> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation layers are empty or moved");
    }

    try {
      std::vector<LayerSnapshot> result;

      for (std::size_t i = 0; i < m_states.size(); ++i) {
        const auto& entry = m_data->entries[i];
        result.push_back(
            {entry.name, entry.duration, m_states[i], entry.mode, entry.referenceTime});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Read animation layers: " + std::string(error.what()));
    }
  }

  auto Layers::running() const -> Core::Result<bool> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation layers are empty or moved");
    }

    return std::ranges::any_of(m_states, [](const auto& state) {
      return state.active && !state.paused && ((state.playing && state.speed != 0) || state.fade);
    });
  }

  auto Layers::contributesLike(const Layers& other) const -> Core::Result<bool> {
    if (!m_data || !other.m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation layers are empty or moved");
    }

    if (m_data != other.m_data) {
      return false;
    }

    for (std::size_t i = 0; i < m_states.size(); ++i) {
      const auto& first = m_states[i];
      const auto& last = other.m_states[i];
      const bool weighted = std::ranges::any_of(m_data->entries[i].tracks,
                                                [](const auto& track) { return track.weight > 0; });
      const bool active = first.active && first.weight > 0 && weighted;
      const bool nextActive = last.active && last.weight > 0 && weighted;

      if (active != nextActive
          || (active && (first.time != last.time || first.weight != last.weight))) {
        return false;
      }
    }

    return true;
  }

  auto Layers::updated(std::string_view name, const LayerControl& operation) const
      -> Core::Result<Layers> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation layers are empty or moved");
    }

    const auto entry = std::ranges::find(m_data->entries, name, &Data::Entry::name);

    if (entry == m_data->entries.end()) {
      return Core::fail(Core::ErrorCode::NotFound, "Animation layer does not exist");
    }

    try {
      auto next = *this;
      auto& state = next.m_states[static_cast<std::size_t>(entry - m_data->entries.begin())];
      auto result = std::visit(
          [&](const auto& value) -> Core::Result<void> {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, PlayLayer>) {
              if (value.restart) {
                state.time = state.speed < 0 ? entry->duration : 0;
              }

              state.active = state.playing = true;
              state.paused = false;
            } else if constexpr (std::is_same_v<T, PauseLayer>) {
              state.playing = false;
              state.paused = true;
            } else if constexpr (std::is_same_v<T, StopLayer>) {
              state.active = state.playing = false;
              state.time = 0;
              state.paused = true;
              state.fade.reset();
            } else if constexpr (std::is_same_v<T, SeekLayer>) {
              if (!std::isfinite(value.seconds) || value.seconds < 0
                  || value.seconds > entry->duration) {
                return Core::fail(Core::ErrorCode::InvalidArgument,
                                  "Layer seek exceeds clip duration");
              }

              state.time = value.seconds;
              state.active = true;
            } else if constexpr (std::is_same_v<T, ConfigureLayer>) {
              if (!std::isfinite(value.speed) || std::abs(value.speed) > 16) {
                return Core::fail(Core::ErrorCode::InvalidArgument,
                                  "Layer speed requires [-16,16]");
              }

              state.speed = value.speed;
              state.looping = value.looping;
            } else if constexpr (std::is_same_v<T, WeightLayer>) {
              if (!std::isfinite(value.weight) || value.weight < 0 || value.weight > 1) {
                return Core::fail(Core::ErrorCode::InvalidArgument, "Layer weight requires [0,1]");
              }

              state.weight = value.weight;
              state.fade.reset();
            } else if constexpr (std::is_same_v<T, FadeLayer>) {
              if (!std::isfinite(value.weight) || value.weight < 0 || value.weight > 1
                  || !std::isfinite(value.seconds) || value.seconds < 0 || value.seconds > 86400
                  || (value.stop && value.weight != 0)) {
                return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid layer weight fade");
              }

              if (value.seconds == 0 || state.weight == value.weight) {
                state.weight = value.weight;
                state.fade.reset();

                if (value.stop) {
                  state.active = state.playing = false;
                  state.paused = true;
                  state.time = 0;
                }
              } else {
                state.fade = LayerFade{state.weight, value.weight, 0, value.seconds, value.stop};
              }
            }

            return {};
          },
          operation);
      return result ? Core::Result<Layers>{std::move(next)} : std::unexpected(result.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Control animation layer: " + std::string(error.what()));
    }
  }

  auto Layers::advanced(double seconds) const -> Core::Result<Layers> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation layers are empty or moved");
    }

    if (!std::isfinite(seconds) || seconds < 0 || seconds > 86400) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Layer advance requires [0,86400] seconds");
    }

    try {
      auto next = *this;

      for (std::size_t i = 0; i < next.m_states.size(); ++i) {
        auto& state = next.m_states[i];
        const auto duration = m_data->entries[i].duration;

        if (!state.active || state.paused || seconds == 0) {
          continue;
        }

        if (state.fade) {
          auto& fade = *state.fade;
          fade.elapsed = std::min(fade.duration, fade.elapsed + seconds);
          state.weight = std::lerp(fade.from, fade.to, fade.elapsed / fade.duration);

          if (fade.elapsed == fade.duration) {
            if (fade.stop) {
              state.active = state.playing = false;
              state.paused = true;
              state.time = 0;
            }

            state.fade.reset();
          }
        }

        if (!state.active || !state.playing || state.speed == 0) {
          continue;
        }

        const auto time = state.time + seconds * state.speed;

        if (duration == 0) {
          state.time = 0;
          state.playing = state.looping;
        } else if (state.looping) {
          state.time = std::fmod(time, duration);

          if (state.time < 0) {
            state.time += duration;
          }
        } else {
          state.time = std::clamp(time, 0., duration);

          if ((state.speed > 0 && time >= duration) || (state.speed < 0 && time <= 0)) {
            state.playing = false;
          }
        }
      }

      return next;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Advance animation layers: " + std::string(error.what()));
    }
  }

  auto Layers::stopped() const -> Core::Result<Layers> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation layers are empty or moved");
    }

    try {
      auto next = *this;

      for (auto& state : next.m_states) {
        state.time = 0;
        state.active = state.playing = false;
        state.paused = true;
        state.fade.reset();
      }

      return next;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Stop animation layers: " + std::string(error.what()));
    }
  }

  auto Layers::restored(std::span<const LayerPlayback> states) const -> Core::Result<Layers> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation layers are empty or moved");
    }

    if (states.size() != m_states.size()) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Layer playback snapshot count mismatch");
    }

    for (std::size_t i = 0; i < states.size(); ++i) {
      const auto& state = states[i];

      if (!std::isfinite(state.time) || state.time < 0 || state.time > m_data->entries[i].duration
          || !std::isfinite(state.weight) || state.weight < 0 || state.weight > 1
          || !std::isfinite(state.speed) || std::abs(state.speed) > 16
          || (!state.active && state.playing) || (state.paused && state.playing)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid layer playback snapshot");
      }

      if (state.fade) {
        const auto& fade = *state.fade;

        if (!std::isfinite(fade.from) || fade.from < 0 || fade.from > 1 || !std::isfinite(fade.to)
            || fade.to < 0 || fade.to > 1 || !std::isfinite(fade.duration) || fade.duration <= 0
            || fade.duration > 86400 || !std::isfinite(fade.elapsed) || fade.elapsed < 0
            || fade.elapsed >= fade.duration || (fade.stop && fade.to != 0)
            || state.weight != std::lerp(fade.from, fade.to, fade.elapsed / fade.duration)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid layer fade snapshot");
        }
      }
    }

    try {
      return Layers(m_data, std::vector<LayerPlayback>(states.begin(), states.end()));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Restore animation layers: " + std::string(error.what()));
    }
  }

  auto Layers::sample() const -> Core::Result<std::vector<LayerSample>> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation layers are empty or moved");
    }

    try {
      std::vector<LayerSample> result;

      for (std::size_t i = 0; i < m_states.size(); ++i) {
        const auto& state = m_states[i];

        if (!state.active || state.weight == 0) {
          continue;
        }

        const auto& entry = m_data->entries[i];

        for (const auto& track : entry.tracks) {
          const auto effective = state.weight * track.weight;

          if (effective == 0) {
            continue;
          }

          auto value = entry.clip.sampleTrack(track.index, state.time);

          if (!value) {
            return Core::fail(value.error().code,
                              "Animation layer " + entry.name + ": " + value.error().message);
          }

          result.push_back({*value, effective, track.reference});
        }
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Sample animation layers: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Animation

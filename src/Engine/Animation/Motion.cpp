module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

module Dot.Animation.Motion;

import Dot.Math;

namespace Dot::Animation {
  namespace {
    using Channel = std::pair<std::uint32_t, Path>;

    auto valid(double value) -> bool {
      return std::isfinite(value) && std::abs(value) <= 1000000;
    }

    auto mixed(Path path, Math::Vec4 first, Math::Vec4 second, double weight)
        -> Core::Result<Math::Vec4> {
      if (weight == 0 || weight == 1) {
        return weight == 0 ? first : second;
      }

      if (path == Path::Rotation) {
        auto result = Math::slerp({first.x, first.y, first.z, first.w},
                                  {second.x, second.y, second.z, second.w}, weight);
        return result ? Core::Result<Math::Vec4>{{result->x, result->y, result->z, result->w}}
                      : std::unexpected(result.error());
      }

      return Math::Vec4{static_cast<float>(std::lerp(double(first.x), double(second.x), weight)),
                        static_cast<float>(std::lerp(double(first.y), double(second.y), weight)),
                        static_cast<float>(std::lerp(double(first.z), double(second.z), weight)),
                        0};
    }

    auto validSample(const ChannelSample& sample) -> bool {
      if (!Math::isFinite(sample.value)) {
        return false;
      }

      const auto value = sample.value;

      if (sample.path == Path::Rotation) {
        const auto length = double(value.x) * value.x + double(value.y) * value.y
                            + double(value.z) * value.z + double(value.w) * value.w;
        return std::abs(length - 1) <= .001;
      }

      return value.w == 0
             && (sample.path != Path::Scale || (value.x != 0 && value.y != 0 && value.z != 0));
    }
  }  // namespace

  struct Motion::Data {
    std::vector<MotionPoint> points;
    std::vector<MotionChannel> channels;
    std::vector<double> durations;
    bool blend{};

    auto weights(double value) const -> std::array<std::pair<std::size_t, double>, 2> {
      if (points.size() == 1 || value <= points.front().threshold) {
        return {{{0, 1}, {0, 0}}};
      }

      if (value >= points.back().threshold) {
        return {{{points.size() - 1, 1}, {0, 0}}};
      }

      const auto upper = std::upper_bound(
          points.begin(), points.end(), value,
          [](double scalar, const auto& point) { return scalar < point.threshold; });
      const auto right = static_cast<std::size_t>(upper - points.begin());
      const auto weight = (value - points[right - 1].threshold)
                          / (points[right].threshold - points[right - 1].threshold);
      return {{{right - 1, 1 - weight}, {right, weight}}};
    }
  };

  Motion::Motion(std::shared_ptr<const Data> data, double value) noexcept
      : m_data(std::move(data)), m_value(value) {
  }

  auto Motion::single(Clip clip) -> Core::Result<Motion> {
    auto description = clip.description();

    if (!description) {
      return std::unexpected(description.error());
    }

    try {
      auto data = std::make_shared<Data>();
      data->durations.push_back(clip.duration().value());

      for (const auto& track : (*description)->tracks) {
        data->channels.push_back({track.node, track.path});
      }

      data->points.push_back({0, std::move(clip)});
      return Motion{std::move(data), 0};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create single motion: " + std::string(error.what()));
    }
  }

  auto Motion::blend1D(std::vector<MotionPoint> points, double value) -> Core::Result<Motion> {
    if (points.size() < 2 || points.size() > 16 || !valid(value)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid 1D motion capacity/value");
    }

    try {
      auto data = std::make_shared<Data>();
      std::set<Channel> channels;

      for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& point = points[i];
        auto description = point.clip.description();

        if (!description || !valid(point.threshold) || !std::isfinite(point.phaseOffset)
            || std::abs(point.phaseOffset) > 1
            || (i && points[i - 1].threshold >= point.threshold)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid ordered 1D motion point");
        }

        data->durations.push_back(point.clip.duration().value());

        for (const auto& track : (*description)->tracks) {
          channels.emplace(track.node, track.path);
        }

        if (channels.size() > 30000) {
          return Core::fail(Core::ErrorCode::Unavailable, "1D motion channel budget exceeded");
        }
      }

      for (const auto& [node, path] : channels) {
        data->channels.push_back({node, path});
      }

      data->points = std::move(points);
      data->blend = true;
      return Motion{std::move(data), value};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create 1D motion: " + std::string(error.what()));
    }
  }

  auto Motion::withValue(double value) const -> Core::Result<Motion> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Motion is empty/moved");
    }

    if (!m_data->blend || !valid(value)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "1D motion value requires finite bounded scalar");
    }

    return Motion{m_data, value};
  }

  auto Motion::value() const -> Core::Result<double> {
    return m_data ? Core::Result<double>{m_value}
                  : Core::fail(Core::ErrorCode::InvalidState, "Motion is empty/moved");
  }

  auto Motion::blended() const -> Core::Result<bool> {
    return m_data ? Core::Result<bool>{m_data->blend}
                  : Core::fail(Core::ErrorCode::InvalidState, "Motion is empty/moved");
  }

  auto Motion::duration() const -> Core::Result<double> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Motion is empty/moved");
    }

    try {
      double duration{};

      for (const auto& [index, weight] : m_data->weights(m_value)) {
        duration += weight * m_data->durations[index];
      }

      return duration;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Read motion duration: " + std::string(error.what()));
    }
  }

  auto Motion::channels() const -> Core::Result<std::span<const MotionChannel>> {
    return m_data ? Core::Result<std::span<const MotionChannel>>{m_data->channels}
                  : Core::fail(Core::ErrorCode::InvalidState, "Motion is empty/moved");
  }

  auto Motion::samples(double time, bool looping) const -> Core::Result<std::vector<MotionSample>> {
    auto length = duration();

    if (!length) {
      return std::unexpected(length.error());
    }

    if (!std::isfinite(time) || time < 0 || time > *length) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Motion time exceeds current cycle");
    }

    try {
      std::vector<MotionSample> result;
      const auto phase = *length > 0 ? time / *length : 0;

      for (const auto& [index, weight] : m_data->weights(m_value)) {
        if (weight == 0) {
          continue;
        }

        const auto offset = m_data->points[index].phaseOffset;
        auto samplePhase = phase + offset;

        if (m_data->blend && looping) {
          samplePhase = std::fmod(samplePhase, 1.0);

          if (samplePhase < 0) {
            samplePhase += 1;
          }

          if (phase == 1 && offset == 0) {
            samplePhase = 1;
          }
        } else {
          samplePhase = std::clamp(samplePhase, 0.0, 1.0);
        }

        result.push_back({index, weight, samplePhase * m_data->durations[index]});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Inspect motion samples: " + std::string(error.what()));
    }
  }

  auto Motion::sample(double time, bool looping, std::span<const ChannelSample> rest) const
      -> Core::Result<std::vector<ChannelSample>> {
    if (m_data && !m_data->blend && rest.empty()) {
      if (!std::isfinite(time) || time < 0 || time > m_data->durations.front()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Motion time exceeds current cycle");
      }

      return m_data->points.front().clip.sample(time);
    }

    auto selected = samples(time, looping);

    if (!selected) {
      return std::unexpected(selected.error());
    }

    try {
      std::map<Channel, Math::Vec4> baseline;

      for (const auto& channel : rest) {
        if (!validSample(channel)
            || !baseline.emplace(Channel{channel.node, channel.path}, channel.value).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid/duplicate motion bind channel");
        }
      }

      if (baseline.size() != m_data->channels.size()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Motion requires exact bind channels");
      }

      for (const auto& channel : m_data->channels) {
        if (!baseline.contains({channel.node, channel.path})) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Motion bind channel is missing");
        }
      }

      auto first = baseline, last = baseline;

      for (std::size_t i = 0; i < selected->size(); ++i) {
        const auto& item = (*selected)[i];
        auto sampled = m_data->points[item.point].clip.sample(item.time);

        if (!sampled) {
          return std::unexpected(sampled.error());
        }

        auto& target = i == 0 ? first : last;

        for (const auto& channel : *sampled) {
          target.at({channel.node, channel.path}) = channel.value;
        }
      }

      std::vector<ChannelSample> result;
      result.reserve(baseline.size());
      const auto weight = selected->size() == 1 ? 0 : selected->back().weight;

      for (const auto& [channel, value] : first) {
        auto blended = mixed(channel.second, value, last.at(channel), weight);

        if (!blended) {
          return std::unexpected(blended.error());
        }

        ChannelSample output{channel.first, channel.second, *blended};

        if (!validSample(output)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Motion blend produced invalid transform channel");
        }

        result.push_back(output);
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Sample motion: " + std::string(error.what()));
    }
  }

  auto Motion::sameDefinition(const Motion& other) const noexcept -> bool {
    return m_data && m_data == other.m_data;
  }
}  // namespace Dot::Animation

module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

module Dot.Animation.Clip;

import Dot.Core.Text;

namespace Dot::Animation {
  namespace {
    auto components(Math::Vec4 value) -> std::array<double, 4> {
      return {value.x, value.y, value.z, value.w};
    }

    auto valueValid(Math::Vec4 value, Path path, bool tangent = false) -> bool {
      if (!Math::isFinite(value)) {
        return false;
      }

      if (path != Path::Rotation) {
        return value.w == 0
               && (tangent || path != Path::Scale
                   || (std::abs(value.x) >= 1e-6F && std::abs(value.y) >= 1e-6F
                       && std::abs(value.z) >= 1e-6F));
      }

      const auto v = components(value);
      const double length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
      return tangent || std::abs(length - 1) <= 1e-4;
    }

    auto evaluate(const Track& track, double seconds) -> Core::Result<Math::Vec4> {
      const auto& keys = track.keys;
      Math::Vec4 result;

      if (seconds <= keys.front().time) {
        result = keys.front().value;
      } else if (seconds >= keys.back().time) {
        result = keys.back().value;
      } else {
        auto right = std::ranges::upper_bound(keys, seconds, {}, &Key::time);
        const auto& first = *std::prev(right);
        const auto& second = *right;
        const double interval = double(second.time) - first.time;
        const double t = (seconds - first.time) / interval;
        auto a = components(first.value);
        auto b = components(second.value);
        std::array<double, 4> out{};

        if (track.interpolation == Interpolation::Step) {
          out = a;
        } else if (track.interpolation == Interpolation::CubicSpline) {
          const auto outgoing = components(first.outTangent);
          const auto incoming = components(second.inTangent);
          const double t2 = t * t, t3 = t2 * t;

          for (std::size_t i = 0; i < 4; ++i) {
            out[i] = (2 * t3 - 3 * t2 + 1) * a[i] + interval * (t3 - 2 * t2 + t) * outgoing[i]
                     + (-2 * t3 + 3 * t2) * b[i] + interval * (t3 - t2) * incoming[i];
          }
        } else if (track.path == Path::Rotation) {
          double dot{};

          for (std::size_t i = 0; i < 4; ++i) {
            dot += a[i] * b[i];
          }

          if (dot < 0) {
            dot = -dot;

            for (auto& value : b) {
              value = -value;
            }
          }

          dot = std::clamp(dot, 0.0, 1.0);
          const double angle = std::acos(dot);
          const double sine = std::sin(angle);
          const double left = sine > 1e-5 ? std::sin((1 - t) * angle) / sine : 1 - t;
          const double rightWeight = sine > 1e-5 ? std::sin(t * angle) / sine : t;

          for (std::size_t i = 0; i < 4; ++i) {
            out[i] = left * a[i] + rightWeight * b[i];
          }
        } else {
          for (std::size_t i = 0; i < 4; ++i) {
            out[i] = a[i] + t * (b[i] - a[i]);
          }
        }

        for (double value : out) {
          if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Animation interpolation overflow");
          }
        }

        result = {static_cast<float>(out[0]), static_cast<float>(out[1]),
                  static_cast<float>(out[2]), static_cast<float>(out[3])};
      }

      if (track.path == Path::Rotation) {
        auto normalized = Math::normalized(Math::Quat{result.x, result.y, result.z, result.w});

        if (!normalized) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Animation sampled a zero/invalid quaternion");
        }

        result = {normalized->x, normalized->y, normalized->z, normalized->w};
      }

      if (!valueValid(result, track.path)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Animation sampled an invalid transform channel");
      }

      return result;
    }
  }  // namespace

  struct Clip::Data {
    ClipDescription description;
    double duration{};
  };

  Clip::Clip(std::shared_ptr<const Data> data) : m_data(std::move(data)) {
  }

  auto Clip::create(ClipDescription description) -> Core::Result<Clip> {
    if (description.name.empty() || description.name.size() > 256
        || description.name.find('\0') != std::string::npos || !Core::isValidUtf8(description.name)
        || description.tracks.empty() || description.tracks.size() > 30000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid animation name/channel count");
    }

    try {
      std::unordered_set<std::uint64_t> pairs;
      std::size_t count{};
      double duration{};

      for (const auto& track : description.tracks) {
        if (track.node >= 10000
            || (track.path != Path::Translation && track.path != Path::Rotation
                && track.path != Path::Scale)
            || (track.interpolation != Interpolation::Step
                && track.interpolation != Interpolation::Linear
                && track.interpolation != Interpolation::CubicSpline)
            || track.keys.empty() || track.keys.size() > 1000000 - count
            || (track.interpolation == Interpolation::CubicSpline && track.keys.size() < 2)
            || !pairs.insert(std::uint64_t(track.node) * 3 + static_cast<unsigned>(track.path))
                    .second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid/duplicate animation track or key count");
        }

        count += track.keys.size();
        double previous = -1;

        for (const auto& key : track.keys) {
          if (!std::isfinite(key.time) || key.time < 0 || key.time > 86400 || key.time <= previous
              || !valueValid(key.value, track.path) || !valueValid(key.inTangent, track.path, true)
              || !valueValid(key.outTangent, track.path, true)
              || (track.interpolation != Interpolation::CubicSpline
                  && (key.inTangent != Math::Vec4{} || key.outTangent != Math::Vec4{}))) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Invalid animation key time/value/tangents");
          }

          previous = key.time;
        }

        duration = std::max(duration, previous);
      }

      return Clip(std::make_shared<const Data>(std::move(description), duration));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create animation clip: " + std::string(error.what()));
    }
  }

  auto Clip::description() const -> Core::Result<const ClipDescription*> {
    return m_data ? Core::Result<const ClipDescription*>{&m_data->description}
                  : Core::fail(Core::ErrorCode::InvalidState, "Animation clip is empty or moved");
  }

  auto Clip::duration() const -> Core::Result<double> {
    return m_data ? Core::Result<double>{m_data->duration}
                  : Core::fail(Core::ErrorCode::InvalidState, "Animation clip is empty or moved");
  }

  auto Clip::sample(double seconds) const -> Core::Result<std::vector<ChannelSample>> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation clip is empty or moved");
    }

    if (!std::isfinite(seconds) || seconds < 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation sample requires nonnegative finite seconds");
    }

    try {
      std::vector<ChannelSample> samples;
      samples.reserve(m_data->description.tracks.size());

      for (const auto& track : m_data->description.tracks) {
        auto result = evaluate(track, seconds);

        if (!result) {
          return Core::fail(result.error().code, "Animation node " + std::to_string(track.node)
                                                     + ": " + result.error().message);
        }

        samples.push_back({track.node, track.path, *result});
      }

      return samples;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Sample animation clip: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Animation

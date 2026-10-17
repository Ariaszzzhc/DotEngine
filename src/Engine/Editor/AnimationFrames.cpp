module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <numeric>
#include <string>
#include <vector>

module Dot.Editor.AnimationFrames;

namespace Dot::Editor {
  AnimationFrames::AnimationFrames(AnimationRate rate) : m_rate(rate) {
  }

  auto AnimationFrames::create(AnimationRate rate) -> Core::Result<AnimationFrames> {
    if (rate.numerator == 0 || rate.numerator > 240000 || rate.denominator == 0
        || rate.denominator > 1001 || rate.numerator < rate.denominator
        || rate.numerator > 240 * rate.denominator) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation frame rate requires 1..240fps and bounded rational terms");
    }

    const auto divisor = std::gcd(rate.numerator, rate.denominator);
    return AnimationFrames{{rate.numerator / divisor, rate.denominator / divisor}};
  }

  auto AnimationFrames::rate() const -> AnimationRate {
    return m_rate;
  }

  auto AnimationFrames::maximum() const -> std::uint64_t {
    return std::uint64_t{86400} * m_rate.numerator / m_rate.denominator;
  }

  auto AnimationFrames::coordinate(double seconds) const -> Core::Result<double> {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 86400) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation frame time requires finite [0,86400] seconds");
    }

    return seconds * m_rate.numerator / m_rate.denominator;
  }

  auto AnimationFrames::time(std::uint64_t frame) const -> Core::Result<double> {
    if (frame > maximum()) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Animation frame exceeds 86400 seconds");
    }

    return static_cast<double>(frame * m_rate.denominator) / m_rate.numerator;
  }

  auto AnimationFrames::seconds(double coordinate) const -> Core::Result<double> {
    const auto maximum = double{86400} * m_rate.numerator / m_rate.denominator;

    if (!std::isfinite(coordinate) || coordinate < 0 || coordinate > maximum) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation frame coordinate exceeds time bounds");
    }

    return std::min(86400., coordinate * m_rate.denominator / m_rate.numerator);
  }

  auto AnimationFrames::sample(std::uint64_t frame) const -> Core::Result<AnimationFrame> {
    auto seconds = time(frame);

    if (!seconds) {
      return std::unexpected(seconds.error());
    }

    const auto native = static_cast<float>(*seconds);
    const auto index
        = std::min(static_cast<double>(maximum()),
                   std::floor(double(native) * m_rate.numerator / m_rate.denominator + .5));

    if (index != static_cast<double>(frame)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation frame aliases a different frame in native float seconds");
    }

    return AnimationFrame{frame, *seconds, native};
  }

  auto AnimationFrames::nearest(double seconds) const -> Core::Result<AnimationFrame> {
    auto at = coordinate(seconds);

    if (!at) {
      return std::unexpected(at.error());
    }

    return sample(std::min(maximum(), static_cast<std::uint64_t>(std::floor(*at + .5))));
  }

  auto AnimationFrames::adjacent(double seconds, int direction) const
      -> Core::Result<AnimationFrame> {
    auto at = coordinate(seconds);

    if (!at || (direction != -1 && direction != 1)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Frame stepping requires valid time and direction -1 or 1");
    }

    auto closest = nearest(seconds);

    if (!closest) {
      return std::unexpected(closest.error());
    }

    const auto index
        = static_cast<float>(seconds) == closest->native
              ? static_cast<std::int64_t>(closest->index) + direction
              : static_cast<std::int64_t>(direction > 0 ? std::ceil(*at) : std::floor(*at));

    if (index < 0 || static_cast<std::uint64_t>(index) > maximum()) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "No adjacent animation frame within time bounds");
    }

    return sample(static_cast<std::uint64_t>(index));
  }

  auto AnimationFrames::ticks(double from, double to, std::size_t budget) const
      -> Core::Result<std::vector<FrameTick>> {
    auto first = coordinate(from);
    auto last = coordinate(to);

    if (!first || !last || to - from < 1e-6 || budget < 2 || budget > 64) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid animation frame ruler range/budget");
    }

    try {
      const auto needed
          = std::max(1., std::ceil((*last - *first) / static_cast<double>(budget - 1)));
      std::uint64_t step{1};

      while (static_cast<double>(step * 10) < needed) {
        step *= 10;
      }

      if (static_cast<double>(step) < needed) {
        step *= static_cast<double>(step * 2) >= needed   ? 2
                : static_cast<double>(step * 5) >= needed ? 5
                                                          : 10;
      }

      const auto start
          = static_cast<std::uint64_t>(std::ceil(*first / static_cast<double>(step))) * step;
      std::vector<FrameTick> result;
      result.reserve(budget);

      for (auto frame = start; frame <= maximum() && result.size() < budget; frame += step) {
        auto seconds = time(frame).value();

        if (seconds > to) {
          break;
        }

        result.push_back({frame, seconds});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Build animation frame ruler: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Editor

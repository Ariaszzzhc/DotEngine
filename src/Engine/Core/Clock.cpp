module;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

module Dot.Core.Clock;

namespace Dot::Core {
  auto Clock::reset() noexcept -> void {
    m_start = Source::now();
  }

  auto Clock::elapsed() const noexcept -> std::chrono::nanoseconds {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Source::now() - m_start);
  }

  FrameStepper::FrameStepper(std::chrono::nanoseconds fixedStep, std::size_t maxUpdates)
      : m_fixedStep(fixedStep), m_maxUpdates(maxUpdates) {
    if (fixedStep.count() <= 0 || maxUpdates == 0) {
      throw std::invalid_argument("Fixed step and update budget must be positive");
    }
  }

  auto FrameStepper::advance(std::chrono::nanoseconds elapsed) -> FrameStep {
    if (elapsed.count() < 0) {
      throw std::invalid_argument("Frame duration must not be negative");
    }
    // Divide first to avoid overflowing when a paused frame approaches the duration limit.
    // 先整除，避免长时间暂停后的时长相加溢出。
    auto whole = elapsed / m_fixedStep;
    auto remainder = elapsed % m_fixedStep;
    const auto room = m_fixedStep - m_accumulator;
    if (remainder >= room) {
      ++whole;
      m_accumulator = remainder - room;
    } else {
      m_accumulator += remainder;
    }
    const auto updates = static_cast<std::size_t>(
        std::min(static_cast<std::uint64_t>(whole), static_cast<std::uint64_t>(m_maxUpdates)));
    const auto dropped = m_fixedStep * (whole - static_cast<std::int64_t>(updates));
    const auto ratio
        = static_cast<double>(m_accumulator.count()) / static_cast<double>(m_fixedStep.count());
    return {updates, std::min(ratio, std::nextafter(1.0, 0.0)), dropped};
  }

  auto FrameStepper::reset() noexcept -> void {
    m_accumulator = {};
  }

  auto FrameStepper::fixedStep() const noexcept -> std::chrono::nanoseconds {
    return m_fixedStep;
  }

}  // namespace Dot::Core

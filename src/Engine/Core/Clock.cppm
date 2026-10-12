module;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

export module Dot.Core.Clock;

export namespace Dot::Core {
  /** Monotonic elapsed time. 不受系统时钟调整影响的耗时计时器。 */
  class Clock {
  public:
    using Source = std::chrono::steady_clock;

    auto reset() noexcept -> void {
      m_start = Source::now();
    }

    [[nodiscard]] auto elapsed() const noexcept -> std::chrono::nanoseconds {
      return std::chrono::duration_cast<std::chrono::nanoseconds>(Source::now() - m_start);
    }

  private:
    Source::time_point m_start{Source::now()};
  };

  struct FrameStep {
    std::size_t updates;
    double interpolation;
    std::chrono::nanoseconds dropped;
  };

  /**
   * Bound catch-up work and retain the fractional remainder. 限制追帧工作并保留不足一步的余量。
   * A negative frame duration is rejected without mutation. 负帧时长不会改变内部状态。
   */
  class FrameStepper {
  public:
    explicit FrameStepper(std::chrono::nanoseconds fixedStep = std::chrono::nanoseconds{16'666'667},
                          std::size_t maxUpdates = 8)
        : m_fixedStep(fixedStep), m_maxUpdates(maxUpdates) {
      if (fixedStep.count() <= 0 || maxUpdates == 0) {
        throw std::invalid_argument("Fixed step and update budget must be positive");
      }
    }

    [[nodiscard]] auto advance(std::chrono::nanoseconds elapsed) -> FrameStep {
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

    auto reset() noexcept -> void {
      m_accumulator = {};
    }

    [[nodiscard]] auto fixedStep() const noexcept -> std::chrono::nanoseconds {
      return m_fixedStep;
    }

  private:
    std::chrono::nanoseconds m_fixedStep;
    std::size_t m_maxUpdates;
    std::chrono::nanoseconds m_accumulator{};
  };
}  // namespace Dot::Core

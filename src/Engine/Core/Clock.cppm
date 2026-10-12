module;
#include <chrono>
#include <cstddef>

export module Dot.Core.Clock;

export namespace Dot::Core {
  /** Monotonic elapsed time. 不受系统时钟调整影响的耗时计时器。 */
  class Clock {
  public:
    using Source = std::chrono::steady_clock;
    auto reset() noexcept -> void;
    [[nodiscard]] auto elapsed() const noexcept -> std::chrono::nanoseconds;

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
                          std::size_t maxUpdates = 8);
    [[nodiscard]] auto advance(std::chrono::nanoseconds elapsed) -> FrameStep;
    auto reset() noexcept -> void;
    [[nodiscard]] auto fixedStep() const noexcept -> std::chrono::nanoseconds;

  private:
    std::chrono::nanoseconds m_fixedStep;
    std::size_t m_maxUpdates;
    std::chrono::nanoseconds m_accumulator{};
  };
}  // namespace Dot::Core

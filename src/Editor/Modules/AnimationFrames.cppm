module;

#include <cstddef>
#include <cstdint>
#include <vector>

export module Dot.Editor.AnimationFrames;

import Dot.Core.Result;

export namespace Dot::Editor {
  struct AnimationRate {
    std::uint32_t numerator{30};
    std::uint32_t denominator{1};
    auto operator==(const AnimationRate&) const -> bool = default;
  };

  struct AnimationFrame {
    std::uint64_t index;
    double seconds;
    float native;
  };

  struct FrameTick {
    std::uint64_t index;
    double seconds;
  };

  /** Rational editing grid, not a runtime simulation rate or clip resampling.
   * Rates 1..240fps, numerator <=240000/denominator <=1001, reduced on creation.
   * Native edits require that float seconds map back to the intended nearest
   * frame; adjacent-frame aliases fail instead of silently selecting a neighbor.
   * Positive half-frame ties round upward. Nominal ticks are labels, not a promise
   * that every long-duration frame is representable in native float storage.
   * Immutable value; no scene/file/device/history or mutable shared cache.
   * 有理数编辑网格，不改变运行时步频、不重采样片段。帧率 1..240，分子至多 240000、
   * 分母至多 1001，创建时约分。原生编辑要求 float 秒数能映回预期最近帧，别名冲突明确
   * 失败，不悄悄改成邻帧；正半帧向上取整。标尺为名义帧标签，不保证长片段的每帧均能
   * 被 float 表达。不可变值，不改场景/文件/设备/历史，无可变共享缓存。 */
  class AnimationFrames {
  public:
    [[nodiscard]] static auto create(AnimationRate rate = {}) -> Core::Result<AnimationFrames>;
    [[nodiscard]] auto rate() const -> AnimationRate;
    [[nodiscard]] auto maximum() const -> std::uint64_t;
    [[nodiscard]] auto coordinate(double seconds) const -> Core::Result<double>;
    [[nodiscard]] auto seconds(double coordinate) const -> Core::Result<double>;
    [[nodiscard]] auto time(std::uint64_t frame) const -> Core::Result<double>;
    [[nodiscard]] auto sample(std::uint64_t frame) const -> Core::Result<AnimationFrame>;
    [[nodiscard]] auto nearest(double seconds) const -> Core::Result<AnimationFrame>;
    /** Adjacent frame, treating the same native float sample as on-grid. Other
     * times step strictly in the chosen direction. Reject missing boundary frames.
     * 同一原生 float 采样视为在网格上；其他时刻沿指定方向严格步进，边界无邻帧则失败。 */
    [[nodiscard]] auto adjacent(double seconds, int direction) const
        -> Core::Result<AnimationFrame>;
    [[nodiscard]] auto ticks(double from, double to, std::size_t budget = 9) const
        -> Core::Result<std::vector<FrameTick>>;

  private:
    explicit AnimationFrames(AnimationRate rate);
    AnimationRate m_rate;
  };
}  // namespace Dot::Editor

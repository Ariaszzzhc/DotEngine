module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

export module Dot.Animation.Motion;

import Dot.Animation.Clip;
import Dot.Core.Result;

export namespace Dot::Animation {
  struct MotionPoint {
    double threshold;
    Clip clip;
    double phaseOffset{};
  };

  struct MotionChannel {
    std::uint32_t node;
    Path path;
    auto operator==(const MotionChannel&) const -> bool = default;
  };

  struct MotionSample {
    std::size_t point;
    double weight;
    double time;
  };

  /** Immutable single-clip or one-dimensional synchronized motion. Blend points are
   * strictly ordered (2..16), finite within +/-1e6, with normalized cycle offsets [-1,1].
   * Clamp outside the axis; only two adjacent clips contribute. Duration is their
   * weighted cycle duration; the owner preserves normalized phase when values change.
   * Missing channels use exact supplied bind values, never a previous output. All
   * channels remain reserved even at zero weight. No scene, device or hidden clock.
   * 不可变单片段/一维同步动作；2..16 个严格有序有限阈值限 +/-1e6，归一周期偏移 [-1,1]。
   * 轴外钳制，仅相邻两片参与，周期时长按权重混合；调用方在改值时保留归一相位。
   * 缺失通道使用精确提供的绑定值，不反馈上次输出；零权重通道仍保留。无场景、设备或隐藏时钟。 */
  class Motion {
  public:
    Motion() = default;
    [[nodiscard]] static auto single(Clip clip) -> Core::Result<Motion>;
    [[nodiscard]] static auto blend1D(std::vector<MotionPoint> points, double value)
        -> Core::Result<Motion>;
    [[nodiscard]] auto withValue(double value) const -> Core::Result<Motion>;
    [[nodiscard]] auto value() const -> Core::Result<double>;
    [[nodiscard]] auto blended() const -> Core::Result<bool>;
    [[nodiscard]] auto duration() const -> Core::Result<double>;
    [[nodiscard]] auto channels() const -> Core::Result<std::span<const MotionChannel>>;
    /** Time is [0,current duration]. Looping offsets wrap, nonlooping offsets clamp.
     * Reverse directional end at offset zero remains the actual end sample.
     * 时间限 [0,当前时长]；循环偏移回绕，非循环偏移钳制；反向零偏移起点保持实际末端样本。 */
    [[nodiscard]] auto samples(double time, bool looping) const
        -> Core::Result<std::vector<MotionSample>>;
    [[nodiscard]] auto sample(double time, bool looping,
                              std::span<const ChannelSample> rest = {}) const
        -> Core::Result<std::vector<ChannelSample>>;
    [[nodiscard]] auto sameDefinition(const Motion& other) const noexcept -> bool;

  private:
    struct Data;
    Motion(std::shared_ptr<const Data> data, double value) noexcept;
    std::shared_ptr<const Data> m_data;
    double m_value{};
  };
}  // namespace Dot::Animation

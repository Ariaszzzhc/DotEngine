module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

export module Dot.Animation.Clip;

import Dot.Core.Result;
import Dot.Math;

export namespace Dot::Animation {
  enum class Path { Translation, Rotation, Scale };
  enum class Interpolation { Step, Linear, CubicSpline };

  struct Key {
    float time{};
    Math::Vec4 value{};
    Math::Vec4 inTangent{};
    Math::Vec4 outTangent{};
  };

  struct Track {
    std::uint32_t node{};
    Path path{Path::Translation};
    Interpolation interpolation{Interpolation::Linear};
    std::vector<Key> keys;
  };

  struct ClipDescription {
    std::string name;
    std::vector<Track> tracks;
  };

  struct ChannelSample {
    std::uint32_t node;
    Path path;
    Math::Vec4 value;
  };

  /** Immutable shared clip with owned tracks. At most 30000 unique node/path
   * channels, nodes [0,9999], one million aggregate keys and times [0,86400].
   * Keys strictly increase; rotation values are unit XYZW quaternions. Vector
   * channels require w=0. Only cubic tracks accept tangents, with >=2 keys.
   * Invalid/default/moved handles report errors; copies share immutable storage.
   * 不可变共享片段，拥有轨道。至多 30000 个唯一节点/路径通道，节点 [0,9999]，合计
   * 百万个键，时间 [0,86400] 严格递增；旋转为单位 XYZW 四元数，向量 w=0。仅三次曲线
   * 接受切线且至少两键；无效/默认/移后句柄报错，副本共享不可变存储。 */
  class Clip {
  public:
    Clip() = default;
    [[nodiscard]] static auto create(ClipDescription description) -> Core::Result<Clip>;
    [[nodiscard]] auto description() const -> Core::Result<const ClipDescription*>;
    [[nodiscard]] auto duration() const -> Core::Result<double>;
    /** Nonnegative finite seconds; each channel clamps to its own endpoints.
     * Linear rotation uses shortest-arc SLERP. Cubic Hermite tangents scale by
     * interval seconds; cubic rotation normalizes the result without changing
     * authored signs/tangents. Zero quaternion/scale or overflow samples fail.
     * Thread-safe reads return owned samples. No scene/device or hidden clock.
     * 非负有限秒，每通道独立钳制端点；旋转线性插值走最短球面弧。三次 Hermite 切线乘
     * 区间秒数，三次旋转只归一化，不更改作者符号/切线。零四元数/缩放或溢出失败；
     * 线程安全读取，结果独立拥有，无场景/设备/隐藏时钟。 */
    [[nodiscard]] auto sample(double seconds) const -> Core::Result<std::vector<ChannelSample>>;
    /** Sample one immutable-description track by index using the exact native
     * evaluator, without visiting/allocating samples for unrelated channels.
     * Index belongs to this clip description; other channel failures are not
     * evaluated. Same time/value/error contracts as sample; no scene/device.
     * 按本片段不可变描述的轨道索引采样，使用同一原生求值器，不遍历或分配无关通道样本。
     * 索引限本片段，其他通道的采样错误不求值；时间/值约束同 sample，无场景/设备。 */
    [[nodiscard]] auto sampleTrack(std::size_t track, double seconds) const
        -> Core::Result<ChannelSample>;

  private:
    struct Data;
    explicit Clip(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> m_data;
  };
}  // namespace Dot::Animation

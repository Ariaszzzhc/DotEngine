module;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module Dot.Animation.Layers;

export import Dot.Animation.Clip;
import Dot.Core.Result;
import Dot.Math;

export namespace Dot::Animation {
  enum class LayerMode { Override, Additive };

  [[nodiscard]] auto layerModeName(LayerMode mode) noexcept -> std::string_view;

  struct LayerChannel {
    std::uint32_t node{};
    Path path{Path::Translation};
    double weight{1};
    auto operator==(const LayerChannel&) const -> bool = default;
  };

  struct LayerDefinition {
    std::string name;
    Clip clip;
    std::vector<LayerChannel> mask;
    double weight{1};
    double speed{1};
    bool looping{};
    bool playOnStart{};
    LayerMode mode{LayerMode::Override};
    double referenceTime{};
  };

  struct LayerFade {
    double from;
    double to;
    double elapsed;
    double duration;
    bool stop;
    auto operator==(const LayerFade&) const -> bool = default;
  };

  struct LayerPlayback {
    double time{};
    double weight{1};
    double speed{1};
    bool looping{};
    bool playing{};
    bool active{};
    bool paused{};
    std::optional<LayerFade> fade{};
    auto operator==(const LayerPlayback&) const -> bool = default;
  };

  struct LayerSnapshot {
    std::string name;
    double duration;
    LayerPlayback playback;
    LayerMode mode{LayerMode::Override};
    double referenceTime{};
    auto operator==(const LayerSnapshot&) const -> bool = default;
  };

  struct LayerSample {
    ChannelSample channel;
    double weight;
    std::optional<Math::Vec4> reference{};
  };

  struct PlayLayer {
    bool restart{};
  };

  struct PauseLayer {};

  struct StopLayer {};

  struct SeekLayer {
    double seconds;
  };

  struct ConfigureLayer {
    double speed;
    bool looping;
  };

  struct WeightLayer {
    double weight;
  };

  struct FadeLayer {
    double weight;
    double seconds;
    bool stop{};
  };

  using LayerControl = std::variant<PlayLayer, PauseLayer, StopLayer, SeekLayer, ConfigureLayer,
                                    WeightLayer, FadeLayer>;

  /** Ordered masked override/additive layers: <=8 unique ASCII names and <=30000 aggregate
   * unique-per-layer channels, all present in their clip. Weights [0,1], speed
   * [-16,16]. Own immutable clips/masks and small independent clocks. Functional
   * updates prepare candidates; the caller samples/composes and commits scene and
   * candidate together. Pause holds a contribution, Stop removes it, nonlooping
   * completion holds its terminal pose. Weight fades use unscaled seconds, can
   * retire at zero, and freeze on explicit layer pause. Speed zero only freezes
   * its clip. Additive uses a fixed same-clip reference at [0,duration], captured once for
   * masked channels, including inactive ones. Override requires referenceTime zero.
   * Samples carry owned reference values; composition applies local translation
   * offsets, post-multiplied shortest rotation deltas and multiplicative scale ratios.
   * Zero effective weight and unmasked tracks are not sampled at runtime. No
   * scene/file/device/history or hidden clock. Copies share immutable configuration, move retires
   * the source. Borrowed channel spans last while this configuration lives; snapshots/samples own
   * their data. 有序遮罩覆盖/叠加层：至多八个唯一 ASCII 名称、合计三万个每层唯一且片段存在的通道。
   * 权重 [0,1]、速度 [-16,16]。持有不可变片段/遮罩及独立小时钟；函数式修改准备候选，
   * 调用方采样混合后随场景原子提交。暂停保持贡献、停止移除贡献，非循环结束保持末姿态。
   * 权重淡化使用未缩放秒数，可在零权重退休，并随显式层暂停冻结；零速只冻结层片段。
   * 叠加以片段内固定参考时刻的遮罩通道为基准，配置时验证/捕获，覆盖层参考时刻须为零。
   * 使用局部位移差、后乘最短旋转差及缩放比；运行时零有效权重及未遮罩通道不采样。不触及场景/文件/设备/历史，无隐藏时钟。副本共享配置，
   * 移动使源失效；借用通道 span 有效期同配置，快照/采样拥有数据。 */
  class Layers {
  public:
    Layers() = default;
    Layers(const Layers&);
    auto operator=(const Layers&) -> Layers&;
    Layers(Layers&&) noexcept;
    auto operator=(Layers&&) noexcept -> Layers&;
    ~Layers();
    [[nodiscard]] static auto create(std::span<const LayerDefinition> definitions)
        -> Core::Result<Layers>;
    [[nodiscard]] auto channels() const -> Core::Result<std::span<const LayerChannel>>;
    [[nodiscard]] auto states() const -> Core::Result<std::vector<LayerSnapshot>>;
    [[nodiscard]] auto running() const -> Core::Result<bool>;
    /** Conservative equality of contribution inputs, without evaluating tracks.
     * Ignore clock-only controls and zero effective contributions. Different
     * configurations always require composition, even if numerically identical.
     * 不求值轨道的保守贡献输入比较；忽略仅时钟控制及零有效贡献。配置不同即要求重混合，
     * 即使最终数值可能相同。 */
    [[nodiscard]] auto contributesLike(const Layers& other) const -> Core::Result<bool>;
    [[nodiscard]] auto updated(std::string_view name, const LayerControl& operation) const
        -> Core::Result<Layers>;
    [[nodiscard]] auto advanced(double seconds) const -> Core::Result<Layers>;
    [[nodiscard]] auto stopped() const -> Core::Result<Layers>;
    /** Prepare an exact ordered clock/weight snapshot without sampling partial
     * states. Reject count/range and inactive-playing inconsistencies.
     * 准备完整有序时钟/权重快照，不采样中间状态；拒绝数量/范围及未活动却播放的不一致。 */
    [[nodiscard]] auto restored(std::span<const LayerPlayback> states) const
        -> Core::Result<Layers>;
    [[nodiscard]] auto sample() const -> Core::Result<std::vector<LayerSample>>;

  private:
    struct Data;
    Layers(std::shared_ptr<const Data> data, std::vector<LayerPlayback> states);
    std::shared_ptr<const Data> m_data;
    std::vector<LayerPlayback> m_states;
  };

  /** Stable non-file identifiers, 1..64 ASCII letters/digits/_/-, first a letter.
   * 稳定的非文件标识，1..64 个 ASCII 字母/数字/_/-，首字符须为字母。 */
  [[nodiscard]] auto validLayerName(std::string_view name) -> bool;
  /** Strict shared product/script control object; numbers retain double precision.
   * 产品/脚本共用的严格控制对象，数字保留 double 精度。 */
  [[nodiscard]] auto decodeLayerControl(std::string_view source) -> Core::Result<LayerControl>;
}  // namespace Dot::Animation

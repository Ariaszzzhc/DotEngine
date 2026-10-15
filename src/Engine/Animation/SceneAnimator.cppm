module;

#include <cstdint>
#include <memory>
#include <span>

export module Dot.Animation.SceneAnimator;

import Dot.Animation.Clip;
import Dot.Core.Result;
import Dot.World.Scene;

export namespace Dot::Animation {
  struct NodeBinding {
    std::uint32_t node;
    World::EntityId entity;
  };

  struct PlaybackState {
    double time{};
    double speed{1};
    bool looping{true};
    bool playing{true};
  };

  /** Owns immutable clip and node bindings, not a borrowed scene address. All
   * bindings must be unique/live in one scene scope and cover every clip target.
   * A scene move preserves scope; replacing/removing animated entities fails.
   * Owner-thread API, moves preserve state; empty/moved handles fail explicitly.
   * 拥有不可变片段和节点绑定，不借用场景地址；绑定须唯一、同范围且有效，并覆盖全部
   * 通道目标。场景移动保留范围，替换/删除动画实体会失败。API 限所有者线程，移动保留
   * 状态；空/移后句柄明确报错。 */
  class SceneAnimator {
  public:
    [[nodiscard]] static auto bind(const World::Scene& scene, Clip clip,
                                   std::span<const NodeBinding> bindings)
        -> Core::Result<SceneAnimator>;
    SceneAnimator(const SceneAnimator&) = delete;
    auto operator=(const SceneAnimator&) -> SceneAnimator& = delete;
    SceneAnimator(SceneAnimator&&) noexcept;
    auto operator=(SceneAnimator&&) noexcept -> SceneAnimator&;
    ~SceneAnimator();
    [[nodiscard]] auto state() const -> Core::Result<PlaybackState>;
    [[nodiscard]] auto configure(double speed, bool looping) -> Core::Result<void>;
    /** State only; pose is applied by seek/advance. Speed [-16,16], reverse and
     * zero supported. Pause freezes scene and clock; play optionally rewinds.
     * 仅修改播放状态，由 seek/advance 应用姿态；速度 [-16,16]，支持反向及零速。
     * 暂停冻结场景/时钟，播放可选择回到起点。 */
    [[nodiscard]] auto play(bool restart = false) -> Core::Result<void>;
    [[nodiscard]] auto pause() -> Core::Result<void>;
    /** Finite time within [0,duration]. Unanimated fields use current scene values.
     * All channel evaluation and local updates commit together through setTransforms;
     * invalid samples/scene failures preserve every pose and playback state.
     * 有限时间须在 [0,时长]，未动画字段使用当前场景值；通道求值及局部批量修改一起提交，
     * 无效采样/场景失败保留全部姿态及播放状态。 */
    [[nodiscard]] auto seek(World::Scene& scene, double time) -> Core::Result<void>;
    /** Finite elapsed seconds [0,86400]. Loop wraps in either direction; nonlooping
     * playback holds the terminal sample and pauses. No hidden wall clock.
     * 有限经过秒数 [0,86400]；循环支持双向回绕，非循环到端点后保持并暂停，无隐藏时钟。 */
    [[nodiscard]] auto advance(World::Scene& scene, double seconds) -> Core::Result<void>;
    /** Restore only animated fields to bind pose and stop at time zero atomically.
     * 仅恢复动画字段的绑定姿态，同时停止并归零；事务失败时保留此前状态。 */
    [[nodiscard]] auto stop(World::Scene& scene) -> Core::Result<void>;

  private:
    struct Impl;
    explicit SceneAnimator(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Animation

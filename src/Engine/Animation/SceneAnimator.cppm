module;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

export module Dot.Animation.SceneAnimator;

import Dot.Animation.Clip;
export import Dot.Animation.Layers;
export import Dot.Animation.Motion;
import Dot.Core.Result;
import Dot.World.Scene;

export namespace Dot::Animation {
  struct NodeBinding {
    std::uint32_t node;
    World::EntityId entity;
  };

  struct TransitionProgress {
    double elapsed;
    double duration;
    auto operator==(const TransitionProgress&) const -> bool = default;
  };

  struct PlaybackState {
    /** Incoming clip time during a transition; progress uses unscaled seconds.
     * 过渡期间为目标片段时钟，淡化进度使用未缩放秒数。 */
    double time{};
    double speed{1};
    bool looping{true};
    bool playing{true};
    std::optional<TransitionProgress> transition{};
    /** Explicit player-wide pause; a naturally ended base clip does not pause layers.
     * 显式播放器总暂停；基础片段自然结束不会暂停独立层。 */
    bool paused{};
  };

  struct PlaybackTransition {
    Clip clip;
    double fadeSeconds{};
    double speed{1};
    bool looping{true};
    bool paused{};
  };

  struct MotionTransition {
    Motion motion;
    double fadeSeconds{};
    double speed{1};
    bool looping{true};
    bool paused{};
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
                                   std::span<const NodeBinding> bindings,
                                   std::span<const LayerDefinition> layers = {})
        -> Core::Result<SceneAnimator>;
    [[nodiscard]] static auto bindMotion(const World::Scene& scene, Motion motion,
                                         std::span<const NodeBinding> bindings,
                                         std::span<const LayerDefinition> layers = {})
        -> Core::Result<SceneAnimator>;
    /** Bound/current immutable motion/value, not pending controller parameters.
     * 已绑定/当前不可变动作与轴值，不是待求值状态机参数。 */
    [[nodiscard]] auto motion() const -> Core::Result<Motion>;
    /** Same-definition value retuning preserves normalized phase and all layer/fade clocks;
     * sample/publication failure retains the prior motion, clocks and scene pose.
     * 同定义轴值调整保留归一相位及全部层/淡化时钟；采样或发布失败保留旧动作、时钟与姿态。 */
    [[nodiscard]] auto retuneAndAdvance(World::Scene& scene, Motion motion, double seconds)
        -> Core::Result<void>;
    [[nodiscard]] auto transitionMotionAndAdvance(World::Scene& scene, MotionTransition transition,
                                                  double seconds) -> Core::Result<void>;
    [[nodiscard]] auto setMotion(World::Scene& scene, Motion motion) -> Core::Result<void>;
    [[nodiscard]] auto crossFadeMotion(World::Scene& scene, Motion motion, double seconds)
        -> Core::Result<void>;
    SceneAnimator(const SceneAnimator&) = delete;
    auto operator=(const SceneAnimator&) -> SceneAnimator& = delete;
    SceneAnimator(SceneAnimator&&) noexcept;
    auto operator=(SceneAnimator&&) noexcept -> SceneAnimator&;
    ~SceneAnimator();
    [[nodiscard]] auto state() const -> Core::Result<PlaybackState>;
    [[nodiscard]] auto configure(double speed, bool looping) -> Core::Result<void>;
    [[nodiscard]] auto layers() const -> Core::Result<std::vector<LayerSnapshot>>;
    /** Native base/cross-fade and ordered masked layers compose before one scene
     * transaction. Layer controls never advance/reset the base clock or transition;
     * failed sampling/pose publication retains every clock, weight and pose.
     * Unanimated masked channels start from bind values, avoiding feedback drift.
     * 基础/淡化及有序遮罩层在一次场景事务前混合。层控制不推进/重置基础时钟或过渡；
     * 采样/姿态发布失败保留全部时钟、权重与姿态。基础未动画的遮罩通道从绑定值开始，避免反馈漂移。
     */
    [[nodiscard]] auto controlLayer(World::Scene& scene, std::string_view name,
                                    const LayerControl& operation) -> Core::Result<void>;
    /** Restore one complete current-clip playback snapshot with no transition.
     * Sample all layers together; no partial replay poses are published. Used by
     * ordinary editor draft recreation. Never changes clip/bindings/definitions.
     * 恢复当前片段的完整无过渡播放快照；一起采样所有层，不发布分步重放姿态。用于普通
     * 编辑器草稿重建，不更改片段/绑定/定义。 */
    [[nodiscard]] auto restorePlayback(World::Scene& scene, PlaybackState state,
                                       std::span<const LayerPlayback> layers) -> Core::Result<void>;
    /** State only; pose is applied by seek/advance. Speed [-16,16], reverse and
     * zero supported. Pause freezes all clocks; play optionally rewinds only the base.
     * 仅修改播放状态，由 seek/advance 应用姿态；速度 [-16,16]，支持反向及零速。
     * 总暂停冻结所有时钟，播放可选择只重置基础片段。 */
    [[nodiscard]] auto play(bool restart = false) -> Core::Result<void>;
    [[nodiscard]] auto pause() -> Core::Result<void>;
    /** Finite time within [0,duration]. Unanimated/unmasked fields use current scene
     * values; unanimated masked fields use bind values without feedback drift.
     * All channel evaluation and local updates commit together through setTransforms;
     * invalid samples/scene failures preserve every pose and playback state. Optional
     * pause commits only after the pose succeeds, atomically with the new clock.
     * 有限时间须在 [0,时长]，未动画/未遮罩字段使用当前场景值，未动画遮罩字段使用绑定值；
     * 通道求值及局部批量修改一起提交，
     * 无效采样/场景失败保留全部姿态及播放状态；可选暂停仅随成功姿态/时钟一起提交。 */
    [[nodiscard]] auto seek(World::Scene& scene, double time, bool pause = false)
        -> Core::Result<void>;
    /** Finite elapsed seconds [0,86400]. Loop wraps in either direction; an ended
     * nonlooping base holds its terminal sample while independent layers continue.
     * No hidden wall clock. 有限经过秒数 [0,86400]；循环支持双向回绕，非循环基础片段
     * 结束后保持末姿态，独立层继续推进，无隐藏时钟。 */
    [[nodiscard]] auto advance(World::Scene& scene, double seconds) -> Core::Result<void>;
    /** Prepare transition configuration and one advance without intermediate
     * publication. Validate/publish only the final complete base/layer pose, then
     * commit all playback together. Outgoing clips retain their previous speed/
     * loop settings; unscaled fade and layer clocks keep their own semantics.
     * 准备转换配置及一次推进，不发布中间姿态；仅验证/发布最终基础/全部层姿态，再共同
     * 提交播放状态。源片段保留原速度/循环，非缩放淡化及独立层时钟语义不变。 */
    [[nodiscard]] auto transitionAndAdvance(World::Scene& scene, PlaybackTransition transition,
                                            double seconds) -> Core::Result<void>;
    /** Restore base/transition/masked fields to bind pose and stop all clocks atomically.
     * 原子恢复基础/过渡/遮罩字段的绑定姿态并停止所有时钟，失败保留此前状态。 */
    [[nodiscard]] auto stop(World::Scene& scene) -> Core::Result<void>;
    /** Switch the clip using original binding poses. Restore former channels and
     * apply the new directional start in one scene transaction, then play. All
     * bindings supplied at bind remain available; invalid targets/samples preserve
     * the old clip, pose and clock. Speed/looping remain unchanged.
     * 用最初绑定姿态切换片段，一次场景事务恢复旧通道并应用新片段正/反向起点，随后播放。
     * bind 提供的全部绑定始终可用；目标/采样无效保留旧片段、姿态及时钟，速度/循环不变。 */
    [[nodiscard]] auto setClip(World::Scene& scene, Clip clip) -> Core::Result<void>;
    /** Cross-fade to a directional clip start over finite seconds [0,86400]. Zero
     * switches immediately. Both clips advance; missing destination channels fade
     * to bind values, missing source channels start from the visible local pose.
     * Rotation uses shortest-arc SLERP. Interruptions retain one frozen visible
     * unlayered origin instead of stacking interrupted fades. Explicit layers apply
     * once after the base fade. Fade time is unscaled; pause freezes both
     * clocks and fade, while speed zero freezes clip clocks only. A paused or
     * unapplied source is frozen. Both live clips use current speed/loop settings.
     * Nonlooping ends
     * hold until fading finishes. Seek/stop/setClip cancel only after a successful
     * pose transaction. Invalid samples preserve all poses, clips and clocks.
     * 用有限秒数 [0,86400] 淡化到新片段正/反向起点，零为立即切换。双片段同时推进；目标
     * 缺失通道淡回绑定值，源缺失通道取当前可见局部姿态，旋转走最短 SLERP。打断冻结当前
     * 未分层姿态，不累积被打断的淡化；显式层在基础淡化之后仅应用一次。淡化使用未缩放
     * 时间，暂停冻结全部时钟，零速仅冻结基础片段。
     * 暂停或尚未应用的源保持冻结；两个活动片段共用当前速度/循环设置。
     * 非循环端点保持至淡化完成；seek/stop/setClip 成功提交后才取消。失败保留全部状态。 */
    [[nodiscard]] auto crossFade(World::Scene& scene, Clip clip, double seconds)
        -> Core::Result<void>;

  private:
    struct Impl;
    explicit SceneAnimator(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Animation

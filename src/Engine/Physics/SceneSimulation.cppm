module;

#include <memory>
#include <optional>
#include <vector>

export module Dot.Physics.SceneSimulation;

import Dot.Core.Result;
import Dot.Math;
import Dot.World.Scene;
import Dot.Physics.World;

export namespace Dot::Physics {
  struct SceneRayHit {
    Dot::World::EntityId entity;
    float fraction;
    Math::Vec3 point;
    std::optional<Math::Vec3> normal;
  };

  struct SceneContactEvent {
    ContactPhase phase;
    ContactEndReason endReason;
    Dot::World::EntityId first;
    Dot::World::EntityId second;
    bool sensor;
    std::uint64_t step;
    std::optional<ContactGeometry> geometry;
  };

  /** Owns a physics world and bindings, without retaining a scene reference.
   * Operations require the creating thread; destruction requires quiescence.
   * Moving a scene preserves its identity and bindings. Queries describe the
   * last successful synchronization; authoring edits apply on synchronize/step.
   * 拥有物理世界和绑定，不保留场景引用；操作须在创建线程进行，销毁须静止。
   * 移动场景保留身份与绑定，查询描述最近成功同步的状态，编辑在同步/步进时应用。 */
  class SceneSimulation {
  public:
    [[nodiscard]] static auto create(const Dot::World::Scene& scene, const WorldConfig& config = {})
        -> Core::Result<SceneSimulation>;
    SceneSimulation(const SceneSimulation&) = delete;
    auto operator=(const SceneSimulation&) -> SceneSimulation& = delete;
    SceneSimulation(SceneSimulation&&) noexcept;
    auto operator=(SceneSimulation&&) noexcept -> SceneSimulation&;
    ~SceneSimulation();

    /** Validate desired bindings before batch mutation. Box dimensions use
     * absolute world scale; sphere/capsule require uniform magnitude within
     * 1e-5 relative tolerance. Shear/unrepresentable physical sizes fail.
     * Pose edits teleport; unchanged initial velocity retains current motion.
     * 校验目标绑定后批量修改；盒尺寸采用世界缩放绝对值，球/胶囊要求在 1e-5 相对容差内
     * 等比缩放，错切和不可表示的物理尺寸失败；位姿编辑表示传送，初始速度未改则保留当前运动。 */
    [[nodiscard]] auto synchronize(const Dot::World::Scene& scene) -> Core::Result<void>;
    /** Synchronize, advance, then atomically write world poses back as local TRS.
     * Parents are processed before children, preserving reflected scale signs.
     * Post-advance failures fault the service; reset is required to resume.
     * 同步、推进后把世界位姿原子写回局部 TRS；先父后子处理并保留反射缩放符号。
     * 推进后的失败使服务故障，须重置后继续。 */
    [[nodiscard]] auto step(Dot::World::Scene& scene, double seconds) -> Core::Result<void>;
    /** Replace with an independently prepared world. Failure retains existing
     * bindings; success resets motion from authoring data and may bind a new scene.
     * 用独立准备的世界替换，失败保留原绑定；成功从编辑数据重置运动，并可绑定另一场景。 */
    [[nodiscard]] auto reset(const Dot::World::Scene& scene) -> Core::Result<void>;
    [[nodiscard]] auto body(Dot::World::EntityId entity) const -> Core::Result<BodyState>;
    [[nodiscard]] auto stats() const -> Core::Result<WorldStats>;
    /** Owned contact events preserve entity values even after component/entity
     * removal. Successful drain consumes them; faulted services reject delivery.
     * Reset discards the old world's stream. Removal IDs may be stale in the scene.
     * 拥有数据的接触事件在组件/实体移除后仍保留实体值；成功读取消耗事件，故障服务拒绝交付。
     * 重置丢弃旧世界事件流，移除事件中的 ID 可能已在场景中失效。 */
    [[nodiscard]] auto drainContactEvents() -> Core::Result<std::vector<SceneContactEvent>>;
    [[nodiscard]] auto setVelocity(Dot::World::EntityId entity, Math::Vec3 velocity)
        -> Core::Result<void>;
    [[nodiscard]] auto setAngularVelocity(Dot::World::EntityId entity, Math::Vec3 velocity)
        -> Core::Result<void>;
    [[nodiscard]] auto addImpulse(Dot::World::EntityId entity, Math::Vec3 impulse)
        -> Core::Result<void>;
    /** Target pose uses world coordinates. 目标位姿采用世界坐标。 */
    [[nodiscard]] auto moveKinematic(Dot::World::EntityId entity, Math::Vec3 position,
                                     Math::Quat rotation, double seconds) -> Core::Result<void>;
    [[nodiscard]] auto rayCast(Math::Vec3 origin, Math::Vec3 displacement) const
        -> Core::Result<std::optional<SceneRayHit>>;

  private:
    struct Impl;
    explicit SceneSimulation(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Physics

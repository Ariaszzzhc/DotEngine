module;

#include <memory>
#include <optional>
#include <span>
#include <vector>

export module Dot.Physics.SceneSimulation;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Physics.Mesh;
import Dot.Physics.Character;
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

  struct SceneCharacterState {
    Math::Vec3 position;
    Math::Quat rotation;
    Math::Vec3 velocity;
    GroundState ground;
    Math::Vec3 groundNormal;
    Math::Vec3 groundVelocity;
    std::optional<Dot::World::EntityId> groundEntity;
    /** Corrected feet displacement / last native character substep. Creation and explicit
     * teleport reset it; support probes without displacement contribute no speed.
     * 修正后脚底位移除以最近原生角色子步时长；创建/显式传送归零，无位移支撑探测不产生速度。 */
    Math::Vec3 measuredVelocity{};
  };

  struct SceneSphereCastFilter {
    bool includeSensors{};
    std::optional<Dot::World::EntityId> ignoreEntity;
  };

  struct SceneSphereHit {
    Dot::World::EntityId entity;
    float fraction;
    Math::Vec3 center;
    Math::Vec3 point;
    std::optional<Math::Vec3> normal;
    float penetrationDepth;
  };

  /** Owns a physics world and bindings, without retaining a scene reference.
   * Operations require the creating thread; destruction requires quiescence.
   * Moving a scene preserves its identity and bindings. Queries describe the
   * last successful synchronization; authoring edits apply on synchronize/step.
   * 拥有物理世界和绑定，不保留场景引用；操作须在创建线程进行，销毁须静止。
   * 移动场景保留身份与绑定，查询描述最近成功同步的状态，编辑在同步/步进时应用。 */
  class SceneSimulation {
  public:
    /** Optional prepared meshes are the complete initial resource set: missing
     * entries fail without file fallback. Later bindings may load from files.
     * Retain up to 64 immutable assets/64 MiB until reset/destruction. Reset
     * reloads files when available; otherwise reuses owned geometry.
     * 可选预备网格为完整初始资源集，缺项失败且不回退读文件；后续绑定可从文件加载。
     * 至多保留 64 个不可变资源/64 MiB，直至重置/销毁；重置有文件时重读，否则复用拥有数据。 */
    [[nodiscard]] static auto create(const Dot::World::Scene& scene, const WorldConfig& config = {},
                                     std::optional<Core::FileSystem> files = {},
                                     std::optional<std::vector<MeshAsset>> prepared = {})
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
    /** One synchronization supplies uncached collision geometry without disk fallback.
     * Existing logical paths retain cached immutable geometry until reset. Supplied
     * paths must be unique and referenced; the combined cache retains its 64/64 MiB bound.
     * 单次同步提供未缓存碰撞几何且不回退磁盘；已有逻辑路径保留不可变缓存直至重置。
     * 提供路径须唯一且被引用，合并缓存仍受 64 项/64 MiB 限制。 */
    [[nodiscard]] auto synchronizePrepared(const Dot::World::Scene& scene,
                                           std::span<const MeshAsset> prepared)
        -> Core::Result<void>;
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
    /** Character dimensions follow positive uniform world scale; feet must remain
     * Y-up. Step/stick distances remain world metres. Inputs are world-space;
     * set yaw without clearing motion. Character poses write back with body poses
     * in the same scene transaction. Support refers to synchronized rigid bodies or characters.
     * 角色尺寸按正向等比世界缩放，脚底须保持 Y 向上，台阶/贴地距离仍为世界米。
     * 输入使用世界坐标，转向保留运动；角色/刚体位姿在同一场景事务写回，支撑指向已同步刚体或角色。
     */
    [[nodiscard]] auto character(Dot::World::EntityId entity) const
        -> Core::Result<SceneCharacterState>;
    /** Current native feet and simulation-scaled capsule configuration. Reject pending
     * character/ancestor edits and faulted physics instead of mixing authored/native state.
     * 当前原生脚底及仿真缩放后的胶囊配置；拒绝未同步角色/祖先修改及故障物理，不混用状态。 */
    [[nodiscard]] auto characterConfig(const Dot::World::Scene& scene,
                                       Dot::World::EntityId entity) const
        -> Core::Result<CharacterConfig>;
    [[nodiscard]] auto setCharacterInput(Dot::World::EntityId entity, const CharacterInput& input)
        -> Core::Result<void>;
    [[nodiscard]] auto setCharacterYaw(Dot::World::EntityId entity, float yawDegrees)
        -> Core::Result<void>;
    /** Immediately place synchronized native/scene poses, clear velocity/input and
     * publish pose-change contacts without advancing time. Reject unsynchronized
     * character/ancestor edits; failures after native mutation fault until reset.
     * Overlap resolution still belongs to the next physics step.
     * 立即同步原生/场景位姿并清空速度/输入，不推进时间；拒绝未同步的角色/祖先修改。
     * 原生修改后的失败使仿真故障直至重置；重叠仍在下次物理步解析。 */
    [[nodiscard]] auto teleportCharacter(Dot::World::Scene& scene, Dot::World::EntityId entity,
                                         Math::Vec3 feet, float yawDegrees) -> Core::Result<void>;
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
    /** Observational volume sweep against synchronized bodies and characters;
     * ignored owners must have either collider kind. 原生体积扫掠只读已同步刚体及角色，
     * 忽略所有者须具有其中一种碰撞体。 */
    [[nodiscard]] auto sphereCast(Math::Vec3 origin, float radius, Math::Vec3 displacement,
                                  const SceneSphereCastFilter& filter = {}) const
        -> Core::Result<std::optional<SceneSphereHit>>;

  private:
    [[nodiscard]] auto synchronizeScene(const Dot::World::Scene& scene,
                                        std::optional<std::span<const MeshAsset>> prepared)
        -> Core::Result<void>;
    struct Impl;
    explicit SceneSimulation(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Physics

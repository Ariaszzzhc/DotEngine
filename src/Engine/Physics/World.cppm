module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

export module Dot.Physics.World;

export import Dot.Physics.Body;
export import Dot.Physics.Character;

import Dot.Core.Result;
import Dot.Math;

export namespace Dot::Physics {
  struct BodyId {
    std::uint64_t world{};
    std::uint64_t value{};
    auto operator==(const BodyId&) const -> bool = default;
  };

  struct CharacterId {
    std::uint64_t world{};
    std::uint64_t value{};
    auto operator==(const CharacterId&) const -> bool = default;
  };

  /** Collider identity preserves its native owner kind; character proxies are
   * never exposed as mutable rigid bodies. 碰撞体身份保留所有者类型，角色代理不伪装成可修改刚体。
   */
  using ColliderId = std::variant<BodyId, CharacterId>;

  struct WorldConfig {
    std::uint32_t maxBodies{1024};
    Math::Vec3 gravity{0, -9.81F, 0};
    std::uint32_t workerThreads{};
    std::uint32_t maxContactEvents{65536};
    std::uint32_t maxCharacters{64};
  };

  struct BodyState {
    Motion motion;
    CollisionDetection collisionDetection;
    Math::Vec3 position;
    Math::Quat rotation;
    Math::Vec3 velocity;
    bool active;
    bool sensor;
    Math::Vec3 angularVelocity{};
  };

  struct WorldStats {
    std::size_t bodies;
    std::size_t active;
    bool faulted{};
    std::size_t contactPairs{};
    std::size_t queuedContactEvents{};
    std::size_t characters{};
  };

  /** Last observed capsule state; removed support has no identity.
   * 最近观测的胶囊状态；已删除支撑不返回身份。 */
  struct CharacterState {
    Math::Vec3 position;
    Math::Quat rotation;
    /** Native controller velocity, including the downward support probe; not a
     * finite difference of corrected feet positions. 原生控制器速度，包含向下支撑探测，
     * 并非修正后脚底位置的差分速度。 */
    Math::Vec3 velocity;
    GroundState ground;
    Math::Vec3 groundNormal;
    Math::Vec3 groundVelocity;
    std::optional<ColliderId> groundCollider;
  };

  struct RayHit {
    ColliderId collider;
    float fraction;
    Math::Vec3 point;
    /** Fraction zero may be inside the shape and has no surface normal.
     * 比例为零可能位于形状内部，此时没有表面法线。 */
    std::optional<Math::Vec3> normal;
  };

  struct SphereCastFilter {
    bool includeSensors{};
    std::optional<ColliderId> ignoreCollider;
  };

  /** Closest sphere sweep contact. Center and obstacle surface point are world
   * coordinates; normal points away from the obstacle. Initial overlaps have
   * fraction zero and nonnegative penetrationDepth; a degenerate overlap may
   * have no unique normal. No depenetration or simulation advancement is applied.
   * 最近球体扫掠接触，中心与障碍表面点使用世界坐标，法线指向障碍外。
   * 初始重叠的比例为零且深度非负，退化重叠可无法确定唯一法线；不脱离重叠或推进仿真。 */
  struct SphereHit {
    ColliderId collider;
    float fraction;
    Math::Vec3 center;
    Math::Vec3 point;
    std::optional<Math::Vec3> normal;
    float penetrationDepth;
  };

  enum class ContactPhase { Begin, Stay, Sleep, End };

  enum class ContactEndReason { Separated, PoseChanged, Destroyed, Reconfigured };

  struct ContactParty {
    ColliderId collider;
    std::uint64_t userTag;
  };

  struct ContactPoint {
    Math::Vec3 first;
    Math::Vec3 second;
  };

  /** Last observed pre-solver manifold for the step, with normal from the lower
   * collider key toward the higher (bodies precede characters). Negative depth denotes speculative
   * contact.
   * 此步最近观测的求解前接触流形，法线从较小碰撞体键指向较大键（刚体在角色之前），负深度表示预测接触。
   */
  struct ContactManifold {
    Math::Vec3 normal;
    float penetrationDepth;
    std::vector<ContactPoint> points;
  };

  /** Native controller observation, not a rigid-body manifold. Normal follows
   * pair ordering; point lies on the counterpart of the observing character.
   * Separation is the backend signed distance including controller padding,
   * and can be positive for a predictive collision that was actually solved.
   * characterIsFirst identifies the observer without borrowing a native pointer.
   * 原生控制器观察，不是刚体流形；法线遵循碰撞对顺序，点位于观察角色的对方表面。
   * separation 为包含控制器外壳的后端有符号距离，实际解算的预测碰撞可为正值。
   * characterIsFirst 标识观察者，不借用原生指针。 */
  struct CharacterContactGeometry {
    Math::Vec3 normal;
    Math::Vec3 point;
    float separation;
    bool characterIsFirst;
  };

  using ContactGeometry = std::variant<ContactManifold, CharacterContactGeometry>;

  struct ContactEvent {
    ContactPhase phase;
    ContactEndReason endReason;
    ContactParty first;
    ContactParty second;
    bool sensor;
    std::uint64_t step;
    /** Geometry accompanies observed Begin/Stay; lifecycle-only events omit it.
     * 已观测的开始/持续事件携带几何，仅生命周期事件不携带几何。 */
    std::optional<ContactGeometry> geometry;
  };

  /** Configuration only creates; handle only destroys; both replace the body
   * configuration while retaining its public handle. Neither is invalid.
   * 仅配置表示创建，仅句柄表示销毁，两者都有表示保留公开句柄替换刚体配置；皆空无效。 */
  struct BodyChange {
    std::optional<BodyId> body;
    std::optional<BodyConfig> config;
    /** Replacement of a moving body can retain trusted backend motion, avoiding
     * authoring-range revalidation of solver rounding. Other inputs remain checked.
     * 运动刚体替换可保留可信的后端运动，避免按编辑范围重新校验求解器舍入；其他输入仍校验。 */
    bool preserveVelocity{};
    bool preserveAngularVelocity{};
  };

  struct CharacterChange {
    std::optional<CharacterId> character;
    std::optional<CharacterConfig> config;
    /** Explicitly retain backend velocity and queued input during replacement;
     * otherwise both reset. Scene callers disable this for teleports.
     * 替换时明确保留后端速度及排队输入，否则清空两者；场景调用者在传送时禁用。 */
    bool preserveMotion{};
  };

  struct WorldChangeResult {
    std::vector<std::optional<BodyId>> bodies;
    std::vector<std::optional<CharacterId>> characters;
  };

  /** Owns a Jolt simulation, shapes, bodies, temporary allocator, and job system.
   * Operations require the creating thread; destruction requires quiescence.
   * Independent worlds share process-lifetime Jolt type registration;
   * public body IDs never reuse values.
   * 拥有 Jolt 仿真、形状、刚体、临时分配器和任务系统；操作须在创建线程进行，销毁须静止。
   * 独立世界共享进程寿命的 Jolt 类型注册，公开刚体 ID 的值不复用。
   */
  class World {
  public:
    /** 1..4096 bodies, 0..8 workers; zero executes synchronously without workers.
     * Gravity magnitude is at most 1000; 1..262144 queued contact events.
     * 1..4096 个刚体、0..8 个工作线程；零表示同步执行而不启动工作线程，
     * 重力大小不超过 1000，接触事件队列容量为 1..262144。
     * Character capacity is 0..256; zero disables character creation.
     * 角色容量为 0..256，零表示禁用角色创建。 */
    [[nodiscard]] static auto create(const WorldConfig& config = {}) -> Core::Result<World>;
    World(const World&) = delete;
    auto operator=(const World&) -> World& = delete;
    World(World&&) noexcept;
    auto operator=(World&&) noexcept -> World&;
    ~World();

    [[nodiscard]] auto createBody(const BodyConfig& config = {}) -> Core::Result<BodyId>;
    /** At most 8192 unique changes. Input/shape/allocation failures occur before
     * mutation. Bodies are prepared outside the slot pool so full-capacity
     * replacements succeed. Results correspond to input order; deletions return
     * nullopt. Unexpected slot-assignment failures may partially apply changes
     * and fault the world; diagnostic reads/destruction remain available.
     * 最多 8192 项不重复变更，输入/形状/准备分配失败发生在修改前；在槽池外准备刚体，
     * 因此满容量时也可替换。结果顺序与输入一致，删除返回 nullopt；意外槽分配失败
     * 可能部分应用并使世界故障，保留诊断读取和销毁操作。 */
    [[nodiscard]] auto applyChanges(std::span<const BodyChange> changes)
        -> Core::Result<std::vector<std::optional<BodyId>>>;
    /** Prepare both kinds before mutation; full-capacity replacements retain IDs.
     * At most 8192 body and 512 character changes, unique per handle kind. Ordered
     * results use nullopt for deletion. Final contacts are queried after publishing
     * both sets; native slot/contact failures then fault the world, with diagnostic
     * reads/destruction still available. Input/capacity failures retain both sets.
     * 修改前准备两类对象，满容量替换保留 ID；刚体/角色变更分别最多 8192/512 项，
     * 各类句柄不重复，结果按输入排序，删除返回空值。发布两类后查询最终接触；随后原生槽位/
     * 接触失败使世界故障，仍可读取诊断和销毁；输入/容量错误保留两类对象。 */
    [[nodiscard]] auto applyChanges(std::span<const BodyChange> bodies,
                                    std::span<const CharacterChange> characters)
        -> Core::Result<WorldChangeResult>;
    [[nodiscard]] auto destroyBody(BodyId body) -> Core::Result<void>;
    [[nodiscard]] auto body(BodyId body) const -> Core::Result<BodyState>;
    /** Own a feet-origin Y-up virtual capsule and native kinematic query/contact
     * proxy. Requires vertical nonpositive gravity. Proxies share the world but
     * retain CharacterId identity; public body capacity and mutations remain separate.
     * 拥有脚底原点、Y 向上的虚拟胶囊及原生运动学查询/接触代理，要求重力竖直向下或为零。
     * 代理保留 CharacterId 身份，公开刚体容量及修改操作保持独立。 */
    [[nodiscard]] auto createCharacter(const CharacterConfig& config = {})
        -> Core::Result<CharacterId>;
    [[nodiscard]] auto destroyCharacter(CharacterId character) -> Core::Result<void>;
    [[nodiscard]] auto character(CharacterId character) const -> Core::Result<CharacterState>;
    /** Horizontal velocity persists; jump edges merge until the next substep, then
     * are consumed even when unsupported. Invalid input preserves pending intent.
     * 水平速度持续有效，跳跃边沿合并至下个子步并消耗，未受支撑也消耗；无效输入保留意图。 */
    [[nodiscard]] auto setCharacterInput(CharacterId character, const CharacterInput& input)
        -> Core::Result<void>;
    /** Teleport clears velocity/input and refreshes contacts without advancing time.
     * Initial overlap is resolved by the next step, not by this pose operation.
     * 传送清空速度/输入并刷新接触而不推进时间；初始重叠在下次步进处理。 */
    [[nodiscard]] auto teleportCharacter(CharacterId character, Math::Vec3 feet,
                                         float yawDegrees = 0) -> Core::Result<void>;
    /** Yaw does not clear motion or queued input. 朝向修改不清除运动或排队输入。 */
    [[nodiscard]] auto setCharacterYaw(CharacterId character, float yawDegrees)
        -> Core::Result<void>;
    /** Step [0.000001,0.25] seconds, subdivided at 60 Hz. Invalid input does not
     * advance time. Backend errors can occur after advancement and fault the
     * world: reads/destruction remain available, further mutations fail.
     * 同步步进 [0.000001,0.25] 秒，按 60 Hz 细分；无效输入不推进时间。
     * 后端错误可能发生在推进后，世界进入故障状态，仅保留读取与销毁操作。 */
    [[nodiscard]] auto step(double seconds) -> Core::Result<void>;
    [[nodiscard]] auto stats() const -> Core::Result<WorldStats>;
    /** Owned events follow operation order, with pairs sorted within each step or
     * lifetime operation, read only on the owner
     * thread after operations. Sleeping contacts remain tracked; waking observations
     * produce Stay. Queue/capture failure faults the world and rejects incomplete
     * streams. A healthy drain consumes events; input/lifecycle preflight failures
     * retain them. End geometry is absent; endReason is meaningful only for End.
     * 在所有者线程于操作结束后读取拥有数据的事件，按操作顺序交付，步进/生命周期操作内按配对值排序。
     * 休眠接触仍跟踪，唤醒后的观测产生持续事件；队列/捕获失败使世界故障，拒绝不完整流。
     * 正常读取消耗事件，输入/生命周期预检失败保留事件；结束事件无几何，结束原因仅用于结束。 */
    [[nodiscard]] auto drainContactEvents() -> Core::Result<std::vector<ContactEvent>>;
    /** Teleport without resetting velocity; moving bodies wake up.
     * 传送位置而不清除速度，运动刚体会被唤醒。 */
    [[nodiscard]] auto setPose(BodyId body, Math::Vec3 position, Math::Quat rotation = {})
        -> Core::Result<void>;
    [[nodiscard]] auto setVelocity(BodyId body, Math::Vec3 velocity) -> Core::Result<void>;
    [[nodiscard]] auto setAngularVelocity(BodyId body, Math::Vec3 velocity) -> Core::Result<void>;
    /** Dynamic bodies only; reject impulses whose resulting speed exceeds 500.
     * 仅支持动态刚体，拒绝使结果速度超过 500 的冲量。 */
    [[nodiscard]] auto addImpulse(BodyId body, Math::Vec3 impulse) -> Core::Result<void>;
    /** Set velocity to reach a target over [0.000001,0.25] seconds; pose changes
     * on subsequent steps. Linear/angular speeds are limited to 500 m/s and
     * 15*pi rad/s; call again to stop at the target.
     * 设置在 [0.000001,0.25] 秒内到达目标的速度，位姿在后续步进时改变；
     * 线/角速度上限为 500 米/秒与 15*pi 弧度/秒，到达后需再次调用以停止。 */
    [[nodiscard]] auto moveKinematic(BodyId body, Math::Vec3 position, Math::Quat rotation,
                                     double seconds) -> Core::Result<void>;
    /** Cast displacement of length >=0.001 with both endpoints within position
     * limits. Includes sensors and both triangle faces; no hit returns nullopt.
     * 沿长度至少为 0.001、两端均在位置范围内的位移投射射线；包含传感器及三角形双面，未命中返回
     * nullopt。 */
    [[nodiscard]] auto rayCast(Math::Vec3 origin, Math::Vec3 displacement) const
        -> Core::Result<std::optional<RayHit>>;
    /** Radius [0.001,10000] metres; finite center endpoints within +/-1e6 and
     * displacement length >=0.001. Sensors are excluded by default; ignoreCollider
     * must be a current body or character of this world.
     * 半径为 [0.001,10000] 米，有限中心端点限制在正负一百万内，位移长度至少 0.001。
     * 默认排除传感器，忽略句柄须为本世界现存刚体或角色。 */
    [[nodiscard]] auto sphereCast(Math::Vec3 origin, float radius, Math::Vec3 displacement,
                                  const SphereCastFilter& filter = {}) const
        -> Core::Result<std::optional<SphereHit>>;

  private:
    struct Impl;
    explicit World(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Physics

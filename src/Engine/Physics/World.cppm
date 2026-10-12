module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

export module Dot.Physics.World;

export import Dot.Physics.Body;

import Dot.Core.Result;
import Dot.Math;

export namespace Dot::Physics {
  struct BodyId {
    std::uint64_t world{};
    std::uint64_t value{};
    auto operator==(const BodyId&) const -> bool = default;
  };

  struct WorldConfig {
    std::uint32_t maxBodies{1024};
    Math::Vec3 gravity{0, -9.81F, 0};
    std::uint32_t workerThreads{};
    std::uint32_t maxContactEvents{65536};
  };

  struct BodyState {
    Motion motion;
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
  };

  struct RayHit {
    BodyId body;
    float fraction;
    Math::Vec3 point;
    /** Fraction zero may be inside the shape and has no surface normal.
     * 比例为零可能位于形状内部，此时没有表面法线。 */
    std::optional<Math::Vec3> normal;
  };

  enum class ContactPhase { Begin, Stay, Sleep, End };

  enum class ContactEndReason { Separated, PoseChanged, Destroyed, Reconfigured };

  struct ContactParty {
    BodyId body;
    std::uint64_t userTag;
  };

  struct ContactPoint {
    Math::Vec3 first;
    Math::Vec3 second;
  };

  /** Last observed pre-solver manifold for the step, with normal from the lower
   * public body value toward the higher. Negative depth denotes speculative contact.
   * 此步最近观测的求解前接触流形，法线从较小公开刚体值指向较大值，负深度表示预测接触。 */
  struct ContactGeometry {
    Math::Vec3 normal;
    float penetrationDepth;
    std::vector<ContactPoint> points;
  };

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
     * 重力大小不超过 1000，接触事件队列容量为 1..262144。 */
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
    [[nodiscard]] auto destroyBody(BodyId body) -> Core::Result<void>;
    [[nodiscard]] auto body(BodyId body) const -> Core::Result<BodyState>;
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
     * limits. Includes sensors; no hit returns nullopt.
     * 沿长度至少为 0.001、两端均在位置范围内的位移投射射线；包含传感器，未命中返回 nullopt。 */
    [[nodiscard]] auto rayCast(Math::Vec3 origin, Math::Vec3 displacement) const
        -> Core::Result<std::optional<RayHit>>;

  private:
    struct Impl;
    explicit World(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Physics

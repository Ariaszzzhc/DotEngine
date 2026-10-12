module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

export module Dot.Physics.World;

import Dot.Core.Result;
import Dot.Math;

export namespace Dot::Physics {
  enum class Motion { Static, Kinematic, Dynamic };

  enum class Shape { Box, Sphere, Capsule };

  struct BodyId {
    std::uint64_t world{};
    std::uint64_t value{};
    auto operator==(const BodyId&) const -> bool = default;
  };

  struct WorldConfig {
    std::uint32_t maxBodies{1024};
    Math::Vec3 gravity{0, -9.81F, 0};
    std::uint32_t workerThreads{};
  };

  /** Metres, kilograms, seconds; all fields must be finite. Shape dimensions
   * are [0.001,10000], mass [0.01,1000000], friction/restitution [0,1], gravity
   * factor [0,10], position components [-1000000,1000000], speed at most 500.
   * Rotation is normalized. Static bodies require zero velocity.
   * 单位为米、千克、秒，所有字段须有限；形状尺寸范围 [0.001,10000]，质量
   * [0.01,1000000]，摩擦/恢复系数 [0,1]，重力倍数 [0,10]，位置分量
   * [-1000000,1000000]，速度不超过 500；旋转会归一化，静态刚体速度须为零。
   */
  struct BodyConfig {
    Motion motion{Motion::Dynamic};
    Shape shape{Shape::Sphere};
    Math::Vec3 halfExtents{0.5F, 0.5F, 0.5F};
    float radius{0.5F};
    float halfHeight{0.5F};
    Math::Vec3 position{};
    Math::Quat rotation{};
    Math::Vec3 velocity{};
    float mass{1};
    float friction{0.5F};
    float restitution{};
    float gravityFactor{1};
    bool sensor{};
  };

  struct BodyState {
    Motion motion;
    Math::Vec3 position;
    Math::Quat rotation;
    Math::Vec3 velocity;
    bool active;
    bool sensor;
  };

  struct WorldStats {
    std::size_t bodies;
    std::size_t active;
  };

  struct RayHit {
    BodyId body;
    float fraction;
    Math::Vec3 point;
    /** Fraction zero may be inside the shape and has no surface normal.
     * 比例为零可能位于形状内部，此时没有表面法线。 */
    std::optional<Math::Vec3> normal;
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
     * Gravity magnitude is at most 1000. 1..4096 个刚体、0..8 个工作线程；零表示
     * 同步执行而不启动工作线程，重力大小不超过 1000。 */
    [[nodiscard]] static auto create(const WorldConfig& config = {}) -> Core::Result<World>;
    World(const World&) = delete;
    auto operator=(const World&) -> World& = delete;
    World(World&&) noexcept;
    auto operator=(World&&) noexcept -> World&;
    ~World();

    [[nodiscard]] auto createBody(const BodyConfig& config = {}) -> Core::Result<BodyId>;
    [[nodiscard]] auto destroyBody(BodyId body) -> Core::Result<void>;
    [[nodiscard]] auto body(BodyId body) const -> Core::Result<BodyState>;
    /** Step [0.000001,0.25] seconds, subdivided at 60 Hz. Invalid input does not
     * advance time. Backend errors can occur after advancement and fault the
     * world: reads/destruction remain available, further mutations fail.
     * 同步步进 [0.000001,0.25] 秒，按 60 Hz 细分；无效输入不推进时间。
     * 后端错误可能发生在推进后，世界进入故障状态，仅保留读取与销毁操作。 */
    [[nodiscard]] auto step(double seconds) -> Core::Result<void>;
    [[nodiscard]] auto stats() const -> Core::Result<WorldStats>;
    /** Teleport without resetting velocity; moving bodies wake up.
     * 传送位置而不清除速度，运动刚体会被唤醒。 */
    [[nodiscard]] auto setPose(BodyId body, Math::Vec3 position, Math::Quat rotation = {})
        -> Core::Result<void>;
    [[nodiscard]] auto setVelocity(BodyId body, Math::Vec3 velocity) -> Core::Result<void>;
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

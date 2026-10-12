module;

export module Dot.Physics.Body;

import Dot.Core.Result;
import Dot.Math;

export namespace Dot::Physics {
  enum class Motion { Static, Kinematic, Dynamic };

  enum class Shape { Box, Sphere, Capsule };

  /** Pose-independent authoring properties in metres, kilograms, and seconds.
   * Every field must be finite, including dimensions for inactive shape variants.
   * Shape dimensions are [0.001,10000], mass [0.01,1000000], friction/restitution
   * [0,1], gravity factor [0,10], speed at most 500; static velocity must be zero.
   * 与位姿无关的编辑属性，单位为米、千克、秒；所有字段须有限，包括非当前形状的尺寸。
   * 形状尺寸范围 [0.001,10000]，质量 [0.01,1000000]，摩擦/恢复系数 [0,1]，
   * 重力倍数 [0,10]，速度不超过 500，静态刚体速度须为零。
   */
  struct BodyProperties {
    Motion motion{Motion::Dynamic};
    Shape shape{Shape::Sphere};
    Math::Vec3 halfExtents{0.5F, 0.5F, 0.5F};
    float radius{0.5F};
    float halfHeight{0.5F};
    Math::Vec3 velocity{};
    float mass{1};
    float friction{0.5F};
    float restitution{};
    float gravityFactor{1};
    bool sensor{};
    auto operator==(const BodyProperties&) const -> bool = default;
  };

  /** Initial world-space pose plus body properties. Position components require
   * [-1000000,1000000]; a finite nonzero quaternion is normalized by validation.
   * 初始世界位姿与刚体属性，位置分量须在 [-1000000,1000000]；校验会归一化有限非零四元数。 */
  struct BodyConfig : BodyProperties {
    Math::Vec3 position{};
    Math::Quat rotation{};
    /** Initial angular speed is at most 15*pi rad/s; static requires zero.
     * 初始角速度不超过 15*pi 弧度/秒，静态刚体须为零。 */
    Math::Vec3 angularVelocity{};
    /** Inactive moving bodies remain asleep until woken by another operation.
     * 非活动的运动刚体保持休眠，直到其他操作将其唤醒。 */
    bool startActive{true};
  };

  /** CPU-only checks shared by authoring, persistence, and simulation boundaries.
   * 编辑、存储及仿真边界共用的纯 CPU 校验。 */
  [[nodiscard]] auto validateBodyProperties(const BodyProperties& properties) -> Core::Result<void>;
  [[nodiscard]] auto validatedBodyConfig(const BodyConfig& config) -> Core::Result<BodyConfig>;
}  // namespace Dot::Physics

module;

#include <cstdint>
#include <optional>
#include <string>

export module Dot.Physics.Body;

import Dot.Core.Result;
import Dot.Math;
export import Dot.Physics.Mesh;

export namespace Dot::Physics {
  enum class Motion { Static, Kinematic, Dynamic };

  enum class Shape { Box, Sphere, Capsule, Mesh };

  /** Continuous uses native linear shape casting for solid dynamic bodies.
   * Rotation is not swept, and crossed sensors are not CCD contacts.
   * 连续模式对实体动态刚体使用原生线性形状投射，不扫掠旋转，不保证跨越传感器的接触。 */
  enum class CollisionDetection { Discrete, Continuous };

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
    CollisionDetection collisionDetection{CollisionDetection::Discrete};
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
    /** Optional native mesh resource identity. Scene mesh colliders require it;
     * low-level worlds consume supplied geometry without file access. Other shapes
     * require null. Mesh colliders are solid/static, with zero velocity and discrete CCD.
     * 可选原生网格资源身份；场景网格碰撞体须提供，底层世界直接使用几何而不访问文件。
     * 其他形状须为空；网格碰撞体仅支持实体静态、零速度、离散碰撞检测。 */
    std::optional<std::string> meshAsset{};
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
    /** Opaque runtime association, copied into contact events and never dereferenced.
     * 不透明的运行时关联值，复制到接触事件中而不作为指针解引用。 */
    std::uint64_t userTag{};
    /** Owned geometry and signed scale for Mesh; other shapes require an empty
     * mesh and unit scale. 非网格形状要求空几何及单位缩放，Mesh 使用拥有的几何及带符号缩放。 */
    TriangleMesh mesh{};
    Math::Vec3 meshScale{1, 1, 1};
  };

  /** CPU-only checks shared by authoring, persistence, and simulation boundaries.
   * 编辑、存储及仿真边界共用的纯 CPU 校验。 */
  [[nodiscard]] auto validateBodyProperties(const BodyProperties& properties) -> Core::Result<void>;
  [[nodiscard]] auto validatedBodyConfig(const BodyConfig& config) -> Core::Result<BodyConfig>;
  /** Apply the simulation's world-scale policy: absolute box dimensions, uniform
   * sphere/capsule scale, signed mesh scale. Validate the complete resulting body.
   * 应用仿真的世界缩放规则：盒体绝对尺寸、球体/胶囊等比缩放、网格带符号缩放，
   * 并校验完整结果。 */
  [[nodiscard]] auto scaledBodyConfig(const BodyProperties& properties,
                                      const Math::TransformParts& pose, TriangleMesh mesh = {})
      -> Core::Result<BodyConfig>;
}  // namespace Dot::Physics

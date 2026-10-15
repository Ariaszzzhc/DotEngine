module;

#include <cstdint>

export module Dot.Physics.Character;

import Dot.Core.Result;
import Dot.Math;

export namespace Dot::Physics {
  enum class GroundState { Grounded, Steep, Unsupported, Airborne };

  /** Y-up capsule with its origin at the feet; distances use metres. Radius and
   * cylinder half-height are [0.05,5], slope [1,85] degrees, step/stick [0,1],
   * mass [1,1000], strength [0,10000], jump speed [0,30], contact budget [1,4096].
   * Y 向上、脚底为原点的胶囊；距离单位为米。半径/圆柱半高、坡度、台阶/贴地距离、
   * 质量、推力、跳跃速度与接触预算均按上述范围校验。 */
  struct CharacterProperties {
    float radius{0.35F};
    float halfHeight{0.55F};
    float maxSlopeDegrees{50};
    float stepHeight{0.35F};
    float stickDistance{0.3F};
    float mass{75};
    float maxStrength{100};
    float jumpSpeed{6};
    std::uint32_t maxContacts{256};
    auto operator==(const CharacterProperties&) const -> bool = default;
  };

  /** Initial feet position within +/- one million metres; finite yaw within
   * +/- one million degrees is normalized on validation. No tilt or roll.
   * 初始脚底位置限制在正负一百万米，有限朝向角限制在正负一百万度并归一化；不支持倾斜。 */
  struct CharacterConfig : CharacterProperties {
    Math::Vec3 position{};
    float yawDegrees{};
  };

  /** World-space horizontal desired speed (Y must be zero), magnitude <=50 m/s.
   * Jump is an edge request, consumed on the next substep, including in air.
   * 世界空间期望水平速度，Y 必须为零，大小不超过 50 米/秒；跳跃是边沿请求，
   * 在下个子步消耗，空中请求同样消耗。 */
  struct CharacterInput {
    Math::Vec3 velocity{};
    bool jump{};
  };

  [[nodiscard]] auto validateCharacterProperties(const CharacterProperties& properties)
      -> Core::Result<void>;
  [[nodiscard]] auto validatedCharacterConfig(const CharacterConfig& config)
      -> Core::Result<CharacterConfig>;
  [[nodiscard]] auto validateCharacterInput(const CharacterInput& input) -> Core::Result<void>;
}  // namespace Dot::Physics

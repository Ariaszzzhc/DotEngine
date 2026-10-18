module;

#include <cstddef>
#include <cstdint>
#include <span>

export module Dot.Navigation.Steering;

import Dot.Core.Result;
import Dot.Math;
import Dot.Navigation.Surface;

export namespace Dot::Navigation {
  struct SteeringBody {
    std::uint64_t id{};
    Math::Vec3 position, velocity, desired;
    float radius{.35F};
    float height{1.8F};
  };

  struct SteeringSettings {
    float maxSpeed{3.2F};
    float neighborDistance{4};
    float timeHorizon{2.5F};
    float margin{.04F};
  };

  struct SteeringResult {
    Math::Vec3 velocity;
    std::size_t neighbors{}, samples{};
  };

  /** Bounded native reciprocal velocity sampling from one measured snapshot.
   * Up to 256 bodies, 64 nearby vertically overlapping circles and 512 oriented
   * navigation walls; unknown/exhausted capacity fails. Own vendor scratch per call;
   * immutable input may be shared. Positions are never integrated or published here.
   * Prediction is local, not a collision-free guarantee; physics decides movement.
   * 从同一实际观测快照进行有界原生互惠速度采样；至多 256 角色、64 个邻近垂直重叠圆和
   * 512 有向导航边界。容量不足明确失败；每次调用独占第三方工作区，可共享不可变输入。
   * 不在此积分或发布位置；局部预测不保证绝不碰撞，移动由物理决定。 */
  [[nodiscard]] auto steer(const SteeringBody& body, std::span<const SteeringBody> neighbors,
                           std::span<const WallSegment> walls,
                           const SteeringSettings& settings = {}) -> Core::Result<SteeringResult>;
}  // namespace Dot::Navigation

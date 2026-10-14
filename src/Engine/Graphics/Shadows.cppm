module;

#include <array>
#include <cstdint>
#include <span>

export module Dot.Graphics.Shadows;

import Dot.Core.Result;
import Dot.Math;
import Dot.Graphics.Geometry;

export namespace Dot::Graphics {
  /** Directional cascaded shadows: power-of-two resolution 128..2048, 1..4 cascades,
   * camera depth coverage .1..10000 meters, practical split blend [0,1]. Biases are
   * normalized depth [0,.02], normal offset [0,.2] meters, PCF radius [0,8] texels.
   * 方向光级联阴影：二次幂分辨率 128..2048、1..4 级，相机深度覆盖 .1..10000 米；
   * 分段混合 [0,1]，归一化深度偏移 [0,.02]，法线偏移 [0,.2] 米，PCF 半径 [0,8] 纹素。 */
  struct ShadowSettings {
    std::uint32_t resolution{1024};
    std::uint32_t cascades{4};
    float distance{100};
    float splitLambda{0.65F};
    float depthBias{0.0002F};
    float slopeBias{0.001F};
    float normalBias{0.01F};
    float filterRadius{2};
    auto operator==(const ShadowSettings&) const -> bool = default;
  };

  struct ShadowCascades {
    std::array<Math::Mat4, 4> matrices;
    std::array<float, 4> splits{};
    std::array<float, 4> texelSize{};
    Math::Vec3 origin;
    Math::Vec3 forward;
    std::uint32_t count;
    float nearDepth;
  };

  [[nodiscard]] auto validateShadowSettings(const ShadowSettings& settings) -> Core::Result<void>;

  /** CPU-only fit of a finite [0,1] camera frustum and at most 10000 world AABBs
   * within +/-1e6 meters. Camera origin must precede its near plane. Square sphere
   * fitting and light-space texel snapping stabilize rotation/translation; adjacent
   * cascades overlap ten percent for filtering. Off-camera casters overlapping a
   * cascade's XY footprint extend its depth, avoiding clipped shadows.
   * 仅 CPU 拟合有限 [0,1] 相机视锥和最多 10000 个世界 AABB（正负 1e6 米）；相机须在
   * 近平面之前。方形包围球及光空间纹素对齐稳定旋转/移动，相邻级重叠百分之十以供过滤；
   * 与级联 XY 范围相交的画外投影体扩展深度，避免阴影被裁剪。 */
  [[nodiscard]] auto fitDirectionalShadows(const Math::Mat4& viewProjection, Math::Vec3 camera,
                                           Math::Vec3 lightDirection,
                                           const ShadowSettings& settings,
                                           std::span<const Bounds> casters = {})
      -> Core::Result<ShadowCascades>;
}  // namespace Dot::Graphics

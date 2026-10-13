module;

#include <optional>
#include <vector>

export module Dot.Graphics.Lighting;

import Dot.Core.Result;
import Dot.Math;

export namespace Dot::Graphics {
  enum class LightKind { Directional, Point, Spot };

  /** World-space direct light. Direction points along emitted rays and is
   * normalized when stored for directional/spot lights; point directions are unused
   * but finite. Linear RGB is [0,1]; intensity is [0,1e6], in lux for
   * directional lights and candela for point/spot lights. Point/spot range is
   * [0.01,1e6] world meters; smooth falloff reaches zero at range. Spot cones are
   * angles from the axis, inner >=0, outer <=90, separated by at least 0.1 degree.
   * 世界空间直接光，方向沿发射光线；方向/聚光保存时归一化，点光方向不用但须有限。
   * 线性 RGB 为 [0,1]，强度 [0,1e6]，
   * 方向光单位 lux，点/聚光单位 candela。点/聚光范围为 [0.01,1e6] 米，平滑衰减至零；
   * 聚光锥角相对轴线，内角 >=0、外角 <=90，至少相差 0.1 度。 */
  struct Light {
    LightKind kind{LightKind::Directional};
    Math::Vec3 direction{0, -1, 0};
    Math::Vec3 position{};
    Math::Vec3 color{1, 1, 1};
    float intensity{1};
    float range{10};
    float innerConeDegrees{20};
    float outerConeDegrees{30};
  };

  /** Owned frame lighting, at most 64 lights. Camera/light positions are finite
   * and within +/-1e6 meters. Empty lighting supplies no invented ambient light.
   * 拥有的帧光照，最多 64 光源；相机/光源位置有限且在正负 1e6 米内；
   * 空光照不补充虚构环境光。 */
  struct Lighting {
    Math::Vec3 viewPosition{0, 0, 1};
    std::vector<Light> lights;
    /** Optional emitted view-ray direction for orthographic cameras, normalized on
     * storage. Absence uses perspective rays from viewPosition.
     * 可选正交相机发射视线方向，保存时归一化；缺省按 viewPosition 计算透视视线。 */
    std::optional<Math::Vec3> viewDirection{};
  };

  [[nodiscard]] auto validatedLighting(const Lighting& lighting) -> Core::Result<Lighting>;
}  // namespace Dot::Graphics

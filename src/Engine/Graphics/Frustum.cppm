module;

#include <array>

export module Dot.Graphics.Frustum;

import Dot.Core.Result;
import Dot.Math;
export import Dot.Graphics.Geometry;

export namespace Dot::Graphics {
  /** Conservative six-plane homogeneous clip test for right-handed [0,1] depth.
   * Tests transformed local AABB corners without dividing by W, retaining boundary
   * contact, reflection, shear and projective models. False means definitely outside;
   * true may include false positives. Double intermediates and a float-roundoff margin
   * prevent visible geometry disappearing at clip boundaries. No device/resources.
   * 右手 [0,1] 深度的保守齐次六平面裁剪；变换局部 AABB 角点而不除 W，保留边界接触、
   * 反射、错切和投影模型。false 确定在外，true 可含保守误判；双精度中间量与浮点裕量
   * 避免裁面边缘可见几何消失，不拥有设备/资源。 */
  class Frustum {
  public:
    [[nodiscard]] static auto create(const Math::Mat4& viewProjection) -> Core::Result<Frustum>;
    [[nodiscard]] auto intersects(const Bounds& local, const Math::Mat4& world = {}) const
        -> Core::Result<bool>;

  private:
    Frustum() = default;
    std::array<std::array<double, 4>, 6> m_planes;
  };
}  // namespace Dot::Graphics

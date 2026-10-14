module;

#include <cstdint>

export module Dot.Graphics.View;

import Dot.Core.Result;
import Dot.Math;
export import Dot.Graphics.Geometry;

export namespace Dot::Graphics {
  /** Independent camera pose/lens; no scene entity or GPU resource ownership.
   * Position is bounded to +/-1e6, clip planes to [.001,1e6], aspect [.01,100].
   * 独立相机位姿/镜头，不拥有场景实体或 GPU 资源；位置 +/-1e6，裁面 [.001,1e6]，纵横比 [.01,100]。
   */
  struct View {
    Math::Vec3 position{6, 4, 8};
    Math::Quat rotation{};
    float verticalFovDegrees{60};
    float nearPlane{0.05F};
    float farPlane{10000};
    bool orthographic{};
    float orthographicHeight{10};
    auto operator==(const View&) const -> bool = default;
  };

  [[nodiscard]] auto validateView(const View& view, float aspect = 1) -> Core::Result<void>;
  [[nodiscard]] auto viewProjection(const View& view, float aspect) -> Core::Result<Math::Mat4>;
  /** Crop one source framebuffer pixel to a 1x1 clip region, preserving Z/W.
   * Top-left UV is [0,1); the chosen pixel center is used consistently at any DPI.
   * 将源帧缓冲单个像素裁为 1x1 裁剪区域，保留 Z/W；左上 UV 为 [0,1)，始终使用像素中心。 */
  [[nodiscard]] auto pixelProjection(const Math::Mat4& viewProjection, std::uint32_t width,
                                     std::uint32_t height, Math::Vec2 uv)
      -> Core::Result<Math::Mat4>;
  [[nodiscard]] auto transformedBounds(const Bounds& bounds, const Math::Mat4& transform)
      -> Core::Result<Bounds>;
}  // namespace Dot::Graphics

module;

#include <optional>
#include <vector>

export module Dot.Graphics.Overlay;

import Dot.Core.Result;
import Dot.Math;

export namespace Dot::Graphics {
  struct OverlaySegment {
    Math::Vec2 from;
    Math::Vec2 to;
  };

  struct OverlayLine {
    OverlaySegment segment;
    Math::Vec4 color{0, 1, 1, 1};
    float thickness{2};
  };

  struct OverlayPoint {
    Math::Vec2 position;
    Math::Vec4 color{0, 1, 1, 1};
    float radius{3};
  };

  /** Display-color geometry in normalized top-left viewport UVs [0,1]. Sizes use
   * logical GUI pixels. At most 4096 lines/4096 points, sizes [.5,16], RGBA [0,1].
   * 以视口左上为原点的归一化 UV [0,1] 显示色几何；尺寸使用 GUI 逻辑像素。
   * 至多各 4096 条线/个点，尺寸 [.5,16]，RGBA [0,1]。 */
  struct ViewportOverlay {
    std::vector<OverlayLine> lines;
    std::vector<OverlayPoint> points;
  };

  [[nodiscard]] auto validateOverlay(const ViewportOverlay& overlay) -> Core::Result<void>;

  /** Homogeneous clipping against x/y in [-w,w], z in [0,w] and positive w.
   * Crossing segments retain their visible interval, including near-plane cuts.
   * Creation requires a finite invertible matrix; invalid input is distinct from
   * valid geometry outside the frustum. No device, scene or GUI is initialized.
   * 对 x/y∈[-w,w]、z∈[0,w] 及正 w 做齐次裁剪，跨裁面线段保留可见区间。
   * 创建要求有限可逆矩阵，无效输入与视锥外几何分别返回错误/缺席，不初始化设备/场景/GUI。 */
  class OverlayProjector {
  public:
    [[nodiscard]] static auto create(const Math::Mat4& viewProjection)
        -> Core::Result<OverlayProjector>;
    [[nodiscard]] auto point(Math::Vec3 world) const -> Core::Result<std::optional<Math::Vec2>>;
    [[nodiscard]] auto segment(Math::Vec3 from, Math::Vec3 to) const
        -> Core::Result<std::optional<OverlaySegment>>;

  private:
    explicit OverlayProjector(Math::Mat4 viewProjection) noexcept;
    Math::Mat4 m_viewProjection;
  };
}  // namespace Dot::Graphics

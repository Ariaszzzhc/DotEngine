module;

#include <span>

export module Dot.Graphics.Skinning;

import Dot.Core.Result;
import Dot.Math;
export import Dot.Animation.Skin;
import Dot.Graphics.Geometry;

export namespace Dot::Graphics {
  /** Owned CPU reference for eight-weight linear blend skinning. Preserve source
   * color/UV/indices, transform normals by inverse-transpose of the blended linear
   * matrix, orthogonalize tangents and retain reflected handedness. Normalized
   * determinant magnitude must exceed 1e-6. Blend orientation must be uniform per
   * primitive; split reflected islands into separate primitives. Invalid samples
   * fail without effects. A baked reflected reference keeps source index order;
   * a static raster comparison must reverse its winding explicitly.
   * 八权重线性混合蒙皮的独立 CPU 参考；保留源颜色/UV/索引，法线用混合线性矩阵的逆转置，
   * 切线重新正交并保留镜像手性。归一化行列式绝对值须大于 1e-6，同一 primitive 混合朝向须
   * 一致；不同镜像岛须拆 primitive。无效采样无副作用；烘焙参考保留源索引，静态光栅对照
   * 镜像结果时须显式反转绕序。 */
  [[nodiscard]] auto deformMesh(const MeshData& mesh, const Animation::Skin& skin,
                                std::span<const Math::Mat4> palette) -> Core::Result<MeshData>;
}  // namespace Dot::Graphics

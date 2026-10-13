module;

#include <cstdint>
#include <vector>

export module Dot.Graphics.Geometry;

import Dot.Core.Result;
import Dot.Math;

export namespace Dot::Graphics {
  struct Vertex {
    Math::Vec3 position;
    Math::Vec4 color{1, 1, 1, 1};
    /** Unit surface normal; zero explicitly denotes an absent attribute.
     * 单位表面法线，零向量明确表示该属性缺失。 */
    Math::Vec3 normal{};
    /** Finite UV coordinates; values outside [0,1] are retained for tiling.
     * 有限 UV 坐标，保留 [0,1] 之外的值用于平铺。 */
    Math::Vec2 texCoord{};
  };

  /** Triangle-list geometry, indexed when indices are present. Colors are linear
   * [0,1]; positions must be finite. Validation bounds resource allocation.
   * 三角形列表几何数据，存在索引时使用索引绘制；颜色为线性 [0,1]，位置须有限。
   * 校验会限制资源分配规模。
   */
  struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
  };

  struct Bounds {
    Math::Vec3 minimum;
    Math::Vec3 maximum;
  };

  [[nodiscard]] auto validateMesh(const MeshData& mesh) -> Core::Result<void>;
  [[nodiscard]] auto meshBounds(const MeshData& mesh) -> Core::Result<Bounds>;

  struct MeshSurface {
    Bounds bounds;
    bool hasNormals{};
    bool hasNormalMapFrame{};
  };

  /** Analyze validated triangles without a device. Normal maps require complete
   * normals, nondegenerate positions/UVs, UV magnitudes <=1e6, absolute UV area
   * >=1e-12 and relative area >1e-6. Shares renderer/export surface requirements.
   * 无设备分析已校验三角面；法线贴图要求完整法线、非退化位置/UV、UV 绝对值 <=1e6，
   * UV 绝对面积 >=1e-12 且相对面积 >1e-6；渲染器和出包共用表面要求。 */
  [[nodiscard]] auto meshSurface(const MeshData& mesh) -> Core::Result<MeshSurface>;

  /** Produce independently owned, non-indexed triangles with winding-derived
   * flat normals. Preserve colors/UVs and the source; reject degenerate faces
   * and expansion beyond one million vertices before allocating the result.
   * 产生独立拥有的无索引三角形，按绕序计算平面法线；保留颜色/UV 和源数据。
   * 拒绝退化面，并在结果分配前拒绝超过一百万顶点的展开。 */
  [[nodiscard]] auto withFlatNormals(const MeshData& mesh) -> Core::Result<MeshData>;
  /** Owned UV sphere with radial normals, explicit seam/pole vertices and outward
   * triangles. Radius is finite positive; sectors 3..256, rings 2..128. Reject
   * position precision that collapses faces. V=0 is the north pole.
   * 拥有的 UV 球带径向法线、显式接缝/极点顶点和向外三角形；半径有限正值，
   * 经向分段 3..256、纬向分段 2..128，拒绝位置精度使面塌缩的情况；V=0 为北极。 */
  [[nodiscard]] auto sphere(float radius = 0.5F, std::uint32_t sectors = 48,
                            std::uint32_t rings = 24) -> Core::Result<MeshData>;
  /** Centered primitives with unit face normals and full-face UVs (V=0 at top).
   * Cubes split 24 face-corner vertices to preserve sharp normals/UV seams.
   * Scaled to positive finite dimensions.
   * 居中几何体具有单位面法线及完整面 UV（顶部 V=0），立方体拆为 24 个面角顶点，
   * 保留硬边法线与 UV 接缝；按正值有限尺寸缩放。 */
  [[nodiscard]] auto cube(Math::Vec3 size = {1, 1, 1}) -> Core::Result<MeshData>;
  [[nodiscard]] auto quad(Math::Vec2 size = {1, 1}) -> Core::Result<MeshData>;
}  // namespace Dot::Graphics

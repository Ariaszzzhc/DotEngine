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
  /** Centered unit primitives, scaled to positive finite dimensions.
   * 居中单位几何体，按正值有限尺寸缩放。 */
  [[nodiscard]] auto cube(Math::Vec3 size = {1, 1, 1}) -> Core::Result<MeshData>;
  [[nodiscard]] auto quad(Math::Vec2 size = {1, 1}) -> Core::Result<MeshData>;
}  // namespace Dot::Graphics

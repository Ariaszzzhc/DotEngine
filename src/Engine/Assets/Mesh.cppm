module;

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

export module Dot.Assets.Mesh;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Graphics.Geometry;

export namespace Dot::Assets {
  enum class MeshFormat { DotMesh, Obj };

  struct MeshAsset {
    Graphics::MeshData data;
    std::string warnings;
  };

  /** Decode at most 64 MiB. DotMesh uses eight magic bytes DOTMESH\0, little-endian
   * uint32 vertex/index counts, sixteen float32 values per vertex (position,
   * color, normal, UV, tangent), then uint32 indices. Exact length, no padding,
   * native-struct dumps or alternate layouts. OBJ
   * imports geometry, colors, authored normals and 2D UVs, splitting attribute
   * seams. Normals are normalized; zero authored normals fail. Missing normals
   * remain explicitly absent. Group metadata loss is reported; materials,
   * volumetric UVs and free-form geometry are rejected. No referenced files load.
   * 解码最多 64 MiB；DotMesh 为八字节 DOTMESH\0、小端 uint32 顶点/索引数量，
   * 每顶点十六个 float32（位置、颜色、法线、UV、切线），最后为 uint32 索引。
   * 严格匹配长度，无填充、原生结构体转储或替代布局。
   * OBJ 导入几何、颜色、作者法线及二维 UV，拆分属性接缝；法线归一化，零作者法线
   * 失败，缺失法线明确保留为缺失。分组元数据丢失报告警告，拒绝材质、体积 UV 和自由曲面。
   * 不加载引用的外部文件。
   */
  [[nodiscard]] auto decodeMesh(std::span<const std::byte> source, MeshFormat format)
      -> Core::Result<MeshAsset>;
  [[nodiscard]] auto encodeMesh(const Graphics::MeshData& mesh)
      -> Core::Result<std::vector<std::byte>>;
  [[nodiscard]] auto loadMesh(const Core::FileSystem& files, const std::filesystem::path& relative)
      -> Core::Result<MeshAsset>;
  /** Save DotMesh only, using atomic replacement; the parent must exist.
   * 仅保存 DotMesh，使用原子替换，父目录须存在。 */
  [[nodiscard]] auto saveMesh(const Core::FileSystem& files, const std::filesystem::path& relative,
                              const Graphics::MeshData& mesh) -> Core::Result<void>;
}  // namespace Dot::Assets

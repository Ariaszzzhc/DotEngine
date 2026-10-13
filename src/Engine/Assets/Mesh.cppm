module;

#include <filesystem>
#include <string>
#include <string_view>

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

  /** Decode at most 64 MiB. DotMesh is a strict JSON document. OBJ
   * imports geometry, colors, authored normals and 2D UVs, splitting attribute
   * seams. Normals are normalized; zero authored normals fail. Missing normals
   * remain explicitly absent. Group metadata loss is reported; materials,
   * volumetric UVs and free-form geometry are rejected. No referenced files load.
   * 解码最多 64 MiB；DotMesh 使用严格 JSON 格式。
   * OBJ 导入几何、颜色、作者法线及二维 UV，拆分属性接缝；法线归一化，零作者法线
   * 失败，缺失法线明确保留为缺失。分组元数据丢失报告警告，拒绝材质、体积 UV 和自由曲面。
   * 不加载引用的外部文件。
   */
  [[nodiscard]] auto decodeMesh(std::string_view source, MeshFormat format)
      -> Core::Result<MeshAsset>;
  [[nodiscard]] auto encodeMesh(const Graphics::MeshData& mesh) -> Core::Result<std::string>;
  [[nodiscard]] auto loadMesh(const Core::FileSystem& files, const std::filesystem::path& relative)
      -> Core::Result<MeshAsset>;
  /** Save DotMesh only, using atomic replacement; the parent must exist.
   * 仅保存 DotMesh，使用原子替换，父目录须存在。 */
  [[nodiscard]] auto saveMesh(const Core::FileSystem& files, const std::filesystem::path& relative,
                              const Graphics::MeshData& mesh) -> Core::Result<void>;
}  // namespace Dot::Assets

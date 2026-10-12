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

  /** Decode at most 64 MiB. DotMesh is a strict versioned JSON document. OBJ
   * imports geometry and vertex colors; materials and free-form geometry are
   * rejected. Normal/UV/group loss is reported in warnings. All shapes merge into
   * one colored mesh. No referenced files load.
   * 解码最多 64 MiB；DotMesh 使用严格的带版本 JSON 格式。
   * OBJ 导入几何与顶点颜色，拒绝材质和自由曲面；丢弃法线/UV/分组时报告警告。
   * 所有形状合并为一个彩色网格。
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

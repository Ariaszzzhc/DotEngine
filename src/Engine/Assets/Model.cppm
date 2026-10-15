module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Assets.Model;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Math;
export import Dot.Assets.Material;
export import Dot.Graphics.Geometry;
export import Dot.Animation.Clip;

export namespace Dot::Assets {
  struct ModelImage {
    std::vector<std::byte> png;
  };

  struct ModelPrimitive {
    Graphics::MeshData mesh;
    std::uint32_t material{};
  };

  struct ModelNode {
    std::string name;
    Math::TransformParts transform;
    std::optional<std::uint32_t> parent;
    std::vector<std::uint32_t> primitives;
  };

  struct ModelAsset {
    std::vector<ModelImage> images;
    std::vector<MaterialDescription> materials;
    std::vector<ModelPrimitive> primitives;
    std::vector<ModelNode> nodes;
    std::string prefix;
    std::vector<std::string> warnings;
    std::uint32_t scene{};
    /** Channel node IDs index nodes, independently of source ordering.
     * 通道节点 ID 索引 nodes，与源文件顺序无关。 */
    std::vector<Animation::Clip> animations;
  };

  /** Owned glTF/GLB import: triangles/strips/fans, sparse/strided data,
   * PBR PNG/JPEG textures and TRS hierarchy. Select default/first scene
   * unless an index is supplied. Project-relative generated references use prefix.
   * No implicit IO outside source root: reject traversal, symlinks and remote URIs.
   * At most 64 MiB/file, 128 MiB source bytes, 256 MiB decoded pixels, one million
   * aggregate vertices/three million indices and 10000 generated entities. Parser
   * allocation is bounded separately to 128 MiB. Unsupported visual semantics
   * fail, rather than dropping unsupported content. TRS animation supports
   * Step/Linear/CubicSpline; at most 1024 clips, 30000 aggregate channels and one
   * million aggregate keys. Targets outside the selected scene are reported and
   * excluded; all source channels are validated. Matrix targets, skins/morphs fail.
   * Metadata
   * loss is reported. Source files remain unchanged; output owns all data.
   * 拥有式导入，支持三角形/条带/扇形、稀疏/交错数据、PBR 贴图及 TRS 层级。
   * 默认选择默认/首场景，可指定索引；生成路径使用项目相对前缀。不越过源根读取，拒绝
   * 回溯、链接及远程 URI；限制文件/总字节、解码像素、几何及实体，解析分配单独有界。
   * TRS 动画支持三种插值；至多 1024 片段、合计 30000 通道/百万键。所选场景外目标
   * 报告并排除，但仍校验全部源通道。矩阵动画/蒙皮/morph 明确失败；结果独立拥有。 */
  [[nodiscard]] auto loadModel(const Core::FileSystem& files, const std::filesystem::path& source,
                               std::string_view prefix, std::optional<std::uint32_t> scene = {})
      -> Core::Result<ModelAsset>;
  /** Write a prepared bundle into caller-owned empty staging, without publishing.
   * Mesh/material/image/animation files and model.dotscene are native editable
   * assets; model.dotmodel records scene, clips and stable node/entity bindings.
   * 在调用者拥有的空暂存目录写入原生可编辑资产与模型场景，不发布目录。 */
  [[nodiscard]] auto writeModel(const Core::FileSystem& staging, const ModelAsset& model)
      -> Core::Result<void>;
}  // namespace Dot::Assets

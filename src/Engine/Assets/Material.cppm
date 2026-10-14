module;

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

export module Dot.Assets.Material;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Math;
import Dot.Graphics.Renderer;

export namespace Dot::Assets {
  struct TextureSource {
    std::string path;
    Graphics::TextureOptions sampling{};
  };

  /** Owned material authoring, without GPU handles. All texture paths are portable,
   * project-root relative PNG/JPEG paths (not relative to the material file).
   * Color slots may use sRGB; normal/metallic-roughness data must be linear.
   * 拥有的材质编辑描述，不含 GPU 句柄；贴图路径为可移植、项目根相对 PNG/JPEG 路径，
   * 不相对材质文件；颜色槽可用 sRGB，法线/金属度粗糙度数据须在线性空间。 */
  struct MaterialDescription {
    Graphics::SurfaceModel model{Graphics::SurfaceModel::MetallicRoughness};
    Math::Vec4 baseColor{1, 1, 1, 1};
    Math::Vec3 emissive{};
    float metallic{};
    float roughness{0.5F};
    float normalScale{1};
    /** Linear R-channel baked occlusion affects indirect light only; strength [0,1].
     * 线性 R 通道烘焙遮蔽仅影响间接光，强度为 [0,1]。 */
    float occlusionStrength{1};
    bool doubleSided{};
    std::optional<TextureSource> baseColorTexture;
    std::optional<TextureSource> metallicRoughnessTexture;
    std::optional<TextureSource> normalTexture;
    std::optional<TextureSource> emissiveTexture;
    std::optional<TextureSource> occlusionTexture;
  };

  [[nodiscard]] auto validateMaterial(const MaterialDescription& material) -> Core::Result<void>;
  /** Single strict JSON structure, at most 1 MiB; unknown/duplicate/missing fields
   * fail. Texture slots are explicitly null or complete descriptions. Loading only
   * reads the material, not referenced textures; rendering resolves those separately.
   * 单一严格 JSON 结构，最多 1 MiB；未知、重复或缺失字段失败；纹理槽为 null 或完整描述。
   * 加载只读取材质，不读取引用纹理，渲染时单独解析纹理。 */
  [[nodiscard]] auto decodeMaterial(std::string_view source) -> Core::Result<MaterialDescription>;
  [[nodiscard]] auto encodeMaterial(const MaterialDescription& material)
      -> Core::Result<std::string>;
  [[nodiscard]] auto loadMaterial(const Core::FileSystem& files,
                                  const std::filesystem::path& relative)
      -> Core::Result<MaterialDescription>;
  /** Atomic .dotmaterial replacement; existing parent directories are required.
   * 原子替换 .dotmaterial，要求父目录已存在。 */
  [[nodiscard]] auto saveMaterial(const Core::FileSystem& files,
                                  const std::filesystem::path& relative,
                                  const MaterialDescription& material) -> Core::Result<void>;
}  // namespace Dot::Assets

module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module Dot.Editor.AssetPreview;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Graphics.Renderer;
import Dot.Assets.Material;

export namespace Dot::Editor {
  enum class AssetPreviewKind { Image, Mesh, Material };

  /** Classify portable PNG/JPEG, DotMesh/OBJ and material paths without I/O.
   * 不执行 I/O，分类可移植的 PNG/JPEG、DotMesh/OBJ 与材质路径。 */
  [[nodiscard]] auto assetPreviewKind(std::string_view path) -> std::optional<AssetPreviewKind>;

  struct AssetPreviewImage {
    Graphics::Image image;
    std::string warning;
  };

  /** Isolated offscreen device with bounded texture resolution and actual PBR/HDR
   * rendering. Source files are never changed. Studio HDRI is optional and baked
   * at creation; absence explicitly uses direct studio lights only. Calls/moves
   * require the creating thread. Images are owned display-encoded CPU readbacks,
   * suitable for bounded cached GUI uploads, not a zero-copy or async pipeline.
   * 独立离屏设备复用有界贴图解析及实际 PBR/HDR，不修改源文件；可选摄影棚 HDRI 在
   * 创建时烘焙，缺省明确只用摄影棚直射光。调用/移动限创建线程，结果拥有显示编码 CPU
   * 像素，供有界缓存上传至 GUI，不是零拷贝或异步管线。 */
  class AssetPreview {
  public:
    [[nodiscard]] static auto create(const Core::FileSystem& files,
                                     const Graphics::HdrImage* studio = nullptr)
        -> Core::Result<AssetPreview>;
    AssetPreview(const AssetPreview&) = delete;
    auto operator=(const AssetPreview&) -> AssetPreview& = delete;
    AssetPreview(AssetPreview&&) noexcept;
    auto operator=(AssetPreview&&) noexcept -> AssetPreview&;
    ~AssetPreview();
    /** Square output is 16..512 pixels. Image aspect/alpha are retained; meshes
     * are framed from actual bounds with diagnostic neutral PBR shading. Materials
     * shade a UV sphere, including normal maps and coverage. Optional draft is
     * only valid for material paths and requires no on-disk material. Missing or
     * malformed sources/textures/budgets return errors, never sample substitutes.
     * 方形输出为 16..512 像素，图片保留比例与透明度，网格按实际边界取景并使用中性 PBR
     * 预览材质；材质作用于带 UV 的球体，含法线贴图及覆盖模式。仅材质可传草稿且无需
     * 已存在材质文件；缺失/无效源、贴图或预算失败均报告错误，不替换样例。 */
    [[nodiscard]] auto render(std::string_view path, std::uint32_t size = 128,
                              const Assets::MaterialDescription* draft = nullptr)
        -> Core::Result<AssetPreviewImage>;
    [[nodiscard]] auto clearCache() -> Core::Result<void>;
    [[nodiscard]] auto stats() const -> Core::Result<Graphics::ResourceStats>;

  private:
    struct Impl;
    explicit AssetPreview(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Editor

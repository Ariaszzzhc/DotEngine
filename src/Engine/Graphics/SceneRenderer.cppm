module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module Dot.Graphics.SceneRenderer;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Math;
import Dot.World.Scene;
import Dot.Graphics.Renderer;

export namespace Dot::Graphics {
  struct SceneRenderStats {
    std::size_t meshes;
    std::uint64_t bytes;
    std::size_t materials{};
    std::size_t textures{};
    std::uint64_t textureBytes{};
  };

  /** Owns bounded shared mesh/material/texture caches and an optional asset-root
   * descriptor. Byte budget covers logical GPU mesh/texture bytes; at most 4096
   * materials and 4096 textures. Immutable assets reload explicitly via clearCache.
   * Scene light poses and camera view rays come from current world transforms.
   * Borrows a renderer that must stay at the same address and outlive this service.
   * Operations/destruction require the renderer's creating thread.
   * 拥有有界共享网格/材质/贴图缓存及可选资产根目录描述；字节预算覆盖逻辑 GPU 数据，
   * 最多 4096 材质/4096 贴图，缓存不可变资源由 clearCache 显式重载；光源位姿/视线取世界变换。
   * 借用渲染器，须保持其地址且更晚销毁。
   * 操作及销毁须在渲染器的创建线程进行。
   */
  class SceneRenderer {
  public:
    [[nodiscard]] static auto create(Renderer& renderer,
                                     std::optional<Core::FileSystem> assets = std::nullopt,
                                     std::uint64_t maxCacheBytes = 256 * 1024 * 1024)
        -> Core::Result<SceneRenderer>;
    SceneRenderer(const SceneRenderer&) = delete;
    auto operator=(const SceneRenderer&) -> SceneRenderer& = delete;
    SceneRenderer(SceneRenderer&&) noexcept;
    auto operator=(SceneRenderer&&) noexcept -> SceneRenderer&;
    ~SceneRenderer();

    /** Camera override must be in the scene; absence selects its primary camera.
     * Missing camera/assets and budget failures return errors without substitutes.
     * Visible objects share cached meshes; resources no longer referenced by the
     * current visible scene are pruned before loading new ones.
     * 指定相机须属于场景，未指定则使用主相机；缺失相机/资产及预算不足返回错误，不替换内容。
     * 可见对象共享缓存网格；加载新网格前释放当前可见场景不再引用的资源。
     */
    [[nodiscard]] auto render(const World::Scene& scene, std::uint32_t width, std::uint32_t height,
                              Math::Vec4 clear = {0, 0, 0, 1},
                              std::optional<World::EntityId> camera = std::nullopt)
        -> Core::Result<Image>;
    [[nodiscard]] auto present(const World::Scene& scene, Math::Vec4 clear = {0, 0, 0, 1},
                               std::optional<World::EntityId> camera = std::nullopt)
        -> Core::Result<PresentResult>;
    /** Render directly into a persistent GPU target without CPU readback.
     * 直接绘制到持久 GPU 目标，不进行 CPU 读回。 */
    [[nodiscard]] auto renderInto(const World::Scene& scene, RenderTarget& target,
                                  Math::Vec4 clear = {0, 0, 0, 1},
                                  std::optional<World::EntityId> camera = std::nullopt)
        -> Core::Result<void>;
    [[nodiscard]] auto stats() const -> Core::Result<SceneRenderStats>;
    [[nodiscard]] auto warnings() const -> Core::Result<std::vector<std::string>>;
    [[nodiscard]] auto clearCache() -> Core::Result<void>;

  private:
    struct Impl;
    explicit SceneRenderer(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Graphics

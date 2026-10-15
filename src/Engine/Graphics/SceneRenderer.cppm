module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

export module Dot.Graphics.SceneRenderer;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Math;
import Dot.World.Scene;
import Dot.Graphics.Renderer;
export import Dot.Graphics.View;
export import Dot.Assets.Material;

export namespace Dot::Graphics {
  /** Primary game camera, explicit scene camera, or independent editor/tool view.
   * 游戏主相机、指定场景相机或独立编辑/工具视图。 */
  using SceneView = std::variant<std::monostate, World::EntityId, View>;

  struct SceneRenderStats {
    std::size_t meshes;
    std::uint64_t bytes;
    std::size_t materials{};
    std::size_t textures{};
    std::uint64_t textureBytes{};
    std::size_t environments{};
    std::uint64_t environmentBytes{};
  };

  struct MaterialPreview {
    std::string path;
    Assets::MaterialDescription value;
  };

  /** Owns bounded shared mesh/material/texture caches and an optional asset-root
   * descriptor. Byte budget covers logical GPU mesh/texture bytes; at most 4096
   * materials and 4096 textures. Immutable assets reload explicitly via clearCache.
   * Scene light poses and camera view rays come from current world transforms.
   * Scene-owned HDR environments share the cache budget; replacement peak must fit
   * both maps. Saved SSAO settings are authoritative per frame; clearCache/destruction
   * detach only this service's environment/occlusion bindings. AO working textures
   * belong to the renderer and are separate from asset cache budgets.
   * Borrows a renderer that must stay at the same address and outlive this service.
   * Operations/destruction require the renderer's creating thread.
   * 拥有有界共享网格/材质/贴图缓存及可选资产根目录描述；字节预算覆盖逻辑 GPU 数据，
   * 最多 4096 材质/4096 贴图，缓存不可变资源由 clearCache 显式重载；光源位姿/视线取世界变换。
   * 场景 HDR 环境共用缓存预算，替换峰值须容纳两张环境；保存的 SSAO 每帧生效；
   * 清缓存/销毁仅解绑本服务的环境/AO。AO 工作纹理归渲染器，与资产缓存预算分离。
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

    /** Entity view must belong to the scene; monostate selects its primary camera.
     * Independent views require no scene camera and never mutate its entities.
     * Missing camera/assets and budget failures return errors without substitutes.
     * Visible objects share cached meshes; resources no longer referenced by the
     * current visible scene are pruned before loading new ones.
     * 指定相机须属于场景，空选择使用主相机；独立视图无需场景相机且不修改实体。
     * 缺失相机/资产及预算不足返回错误，不替换内容。
     * 可见对象共享缓存网格；加载新网格前释放当前可见场景不再引用的资源。
     */
    [[nodiscard]] auto render(const World::Scene& scene, std::uint32_t width, std::uint32_t height,
                              Math::Vec4 clear = {0, 0, 0, 1}, SceneView view = {})
        -> Core::Result<Image>;
    [[nodiscard]] auto present(const World::Scene& scene, Math::Vec4 clear = {0, 0, 0, 1},
                               SceneView view = {}) -> Core::Result<PresentResult>;
    /** Render directly into a persistent GPU target without CPU readback.
     * 直接绘制到持久 GPU 目标，不进行 CPU 读回。 */
    [[nodiscard]] auto renderInto(const World::Scene& scene, RenderTarget& target,
                                  Math::Vec4 clear = {0, 0, 0, 1}, SceneView view = {})
        -> Core::Result<void>;
    /** GPU-visible opaque entity; selection identity never passes through floats.
     * GPU 可见不透明实体；选择身份不经过浮点数，未命中返回空 optional。 */
    [[nodiscard]] auto pick(const World::Scene& scene, std::uint32_t width, std::uint32_t height,
                            Math::Vec2 uv, SceneView view = {})
        -> Core::Result<std::optional<World::EntityId>>;
    /** Current mesh bounds in world space, including hierarchy/rotation/reflection.
     * Nonmesh entities are points; assets/cache failures are reported, not guessed.
     * 当前网格世界包围盒，含层级/旋转/镜像；非网格实体是点，不猜测资产/缓存失败。 */
    [[nodiscard]] auto bounds(const World::Scene& scene, World::EntityId entity)
        -> Core::Result<Bounds>;
    [[nodiscard]] auto stats() const -> Core::Result<SceneRenderStats>;
    [[nodiscard]] auto warnings() const -> Core::Result<std::vector<std::string>>;
    [[nodiscard]] auto clearCache() -> Core::Result<void>;
    /** Own one unsaved material override for every matching scene binding.
     * Validate the description now; normal rendering resolves textures/budgets
     * and reports failures. Null restores disk rendering. Never writes assets
     * or mutates the scene. Cache reload retains this explicit override.
     * 拥有一份未保存材质覆盖并用于所有匹配的场景绑定；立即校验描述，实际渲染负责
     * 解析贴图/预算并报告错误；空值恢复磁盘材质，不写资源或修改场景，重载缓存保留覆盖。 */
    [[nodiscard]] auto previewMaterial(std::optional<MaterialPreview> preview)
        -> Core::Result<void>;
    /** Resolve a validated material through this service's bounded, shared
     * texture cache for independent mesh draws. Own returned handles; no scene,
     * renderer lighting or files are changed. clearCache reloads future requests.
     * 为独立网格绘制复用本服务有界共享贴图缓存，校验材质并返回拥有的句柄；
     * 不更改场景、渲染器光照或文件，clearCache 使后续请求重新加载。 */
    [[nodiscard]] auto resolveMaterial(const Assets::MaterialDescription& value)
        -> Core::Result<Material>;

  private:
    struct Impl;
    explicit SceneRenderer(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Graphics

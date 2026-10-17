module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
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

  enum class ScenePreparationStage {
    Ready,
    Environment,
    Shadows,
    Occlusion,
    Skins,
    SkinPoses,
    EnvironmentMap,
    Materials
  };

  struct SceneRenderStats {
    std::size_t meshes;
    std::uint64_t bytes;
    std::size_t materials{};
    std::size_t textures{};
    std::uint64_t textureBytes{};
    std::size_t environments{};
    std::uint64_t environmentBytes{};
    std::size_t skins{};
    std::uint64_t skinBytes{};
  };

  struct MaterialPreview {
    std::string path;
    Assets::MaterialDescription value;
  };

  /** Owns bounded shared mesh/material/texture caches and an optional asset-root
   * descriptor. Byte budget covers logical GPU mesh/texture bytes; at most 4096
   * materials and 4096 textures. Immutable assets reload explicitly via clearCache.
   * Scene light poses and camera view rays come from current world transforms.
   * Enabled saved skins share immutable source data, own one GPU controller per
   * mesh owner, and update changed joint-world palettes as one atomic visible batch.
   * Exact unchanged palettes reuse validated deformation, including bounds/picking.
   * Both posed buffers/weights/palette/control storage count toward the cache budget;
   * source meshes remain shared. clearCache reloads files. Bounds/pick/shadow/AO use
   * the posed mesh. Hidden/disabled/rebound/deleted owners are pruned before inputs.
   * Scene-owned HDR environments share the cache budget; replacement peak must fit
   * both maps. Saved SSAO settings are authoritative per frame; clearCache/destruction
   * detach only this service's environment/occlusion bindings. AO working textures
   * belong to the renderer and are separate from asset cache budgets.
   * Borrows a renderer that must stay at the same address and outlive this service.
   * Operations/destruction require the renderer's creating thread.
   * 拥有有界共享网格/材质/贴图缓存及可选资产根目录描述；字节预算覆盖逻辑 GPU 数据，
   * 最多 4096 材质/4096 贴图，缓存不可变资源由 clearCache 显式重载；光源位姿/视线取世界变换。
   * 启用蒙皮共用不可变源数据，每网格所有者持有 GPU 控制器，变化的可见关节调色板一次事务更新；
   * 完全相同的调色板复用已验证变形，包围盒/拾取共用。
   * 两个变形缓冲/权重/调色板/控制数据计入预算，源网格共享；包围盒/拾取/阴影/AO 用变形网格。
   * clearCache 重载文件；隐藏/禁用/改绑/删除的所有者先于共用输入释放。
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

    /** Prepare pipelines, the HDR environment and visible materials incrementally.
     * HDR/LDR reads/decode/mips use one owned worker, at most one bounded pyramid;
     * each poll uploads at most one texture on the owner. Publish complete materials
     * and GPU-completed environment maps only; retain the old binding until drawn.
     * GPU bake completion uses zero-wait polling. Small material JSON reads, GPU
     * uploads and other assets remain synchronous. No pixels are produced.
     * Initial visible skin poses submit one owned atomic GPU batch and poll it
     * without waiting. Changed bindings discard unpublished work. Advancing joints
     * reuse the submitted transaction; drawing validates the latest palette before
     * producing pixels. Later animated updates and explicit rendering stay synchronous.
     * Compact readbacks add <=32 bytes/controller beyond resident skin storage.
     * Clear/draft/scene cancellation discards late work without joining a codec;
     * destruction requests stop and joins active work before renderer teardown.
     * 渐进准备管线、HDR 环境及可见材质；单个拥有的工作线程读取/解码/生成至多一张有界
     * mip 金字塔，每次轮询在所属线程至多上传一张纹理，仅完整材质发布到缓存。
     * 环境仅在 GPU 完成后发布缓存，绘制前保留旧绑定；零等待轮询烘焙完成。
     * 初始可见蒙皮姿态提交拥有的原子 GPU 批次并零等待轮询；绑定变化丢弃旧任务。
     * 关节推进复用已提交事务，绘制前校验最新调色板，之后才产生像素。
     * 紧凑读回在常驻存储外每控制器至多增加 32 字节；后续动画更新及显式绘制仍同步。
     * 小型材质 JSON、GPU 上传及其他资产仍同步，不生成像素。
     * 清缓存/草稿/场景取消丢弃迟到结果，不在帧内等待编解码；销毁请求停止并等待工作结束。 */
    [[nodiscard]] auto prepare(const World::Scene& scene) -> Core::Result<ScenePreparationStage>;
    /** Pure current-scene readiness, including inside GUI layout. No I/O, polling,
     * preparation or cache mutation. Ready concerns only the prepared requirements.
     * 只读当前场景就绪状态，允许 GUI 布局调用；不读文件、轮询、准备或修改缓存；
     * Ready 仅代表上述准备项已完成。 */
    [[nodiscard]] auto readiness(const World::Scene& scene) const
        -> Core::Result<ScenePreparationStage>;

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
                              Math::Vec4 clear = {0, 0, 0, 1}, SceneView view = {},
                              std::span<const ScreenDraw> screen = {}) -> Core::Result<Image>;
    [[nodiscard]] auto present(const World::Scene& scene, Math::Vec4 clear = {0, 0, 0, 1},
                               SceneView view = {}, std::span<const ScreenDraw> screen = {})
        -> Core::Result<PresentResult>;
    /** Render directly into a persistent GPU target without CPU readback.
     * 直接绘制到持久 GPU 目标，不进行 CPU 读回。 */
    [[nodiscard]] auto renderInto(const World::Scene& scene, RenderTarget& target,
                                  Math::Vec4 clear = {0, 0, 0, 1}, SceneView view = {},
                                  std::span<const ScreenDraw> screen = {}) -> Core::Result<void>;
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

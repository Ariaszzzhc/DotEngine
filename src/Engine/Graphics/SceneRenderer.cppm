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
    Materials,
    Geometry
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

  struct SceneFrameReceipt {
    std::uint64_t serial{};
    std::uint64_t tag{};
    std::uint64_t scene{};
    std::uint32_t width{};
    std::uint32_t height{};
    Math::Mat4 viewProjection{};
  };

  struct SceneAsyncStats {
    std::uint64_t submitted{};
    std::uint64_t completed{};
    std::uint64_t deferred{};
    std::uint64_t cancelled{};
    std::optional<std::uint64_t> pending;
    std::optional<SceneFrameReceipt> latest;
    bool picking{};
    std::optional<Core::Error> error;
  };

  /** accepted concerns this call's tag; completed may belong to an earlier tag.
   * An error after a completed draw is returned alongside its actual receipt,
   * never as a rollback or unconditional success.
   * accepted 对应本次 tag，completed 可能属于较早 tag；绘制完成后的错误与真实回执
   * 一起返回，不伪造回滚或无条件成功。 */
  struct SceneAsyncResult {
    bool accepted{};
    std::optional<SceneFrameReceipt> completed;
    std::optional<Core::Error> error;
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

    /** Incremental pipeline/environment/material/geometry and first-pose preparation.
     * HDR/LDR decode/mips share one owned worker and one bounded pyramid; a separate
     * single geometry worker owns at most one mesh/skin decode or staged result.
     * Workers never borrow scenes, controllers or devices. Mesh vertex/surface
     * analysis produces immutable PreparedMesh data. Each poll publishes at most
     * one mesh or skin controller and one texture on the owner; uploads, binding,
     * small material JSON and driver validation remain owner operations.
     * Publish complete materials and GPU-completed environment maps only. Initial
     * skin poses use an owned atomic batch with zero-wait polls; animated Play/player
     * frames use the bounded async frame path. Explicit drawing remains exact/sync.
     * Readiness includes all visible geometry; preparation never draws pixels.
     * Decode errors persist for the same source until clear/change. GPU budget
     * failures retain owned CPU data for retry after unneeded residency retires.
     * Cancel/clear/source changes and exact operations discard late data without joining an active
     * codec; destruction requests stop and joins before renderer teardown.
     * 渐进准备管线、环境、材质、几何和初始姿态。HDR/LDR 共用一个有界金字塔工作线程；
     * 另一个几何线程至多拥有一个网格/蒙皮解码或暂存结果，不借用场景、控制器或设备。
     * 顶点/表面分析产生不可变 PreparedMesh；每次在所属线程至多发布一个网格或蒙皮控制器
     * 及一张贴图。上传、绑定、小型材质 JSON 和驱动校验仍在所属线程进行。
     * 仅完整材质及 GPU 完成的环境发布；初始蒙皮原子批次零等待轮询，动态 Play/原生游戏
     * 使用有界异步帧路径，显式绘制仍精确同步。就绪包含所有可见几何，准备不绘制像素。
     * 同源解码失败保持至清缓存或来源变化；GPU 预算失败保留 CPU 结果，释放无用常驻项后可重试。
     * 取消/清缓存/换源及显式同步操作禁止迟到数据发布，不等待活动编解码；销毁停止并在设备退出前回收。
     */
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
    /** Own at most one submitted and one completed frame. Snapshot transforms,
     * camera, materials, lighting, post process and screen handles together; skin
     * completion is polled without waiting. Simulation may continue independently.
     * Tags are fresh nonzero uint64 values across this service's lifetime; only
     * accepted tags advance that watermark. Epoch changes invalidate the source.
     * Newly submitted skin jobs are first polled on a later call.
     * Scene/layout/target/size changes and explicit sync operations cancel frames.
     * Target pixels stay unchanged while pending. Inspect actual completed tags;
     * accept/submit does not prove rendering. Caller screen copies retain textures.
     * 每次至多拥有一个提交帧和一个完成帧，变换、相机、材质、光照、后处理及屏幕句柄
     * 一起快照；零等待轮询蒙皮，模拟可独立继续。tag 为本服务寿命内递增的非零值，
     * 仅接受的请求推进水位；epoch 变化使旧来源失效，新蒙皮任务从后续调用开始轮询。
     * 场景/资源布局/目标/尺寸变化及显式同步操作取消帧；等待期间目标像素不变。
     * 按真实完成 tag 检查结果，接受不代表绘制；屏幕句柄副本会保留贴图。 */
    [[nodiscard]] auto renderIntoAsync(const World::Scene& scene, RenderTarget& target,
                                       std::uint64_t tag, std::uint64_t epoch,
                                       Math::Vec4 clear = {0, 0, 0, 1}, SceneView view = {},
                                       std::span<const ScreenDraw> screen = {})
        -> Core::Result<SceneAsyncResult>;
    [[nodiscard]] auto presentAsync(const World::Scene& scene, std::uint64_t tag,
                                    std::uint64_t epoch, Math::Vec4 clear = {0, 0, 0, 1},
                                    SceneView view = {}, std::span<const ScreenDraw> screen = {})
        -> Core::Result<SceneAsyncResult>;
    [[nodiscard]] auto asyncStats() const -> Core::Result<SceneAsyncStats>;
    [[nodiscard]] auto cancelAsync() -> Core::Result<void>;
    /** Pick the actual completed snapshot; never rebuild from a newer live pose.
     * 拾取真实完成的快照，不用较新的实时姿态重建。 */
    [[nodiscard]] auto pickAsync(std::uint64_t serial, Math::Vec2 uv)
        -> Core::Result<std::optional<World::EntityId>>;
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

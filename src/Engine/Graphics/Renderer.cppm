module;

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

export module Dot.Graphics.Renderer;

import Dot.Core.Result;
import Dot.Math;
import Dot.Platform.Window;
import Dot.Platform.Events;
export import Dot.Graphics.Geometry;

export namespace Dot::Graphics {
  enum class Backend { Metal, D3D12, Vulkan };

  struct AdapterInfo {
    Backend backend;
    std::string name;
    std::string description;
    std::uint32_t maxTextureDimension;
  };

  /** RGBA8 pixels in top-to-bottom row order; no row padding.
   * RGBA8 像素按从上到下的行顺序存储，无行填充。 */
  struct Image {
    std::uint32_t width;
    std::uint32_t height;
    std::vector<std::uint8_t> pixels;
  };

  enum class PresentResult { Presented, Deferred };

  struct MeshInfo {
    std::uint32_t vertices;
    std::uint32_t indices;
  };

  struct ResourceStats {
    std::uint64_t meshes;
    std::uint64_t vertexBytes;
    std::uint64_t indexBytes;
    std::uint64_t targets{};
    std::uint64_t targetBytes{};
  };

  struct TargetInfo {
    std::uint32_t width;
    std::uint32_t height;
    std::uint64_t frames;
  };

  struct GuiConfig {
    std::filesystem::path font;
    float fontSize{18};
    std::uint32_t maxVertices{1'000'000};
    std::uint32_t maxIndices{3'000'000};
  };

  struct GuiStats {
    bool enabled{};
    std::uint64_t frames{};
    std::uint32_t vertices{};
    std::uint32_t indices{};
    bool mouse{};
    bool keyboard{};
    bool text{};
  };

  using GuiDraw = std::function<Core::Result<void>()>;

  /** Live renderer-owned GUI contexts; observe when operations are quiescent.
   * 渲染器拥有的存活 GUI 上下文数量，操作静止时观察。 */
  [[nodiscard]] auto guiContexts() noexcept -> std::uint64_t;

  /** Shared GPU color/depth target. Copies observe the same rendered content;
   * its buffers can outlive the renderer for destruction, but cannot then be
   * used. Queries require the renderer's creating thread; quiescent destruction
   * may run elsewhere. A newly created target has no readable frame. 共享 GPU
   * 颜色/深度目标；副本观察同一绘制内容，缓冲可晚于渲染器销毁，但届时不能使用。
   * 查询限渲染器创建线程，静止销毁可异线程进行；新目标尚无可读帧。 */
  class RenderTarget {
  public:
    RenderTarget() noexcept;
    RenderTarget(const RenderTarget&) noexcept;
    auto operator=(const RenderTarget&) noexcept -> RenderTarget&;
    RenderTarget(RenderTarget&&) noexcept;
    auto operator=(RenderTarget&&) noexcept -> RenderTarget&;
    ~RenderTarget();
    [[nodiscard]] auto info() const -> Core::Result<TargetInfo>;

  private:
    friend class Renderer;
    struct Impl;
    explicit RenderTarget(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> m_impl;
  };

  /** Immutable GPU mesh with shared ownership. Copies share the uploaded buffers;
   * a mesh can outlive its renderer for destruction, but cannot then be drawn.
   * 共享所有权的不可变 GPU 网格；副本共享已上传缓冲区。
   * 网格可在渲染器销毁后释放，但届时无法绘制。
   */
  class Mesh {
  public:
    Mesh() noexcept;
    Mesh(const Mesh&) noexcept;
    auto operator=(const Mesh&) noexcept -> Mesh&;
    Mesh(Mesh&&) noexcept;
    auto operator=(Mesh&&) noexcept -> Mesh&;
    ~Mesh();

    [[nodiscard]] auto info() const -> Core::Result<MeshInfo>;

  private:
    friend class Renderer;
    struct Impl;
    explicit Mesh(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> m_impl;
  };

  /** Borrows a mesh for one call; transform maps local vertices into world space.
   * 单次调用期间借用网格；变换将局部顶点映射至世界空间。 */
  struct MeshDraw {
    const Mesh* mesh{};
    Math::Mat4 transform{};
    Math::Vec4 tint{1, 1, 1, 1};
  };

  /** Owns a Dawn device and optional native surface; all operations and destruction
   * must occur on the creating thread. A supplied window must stay at the same
   * address and outlive the renderer; surface operations require the main thread.
   * 拥有 Dawn 设备与可选原生 surface；操作及销毁须在创建线程进行。
   * 传入窗口须保持地址不变并比渲染器更晚销毁，surface 操作须在主线程进行。
   */
  class Renderer {
  public:
    [[nodiscard]] static auto create(Platform::Window* window = nullptr) -> Core::Result<Renderer>;
    Renderer(const Renderer&) = delete;
    auto operator=(const Renderer&) -> Renderer& = delete;
    Renderer(Renderer&&) noexcept;
    auto operator=(Renderer&&) noexcept -> Renderer&;
    ~Renderer();

    [[nodiscard]] auto adapterInfo() const -> Core::Result<AdapterInfo>;
    [[nodiscard]] auto surfaceSize() const -> Core::Result<Platform::Extent>;
    [[nodiscard]] auto upload(const MeshData& data) -> Core::Result<Mesh>;
    /** Logical bytes owned by live mesh/target resources, including shared copies once.
     * A coherent snapshot requires resource destruction to be quiescent.
     * 活跃网格/目标资源拥有的逻辑字节数，共享副本仅统计一次；一致快照要求资源销毁处于静止状态。 */
    [[nodiscard]] auto resourceStats() const -> Core::Result<ResourceStats>;

    /** Persistent RGBA8/depth target, each dimension 1..4096 and within device
     * limits; at most 128 targets/256 MiB logical target bytes per renderer.
     * Logical target bytes count color+depth as eight bytes per pixel,
     * independent of driver allocation. Reuse between frames to avoid readback
     * or reallocating viewport storage. 持久 RGBA8/深度目标，每维 1..4096 且受设备限制；
     * 每渲染器最多 128 个目标/256 MiB 逻辑目标字节，按颜色+深度每像素八字节计，
     * 不表示驱动分配量；逐帧复用避免读回及
     * 重新分配视口存储。 */
    [[nodiscard]] auto createTarget(std::uint32_t width, std::uint32_t height)
        -> Core::Result<RenderTarget>;
    /** Invalid drawing input retains the last frame; GPU failure after drawing
     * submission faults the target, requiring a new target. 无效绘制输入保留上一帧，
     * 绘制提交后 GPU 失败使目标故障，须新建目标。 */
    [[nodiscard]] auto renderMeshesInto(RenderTarget& target, std::span<const MeshDraw> draws,
                                        const Math::Mat4& viewProjection,
                                        Math::Vec4 clear = {0, 0, 0, 1}) -> Core::Result<void>;
    /** Diagnostic/export readback only; copy/map failures return errors without
     * changing the rendered content.
     * Targets from another renderer and uninitialized/faulted frames are rejected.
     * 仅诊断/导出读回，复制/映射失败返回错误，不更改绘制内容。
     * 拒绝其他渲染器的目标及未初始化/故障帧。 */
    [[nodiscard]] auto readTarget(const RenderTarget& target) -> Core::Result<Image>;

    /** Own an isolated Dear ImGui context and official WebGPU/optional SDL3
     * backends. GUI ownership requires the process main thread. Widgets execute
     * only inside the draw callback on this thread;
     * the callback may call guiImage, but must not replace/change ImGui context
     * or reenter renderer operations. Input events are explicitly forwarded.
     * Empty font uses the built-in font; an explicit font must load successfully.
     * Frame failures do not roll back widget/native side effects. No ini file or
     * detached viewport is implicitly created. 拥有独立 Dear ImGui 上下文及官方
     * WebGPU/可选 SDL3 后端，GUI 所有权限进程主线程；控件仅在本线程绘制回调内执行，回调可调用
     * guiImage， 但不得替换/更改 ImGui 上下文或重入渲染操作；输入事件须显式转发。空字体用内置
     * 字体，显式字体须加载成功；帧失败不回滚控件/原生副作用，不隐式创建 ini 文件或
     * 独立视口。 */
    [[nodiscard]] auto enableGui(const GuiConfig& config = {}) -> Core::Result<void>;
    [[nodiscard]] auto closeGui() -> Core::Result<void>;
    [[nodiscard]] auto guiEvent(const Platform::Event& event) -> Core::Result<bool>;
    [[nodiscard]] auto guiStats() const -> Core::Result<GuiStats>;
    /** UI image retains the target through submission; size uses logical pixels.
     * UI 图像持有目标至提交结束，尺寸使用逻辑像素。 */
    [[nodiscard]] auto guiImage(const RenderTarget& target, float width, float height)
        -> Core::Result<void>;
    [[nodiscard]] auto renderGui(const GuiDraw& draw, std::uint32_t width, std::uint32_t height,
                                 double seconds = 1.0 / 60, float scale = 1) -> Core::Result<Image>;
    [[nodiscard]] auto presentGui(const GuiDraw& draw, double seconds = 1.0 / 60)
        -> Core::Result<PresentResult>;

    /** Synchronous GPU readback, bounded to 4096 pixels per dimension and the
     * device limit. GPU waits have a ten-second timeout; failure is reported.
     * 同步 GPU 读回，每维最多 4096 像素并受设备限制。
     * GPU 等待超时为十秒，失败会返回错误。
     * Triangle-list vertices, finite transform, and linear [0,1] colors are
     * required. Depth testing uses [0,1]; empty geometry clears the target.
     * 需要三角形列表顶点、有限变换和线性 [0,1] 颜色；深度测试范围为 [0,1]。
     * 空几何体用于清空目标。
     */
    [[nodiscard]] auto render(std::span<const Vertex> vertices, const Math::Mat4& transform,
                              std::uint32_t width, std::uint32_t height,
                              Math::Vec4 clear = {0, 0, 0, 1}) -> Core::Result<Image>;
    /** Timeout/minimized/reconfigured frames return Deferred; GPU failures return
     * errors. The caller may retry Deferred on its next frame.
     * 超时、最小化或重新配置帧返回 Deferred；GPU 故障返回错误。
     * 调用者可在下一帧重试 Deferred。
     */
    [[nodiscard]] auto present(std::span<const Vertex> vertices, const Math::Mat4& transform,
                               Math::Vec4 clear = {0, 0, 0, 1}) -> Core::Result<PresentResult>;

    /** Render at most 4096 draws using this device's meshes. The view-projection
     * matrix is multiplied by each draw's world transform; all inputs are checked
     * before submission. Each draw has independent uniform data.
     * 使用本设备的网格绘制最多 4096 个对象；视图投影矩阵乘各对象的世界变换。
     * 提交前校验全部输入，每个绘制对象拥有独立的 uniform 数据。
     */
    [[nodiscard]] auto renderMeshes(std::span<const MeshDraw> draws,
                                    const Math::Mat4& viewProjection, std::uint32_t width,
                                    std::uint32_t height, Math::Vec4 clear = {0, 0, 0, 1})
        -> Core::Result<Image>;
    /** CPU drawing may execute before a surface frame is deferred/failed.
     * GUI seconds is finite [0,1]; zero uses a 1-microsecond UI delta. Readback
     * scale is finite [0.25,8]; presentation derives native DPI independently.
     * CPU 绘制可能在 surface 帧延迟/失败之前执行；GUI 秒数须有限 [0,1]，零用
     * 1 微秒界面时差；读回缩放须有限 [0.25,8]，呈现独立采用原生 DPI。 */
    [[nodiscard]] auto presentMeshes(std::span<const MeshDraw> draws,
                                     const Math::Mat4& viewProjection,
                                     Math::Vec4 clear = {0, 0, 0, 1})
        -> Core::Result<PresentResult>;

  private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Graphics

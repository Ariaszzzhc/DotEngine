module;

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module Dot.Graphics.Renderer;

import Dot.Core.Result;
import Dot.Math;
import Dot.Platform.Window;
import Dot.Platform.Events;
export import Dot.Graphics.Geometry;
export import Dot.Graphics.Image;
export import Dot.Graphics.PostProcess;
export import Dot.Graphics.Lighting;
export import Dot.Graphics.Environment;
export import Dot.Graphics.Occlusion;

export namespace Dot::Graphics {
  enum class Backend { Metal, D3D12, Vulkan };

  struct AdapterInfo {
    Backend backend;
    std::string name;
    std::string description;
    std::uint32_t maxTextureDimension;
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
    std::uint64_t textures{};
    std::uint64_t textureBytes{};
    /** Reused renderer working HDR/depth storage, twelve logical bytes per pixel.
     * 渲染器复用的 HDR/深度工作存储，每像素十二逻辑字节。 */
    std::uint64_t frameBytes{};
    std::uint64_t environments{};
    std::uint64_t environmentBytes{};
    /** Renderer working depth maps, four bytes per texel, at most 64 MiB.
     * 渲染器工作深度图每纹素四字节，最多 64 MiB。 */
    std::uint64_t shadowBytes{};
    /** Full-resolution SSAO depth/normal/raw/filtered working storage; logical bytes.
     * 全分辨率 SSAO 深度/法线/原始及过滤结果的工作存储，记录逻辑字节。 */
    std::uint64_t occlusionBytes{};
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

  enum class TextureFilter { Nearest, Linear };
  enum class TextureAddress { Clamp, Repeat, Mirror };

  struct TextureOptions {
    ColorSpace colorSpace{ColorSpace::Srgb};
    TextureFilter filter{TextureFilter::Linear};
    TextureAddress addressU{TextureAddress::Repeat};
    TextureAddress addressV{TextureAddress::Repeat};
    bool mipmaps{true};
    std::uint16_t maxAnisotropy{1};
  };

  /** CPU-only validation of sampling enums and anisotropy [1,16]; nearest
   * filtering requires anisotropy one. 不初始化设备，校验采样枚举及 [1,16] 各向异性，
   * 最近点过滤要求各向异性为一。 */
  [[nodiscard]] auto validateTextureOptions(const TextureOptions& options) -> Core::Result<void>;

  struct TextureInfo {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t mipLevels;
    TextureOptions options;
  };

  /** Immutable shared GPU texture/sampler. Queries require its creating thread;
   * copies retain the resource once. Destruction after quiescence may occur on
   * another thread or after renderer shutdown; subsequent queries/draws fail.
   * 不可变共享 GPU 纹理/采样器，查询限创建线程；副本仅持有同一资源。
   * 静止后可在其他线程或渲染器关闭后销毁，此后查询/绘制失败。 */
  class Texture {
  public:
    Texture() noexcept;
    Texture(const Texture&) noexcept;
    auto operator=(const Texture&) noexcept -> Texture&;
    Texture(Texture&&) noexcept;
    auto operator=(Texture&&) noexcept -> Texture&;
    ~Texture();
    [[nodiscard]] auto info() const -> Core::Result<TextureInfo>;

  private:
    friend class Renderer;
    struct Impl;
    explicit Texture(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> m_impl;
  };

  /** Immutable GPU radiance/diffuse/specular cube maps and correlated-Smith GGX
   * split-sum BRDF LUT. Diffuse stores irradiance/pi; specular levels map linear
   * perceptual roughness 0..1. Shared copies count once; queries require the owner
   * thread/live device, quiescent destruction may outlive it.
   * 不可变 GPU 辐射/漫反射/镜面立方体及相关 Smith GGX 分离积分 BRDF LUT；漫反射
   * 存辐照度/pi，镜面 mip 对应线性感知粗糙度 0..1。共享副本只计一次，查询限所有者
   * 线程和存活设备，静止销毁可晚于设备。 */
  class EnvironmentMap {
  public:
    EnvironmentMap() noexcept;
    EnvironmentMap(const EnvironmentMap&) noexcept;
    auto operator=(const EnvironmentMap&) noexcept -> EnvironmentMap&;
    EnvironmentMap(EnvironmentMap&&) noexcept;
    auto operator=(EnvironmentMap&&) noexcept -> EnvironmentMap&;
    ~EnvironmentMap();
    [[nodiscard]] auto info() const -> Core::Result<EnvironmentInfo>;
    /** Compare shared resource identity without querying a device.
     * 不查询设备，仅比较共享资源身份。 */
    auto operator==(const EnvironmentMap&) const noexcept -> bool = default;

  private:
    friend class Renderer;
    struct Impl;
    explicit EnvironmentMap(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> m_impl;
  };

  enum class SurfaceModel { Unlit, MetallicRoughness };

  /** Shared environment for mesh rendering: linear intensity [0,64], yaw degrees
   * [-360,360]. Background rays use the draw's inverse view-projection and current
   * Lighting camera pose; orthographic views use their emitted view direction.
   * 网格渲染共享环境：线性强度 [0,64]，绕 Y 轴角度 [-360,360]；背景视线使用绘制的
   * 逆视投影及 Lighting 相机位姿，正交相机使用发射视线方向。 */
  struct EnvironmentLighting {
    EnvironmentMap map;
    float intensity{1};
    float rotationDegrees{};
    bool background{true};
  };

  /** Linear base-color factor multiplied by vertex color, object tint and sampled
   * base-color texture. Absence means a constant white texel; an explicitly empty
   * or foreign texture fails. Unlit and metallic/roughness surfaces share the same
   * HDR pipeline. Physical data textures require linear encoding. Owns texture
   * slots by shared value; alpha is retained without transparency blending.
   * 线性基础颜色因子乘顶点颜色、对象染色及基础颜色纹理；缺省表示常量白纹素，
   * 显式空纹理或跨设备纹理失败。无光照和金属度/粗糙度表面使用同一 HDR 管线，
   * 物理数据纹理要求线性编码；通过共享值拥有纹理槽，保留 alpha，尚无透明混合。 */
  struct Material {
    Math::Vec4 baseColor{1, 1, 1, 1};
    std::optional<Texture> baseColorTexture;
    /** Linear emissive radiance per channel in [0,65503]. No exposure baked in.
     * 每通道 [0,65503] 的线性自发光亮度，不烘焙曝光。 */
    Math::Vec3 emissive{};
    SurfaceModel model{SurfaceModel::Unlit};
    float metallic{};
    float roughness{0.5F};
    float normalScale{1};
    bool doubleSided{};
    /** Linear packed G=roughness, B=metallic data; factors multiply samples.
     * 线性打包数据 G=粗糙度、B=金属度，因子乘采样值。 */
    std::optional<Texture> metallicRoughnessTexture{};
    /** Linear tangent-space RGB normals with source B>=128 (positive Z); requires
     * nondegenerate position/UV faces.
     * 线性切线空间 RGB 法线，源 B>=128（正 Z），要求位置/UV 三角面非退化。 */
    std::optional<Texture> normalTexture{};
    std::optional<Texture> emissiveTexture{};
  };

  /** Borrows a mesh for one call; transform maps local vertices into world space.
   * 单次调用期间借用网格；变换将局部顶点映射至世界空间。 */
  struct MeshDraw {
    const Mesh* mesh{};
    Math::Mat4 transform{};
    Math::Vec4 tint{1, 1, 1, 1};
    Material material{};
    /** Requested shadows require finite affine caster transforms, including unlit
     * geometry. Receiving attenuates only the selected direct light, preserving IBL/emission.
     * 请求阴影时投影体（包括无光照几何）须有有限仿射变换；接收仅衰减指定直接光，保留
     * 环境光及自发光。 */
    bool castsShadow{true};
    bool receivesShadow{true};
  };

  /** Texture residency budget, checked before upload/mip allocation. Bounds are
   * 1..4096 resources and 1..512 MiB logical bytes; internal constant texels are
   * pipeline storage, not asset residency. Logical bytes are not driver memory.
   * 纹理驻留预算在上传/mip 分配前校验；资源数 1..4096，逻辑字节 1..512 MiB。
   * 内部常量纹素属于管线存储，不属于资产驻留；逻辑字节不代表驱动内存。 */
  struct RendererConfig {
    std::uint32_t maxTextures{4096};
    std::uint64_t maxTextureBytes{512 * 1024 * 1024};
    PostProcessSettings postProcess{};
    Lighting lighting{};
  };

  /** Owns a Dawn device and optional native surface; all operations and destruction
   * must occur on the creating thread. A supplied window must stay at the same
   * address and outlive the renderer; surface operations require the main thread.
   * 拥有 Dawn 设备与可选原生 surface；操作及销毁须在创建线程进行。
   * 传入窗口须保持地址不变并比渲染器更晚销毁，surface 操作须在主线程进行。
   */
  class Renderer {
  public:
    [[nodiscard]] static auto create(Platform::Window* window = nullptr,
                                     const RendererConfig& config = {}) -> Core::Result<Renderer>;
    Renderer(const Renderer&) = delete;
    auto operator=(const Renderer&) -> Renderer& = delete;
    Renderer(Renderer&&) noexcept;
    auto operator=(Renderer&&) noexcept -> Renderer&;
    ~Renderer();

    [[nodiscard]] auto adapterInfo() const -> Core::Result<AdapterInfo>;
    [[nodiscard]] auto surfaceSize() const -> Core::Result<Platform::Extent>;
    [[nodiscard]] auto upload(const MeshData& data) -> Core::Result<Mesh>;
    /** RGBA8 texture upload within image/device limits; at most 4096 textures and
     * 512 MiB logical texels including mip levels (configurable residency budget). Mips average
     * linear light (sRGB decoded/re-encoded; alpha stays linear), preserve odd edges, and are owned
     * independently from the source. Nearest sampling requires anisotropy one;
     * linear sampling supports 1..16. UV (0,0) addresses the top-left image texel.
     * RGBA8 纹理上传受图像/设备限制，最多 4096 纹理及含 mip 的 512 MiB 逻辑纹素。
     * mip 平均线性光（sRGB 解码/重编码，alpha 保持线性），保留奇数边缘并独立于源；
     * 最近采样各向异性须为一，线性采样支持 1..16；UV (0,0) 对应图像左上纹素。 */
    [[nodiscard]] auto uploadTexture(const Image& image, const TextureOptions& options = {})
        -> Core::Result<Texture>;
    /** Logical bytes owned by live mesh/texture/target resources, including shared copies once.
     * A coherent snapshot requires resource destruction to be quiescent.
     * 活跃网格/目标资源拥有的逻辑字节数，共享副本仅统计一次；一致快照要求资源销毁处于静止状态。 */
    [[nodiscard]] auto resourceStats() const -> Core::Result<ResourceStats>;
    /** Preprocess an actual HDR panorama on GPU. At most 32 owned environments;
     * output residency and temporary RGBA32F source/mips share maxTextureBytes
     * with ordinary textures. Failure publishes no partial resource.
     * 在 GPU 预处理真实 HDR 全景，最多 32 个环境；输出与临时 RGBA32F 源/mip 及普通
     * 纹理共用 maxTextureBytes 预算，失败不发布部分资源。 */
    [[nodiscard]] auto bakeEnvironment(const HdrImage& panorama,
                                       const EnvironmentConfig& config = {})
        -> Core::Result<EnvironmentMap>;
    /** Diagnostic float readback of one face/mip; BRDF requires face/mip zero.
     * Missing/foreign/expired maps fail; no implicit GPU generation during reads.
     * 诊断性浮点读回一个面/mip；BRDF 仅允许面/mip 零；缺失、跨设备或过期地图失败，
     * 读取时不隐式生成 GPU 数据。 */
    [[nodiscard]] auto readEnvironment(const EnvironmentMap& map, EnvironmentSurface surface,
                                       std::uint32_t face = 0, std::uint32_t level = 0)
        -> Core::Result<HdrImage>;

    /** Transactionally bind owned environment lighting to future mesh frames.
     * Null clears it; absent/foreign maps and invalid settings fail without replacing
     * the prior binding. Existing targets retain pixels until explicitly redrawn.
     * 事务式绑定拥有的环境光至未来网格帧；空值解绑，无效/跨设备环境或设置失败时保留
     * 原绑定；已有目标的像素保留至显式重绘。 */
    [[nodiscard]] auto setEnvironment(std::optional<EnvironmentLighting> environment)
        -> Core::Result<void>;
    [[nodiscard]] auto environment() const -> Core::Result<std::optional<EnvironmentLighting>>;

    /** Optional geometric SSAO for future mesh frames. Null releases working maps.
     * Only environment diffuse is attenuated; direct/specular/emissive/unlit output
     * is preserved. Input failures retain settings and the previous committed image.
     * 为未来网格帧配置可选几何 SSAO；空值释放工作图，仅衰减环境漫反射，保留直接光、
     * 镜面、自发光和无光照输出；输入失败保留设置及已提交的图像。 */
    [[nodiscard]] auto setOcclusion(std::optional<OcclusionSettings> settings)
        -> Core::Result<void>;
    [[nodiscard]] auto occlusion() const -> Core::Result<std::optional<OcclusionSettings>>;
    /** Diagnostic grayscale visibility of the last committed AO mesh frame;
     * requires the owner thread/device and performs no implicit AO computation.
     * 诊断读取最近已提交 AO 网格帧的灰度可见性，限所有者线程/设备，不隐式生成 AO。 */
    [[nodiscard]] auto readOcclusion() -> Core::Result<Image>;

    /** Replace owned camera/direct-light data for future frames transactionally.
     * Lit draws require complete unit normals and finite affine world transforms;
     * no-direct-light frames retain emission and explicitly bound IBL. Directional
     * shadows are explicit; point/spot shadows remain planned.
     * 事务式替换未来帧拥有的相机/直接光数据；受光绘制要求完整单位法线和有限仿射世界变换，
     * 无直接光帧保留自发光及显式绑定的 IBL；方向光阴影须显式请求，点光/聚光阴影待实现。 */
    [[nodiscard]] auto setLighting(const Lighting& lighting) -> Core::Result<void>;
    [[nodiscard]] auto lighting() const -> Core::Result<Lighting>;

    /** Replace future-frame exposure/tone mapping/encoding transactionally; existing
     * completed targets retain their pixels until redrawn. Validate before change.
     * 事务式替换未来帧的曝光/映射/编码；已完成目标保留像素直到重绘，修改前校验。 */
    [[nodiscard]] auto setPostProcess(const PostProcessSettings& settings) -> Core::Result<void>;
    [[nodiscard]] auto postProcess() const -> Core::Result<PostProcessSettings>;

    /** Persistent RGBA16F HDR + RGBA8 display + depth target, each dimension 1..4096 and within
     * device limits; at most 128 targets/256 MiB logical target bytes per renderer. Logical target
     * bytes count HDR+display+depth as sixteen bytes per pixel, independent of driver allocation.
     * Reuse between frames to avoid readback or reallocating viewport storage. 持久 RGBA16F
     * HDR/RGBA8 显示/深度目标，每维 1..4096 且受设备限制； 每渲染器最多 128 个目标/256 MiB
     * 逻辑目标字节，按 HDR+显示+深度每像素十六字节计， 不表示驱动分配量；逐帧复用避免读回及
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
    /** Read the same completed frame before exposure/tone mapping/encoding. RGBA16F
     * values are returned as owned floats, preserving radiance above one.
     * 读回同一完成帧在曝光/映射/编码之前的 RGBA16F 数据，返回拥有的浮点值，保留超过一的亮度。 */
    [[nodiscard]] auto readHdrTarget(const RenderTarget& target) -> Core::Result<HdrImage>;

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
     * required. Clear RGB is [0,65504], alpha [0,1]; HDR/tone mapping produces
     * display pixels in the selected output encoding. Depth uses [0,1]. Empty geometry clears.
     * 需要三角形列表顶点、有限变换和线性 [0,1] 顶点颜色；清空 RGB 在 [0,65504]、alpha
     * 在 [0,1]，经 HDR/色调映射返回指定编码的显示像素；深度测试范围为 [0,1]。
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

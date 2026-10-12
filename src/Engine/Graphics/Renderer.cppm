module;

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

export module Dot.Graphics.Renderer;

import Dot.Core.Result;
import Dot.Math;
import Dot.Platform.Window;
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
    [[nodiscard]] auto upload(const MeshData& data) -> Core::Result<Mesh>;
    /** Logical bytes owned by live mesh resources, including shared copies once.
     * A coherent snapshot requires resource destruction to be quiescent.
     * 活跃网格资源拥有的逻辑字节数，共享副本仅统计一次；一致快照要求资源销毁处于静止状态。 */
    [[nodiscard]] auto resourceStats() const -> Core::Result<ResourceStats>;

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

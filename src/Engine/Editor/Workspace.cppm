module;

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

export module Dot.Editor.Workspace;

import Dot.Core.Result;
import Dot.Project;
import Dot.Graphics.Renderer;

export namespace Dot::Editor {
  struct WorkspaceConfig {
    /** Empty loads the editor's packaged font; dimensions must be at least
     * 800x480. 空路径加载编辑器随附字体，尺寸至少为 800x480。 */
    std::filesystem::path font;
    int width{1280};
    int height{800};
  };

  /** Native project editing window, owning document/application/rendering in
   * stable heap storage. Operations/destruction require the process main thread;
   * moves preserve borrows. The temporary control directory lives only while
   * this workspace owns it. GUI and automation use the same document dispatcher.
   * 原生项目编辑窗口，在稳定堆存储内拥有文档/应用/渲染；操作/销毁限进程主线程，
   * 移动保留借用。临时控制目录仅在工作区持有时存在，GUI 与自动化共用文档分发器。 */
  class Workspace {
  public:
    [[nodiscard]] static auto open(const Project::Project& project,
                                   const WorkspaceConfig& config = {}) -> Core::Result<Workspace>;
    Workspace(const Workspace&) = delete;
    auto operator=(const Workspace&) -> Workspace& = delete;
    Workspace(Workspace&&) noexcept;
    auto operator=(Workspace&&) noexcept -> Workspace&;
    ~Workspace();
    [[nodiscard]] auto run(std::size_t frames = 0) -> Core::Result<void>;
    [[nodiscard]] auto step(std::chrono::nanoseconds elapsed) -> Core::Result<void>;
    [[nodiscard]] auto dispatch(std::string_view request) -> Core::Result<std::string>;
    [[nodiscard]] auto snapshot() const -> Core::Result<std::string>;
    [[nodiscard]] auto controlDirectory() const -> Core::Result<std::filesystem::path>;
    /** Read actual viewport pixels for diagnostics; requires a rendered preview.
     * 诊断时读取真实视口像素，要求已绘制预览。 */
    [[nodiscard]] auto captureViewport() -> Core::Result<Graphics::Image>;

  private:
    struct Impl;
    explicit Workspace(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };

  /** Atomically submit one strict UTF-8 request to a live control directory.
   * IDs use 1..64 ASCII letters/digits/_/- and cannot be reused for different
   * content. Responses are JSON files; requests commit independently. This is
   * local automation, not isolation from hostile filesystem races. Callers
   * serialize submissions. A session retains at most 4096 request IDs and
   * accepts at most 256 pending requests, processed in lexical order (16/frame).
   * Response failure after effects faults the session without replay; save and
   * inspection remain available through the owning Workspace.
   * 向存活控制目录原子提交严格 UTF-8 请求，ID 为 1..64 个 ASCII 字母/数字/_/-，
   * 不同内容不能复用 ID；响应为 JSON 文件，请求独立提交，不隔离恶意文件系统竞争。
   * 调用者串行提交；每会话保留至多 4096 个 ID、256 个待处理请求，按字典序每帧处理
   * 至多 16 个。副作用后的响应失败使会话故障且不重放，所有者仍可保存/检查。 */
  [[nodiscard]] auto submitWorkspaceRequest(const std::filesystem::path& control,
                                            std::string_view id, std::string_view request)
      -> Core::Result<std::filesystem::path>;
}  // namespace Dot::Editor

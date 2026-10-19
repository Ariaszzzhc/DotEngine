module;

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module Dot.Editor.Home;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Project;

export namespace Dot::Editor {
  struct HomeConfig {
    /** Empty resolves resources adjacent to the editor executable.
     * 空路径使用编辑器相邻资源目录。 */
    std::filesystem::path resources;
    int width{960};
    int height{640};
    /** Empty uses the process temporary directory; explicit control parents must exist.
     * 空路径使用进程临时目录，显式控制父目录须已存在。 */
    std::filesystem::path controlParent;
  };

  /** Shared strict open/create operation used by GUI and automation. Open does
   * not write. Create validates in owned staging before publication; resources
   * must include current SDK/compiler and named templates. No native UI starts.
   * GUI 与自动化共用的严格打开/创建操作；打开不写入，创建在拥有的暂存中校验后发布。
   * 资源须含当前 SDK/编译器和命名模板，不启动原生 UI。 */
  [[nodiscard]] auto performProjectAction(const Core::FileSystem& resources,
                                          std::string_view request)
      -> Core::Result<Project::Project>;

  /** Main-thread native project selector with shared commands and an owned
   * control directory. Selection retires its endpoint after the loop; shared receipt
   * archives remain until explicit clear. Native folder selection uses an owned asynchronous SDL
   * callback mailbox. Cancellation preserves the form; selected project folders use the same strict
   * open operation. releaseWindow requires any dialog to finish. releaseWindow releases GUI/video
   * before opening a workspace.
   * 主线程原生项目选择器，共享命令并持有控制目录；选择后停止循环并退役端点，响应保留至显式清理。
   * 异步 SDL 文件夹选择使用拥有的回调邮箱，取消保留表单，选择项目复用严格打开操作。
   * releaseWindow 要求对话框先完成，在打开工作区前释放 GUI/视频资源。 */
  class Home {
  public:
    [[nodiscard]] static auto open(const HomeConfig& config = {}) -> Core::Result<Home>;
    Home(const Home&) = delete;
    auto operator=(const Home&) -> Home& = delete;
    Home(Home&&) noexcept;
    auto operator=(Home&&) noexcept -> Home&;
    ~Home();
    [[nodiscard]] auto run(std::size_t frames = 0) -> Core::Result<void>;
    [[nodiscard]] auto step(std::chrono::nanoseconds elapsed) -> Core::Result<void>;
    [[nodiscard]] auto dispatch(std::string_view request) -> Core::Result<std::string>;
    [[nodiscard]] auto selection() const -> Core::Result<std::optional<Project::Project>>;
    [[nodiscard]] auto controlDirectory() const -> Core::Result<std::filesystem::path>;
    /** Persistent replies survive the live endpoint; callers consume/clear them explicitly.
     * 回执在实时端点退役后保留，由调用者显式消费/清理。 */
    [[nodiscard]] auto receiptDirectory() const -> Core::Result<std::filesystem::path>;
    [[nodiscard]] auto releaseWindow() -> Core::Result<void>;

  private:
    struct Impl;
    explicit Home(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Editor

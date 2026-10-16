module;

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

export module Dot.Platform.Window;

import Dot.Core.Result;

export namespace Dot::Platform {
  struct Extent {
    int width;
    int height;
  };

  struct WindowConfig {
    std::string title{"DotEngine"};
    int width{1280};
    int height{720};
    bool hidden{false};
    bool resizable{true};
    bool gpuSurface{false};
  };

  /**
   * Owns a native window and one reference to the video subsystem.
   * 拥有原生窗口及视频子系统的一次引用。
   * Creation, mutations, queries, and destruction require the main thread.
   * 创建、修改、查询与销毁均须在主线程进行。
   */
  class Window {
  public:
    [[nodiscard]] static auto create(const WindowConfig& config = {}) -> Core::Result<Window>;
    Window(const Window&) = delete;
    auto operator=(const Window&) -> Window& = delete;
    Window(Window&&) noexcept;
    auto operator=(Window&&) noexcept -> Window&;
    ~Window();

    [[nodiscard]] auto id() const noexcept -> std::uint32_t;
    [[nodiscard]] auto size() const -> Core::Result<Extent>;
    [[nodiscard]] auto pixelSize() const -> Core::Result<Extent>;
    [[nodiscard]] auto setTitle(std::string_view title) -> Core::Result<void>;
    [[nodiscard]] auto resize(int width, int height) -> Core::Result<void>;
    [[nodiscard]] auto show() -> Core::Result<void>;
    /** Request activation/raising of a visible, non-minimized window. The window
     * manager may defer or deny focus; query focused() for the actual state.
     * 请求激活并置前已显示且未最小化的窗口；窗口管理器可能延迟或拒绝聚焦，实际状态须查询
     * focused()。 */
    [[nodiscard]] auto raise() -> Core::Result<void>;
    [[nodiscard]] auto setTextInput(bool enabled) -> Core::Result<void>;
    /** Actual native text/IME mode; disabling it leaves physical key events
     * available for gameplay. Query requires the main thread and a live owner.
     * 原生文本/输入法模式；关闭后仍保留游戏物理按键，查询限主线程及有效窗口。 */
    [[nodiscard]] auto textInputActive() const -> Core::Result<bool>;
    /** Relative mode hides/confines the cursor while focused and permits motion
     * beyond window edges. SDL flushes pending motion when the mode changes.
     * 相对模式在聚焦时隐藏/限制光标，支持越过窗口边界的连续运动；切换时 SDL 清除待处理移动。 */
    [[nodiscard]] auto setRelativeMouse(bool enabled) -> Core::Result<void>;
    [[nodiscard]] auto relativeMouse() const -> Core::Result<bool>;
    [[nodiscard]] auto focused() const -> Core::Result<bool>;
    /** Borrowed backend handle, valid until destruction. 借用后端句柄，销毁前有效。 */
    [[nodiscard]] auto nativeHandle() const noexcept -> void*;

  private:
    struct Impl;
    explicit Window(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Platform

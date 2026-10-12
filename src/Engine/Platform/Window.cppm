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
    [[nodiscard]] auto setTextInput(bool enabled) -> Core::Result<void>;
    /** Borrowed backend handle, valid until destruction. 借用后端句柄，销毁前有效。 */
    [[nodiscard]] auto nativeHandle() const noexcept -> void*;

  private:
    struct Impl;
    explicit Window(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Platform

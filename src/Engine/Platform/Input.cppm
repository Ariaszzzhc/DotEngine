module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

export module Dot.Platform.Input;

import Dot.Platform.Events;

export namespace Dot::Platform {
  struct ButtonState {
    bool down{false};
    bool pressed{false};
    bool released{false};
  };

  struct Point {
    float x{};
    float y{};
  };

  /** Frame edges persist until beginFrame; focus loss releases held buttons.
   * 帧内边沿保留到下一帧，失焦释放按键。 */
  class InputState {
  public:
    explicit InputState(std::uint32_t windowId = 0) noexcept;
    auto beginFrame() noexcept -> void;
    /** Clear only motion already consumed before a native pointer-mode change;
     * preserve button edges, position, wheel and text. 清除原生鼠标模式切换前已消费的位移，
     * 保留按键边沿、位置、滚轮与文本。 */
    auto clearPointerDelta() noexcept -> void;
    auto consume(const Event& event) -> bool;
    [[nodiscard]] auto key(Key key) const noexcept -> ButtonState;
    [[nodiscard]] auto button(MouseButton button) const noexcept -> ButtonState;
    [[nodiscard]] auto pointer() const noexcept -> Point;
    [[nodiscard]] auto pointerDelta() const noexcept -> Point;
    [[nodiscard]] auto wheel() const noexcept -> Point;
    [[nodiscard]] auto text() const noexcept -> std::string_view;

  private:
    std::uint32_t m_windowId;
    std::array<ButtonState, static_cast<std::size_t>(Key::Count)> m_keys{};
    std::array<ButtonState, static_cast<std::size_t>(MouseButton::Count)> m_buttons{};
    Point m_pointer{};
    Point m_delta{};
    Point m_wheel{};
    std::string m_text;
  };
}  // namespace Dot::Platform

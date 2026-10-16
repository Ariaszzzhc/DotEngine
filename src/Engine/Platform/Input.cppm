module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

export module Dot.Platform.Input;

import Dot.Platform.Events;

export namespace Dot::Platform {
  using KeyName = std::pair<std::string_view, Key>;
  using ButtonName = std::pair<std::string_view, MouseButton>;

  /** Canonical input names shared by scripts and product agent operations.
   * Returned views refer to immutable process-lifetime storage; Unknown is excluded.
   * 脚本与产品智能体操作共用的标准输入名称；返回进程全生命周期的只读视图，不含 Unknown。 */
  [[nodiscard]] auto keyNames() noexcept -> std::span<const KeyName>;
  [[nodiscard]] auto buttonNames() noexcept -> std::span<const ButtonName>;

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
   * Button-local coordinates update pointer position without inventing relative
   * motion. Invalid coordinates reject the whole event before any state changes.
   * 帧内边沿保留到下一帧，失焦释放按键。按钮事件坐标更新位置，但不产生相对位移；
   * 无效坐标在任何状态变更前拒绝整个事件。 */
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

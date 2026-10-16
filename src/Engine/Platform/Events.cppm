module;

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

export module Dot.Platform.Events;

import Dot.Core.Result;

export namespace Dot::Platform {
  enum class Key {
    Unknown,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Digit0,
    Enter,
    Escape,
    Backspace,
    Tab,
    Space,
    Left,
    Right,
    Up,
    Down,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    LeftControl,
    RightControl,
    LeftShift,
    RightShift,
    LeftAlt,
    RightAlt,
    LeftSuper,
    RightSuper,
    Delete,
    Insert,
    Home,
    End,
    PageUp,
    PageDown,
    KeypadEnter,
    Count
  };

  enum class MouseButton { Unknown, Left, Middle, Right, Back, Forward, Count };

  struct Quit {};

  struct Close {};

  struct Resize {
    int width;
    int height;
    bool pixels;
  };

  struct Focus {
    bool focused;
  };

  struct KeyChanged {
    Key key;
    bool down;
    bool repeat;
  };

  struct ButtonChanged {
    MouseButton button;
    bool down;
    /** Native event-local client coordinates in logical window units. Semantic
     * button-only input may omit position; consumers then retain their pointer.
     * 原生事件自带的窗口客户区逻辑坐标；语义按键输入可省略位置，消费者保留原指针位置。 */
    std::optional<std::array<float, 2>> position{};
  };

  struct PointerMoved {
    float x;
    float y;
    float deltaX;
    float deltaY;
  };

  struct Wheel {
    float x;
    float y;
  };

  struct Text {
    std::string value;
  };

  using EventData = std::variant<Quit, Close, Resize, Focus, KeyChanged, ButtonChanged,
                                 PointerMoved, Wheel, Text>;

  struct Event {
    std::uint32_t windowId;
    std::uint64_t timestamp;
    EventData data;
  };

  /** Drain all windows' events on the main thread. 在主线程读取所有窗口的待处理事件。 */
  [[nodiscard]] auto pollEvents() -> Core::Result<std::vector<Event>>;
  [[nodiscard]] auto requestQuit() -> Core::Result<void>;
}  // namespace Dot::Platform

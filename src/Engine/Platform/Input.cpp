module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

module Dot.Platform.Input;

import Dot.Platform.Events;

namespace Dot::Platform {
  namespace {
    auto updateButton(ButtonState& state, bool down) noexcept -> void {
      if (down && !state.down) {
        state.pressed = true;
      }
      if (!down && state.down) {
        state.released = true;
      }
      state.down = down;
    }
  }  // namespace

  InputState::InputState(std::uint32_t windowId) noexcept : m_windowId(windowId) {
  }

  auto InputState::beginFrame() noexcept -> void {
    for (auto& key : m_keys) {
      key.pressed = false;
      key.released = false;
    }
    for (auto& button : m_buttons) {
      button.pressed = false;
      button.released = false;
    }
    m_delta = {};
    m_wheel = {};
    m_text.clear();
  }

  auto InputState::consume(const Event& event) -> bool {
    if (m_windowId != 0 && event.windowId != m_windowId) {
      return false;
    }
    return std::visit(
        [this](const auto& data) -> bool {
          using Data = std::decay_t<decltype(data)>;
          if constexpr (std::is_same_v<Data, KeyChanged>) {
            const auto index = static_cast<std::size_t>(data.key);
            if (data.key == Key::Unknown || index >= m_keys.size()) {
              return false;
            }
            if (data.down && data.repeat) {
              m_keys[index].down = true;
            } else {
              updateButton(m_keys[index], data.down);
            }
          } else if constexpr (std::is_same_v<Data, ButtonChanged>) {
            const auto index = static_cast<std::size_t>(data.button);
            if (data.button == MouseButton::Unknown || index >= m_buttons.size()) {
              return false;
            }
            updateButton(m_buttons[index], data.down);
          } else if constexpr (std::is_same_v<Data, Focus>) {
            if (!data.focused) {
              for (auto& key : m_keys) {
                updateButton(key, false);
              }
              for (auto& button : m_buttons) {
                updateButton(button, false);
              }
            }
          } else if constexpr (std::is_same_v<Data, PointerMoved>) {
            if (!std::isfinite(data.x) || !std::isfinite(data.y) || !std::isfinite(data.deltaX)
                || !std::isfinite(data.deltaY)) {
              return false;
            }
            const Point delta{m_delta.x + data.deltaX, m_delta.y + data.deltaY};
            if (!std::isfinite(delta.x) || !std::isfinite(delta.y)) {
              return false;
            }
            m_pointer = {data.x, data.y};
            m_delta = delta;
          } else if constexpr (std::is_same_v<Data, Wheel>) {
            if (!std::isfinite(data.x) || !std::isfinite(data.y)) {
              return false;
            }
            const Point wheel{m_wheel.x + data.x, m_wheel.y + data.y};
            if (!std::isfinite(wheel.x) || !std::isfinite(wheel.y)) {
              return false;
            }
            m_wheel = wheel;
          } else if constexpr (std::is_same_v<Data, Text>) {
            m_text += data.value;
          } else {
            return false;
          }
          return true;
        },
        event.data);
  }

  auto InputState::key(Key key) const noexcept -> ButtonState {
    const auto index = static_cast<std::size_t>(key);
    return index < m_keys.size() ? m_keys[index] : ButtonState{};
  }
  auto InputState::button(MouseButton button) const noexcept -> ButtonState {
    const auto index = static_cast<std::size_t>(button);
    return index < m_buttons.size() ? m_buttons[index] : ButtonState{};
  }
  auto InputState::pointer() const noexcept -> Point {
    return m_pointer;
  }
  auto InputState::pointerDelta() const noexcept -> Point {
    return m_delta;
  }
  auto InputState::wheel() const noexcept -> Point {
    return m_wheel;
  }
  auto InputState::text() const noexcept -> std::string_view {
    return m_text;
  }
}  // namespace Dot::Platform

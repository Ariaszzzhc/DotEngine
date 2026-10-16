module;

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

module Dot.Platform.Input;

import Dot.Platform.Events;

namespace Dot::Platform {
  namespace {
    constexpr std::array keyMapping{KeyName{"A", Key::A},
                                    KeyName{"B", Key::B},
                                    KeyName{"C", Key::C},
                                    KeyName{"D", Key::D},
                                    KeyName{"E", Key::E},
                                    KeyName{"F", Key::F},
                                    KeyName{"G", Key::G},
                                    KeyName{"H", Key::H},
                                    KeyName{"I", Key::I},
                                    KeyName{"J", Key::J},
                                    KeyName{"K", Key::K},
                                    KeyName{"L", Key::L},
                                    KeyName{"M", Key::M},
                                    KeyName{"N", Key::N},
                                    KeyName{"O", Key::O},
                                    KeyName{"P", Key::P},
                                    KeyName{"Q", Key::Q},
                                    KeyName{"R", Key::R},
                                    KeyName{"S", Key::S},
                                    KeyName{"T", Key::T},
                                    KeyName{"U", Key::U},
                                    KeyName{"V", Key::V},
                                    KeyName{"W", Key::W},
                                    KeyName{"X", Key::X},
                                    KeyName{"Y", Key::Y},
                                    KeyName{"Z", Key::Z},
                                    KeyName{"Digit1", Key::Digit1},
                                    KeyName{"Digit2", Key::Digit2},
                                    KeyName{"Digit3", Key::Digit3},
                                    KeyName{"Digit4", Key::Digit4},
                                    KeyName{"Digit5", Key::Digit5},
                                    KeyName{"Digit6", Key::Digit6},
                                    KeyName{"Digit7", Key::Digit7},
                                    KeyName{"Digit8", Key::Digit8},
                                    KeyName{"Digit9", Key::Digit9},
                                    KeyName{"Digit0", Key::Digit0},
                                    KeyName{"Enter", Key::Enter},
                                    KeyName{"Escape", Key::Escape},
                                    KeyName{"Backspace", Key::Backspace},
                                    KeyName{"Tab", Key::Tab},
                                    KeyName{"Space", Key::Space},
                                    KeyName{"Left", Key::Left},
                                    KeyName{"Right", Key::Right},
                                    KeyName{"Up", Key::Up},
                                    KeyName{"Down", Key::Down},
                                    KeyName{"F1", Key::F1},
                                    KeyName{"F2", Key::F2},
                                    KeyName{"F3", Key::F3},
                                    KeyName{"F4", Key::F4},
                                    KeyName{"F5", Key::F5},
                                    KeyName{"F6", Key::F6},
                                    KeyName{"F7", Key::F7},
                                    KeyName{"F8", Key::F8},
                                    KeyName{"F9", Key::F9},
                                    KeyName{"F10", Key::F10},
                                    KeyName{"F11", Key::F11},
                                    KeyName{"F12", Key::F12},
                                    KeyName{"LeftControl", Key::LeftControl},
                                    KeyName{"RightControl", Key::RightControl},
                                    KeyName{"LeftShift", Key::LeftShift},
                                    KeyName{"RightShift", Key::RightShift},
                                    KeyName{"LeftAlt", Key::LeftAlt},
                                    KeyName{"RightAlt", Key::RightAlt},
                                    KeyName{"LeftSuper", Key::LeftSuper},
                                    KeyName{"RightSuper", Key::RightSuper},
                                    KeyName{"Delete", Key::Delete},
                                    KeyName{"Insert", Key::Insert},
                                    KeyName{"Home", Key::Home},
                                    KeyName{"End", Key::End},
                                    KeyName{"PageUp", Key::PageUp},
                                    KeyName{"PageDown", Key::PageDown},
                                    KeyName{"KeypadEnter", Key::KeypadEnter}};

    constexpr std::array buttonMapping{
        ButtonName{"Left", MouseButton::Left}, ButtonName{"Middle", MouseButton::Middle},
        ButtonName{"Right", MouseButton::Right}, ButtonName{"Back", MouseButton::Back},
        ButtonName{"Forward", MouseButton::Forward}};

    template <typename Enum, std::size_t N>
    constexpr auto complete(const std::array<std::pair<std::string_view, Enum>, N>& entries,
                            Enum count) -> bool {
      if (N + 1 != static_cast<std::size_t>(count)) {
        return false;
      }

      std::array<bool, N + 1> seen{};

      for (const auto& [name, value] : entries) {
        for (const auto& previous : entries) {
          if (previous.second != value && std::string_view(previous.first) == name) {
            return false;
          }
        }

        const auto index = static_cast<std::size_t>(value);

        if (index == 0 || index >= seen.size() || seen[index]) {
          return false;
        }

        seen[index] = true;
      }

      return true;
    }

    static_assert(complete(keyMapping, Key::Count));
    static_assert(complete(buttonMapping, MouseButton::Count));

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

  auto keyNames() noexcept -> std::span<const KeyName> {
    return keyMapping;
  }

  auto buttonNames() noexcept -> std::span<const ButtonName> {
    return buttonMapping;
  }

  InputState::InputState(std::uint32_t windowId) noexcept : m_windowId(windowId) {
  }

  auto InputState::clearPointerDelta() noexcept -> void {
    m_delta = {};
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

            if (data.position) {
              const auto& point = *data.position;

              if (!std::isfinite(point[0]) || !std::isfinite(point[1])) {
                return false;
              }

              m_pointer = {point[0], point[1]};
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

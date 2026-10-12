module;

#include <SDL3/SDL.h>

#include <format>
#include <optional>
#include <utility>
#include <vector>

module Dot.Platform.Events;

import Dot.Core.Result;
import Dot.Platform.System;

namespace Dot::Platform {
  namespace {
    auto keyFromNative(SDL_Scancode code) -> Key {
      if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) {
        return static_cast<Key>(static_cast<int>(Key::A) + code - SDL_SCANCODE_A);
      }

      if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_0) {
        return static_cast<Key>(static_cast<int>(Key::Digit1) + code - SDL_SCANCODE_1);
      }

      if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12) {
        return static_cast<Key>(static_cast<int>(Key::F1) + code - SDL_SCANCODE_F1);
      }

      switch (code) {
        case SDL_SCANCODE_RETURN:
          return Key::Enter;
        case SDL_SCANCODE_ESCAPE:
          return Key::Escape;
        case SDL_SCANCODE_BACKSPACE:
          return Key::Backspace;
        case SDL_SCANCODE_TAB:
          return Key::Tab;
        case SDL_SCANCODE_SPACE:
          return Key::Space;
        case SDL_SCANCODE_LEFT:
          return Key::Left;
        case SDL_SCANCODE_RIGHT:
          return Key::Right;
        case SDL_SCANCODE_UP:
          return Key::Up;
        case SDL_SCANCODE_DOWN:
          return Key::Down;
        case SDL_SCANCODE_LCTRL:
          return Key::LeftControl;
        case SDL_SCANCODE_RCTRL:
          return Key::RightControl;
        case SDL_SCANCODE_LSHIFT:
          return Key::LeftShift;
        case SDL_SCANCODE_RSHIFT:
          return Key::RightShift;
        case SDL_SCANCODE_LALT:
          return Key::LeftAlt;
        case SDL_SCANCODE_RALT:
          return Key::RightAlt;
        case SDL_SCANCODE_LGUI:
          return Key::LeftSuper;
        case SDL_SCANCODE_RGUI:
          return Key::RightSuper;
        default:
          return Key::Unknown;
      }
    }

    auto buttonFromNative(Uint8 button) -> MouseButton {
      switch (button) {
        case SDL_BUTTON_LEFT:
          return MouseButton::Left;
        case SDL_BUTTON_MIDDLE:
          return MouseButton::Middle;
        case SDL_BUTTON_RIGHT:
          return MouseButton::Right;
        case SDL_BUTTON_X1:
          return MouseButton::Back;
        case SDL_BUTTON_X2:
          return MouseButton::Forward;
        default:
          return MouseButton::Unknown;
      }
    }
  }  // namespace

  auto pollEvents() -> Core::Result<std::vector<Event>> {
    if (!isMainThread() || !videoInitialized()) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Event polling requires initialized video on the main thread");
    }

    std::vector<Event> events;
    SDL_Event native{};

    while (SDL_PollEvent(&native)) {
      const auto timestamp = native.common.timestamp;

      switch (native.type) {
        case SDL_EVENT_QUIT:
          events.push_back({0, timestamp, Quit{}});
          break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
          events.push_back({native.window.windowID, timestamp, Close{}});
          break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
          events.push_back({native.window.windowID, timestamp,
                            Resize{native.window.data1, native.window.data2,
                                   native.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED}});
          break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
          events.push_back({native.window.windowID, timestamp,
                            Focus{native.type == SDL_EVENT_WINDOW_FOCUS_GAINED}});
          break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
          events.push_back(
              {native.key.windowID, timestamp,
               KeyChanged{keyFromNative(native.key.scancode), native.key.down, native.key.repeat}});
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
          events.push_back(
              {native.button.windowID, timestamp,
               ButtonChanged{buttonFromNative(native.button.button), native.button.down}});
          break;
        case SDL_EVENT_MOUSE_MOTION:
          events.push_back({native.motion.windowID, timestamp,
                            PointerMoved{native.motion.x, native.motion.y, native.motion.xrel,
                                         native.motion.yrel}});
          break;
        case SDL_EVENT_MOUSE_WHEEL: {
          const auto direction = native.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
          events.push_back({native.wheel.windowID, timestamp,
                            Wheel{native.wheel.x * direction, native.wheel.y * direction}});
          break;
        }
        case SDL_EVENT_TEXT_INPUT:
          if (native.text.text) {
            events.push_back({native.text.windowID, timestamp, Text{native.text.text}});
          }

          break;
        default:
          break;
      }
    }

    return events;
  }

  auto requestQuit() -> Core::Result<void> {
    if (!videoInitialized()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Quit events require initialized video");
    }

    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;

    if (!SDL_PushEvent(&event)) {
      return Core::fail(Core::ErrorCode::External,
                        std::format("Cannot queue quit event: {}", SDL_GetError()));
    }

    return {};
  }
}  // namespace Dot::Platform

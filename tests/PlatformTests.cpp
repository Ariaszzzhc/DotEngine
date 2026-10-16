#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <array>
#include <expected>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>

import Dot.Core.Result;
import Dot.Platform.Events;
import Dot.Platform.System;
import Dot.Platform.Window;

using namespace Dot::Platform;

namespace {
  auto hiddenWindow() -> WindowConfig {
    return {"DotEngine test", 320, 240, true, true, false};
  }
}  // namespace

TEST_CASE("window.create_resize_title_and_release") {
  CHECK_FALSE(videoInitialized());
  {
    auto window = Window::create(hiddenWindow());
    REQUIRE(window.has_value());
    CHECK(videoInitialized());
    CHECK(window->id() != 0);
    CHECK(window->nativeHandle() != nullptr);
    auto size = window->size();
    REQUIRE(size.has_value());
    CHECK(size->width == 320);
    CHECK(size->height == 240);
    auto pixels = window->pixelSize();
    REQUIRE(pixels.has_value());
    CHECK(pixels->width > 0);
    REQUIRE(window->resize(640, 480).has_value());
    size = window->size();
    REQUIRE(size.has_value());
    CHECK(size->width == 640);
    CHECK(size->height == 480);
    CHECK(window->setTitle("测试 🌏").has_value());
    CHECK(window->show().has_value());
    CHECK(window->raise().has_value());
    CHECK(window->focused().value() == (SDL_GetKeyboardFocus() == window->nativeHandle()));
    CHECK_FALSE(window->textInputActive().value());
    CHECK(window->setTextInput(true).has_value());
    CHECK(window->textInputActive().value());
    CHECK(window->setTextInput(true).has_value());
    CHECK(window->textInputActive().value());
    CHECK(window->setTextInput(false).has_value());
    CHECK_FALSE(window->textInputActive().value());
    CHECK(window->setTextInput(false).has_value());
  }

  CHECK_FALSE(videoInitialized());
}

TEST_CASE("platform.main_thread_detection_before_video_initialization") {
  CHECK_FALSE(videoInitialized());
  CHECK(isMainThread());
  bool rejected = false;
  std::thread worker([&] { rejected = !isMainThread(); });
  worker.join();
  CHECK(rejected);
  CHECK_FALSE(videoInitialized());
}

TEST_CASE("window.invalid_configuration_and_mutations") {
  auto invalid = hiddenWindow();
  invalid.width = 0;
  CHECK_FALSE(Window::create(invalid).has_value());
  CHECK_FALSE(videoInitialized());
  invalid = hiddenWindow();
  invalid.title = std::string{"bad\0title", 9};
  CHECK_FALSE(Window::create(invalid).has_value());
  auto window = Window::create(hiddenWindow());
  REQUIRE(window.has_value());
  CHECK_FALSE(window->resize(-1, 240).has_value());
  CHECK_FALSE(window->setTitle(std::string{"a\0b", 3}).has_value());
  CHECK(window->size()->width == 320);
}

TEST_CASE("window.shared_video_lifetime") {
  std::optional<Window> first;
  first.emplace(Window::create(hiddenWindow()).value());
  {
    auto second = Window::create(hiddenWindow());
    REQUIRE(second.has_value());
    CHECK(second->id() != first->id());
    first.reset();
    CHECK(videoInitialized());
    CHECK(second->size().has_value());
  }

  CHECK_FALSE(videoInitialized());
}

TEST_CASE("window.move_releases_old_resources") {
  {
    auto first = Window::create(hiddenWindow()).value();
    auto second = Window::create(hiddenWindow()).value();
    const auto id = second.id();
    first = std::move(second);
    CHECK(first.id() == id);
    CHECK(second.id() == 0);
    CHECK(second.nativeHandle() == nullptr);
    CHECK_FALSE(second.size().has_value());
    CHECK_FALSE(second.show().has_value());
    CHECK_FALSE(second.raise());
    CHECK_FALSE(second.setRelativeMouse(true));
    CHECK_FALSE(second.relativeMouse());
    CHECK_FALSE(second.focused());
    CHECK_FALSE(second.textInputActive());
    CHECK_FALSE(second.setTextInput(true));
  }

  CHECK_FALSE(videoInitialized());
}

TEST_CASE("window.repeated_initialization") {
  for (int cycle = 0; cycle < 50; ++cycle) {
    {
      auto window = Window::create(hiddenWindow());
      REQUIRE(window.has_value());
      CHECK(window->size().has_value());
    }

    CHECK_FALSE(videoInitialized());
  }
}

TEST_CASE("window.reject_worker_thread_operations") {
  auto window = Window::create(hiddenWindow());
  REQUIRE(window.has_value());
  bool rejected = false;
  std::jthread worker([&] {
    const auto other = Window::create(hiddenWindow());
    rejected = !other && !window->resize(640, 480) && !pollEvents()
               && !window->setRelativeMouse(true) && !window->relativeMouse() && !window->focused();
    rejected
        = rejected && !window->setTextInput(true) && !window->textInputActive() && !window->raise();
  });
  worker.join();
  CHECK(rejected);
  CHECK(window->size()->width == 320);
}

TEST_CASE("window.relative_mouse_reports_native_mode_and_focus_with_dummy_video") {
  auto window = Window::create(hiddenWindow()).value();
  auto* native = static_cast<SDL_Window*>(window.nativeHandle());
  CHECK_FALSE(window.relativeMouse().value());
  CHECK(window.focused().value() == (SDL_GetKeyboardFocus() == native));
  REQUIRE(window.setRelativeMouse(false));
  auto captured = window.setRelativeMouse(true);

  if (captured) {
    CHECK(window.relativeMouse().value());
    CHECK(SDL_GetWindowRelativeMouseMode(native));
    REQUIRE(window.setRelativeMouse(true));
  } else {
    CHECK(captured.error().code == Dot::Core::ErrorCode::External);
    CHECK_FALSE(window.relativeMouse().value());
  }

  REQUIRE(window.setRelativeMouse(false));
  CHECK_FALSE(window.relativeMouse().value());
}

#if defined(__APPLE__)
TEST_CASE("window.partial_initialization_is_cleaned_up") {
  auto config = hiddenWindow();
  config.gpuSurface = true;
  CHECK_FALSE(Window::create(config).has_value());
  CHECK_FALSE(videoInitialized());
}
#endif

TEST_CASE("platform.events_require_initialization") {
  CHECK_FALSE(pollEvents().has_value());
  CHECK_FALSE(requestQuit().has_value());
}

TEST_CASE("platform.editor_navigation_scancodes_preserve_press_release") {
  auto window = Window::create(hiddenWindow()).value();
  REQUIRE(pollEvents());
  constexpr std::array mapping{std::pair{SDL_SCANCODE_DELETE, Key::Delete},
                               std::pair{SDL_SCANCODE_INSERT, Key::Insert},
                               std::pair{SDL_SCANCODE_HOME, Key::Home},
                               std::pair{SDL_SCANCODE_END, Key::End},
                               std::pair{SDL_SCANCODE_PAGEUP, Key::PageUp},
                               std::pair{SDL_SCANCODE_PAGEDOWN, Key::PageDown},
                               std::pair{SDL_SCANCODE_KP_ENTER, Key::KeypadEnter}};

  for (const auto& [code, expected] : mapping) {
    for (auto down : {true, false}) {
      SDL_Event native{};
      native.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
      native.key.windowID = window.id();
      native.key.scancode = code;
      native.key.down = down;
      REQUIRE(SDL_PushEvent(&native));
      auto events = pollEvents().value();
      bool found = false;

      for (const auto& event : events) {
        if (const auto* key = std::get_if<KeyChanged>(&event.data)) {
          found = found
                  || (key->key == expected && key->down == down && event.windowId == window.id());
        }
      }

      CHECK(found);
    }
  }
}

TEST_CASE("platform.translate_native_events") {
  auto window = Window::create(hiddenWindow());
  REQUIRE(window.has_value());
  REQUIRE(pollEvents().has_value());
  SDL_Event key{};
  key.type = SDL_EVENT_KEY_DOWN;
  key.key.windowID = window->id();
  key.key.scancode = SDL_SCANCODE_A;
  key.key.down = true;
  REQUIRE(SDL_PushEvent(&key));
  SDL_Event motion{};
  motion.type = SDL_EVENT_MOUSE_MOTION;
  motion.motion.windowID = window->id();
  motion.motion.x = 12;
  motion.motion.y = 23;
  motion.motion.xrel = 4;
  REQUIRE(SDL_PushEvent(&motion));
  REQUIRE(requestQuit().has_value());
  const auto events = pollEvents();
  REQUIRE(events.has_value());
  bool gotKey = false;
  bool gotMotion = false;
  bool gotQuit = false;

  for (const auto& event : *events) {
    if (const auto* data = std::get_if<KeyChanged>(&event.data)) {
      gotKey = data->key == Key::A && data->down && event.windowId == window->id();
    }

    if (const auto* data = std::get_if<PointerMoved>(&event.data)) {
      gotMotion = data->x == 12 && data->y == 23 && data->deltaX == 4;
    }

    gotQuit |= std::holds_alternative<Quit>(event.data);
  }

  CHECK(gotKey);
  CHECK(gotMotion);
  CHECK(gotQuit);
}

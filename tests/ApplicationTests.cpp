#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <chrono>
#include <expected>
#include <latch>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

import Dot.Core.Application;
import Dot.Core.Events;
import Dot.Core.Result;
import Dot.Platform.Events;
import Dot.Platform.Input;
import Dot.Platform.System;
import Dot.Platform.Window;

using namespace Dot::Core;
using namespace std::chrono_literals;

namespace {
  auto testConfig(bool pause = false) -> ApplicationConfig {
    ApplicationConfig config;
    config.window = {"Application test", 320, 240, true, true, false};
    config.fixedStep = 10ms;
    config.maxUpdates = 3;
    config.frameDelay = 0ns;
    config.pauseOnFocusLoss = pause;
    return config;
  }

  auto windowEvent(std::uint32_t id, Uint32 type) -> void {
    SDL_Event event{};
    event.type = type;
    event.window.windowID = id;
    REQUIRE(SDL_PushEvent(&event));
  }
}  // namespace

TEST_CASE("application.fixed_and_frame_callback_order") {
  auto application = Application::create(testConfig()).value();
  std::vector<std::string> calls;
  LoopCallbacks callbacks;
  callbacks.fixedUpdate = [&](double delta) -> Result<void> {
    CHECK(delta == doctest::Approx(0.01));
    calls.emplace_back("fixed");
    return {};
  };
  callbacks.update = [&](double delta) -> Result<void> {
    CHECK(delta == doctest::Approx(0.025));
    calls.emplace_back("update");
    return {};
  };
  callbacks.render = [&](const FrameState& frame) -> Result<void> {
    CHECK(frame.index == 0);
    CHECK(frame.updates == 2);
    CHECK(frame.interpolation == 0.5);
    CHECK(frame.elapsed == 25ms);
    calls.emplace_back("render");
    return {};
  };
  const auto frame = application.step(25ms, callbacks);
  REQUIRE(frame.has_value());
  CHECK(frame->running);
  CHECK(calls == std::vector<std::string>{"fixed", "fixed", "update", "render"});
}

TEST_CASE("application.bounded_catch_up") {
  auto application = Application::create(testConfig()).value();
  int updates = 0;
  LoopCallbacks callbacks;
  callbacks.fixedUpdate = [&](double) -> Result<void> {
    ++updates;
    return {};
  };
  const auto frame = application.step(105ms, callbacks);
  REQUIRE(frame.has_value());
  CHECK(updates == 3);
  CHECK(frame->updates == 3);
  CHECK(frame->dropped == 70ms);
  CHECK(frame->interpolation == 0.5);
}

TEST_CASE("application.focus_pause_does_not_accumulate_time") {
  auto application = Application::create(testConfig(true)).value();
  REQUIRE(Dot::Platform::pollEvents().has_value());
  int updates = 0;
  int renders = 0;
  LoopCallbacks callbacks;
  callbacks.fixedUpdate = [&](double) -> Result<void> {
    ++updates;
    return {};
  };
  callbacks.render = [&](const FrameState&) -> Result<void> {
    ++renders;
    return {};
  };
  windowEvent(application.window().id(), SDL_EVENT_WINDOW_FOCUS_LOST);
  auto frame = application.step(50ms, callbacks);
  REQUIRE(frame.has_value());
  CHECK(frame->paused);
  CHECK(frame->updates == 0);
  CHECK(updates == 0);
  CHECK(renders == 1);
  windowEvent(application.window().id(), SDL_EVENT_WINDOW_FOCUS_GAINED);
  frame = application.step(10ms, callbacks);
  REQUIRE(frame.has_value());
  CHECK_FALSE(frame->paused);
  CHECK(frame->updates == 1);
  CHECK(updates == 1);
  CHECK(renders == 2);
}

TEST_CASE("application.quit_stops_before_callbacks") {
  auto application = Application::create(testConfig()).value();
  int quitEvents = 0;
  auto connection = application.events().subscribe([&](const Dot::Platform::Event& event) {
    if (std::holds_alternative<Dot::Platform::Quit>(event.data)) {
      ++quitEvents;
    }
  });
  LoopCallbacks callbacks;
  callbacks.update = [](double) -> Result<void> {
    FAIL("Update after quit");
    return {};
  };
  callbacks.render = [](const FrameState&) -> Result<void> {
    FAIL("Render after quit");
    return {};
  };
  REQUIRE(Dot::Platform::requestQuit().has_value());
  const auto frame = application.step(10ms, callbacks);
  REQUIRE(frame.has_value());
  CHECK_FALSE(frame->running);
  CHECK(quitEvents == 1);
}

TEST_CASE("application.only_own_close_stops_loop") {
  auto application = Application::create(testConfig()).value();
  auto other = Dot::Platform::Window::create(testConfig().window).value();
  windowEvent(other.id(), SDL_EVENT_WINDOW_CLOSE_REQUESTED);
  auto frame = application.step(10ms);
  REQUIRE(frame.has_value());
  CHECK(frame->running);
  windowEvent(application.window().id(), SDL_EVENT_WINDOW_CLOSE_REQUESTED);
  frame = application.step(10ms);
  REQUIRE(frame.has_value());
  CHECK_FALSE(frame->running);
}

TEST_CASE("application.callback_error_preserves_details") {
  {
    auto application = Application::create(testConfig()).value();
    LoopCallbacks callbacks;
    callbacks.update = [](double) -> Result<void> {
      return fail(ErrorCode::InvalidArgument, "Game rejected input");
    };
    const auto frame = application.step(10ms, callbacks);
    REQUIRE_FALSE(frame.has_value());
    CHECK(frame.error().code == ErrorCode::InvalidArgument);
    CHECK(frame.error().message == "Game rejected input");
    CHECK_FALSE(application.running());
  }
  CHECK_FALSE(Dot::Platform::videoInitialized());
}

TEST_CASE("application.callback_exception_is_reported") {
  auto application = Application::create(testConfig()).value();
  LoopCallbacks callbacks;
  callbacks.fixedUpdate = [](double) -> Result<void> { throw std::runtime_error("Script failed"); };
  const auto frame = application.step(10ms, callbacks);
  REQUIRE_FALSE(frame.has_value());
  CHECK(frame.error().code == ErrorCode::External);
  CHECK(frame.error().message.find("Script failed") != std::string::npos);
  CHECK_FALSE(application.running());
}

TEST_CASE("application.invalid_timing_does_not_initialize_video") {
  auto config = testConfig();
  config.fixedStep = 0ns;
  CHECK_FALSE(Application::create(config).has_value());
  config = testConfig();
  config.maxUpdates = 0;
  CHECK_FALSE(Application::create(config).has_value());
  config = testConfig();
  config.frameDelay = -1ns;
  CHECK_FALSE(Application::create(config).has_value());
  CHECK_FALSE(Dot::Platform::videoInitialized());
}

TEST_CASE("application.invalid_frame_is_transactional") {
  auto application = Application::create(testConfig()).value();
  REQUIRE(application.step(5ms).has_value());
  CHECK_FALSE(application.step(-1ns).has_value());
  CHECK(application.running());
  const auto frame = application.step(5ms);
  REQUIRE(frame.has_value());
  CHECK(frame->updates == 1);
}

TEST_CASE("application.input_and_event_subscription") {
  auto application = Application::create(testConfig()).value();
  SDL_Event key{};
  key.type = SDL_EVENT_KEY_DOWN;
  key.key.windowID = application.window().id();
  key.key.scancode = SDL_SCANCODE_SPACE;
  key.key.down = true;
  REQUIRE(SDL_PushEvent(&key));
  REQUIRE(application.step(10ms).has_value());
  CHECK(application.input().key(Dot::Platform::Key::Space).pressed);
  REQUIRE(application.step(10ms).has_value());
  CHECK_FALSE(application.input().key(Dot::Platform::Key::Space).pressed);
  CHECK(application.input().key(Dot::Platform::Key::Space).down);
}

TEST_CASE("application.frame_budget_and_move") {
  auto application = Application::create(testConfig()).value();
  const auto windowId = application.window().id();
  auto moved = std::move(application);
  CHECK_FALSE(application.running());
  CHECK_FALSE(application.step(10ms).has_value());
  CHECK_THROWS_AS(static_cast<void>(application.window()), std::logic_error);
  CHECK(moved.window().id() == windowId);
  int frames = 0;
  LoopCallbacks callbacks;
  callbacks.render = [&](const FrameState&) -> Result<void> {
    ++frames;
    return {};
  };
  REQUIRE(moved.run(callbacks, 3).has_value());
  CHECK(frames == 3);
}

TEST_CASE("application.stop_from_fixed_callback") {
  auto application = Application::create(testConfig()).value();
  LoopCallbacks callbacks;
  callbacks.fixedUpdate = [&](double) -> Result<void> {
    application.stop();
    return {};
  };
  callbacks.render = [](const FrameState&) -> Result<void> {
    FAIL("Rendered after stop");
    return {};
  };
  const auto frame = application.step(35ms, callbacks);
  REQUIRE(frame.has_value());
  CHECK_FALSE(frame->running);
  CHECK(frame->updates == 1);
}

TEST_CASE("application.thread_safe_stop") {
  auto application = Application::create(testConfig()).value();
  std::latch started(1);
  std::jthread worker([&] {
    started.wait();
    application.stop();
  });
  bool first = true;
  LoopCallbacks callbacks;
  callbacks.update = [&](double) -> Result<void> {
    if (first) {
      first = false;
      started.count_down();
    }
    return {};
  };
  const auto result = application.run(callbacks);
  worker.join();
  REQUIRE(result.has_value());
  CHECK_FALSE(application.running());
}

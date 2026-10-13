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

TEST_CASE("application.close_request_can_be_deferred_and_errors_stop_dispatch") {
  ApplicationConfig config;
  config.window = {"Close test", 320, 240, true, true, false};
  auto application = Application::create(config).value();
  LoopCallbacks callbacks;
  unsigned closes = 0;
  unsigned renders = 0;
  callbacks.acceptClose = [&]() -> Result<bool> {
    ++closes;
    return false;
  };
  callbacks.render = [&](const auto&) -> Result<void> {
    ++renders;
    return {};
  };
  REQUIRE(Dot::Platform::requestQuit());
  REQUIRE(application.step(0ns, callbacks));
  CHECK(application.running());
  CHECK(closes == 1);
  CHECK(renders == 1);
  callbacks.acceptClose
      = []() -> Result<bool> { return fail(ErrorCode::External, "close handler failed"); };
  REQUIRE(Dot::Platform::requestQuit());
  auto failed = application.step(0ns, callbacks);
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message == "close handler failed");
  CHECK_FALSE(application.running());
  CHECK(renders == 1);
}

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

TEST_CASE("application.prepare_frame_observes_events_before_fixed_and_paused_callbacks") {
  auto application = Application::create(testConfig(true)).value();
  REQUIRE(Dot::Platform::pollEvents());
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.windowID = application.window().id();
  event.key.scancode = SDL_SCANCODE_A;
  event.key.down = true;
  REQUIRE(SDL_PushEvent(&event));
  std::vector<std::string> order;
  LoopCallbacks callbacks;
  callbacks.prepareFrame = [&](const Dot::Platform::InputState& state) -> Result<void> {
    CHECK(state.key(Dot::Platform::Key::A).pressed);
    order.emplace_back("input");
    return {};
  };

  callbacks.fixedUpdate = [&](double) -> Result<void> {
    order.emplace_back("fixed");
    return {};
  };

  callbacks.update = [&](double) -> Result<void> {
    order.emplace_back("update");
    return {};
  };

  callbacks.render = [&](const FrameState&) -> Result<void> {
    order.emplace_back("render");
    return {};
  };

  REQUIRE(application.step(20ms, callbacks));
  CHECK(order == std::vector<std::string>{"input", "fixed", "fixed", "update", "render"});
  order.clear();
  callbacks.prepareFrame = [&](const Dot::Platform::InputState& state) -> Result<void> {
    CHECK(state.key(Dot::Platform::Key::A).released);
    CHECK_FALSE(state.key(Dot::Platform::Key::A).down);
    order.emplace_back("input");
    return {};
  };

  windowEvent(application.window().id(), SDL_EVENT_WINDOW_FOCUS_LOST);
  auto paused = application.step(20ms, callbacks);
  REQUIRE(paused);
  CHECK(paused->paused);
  CHECK(order == std::vector<std::string>{"input", "render"});
}

TEST_CASE("application_prepare_frame_failures_exceptions_stop_and_quit_skip_later_callbacks") {
  for (int mode = 0; mode < 4; ++mode) {
    auto application = Application::create(testConfig()).value();
    LoopCallbacks callbacks;
    int prepared = 0;
    callbacks.prepareFrame = [&](const Dot::Platform::InputState&) -> Result<void> {
      ++prepared;

      if (mode == 0) {
        return fail(ErrorCode::Io, "Input preparation failed");
      }

      if (mode == 1) {
        throw std::runtime_error("Input preparation threw");
      }

      application.stop();
      return {};
    };

    callbacks.fixedUpdate = [](double) -> Result<void> {
      FAIL("Fixed callback after preparation failure/stop");
      return {};
    };

    callbacks.update = callbacks.fixedUpdate;
    callbacks.render = [](const FrameState&) -> Result<void> {
      FAIL("Render after preparation failure/stop");
      return {};
    };

    if (mode == 3) {
      REQUIRE(Dot::Platform::requestQuit());
    }

    auto result = application.step(20ms, callbacks);
    CHECK_FALSE(application.running());
    CHECK(prepared == (mode == 3 ? 0 : 1));

    if (mode < 2) {
      REQUIRE_FALSE(result);
      CHECK(result.error().code == (mode == 0 ? ErrorCode::Io : ErrorCode::External));
    } else {
      REQUIRE(result);
      CHECK_FALSE(result->running);
    }
  }
}

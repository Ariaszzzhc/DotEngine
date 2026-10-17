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
  REQUIRE(application.setProfiling(true));
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
  CHECK(application.profiling()->frames.back().renderNanoseconds.has_value());
  callbacks.acceptClose
      = []() -> Result<bool> { return fail(ErrorCode::External, "close handler failed"); };
  REQUIRE(Dot::Platform::requestQuit());
  auto failed = application.step(0ns, callbacks);
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message == "close handler failed");
  CHECK_FALSE(application.running());
  CHECK(renders == 1);
  const auto profile = application.profiling().value();
  REQUIRE(profile.frames.size() == 2);
  CHECK(profile.frames.back().eventsNanoseconds.has_value());
  CHECK_FALSE(profile.frames.back().prepareNanoseconds);
  CHECK_FALSE(profile.frames.back().renderNanoseconds);
  REQUIRE(profile.frames.back().error);
  CHECK(profile.frames.back().error->message == "close handler failed");
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
  callbacks.updateFrame = [&](const FrameState& frame) -> Result<void> {
    CHECK(frame.elapsed == 25ms);
    CHECK(frame.updates == 2);
    CHECK(frame.interpolation == 0.5);
    calls.emplace_back("frame-update");
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
  CHECK(calls == std::vector<std::string>{"fixed", "fixed", "frame-update", "update", "render"});
}

TEST_CASE("application.exact_frame_updates_preserve_nanoseconds_and_stop_boundaries") {
  for (const bool failure : {false, true}) {
    auto application = Application::create(testConfig()).value();
    REQUIRE(application.setProfiling(true));
    const auto elapsed = 27ms + 3ns;
    LoopCallbacks callbacks;
    callbacks.updateFrame = [&](const FrameState& frame) -> Result<void> {
      CHECK(frame.elapsed == elapsed);
      CHECK(frame.updates == 2);

      if (failure) {
        return fail(ErrorCode::Io, "Frame simulation failed");
      }

      application.stop();
      return {};
    };
    callbacks.update = [](double) -> Result<void> {
      FAIL("Variable callback after exact frame stop/failure");
      return {};
    };
    callbacks.render = [](const FrameState&) -> Result<void> {
      FAIL("Render after exact frame stop/failure");
      return {};
    };
    const auto result = application.step(elapsed, callbacks);
    CHECK(result.has_value() == !failure);
    const auto profile = application.profiling().value();
    REQUIRE(profile.frames.size() == 1);
    CHECK(profile.frames.back().updateNanoseconds.has_value());
    CHECK_FALSE(profile.frames.back().renderNanoseconds);
    CHECK(profile.frames.back().error.has_value() == failure);
  }
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

TEST_CASE("application.profiling_measures_real_phases_and_preserves_simulation") {
  auto application = Application::create(testConfig()).value();
  CHECK_FALSE(application.profiling()->enabled);
  REQUIRE(application.setProfiling(true));
  LoopCallbacks callbacks;
  callbacks.prepareFrame = [](const auto&) -> Result<void> { return {}; };
  callbacks.fixedUpdate = [](double) -> Result<void> { return {}; };
  callbacks.update = [](double) -> Result<void> { return {}; };
  callbacks.render = [](const FrameState&) -> Result<void> { return {}; };
  REQUIRE(application.step(25ms, callbacks));
  const auto profile = application.profiling().value();
  REQUIRE(profile.frames.size() == 1);
  CHECK(profile.sampled == 1);
  const auto& frame = profile.frames[0];
  CHECK(frame.frame.index == 0);
  CHECK(frame.frame.elapsed == 25ms);
  CHECK(frame.frame.updates == 2);
  CHECK(frame.frame.dropped == 0ns);
  CHECK(frame.frame.running);
  CHECK_FALSE(frame.frame.paused);
  CHECK_FALSE(frame.error);
  REQUIRE(frame.eventsNanoseconds);
  REQUIRE(frame.prepareNanoseconds);
  REQUIRE(frame.fixedNanoseconds);
  REQUIRE(frame.updateNanoseconds);
  REQUIRE(frame.renderNanoseconds);
  CHECK(*frame.eventsNanoseconds + *frame.prepareNanoseconds + *frame.fixedNanoseconds
            + *frame.updateNanoseconds + *frame.renderNanoseconds
        <= frame.totalNanoseconds);
  REQUIRE(application.setProfiling(true));
  CHECK(application.profiling()->sampled == 1);
  CHECK(application.profiling()->session == profile.session);
  REQUIRE(application.step(105ms));
  const auto latest = application.profiling()->frames.back();
  CHECK(latest.frame.updates == 3);
  CHECK(latest.frame.dropped == 80ms);
  CHECK(latest.eventsNanoseconds.has_value());
  CHECK_FALSE(latest.prepareNanoseconds);
  CHECK_FALSE(latest.fixedNanoseconds);
  CHECK_FALSE(latest.updateNanoseconds);
  CHECK_FALSE(latest.renderNanoseconds);
}

TEST_CASE("application.profiling_retains_failure_boundaries_and_owned_errors") {
  for (int phase = 0; phase < 5; ++phase) {
    auto application = Application::create(testConfig()).value();
    REQUIRE(application.setProfiling(true));
    LoopCallbacks callbacks;
    callbacks.prepareFrame = [=](const auto&) -> Result<void> {
      return phase == 0 ? fail(ErrorCode::Io, "Input publication failed") : Result<void>{};
    };
    callbacks.fixedUpdate = [=](double) -> Result<void> {
      return phase == 1 ? fail(ErrorCode::External, "Physics failed") : Result<void>{};
    };
    callbacks.update = [=](double) -> Result<void> {
      if (phase == 2) {
        throw std::runtime_error("Script callback threw");
      }

      return {};
    };
    callbacks.render = [=](const FrameState&) -> Result<void> {
      if (phase == 4) {
        throw 42;
      }

      return fail(ErrorCode::Unavailable, "Surface failed");
    };
    const auto result = application.step(10ms, callbacks);
    REQUIRE_FALSE(result);
    auto profile = application.profiling().value();
    REQUIRE(profile.frames.size() == 1);
    const auto& frame = profile.frames[0];
    REQUIRE(frame.error);
    CHECK(frame.error->code == result.error().code);
    CHECK(frame.error->message == result.error().message);
    CHECK_FALSE(frame.frame.running);
    CHECK(frame.prepareNanoseconds.has_value());
    CHECK(frame.fixedNanoseconds.has_value() == (phase >= 1));
    CHECK(frame.updateNanoseconds.has_value() == (phase >= 2));
    CHECK(frame.renderNanoseconds.has_value() == (phase >= 3));
    REQUIRE(profile.phasePeaks.events);
    REQUIRE(profile.phasePeaks.prepare);
    CHECK(profile.phasePeaks.fixed.has_value() == (phase >= 1));
    CHECK(profile.phasePeaks.update.has_value() == (phase >= 2));
    CHECK(profile.phasePeaks.render.has_value() == (phase >= 3));
    REQUIRE(profile.phasePeaks.prepare->error);
    CHECK(profile.phasePeaks.prepare->error->message == result.error().message);
    REQUIRE(application.step(1s));
    CHECK(application.profiling()->sampled == 1);
    REQUIRE(application.setProfiling(false));
    CHECK(application.profiling()->frames.empty());
    CHECK_FALSE(application.profiling()->phasePeaks.events);
    CHECK(profile.phasePeaks.prepare->error->message == result.error().message);
    CHECK(profile.frames[0].error->message == result.error().message);
  }
}

TEST_CASE("application.profiling_phase_peaks_keep_independent_frame_context_after_eviction") {
  auto application = Application::create(testConfig()).value();
  REQUIRE(application.setProfiling(true));
  LoopCallbacks callbacks;
  callbacks.update = [](double) -> Result<void> { return {}; };
  REQUIRE(application.step(0ns, callbacks));
  const auto update = application.profiling()->frames.back();
  callbacks = {};
  callbacks.fixedUpdate = [](double) -> Result<void> { return {}; };
  REQUIRE(application.step(10ms, callbacks));
  const auto fixed = application.profiling()->frames.back();
  callbacks = {};
  callbacks.prepareFrame = [](const auto&) -> Result<void> { return {}; };
  REQUIRE(application.step(0ns, callbacks));
  const auto prepare = application.profiling()->frames.back();
  callbacks = {};
  callbacks.render = [](const FrameState&) -> Result<void> { return {}; };
  REQUIRE(application.step(0ns, callbacks));
  const auto render = application.profiling()->frames.back();
  auto greatestEvents = update;
  REQUIRE(greatestEvents.eventsNanoseconds);

  const auto initial = application.profiling().value();

  for (const auto& sample : initial.frames) {
    REQUIRE(sample.eventsNanoseconds);

    if (*sample.eventsNanoseconds > *greatestEvents.eventsNanoseconds) {
      greatestEvents = sample;
    }
  }

  for (int index = 0; index < 140; ++index) {
    REQUIRE(application.step(1ms));
    const auto sample = application.profiling()->frames.back();
    REQUIRE(sample.eventsNanoseconds);

    if (*sample.eventsNanoseconds > *greatestEvents.eventsNanoseconds) {
      greatestEvents = sample;
    }
  }

  const auto profile = application.profiling().value();
  CHECK(profile.sampled == 144);
  REQUIRE(profile.frames.size() == 120);
  CHECK(profile.frames.front().frame.index == 24);
  REQUIRE(profile.phasePeaks.events);
  REQUIRE(profile.phasePeaks.prepare);
  REQUIRE(profile.phasePeaks.fixed);
  REQUIRE(profile.phasePeaks.update);
  REQUIRE(profile.phasePeaks.render);
  CHECK(profile.phasePeaks.events->frame.index == greatestEvents.frame.index);
  CHECK(profile.phasePeaks.events->eventsNanoseconds == greatestEvents.eventsNanoseconds);
  CHECK(profile.phasePeaks.prepare->frame.index == prepare.frame.index);
  CHECK(profile.phasePeaks.prepare->prepareNanoseconds == prepare.prepareNanoseconds);
  CHECK(profile.phasePeaks.fixed->frame.index == fixed.frame.index);
  CHECK(profile.phasePeaks.fixed->fixedNanoseconds == fixed.fixedNanoseconds);
  CHECK(profile.phasePeaks.update->frame.index == update.frame.index);
  CHECK(profile.phasePeaks.update->updateNanoseconds == update.updateNanoseconds);
  CHECK(profile.phasePeaks.render->frame.index == render.frame.index);
  CHECK(profile.phasePeaks.render->renderNanoseconds == render.renderNanoseconds);
  CHECK(profile.phasePeaks.fixed->frame.updates == 1);
  CHECK(profile.phasePeaks.update->frame.updates == 0);
  CHECK_FALSE(profile.phasePeaks.fixed->updateNanoseconds);
  CHECK_FALSE(profile.phasePeaks.render->fixedNanoseconds);
  REQUIRE(application.setProfiling(false));
  CHECK_FALSE(application.profiling()->phasePeaks.update);
  REQUIRE(application.setProfiling(true));
  CHECK(application.profiling()->session > profile.session);
  CHECK_FALSE(application.profiling()->phasePeaks.render);
  auto moved = std::move(application);
  CHECK_FALSE(application.profiling());
  CHECK_FALSE(moved.profiling()->phasePeaks.render);
  CHECK(profile.phasePeaks.update->updateNanoseconds == update.updateNanoseconds);
}

TEST_CASE("application.profiling_history_keeps_the_slowest_step_after_eviction") {
  auto application = Application::create(testConfig()).value();
  REQUIRE(application.setProfiling(true));
  LoopCallbacks callbacks;
  callbacks.render = [](const FrameState&) -> Result<void> {
    std::this_thread::sleep_for(5ms);
    return {};
  };
  REQUIRE(application.step(105ms, callbacks));
  auto maximum = application.profiling()->frames.back();

  for (int frame = 1; frame < 140; ++frame) {
    REQUIRE(application.step(1ms));
    auto latest = application.profiling()->frames.back();

    if (latest.totalNanoseconds > maximum.totalNanoseconds) {
      maximum = latest;
    }
  }

  auto profile = application.profiling().value();
  CHECK(profile.sampled == 140);
  REQUIRE(profile.frames.size() == 120);
  CHECK(profile.frames.front().frame.index == 20);
  CHECK(profile.frames.back().frame.index == 139);
  REQUIRE(profile.slowest);
  CHECK(profile.slowest->frame.index == maximum.frame.index);
  CHECK(profile.slowest->totalNanoseconds == maximum.totalNanoseconds);
  REQUIRE(profile.longestInterval);
  CHECK(profile.longestInterval->frame.index == 0);
  CHECK(profile.longestInterval->frame.elapsed == 105ms);
  CHECK(profile.longestInterval->frame.dropped == 70ms);
  REQUIRE(application.setProfiling(false));
  CHECK(application.profiling()->sampled == 0);
  CHECK_FALSE(application.profiling()->slowest);
  REQUIRE(application.setProfiling(true));
  CHECK(application.profiling()->session > profile.session);
  CHECK(application.profiling()->frames.empty());
  CHECK(profile.frames.size() == 120);
}

TEST_CASE("application.profiling_callback_toggles_pause_move_and_thread_guards") {
  auto application = Application::create(testConfig(true)).value();
  LoopCallbacks callbacks;
  callbacks.prepareFrame = [&](const auto&) { return application.setProfiling(true); };
  REQUIRE(application.step(10ms, callbacks));
  CHECK(application.profiling()->sampled == 0);
  REQUIRE(application.step(10ms, callbacks));
  CHECK(application.profiling()->sampled == 1);
  callbacks.render = [&](const auto&) -> Result<void> {
    REQUIRE(application.setProfiling(false));
    return application.setProfiling(true);
  };
  REQUIRE(application.step(10ms, callbacks));
  CHECK(application.profiling()->sampled == 0);
  callbacks.render = [](const auto&) -> Result<void> { return {}; };
  windowEvent(application.window().id(), SDL_EVENT_WINDOW_FOCUS_LOST);
  REQUIRE(application.step(50ms, callbacks));
  const auto paused = application.profiling()->frames.back();
  CHECK(paused.frame.paused);
  CHECK_FALSE(paused.fixedNanoseconds);
  CHECK_FALSE(paused.updateNanoseconds);
  CHECK(paused.renderNanoseconds.has_value());
  CHECK_FALSE(application.step(-1ns));
  CHECK(application.profiling()->sampled == 1);
  std::thread foreign([&] {
    CHECK_FALSE(application.setProfiling(false));
    CHECK_FALSE(application.profiling());
  });
  foreign.join();
  auto moved = std::move(application);
  CHECK_FALSE(application.profiling());
  CHECK_FALSE(application.setProfiling(true));
  CHECK(moved.profiling()->sampled == 1);
  REQUIRE(moved.setProfiling(false));
  REQUIRE(moved.setProfiling(false));
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

  callbacks.updateFrame = [](const FrameState&) -> Result<void> {
    FAIL("Exact frame update while paused");
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
  callbacks.updateFrame = {};
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

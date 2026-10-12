module;

#include <atomic>
#include <chrono>
#include <expected>
#include <format>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <variant>

module Dot.Core.Application;

import Dot.Core.Clock;
import Dot.Core.Events;
import Dot.Core.Result;
import Dot.Platform.Events;
import Dot.Platform.Input;
import Dot.Platform.System;
import Dot.Platform.Window;

namespace Dot::Core {
  struct Application::Impl {
    Platform::Window window;
    Platform::InputState input;
    FrameStepper stepper;
    Signal<Platform::Event> events;
    std::chrono::nanoseconds frameDelay;
    bool pauseOnFocusLoss;
    bool focused{true};
    std::atomic<bool> running{true};
    std::uint64_t frameIndex{0};

    Impl(Platform::Window window, const ApplicationConfig& config)
        : window(std::move(window)),
          input(this->window.id()),
          stepper(config.fixedStep, config.maxUpdates),
          frameDelay(config.frameDelay),
          pauseOnFocusLoss(config.pauseOnFocusLoss) {
    }
  };

  Application::Application(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Application::Application(Application&&) noexcept = default;

  auto Application::operator=(Application&&) noexcept -> Application& = default;

  Application::~Application() = default;

  auto Application::create(const ApplicationConfig& config) -> Result<Application> {
    if (config.fixedStep.count() <= 0 || config.maxUpdates == 0 || config.frameDelay.count() < 0) {
      return fail(ErrorCode::InvalidArgument, "Invalid application timing configuration");
    }

    auto window = Platform::Window::create(config.window);

    if (!window) {
      return std::unexpected(window.error());
    }

    return Application(std::make_unique<Impl>(std::move(*window), config));
  }

  auto Application::step(std::chrono::nanoseconds elapsed, const LoopCallbacks& callbacks)
      -> Result<FrameState> {
    if (!m_impl || !Platform::isMainThread()) {
      return fail(ErrorCode::InvalidState,
                  "Application stepping requires a live owner on the main thread");
    }

    if (elapsed.count() < 0) {
      return fail(ErrorCode::InvalidArgument, "Frame duration must not be negative");
    }

    FrameState frame{running(), false, m_impl->frameIndex, 0, 0, elapsed, {}};

    if (!frame.running) {
      return frame;
    }

    try {
      m_impl->input.beginFrame();
      auto events = Platform::pollEvents();

      if (!events) {
        stop();
        return std::unexpected(events.error());
      }

      for (const auto& event : *events) {
        m_impl->input.consume(event);

        if (std::holds_alternative<Platform::Quit>(event.data)
            || (event.windowId == m_impl->window.id()
                && std::holds_alternative<Platform::Close>(event.data))) {
          stop();
        }

        if (event.windowId == m_impl->window.id()) {
          if (const auto* focus = std::get_if<Platform::Focus>(&event.data)) {
            m_impl->focused = focus->focused;
          }
        }

        m_impl->events.publish(event);
      }

      if (!running()) {
        frame.running = false;
        return frame;
      }

      frame.paused = m_impl->pauseOnFocusLoss && !m_impl->focused;
      const auto timing
          = frame.paused ? FrameStep{0, 0, elapsed} : m_impl->stepper.advance(elapsed);

      if (frame.paused) {
        m_impl->stepper.reset();
      }

      frame.interpolation = timing.interpolation;
      frame.dropped = timing.dropped;
      const auto fixedSeconds = std::chrono::duration<double>(m_impl->stepper.fixedStep()).count();

      for (std::size_t update = 0; update < timing.updates && running(); ++update) {
        if (callbacks.fixedUpdate) {
          auto result = callbacks.fixedUpdate(fixedSeconds);

          if (!result) {
            stop();
            return std::unexpected(result.error());
          }
        }

        ++frame.updates;
      }

      if (running() && !frame.paused && callbacks.update) {
        auto result = callbacks.update(std::chrono::duration<double>(elapsed).count());

        if (!result) {
          stop();
          return std::unexpected(result.error());
        }
      }

      frame.running = running();

      if (frame.running && callbacks.render) {
        auto result = callbacks.render(frame);

        if (!result) {
          stop();
          return std::unexpected(result.error());
        }
      }

      frame.running = running();
      ++m_impl->frameIndex;
      return frame;
    } catch (const std::exception& error) {
      stop();
      return fail(ErrorCode::External,
                  std::format("Application callback failed: {}", error.what()));
    } catch (...) {
      stop();
      return fail(ErrorCode::External, "Application callback failed with an unknown exception");
    }
  }

  auto Application::run(const LoopCallbacks& callbacks, std::size_t frameBudget) -> Result<void> {
    if (!m_impl || !Platform::isMainThread()) {
      return fail(ErrorCode::InvalidState,
                  "Application running requires a live owner on the main thread");
    }

    Clock clock;
    std::chrono::nanoseconds previous{};
    std::size_t frames = 0;

    while (running() && (frameBudget == 0 || frames < frameBudget)) {
      const auto current = clock.elapsed();
      auto result = step(current - previous, callbacks);
      previous = current;

      if (!result) {
        return std::unexpected(result.error());
      }

      ++frames;

      if (running() && (frameBudget == 0 || frames < frameBudget)
          && m_impl->frameDelay.count() > 0) {
        std::this_thread::sleep_for(m_impl->frameDelay);
      }
    }

    return {};
  }

  auto Application::stop() noexcept -> void {
    if (m_impl) {
      m_impl->running.store(false);
    }
  }

  auto Application::running() const noexcept -> bool {
    return m_impl && m_impl->running.load();
  }

  auto Application::window() -> Platform::Window& {
    if (!m_impl) {
      throw std::logic_error("Application has been moved from");
    }

    return m_impl->window;
  }

  auto Application::input() const -> const Platform::InputState& {
    if (!m_impl) {
      throw std::logic_error("Application has been moved from");
    }

    return m_impl->input;
  }

  auto Application::events() -> Signal<Platform::Event>& {
    if (!m_impl) {
      throw std::logic_error("Application has been moved from");
    }

    return m_impl->events;
  }
}  // namespace Dot::Core

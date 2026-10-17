module;

#include <atomic>
#include <chrono>
#include <deque>
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <optional>
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
  namespace {
    using ProfileClock = std::chrono::steady_clock;

    class StageTimer {
    public:
      explicit StageTimer(std::optional<std::uint64_t>* destination)
          : m_destination(destination),
            m_start(destination ? ProfileClock::now() : ProfileClock::time_point{}) {
      }

      StageTimer(const StageTimer&) = delete;
      auto operator=(const StageTimer&) -> StageTimer& = delete;

      ~StageTimer() {
        if (m_destination) {
          *m_destination = static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(ProfileClock::now() - m_start)
                  .count());
        }
      }

    private:
      std::optional<std::uint64_t>* m_destination;
      ProfileClock::time_point m_start;
    };
  }  // namespace

  struct Application::Impl {
    struct Profile {
      std::uint64_t sampled{};
      std::deque<ApplicationFrameTiming> frames;
      std::optional<ApplicationFrameTiming> slowest;
      std::optional<ApplicationFrameTiming> longestInterval;
    };

    Platform::Window window;
    Platform::InputState input;
    FrameStepper stepper;
    Signal<Platform::Event> events;
    std::chrono::nanoseconds frameDelay;
    bool pauseOnFocusLoss;
    bool focused{true};
    std::atomic<bool> running{true};
    std::uint64_t frameIndex{0};
    std::uint64_t profileSession{};
    std::unique_ptr<Profile> profile;

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

    std::optional<ApplicationFrameTiming> sample;
    const auto session = m_impl->profileSession;

    if (m_impl->profile) {
      sample.emplace();
    }

    const auto started = sample ? ProfileClock::now() : ProfileClock::time_point{};
    auto result = [&]() -> Result<FrameState> {
      try {
        {
          StageTimer timer(sample ? &sample->eventsNanoseconds : nullptr);
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
              auto accepted = callbacks.acceptClose ? callbacks.acceptClose() : Result<bool>{true};

              if (!accepted) {
                stop();
                return std::unexpected(accepted.error());
              }

              if (*accepted) {
                stop();
              }
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
        }

        if (callbacks.prepareFrame) {
          StageTimer timer(sample ? &sample->prepareNanoseconds : nullptr);
          auto prepared = callbacks.prepareFrame(m_impl->input);

          if (!prepared) {
            stop();
            return std::unexpected(prepared.error());
          }
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
        const auto fixedSeconds
            = std::chrono::duration<double>(m_impl->stepper.fixedStep()).count();

        {
          StageTimer timer(sample && callbacks.fixedUpdate && timing.updates
                               ? &sample->fixedNanoseconds
                               : nullptr);
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
        }

        if (running() && !frame.paused && (callbacks.updateFrame || callbacks.update)) {
          StageTimer timer(sample ? &sample->updateNanoseconds : nullptr);

          if (callbacks.updateFrame) {
            auto result = callbacks.updateFrame(frame);

            if (!result) {
              stop();
              return std::unexpected(result.error());
            }
          }

          if (running() && callbacks.update) {
            auto result = callbacks.update(std::chrono::duration<double>(elapsed).count());

            if (!result) {
              stop();
              return std::unexpected(result.error());
            }
          }
        }

        frame.running = running();

        if (frame.running && callbacks.render) {
          StageTimer timer(sample ? &sample->renderNanoseconds : nullptr);
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
    }();

    if (sample && m_impl->profile && session == m_impl->profileSession) {
      sample->frame = frame;
      sample->frame.running = running();
      sample->totalNanoseconds = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(ProfileClock::now() - started)
              .count());

      try {
        if (!result) {
          sample->error = result.error();
        }

        auto& profile = *m_impl->profile;

        if (profile.sampled == std::numeric_limits<std::uint64_t>::max()) {
          stop();
          return fail(ErrorCode::Unavailable, "Application profile sample counter exhausted");
        }

        std::optional<ApplicationFrameTiming> slowest;
        std::optional<ApplicationFrameTiming> longestInterval;

        if (!profile.slowest || sample->totalNanoseconds > profile.slowest->totalNanoseconds) {
          slowest = *sample;
        }

        if (!profile.longestInterval
            || sample->frame.elapsed > profile.longestInterval->frame.elapsed) {
          longestInterval = *sample;
        }

        profile.frames.push_back(std::move(*sample));

        if (profile.frames.size() > 120) {
          profile.frames.pop_front();
        }

        if (slowest) {
          profile.slowest = std::move(slowest);
        }

        if (longestInterval) {
          profile.longestInterval = std::move(longestInterval);
        }

        ++profile.sampled;
      } catch (const std::exception& error) {
        stop();
        return fail(ErrorCode::External,
                    "Record application profile: " + std::string(error.what()));
      }
    }

    return result;
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

  auto Application::setProfiling(bool enabled) -> Result<void> {
    if (!m_impl || !Platform::isMainThread()) {
      return fail(ErrorCode::InvalidState, "Application profiling requires its main-thread owner");
    }

    if (enabled == bool(m_impl->profile)) {
      return {};
    }

    if (enabled) {
      if (m_impl->profileSession == std::numeric_limits<std::uint64_t>::max()) {
        return fail(ErrorCode::Unavailable, "Application profiling session exhausted");
      }

      try {
        auto profile = std::make_unique<Impl::Profile>();
        m_impl->profile = std::move(profile);
        ++m_impl->profileSession;
      } catch (const std::exception& error) {
        return fail(ErrorCode::External,
                    "Enable application profiling: " + std::string(error.what()));
      }
    } else {
      m_impl->profile.reset();
    }

    return {};
  }

  auto Application::profiling() const -> Result<ApplicationProfiling> {
    if (!m_impl || !Platform::isMainThread()) {
      return fail(ErrorCode::InvalidState, "Application profiling requires its main-thread owner");
    }

    try {
      ApplicationProfiling result{bool(m_impl->profile), m_impl->profileSession, 0, {}, {}, {}};

      if (m_impl->profile) {
        const auto& profile = *m_impl->profile;
        result.sampled = profile.sampled;
        result.frames.assign(profile.frames.begin(), profile.frames.end());
        result.slowest = profile.slowest;
        result.longestInterval = profile.longestInterval;
      }

      return result;
    } catch (const std::exception& error) {
      return fail(ErrorCode::External,
                  "Inspect application profiling: " + std::string(error.what()));
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

  auto Application::window() const -> const Platform::Window& {
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

module;

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <thread>
#include <utility>

module Dot.Audio.Playback;

import Dot.Audio.Output;
import Dot.Audio.Mixer;
import Dot.Core.Result;

namespace Dot::Audio {
  struct Playback::Impl {
    struct Command {
      virtual ~Command() = default;
      virtual auto execute(Mixer* mixer, Output* output, const Core::Error* error) -> void = 0;
    };

    template <typename T>
    struct Task final : Command {
      std::packaged_task<Core::Result<T>(Mixer*, Output*, const Core::Error*)> m_task;

      explicit Task(decltype(m_task) task) : m_task(std::move(task)) {
      }

      auto execute(Mixer* mixer, Output* output, const Core::Error* error) -> void override {
        m_task(mixer, output, error);
      }
    };

    PlaybackConfig m_config;
    std::thread::id m_client{std::this_thread::get_id()};
    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::unique_ptr<Command> m_pending;
    bool m_closing{};
    bool m_closed{};
    std::optional<Core::Error> m_error;
    std::promise<Core::Result<void>> m_started;
    OutputInfo m_info{};
    PlaybackStats m_last{PlaybackState::Closed, {}, {}, std::nullopt};

    explicit Impl(PlaybackConfig config) : m_config(config) {
    }

    ~Impl() {
      shutdown();
    }

    auto checkClient() const -> Core::Result<void> {
      if (m_client != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Playback requires its creating client thread");
      }

      return {};
    }

    auto shutdown() -> void {
      {
        std::lock_guard lock(m_mutex);
        m_closing = true;
      }

      m_wake.notify_one();

      if (m_worker.joinable()) {
        m_worker.join();
      }
    }

    template <typename T, typename Function>
    auto request(Function function) -> Core::Result<T> {
      auto checked = checkClient();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      try {
        std::packaged_task<Core::Result<T>(Mixer*, Output*, const Core::Error*)> task{
            [function = std::move(function)](Mixer* mixer, Output* output,
                                             const Core::Error* error) mutable -> Core::Result<T> {
              if (error) {
                return std::unexpected(*error);
              }

              return function(*mixer, *output);
            }};
        auto result = task.get_future();

        {
          std::lock_guard lock(m_mutex);

          if (m_error) {
            return std::unexpected(*m_error);
          }

          if (m_closing || m_closed) {
            return Core::fail(Core::ErrorCode::InvalidState, "Playback is closed");
          }

          if (m_pending) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Playback already has a pending control");
          }

          m_pending = std::make_unique<Task<T>>(std::move(task));
        }

        m_wake.notify_one();
        return result.get();
      } catch (const std::exception& error) {
        return Core::fail(Core::ErrorCode::External,
                          std::format("Playback control: {}", error.what()));
      }
    }

    static auto snapshot(Mixer& mixer, Output& output) -> Core::Result<PlaybackStats> {
      auto mix = mixer.stats();
      auto out = output.stats();

      if (!mix) {
        return std::unexpected(mix.error());
      }

      if (!out) {
        return std::unexpected(out.error());
      }

      const auto state = mix->faulted || out->fault != OutputFault::None || !out->running
                             ? PlaybackState::Faulted
                             : PlaybackState::Running;
      return PlaybackStats{state, *mix, *out, std::nullopt};
    }

    auto interrupted(bool pending = true) -> bool {
      std::lock_guard lock(m_mutex);
      return m_closing || (pending && static_cast<bool>(m_pending));
    }

    auto fill(Mixer& mixer, Output& output, bool startup = false) -> Core::Result<void> {
      const auto batches
          = (m_config.targetFrames + m_config.blockFrames - 1) / m_config.blockFrames;

      for (std::uint32_t batch = 0; batch < batches; ++batch) {
        if (!startup && interrupted(batch != 0)) {
          break;
        }

        auto stats = output.stats();

        if (!stats) {
          return std::unexpected(stats.error());
        }

        if (stats->fault != OutputFault::None || (!startup && !stats->running)) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Playback worker observed a stopped/faulted native output");
        }

        if (stats->queuedFrames >= m_config.targetFrames) {
          break;
        }

        const auto frames
            = std::min(m_config.blockFrames, m_config.targetFrames - stats->queuedFrames);
        auto mixed = output.mix(mixer, frames);

        if (!mixed) {
          return mixed;
        }
      }

      return {};
    }

    auto prepare(std::optional<Mixer>& mixer, std::optional<Output>& output) -> Core::Result<void> {
      auto createdMixer = Mixer::create(m_config.mixer);

      if (!createdMixer) {
        return std::unexpected(createdMixer.error());
      }

      mixer.emplace(std::move(*createdMixer));
      auto createdOutput = Output::create(m_config.output);

      if (!createdOutput) {
        return std::unexpected(createdOutput.error());
      }

      output.emplace(std::move(*createdOutput));
      auto info = output->info();

      if (!info) {
        return std::unexpected(info.error());
      }

      m_info = std::move(*info);
      auto filled = fill(*mixer, *output, true);

      if (!filled) {
        return filled;
      }

      return output->start();
    }

    auto run() -> void {
      std::optional<Mixer> mixer;
      std::optional<Output> output;
      std::optional<Core::Error> error;
      bool announced = false;

      try {
        auto prepared = prepare(mixer, output);

        if (!prepared) {
          error = prepared.error();
        } else {
          m_started.set_value(Core::Result<void>{});
          announced = true;

          while (true) {
            std::unique_ptr<Command> command;

            {
              std::lock_guard lock(m_mutex);

              if (m_closing) {
                break;
              }

              command = std::move(m_pending);
              m_pending.reset();
            }

            if (command) {
              command->execute(&*mixer, &*output, nullptr);
            }

            auto filled = fill(*mixer, *output);

            if (!filled) {
              error = filled.error();
              break;
            }

            auto current = snapshot(*mixer, *output);

            if (!current) {
              error = current.error();
              break;
            }

            {
              std::lock_guard lock(m_mutex);
              m_last = *current;
            }

            const auto queued = current->output.queuedFrames;
            const auto nanos
                = std::max<std::uint64_t>(50000, static_cast<std::uint64_t>(queued) * 1000000000
                                                     / m_config.output.sampleRate / 4);
            const auto wait
                = std::min(nanos, static_cast<std::uint64_t>(m_config.pollMilliseconds) * 1000000);
            std::unique_lock lock(m_mutex);
            m_wake.wait_for(lock, std::chrono::nanoseconds{static_cast<std::int64_t>(wait)},
                            [&] { return m_closing || static_cast<bool>(m_pending); });
          }
        }
      } catch (const std::exception& failure) {
        error = Core::Error{Core::ErrorCode::External,
                            std::format("Playback worker: {}", failure.what()),
                            std::source_location::current()};
      }

      if (output) {
        auto stopped = output->stop();

        if (!stopped && !error) {
          error = stopped.error();
        }
      }

      if (mixer && output) {
        auto final = snapshot(*mixer, *output);

        if (final) {
          m_last = *final;
          m_last.output.discardedFrames += m_last.output.queuedFrames;
        } else if (!error) {
          error = final.error();
        }
      }

      output.reset();
      mixer.reset();
      std::unique_ptr<Command> pending;

      {
        std::lock_guard lock(m_mutex);
        m_error = error;
        m_closed = true;
        m_last.state = error ? PlaybackState::Faulted : PlaybackState::Closed;
        m_last.error = error;
        m_last.mixer.voices = 0;
        m_last.mixer.playing = 0;
        m_last.mixer.backend = {};
        m_last.output.running = false;
        m_last.output.queuedFrames = 0;
        m_last.output.availableFrames = 0;
        m_last.output.backend = {};
        pending = std::move(m_pending);
        m_pending.reset();
      }

      if (pending) {
        auto reason = error.value_or(Core::Error{Core::ErrorCode::InvalidState,
                                                 "Playback closed before control execution",
                                                 std::source_location::current()});
        pending->execute(nullptr, nullptr, &reason);
      }

      if (!announced) {
        m_started.set_value(std::unexpected(*error));
      }
    }
  };

  Playback::Playback(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Playback::Playback(Playback&&) noexcept = default;

  auto Playback::operator=(Playback&&) noexcept -> Playback& = default;

  Playback::~Playback() = default;

  auto Playback::create(const PlaybackConfig& config) -> Core::Result<Playback> {
    if (config.mixer.channels != config.output.channels
        || config.mixer.sampleRate != config.output.sampleRate || config.blockFrames == 0
        || config.blockFrames > 65536 || config.blockFrames > config.targetFrames
        || config.targetFrames > config.output.queueFrames
        || static_cast<std::uint64_t>(config.targetFrames)
               < static_cast<std::uint64_t>(config.output.periodFrames) * 2
        || config.pollMilliseconds == 0 || config.pollMilliseconds > 20) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid playback format/buffering/scheduler configuration");
    }

    try {
      auto impl = std::make_unique<Impl>(config);
      auto started = impl->m_started.get_future();
      impl->m_worker = std::thread{&Impl::run, impl.get()};
      auto ready = started.get();

      if (!ready) {
        return std::unexpected(ready.error());
      }

      return Playback{std::move(impl)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        std::format("Create playback worker: {}", error.what()));
    }
  }

  auto Playback::createVoice(const Clip& clip, const VoiceSettings& settings)
      -> Core::Result<VoiceId> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Playback has been moved");
    }

    return m_impl->request<VoiceId>([source = clip, settings](Mixer& mixer, Output&) {
      return mixer.createVoice(source, settings);
    });
  }

  auto Playback::destroyVoice(VoiceId id) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Playback has been moved");
    }

    return m_impl->request<void>([id](Mixer& mixer, Output&) { return mixer.destroyVoice(id); });
  }

  auto Playback::play(VoiceId id) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Playback has been moved");
    }

    return m_impl->request<void>([id](Mixer& mixer, Output&) { return mixer.play(id); });
  }

  auto Playback::pause(VoiceId id) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Playback has been moved");
    }

    return m_impl->request<void>([id](Mixer& mixer, Output&) { return mixer.pause(id); });
  }

  auto Playback::seek(VoiceId id, std::uint64_t frame) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Playback has been moved");
    }

    return m_impl->request<void>(
        [id, frame](Mixer& mixer, Output&) { return mixer.seek(id, frame); });
  }

  auto Playback::configureVoice(VoiceId id, const VoiceSettings& settings) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Playback has been moved");
    }

    return m_impl->request<void>(
        [id, settings](Mixer& mixer, Output&) { return mixer.configureVoice(id, settings); });
  }

  auto Playback::voice(VoiceId id) -> Core::Result<VoiceState> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Playback has been moved");
    }

    return m_impl->request<VoiceState>([id](Mixer& mixer, Output&) { return mixer.voice(id); });
  }

  auto Playback::setVolume(float volume) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Playback has been moved");
    }

    return m_impl->request<void>(
        [volume](Mixer& mixer, Output&) { return mixer.setVolume(volume); });
  }

  auto Playback::setOutputVolume(float volume) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Playback has been moved");
    }

    return m_impl->request<void>(
        [volume](Mixer&, Output& output) { return output.setVolume(volume); });
  }

  auto Playback::info() const -> Core::Result<OutputInfo> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Playback has been moved");
    }

    auto checked = m_impl->checkClient();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->m_info;
  }

  auto Playback::stats() -> Core::Result<PlaybackStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Playback has been moved");
    }

    auto checked = m_impl->checkClient();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    {
      std::lock_guard lock(m_impl->m_mutex);

      if (m_impl->m_closed) {
        return m_impl->m_last;
      }
    }

    auto result = m_impl->request<PlaybackStats>(
        [](Mixer& mixer, Output& output) { return Impl::snapshot(mixer, output); });

    if (!result) {
      std::lock_guard lock(m_impl->m_mutex);

      if (m_impl->m_closed) {
        return m_impl->m_last;
      }
    }

    return result;
  }

  auto Playback::close() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Playback has been moved");
    }

    auto checked = m_impl->checkClient();

    if (!checked) {
      return checked;
    }

    m_impl->shutdown();
    std::lock_guard lock(m_impl->m_mutex);

    if (m_impl->m_error) {
      return std::unexpected(*m_impl->m_error);
    }

    return {};
  }
}  // namespace Dot::Audio

module;

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "BackendMemory.hpp"
#include "FrameQueue.hpp"

module Dot.Audio.Output;

import Dot.Audio.Mixer;
import Dot.Core.Result;
import Dot.Core.Text;

namespace Dot::Audio {
  namespace {
    auto nativeBackend(AudioBackend value) -> ma_backend {
      switch (value) {
        case AudioBackend::CoreAudio:
          return ma_backend_coreaudio;
        case AudioBackend::Wasapi:
          return ma_backend_wasapi;
        case AudioBackend::PulseAudio:
          return ma_backend_pulseaudio;
        case AudioBackend::Alsa:
          return ma_backend_alsa;
        case AudioBackend::Jack:
          return ma_backend_jack;
        default:
          return ma_backend_null;
      }
    }

    auto publicBackend(ma_backend value) -> AudioBackend {
      switch (value) {
        case ma_backend_coreaudio:
          return AudioBackend::CoreAudio;
        case ma_backend_wasapi:
          return AudioBackend::Wasapi;
        case ma_backend_pulseaudio:
          return AudioBackend::PulseAudio;
        case ma_backend_alsa:
          return AudioBackend::Alsa;
        case ma_backend_jack:
          return AudioBackend::Jack;
        default:
          return AudioBackend::Default;
      }
    }

    auto failure(ma_result status, std::string_view operation) -> std::unexpected<Core::Error> {
      return Core::fail(Core::ErrorCode::Unavailable,
                        std::format("{}: {}", operation, ma_result_description(status)));
    }

    auto faultName(OutputFault value) -> std::string_view {
      switch (value) {
        case OutputFault::Callback:
          return "invalid native callback";
        case OutputFault::Stopped:
          return "device stopped unexpectedly";
        case OutputFault::Rerouted:
          return "device rerouted";
        case OutputFault::Interrupted:
          return "device interrupted";
        case OutputFault::Start:
          return "device startup failed";
        case OutputFault::Stop:
          return "device stop failed";
        default:
          return "none";
      }
    }
  }  // namespace

  struct Output::Impl {
    OutputConfig m_config;
    Detail::BackendHeap m_heap;
    Detail::FrameQueue m_queue;
    std::vector<float> m_scratch;
    ma_context m_context{};
    ma_device m_device{};
    bool m_contextReady{};
    bool m_deviceReady{};
    std::thread::id m_thread{std::this_thread::get_id()};
    OutputInfo m_info{};
    std::atomic<bool> m_accepting{};
    std::atomic<bool> m_expectedRunning{};
    std::atomic<OutputFault> m_fault{OutputFault::None};
    std::atomic<std::uint64_t> m_consumed{};
    std::atomic<std::uint64_t> m_callbackFrames{};
    std::atomic<std::uint64_t> m_underruns{};
    std::atomic<std::uint64_t> m_callbacks{};
    std::atomic<std::uint64_t> m_clipped{};
    std::uint64_t m_submitted{};
    std::uint64_t m_discarded{};

    explicit Impl(OutputConfig config)
        : m_config(config),
          m_heap(config.backendBytes, Detail::outputMemory),
          m_queue(config.channels, config.queueFrames),
          m_scratch(static_cast<std::size_t>(config.channels) * config.queueFrames) {
    }

    ~Impl() {
      m_expectedRunning.store(false);
      m_accepting.store(false);

      if (m_deviceReady) {
        ma_device_uninit(&m_device);
      }

      if (m_contextReady) {
        ma_context_uninit(&m_context);
      }
    }

    auto fault(OutputFault reason) noexcept -> void {
      auto expected = OutputFault::None;
      m_fault.compare_exchange_strong(expected, reason);
      m_accepting.store(false);
    }

    static auto callback(ma_device* device, void* output, const void*, ma_uint32 frames) noexcept
        -> void {
      auto& self = *static_cast<Impl*>(device->pUserData);
      self.m_callbacks.fetch_add(1);

      if (frames == 0) {
        return;
      }

      if (!output) {
        self.fault(OutputFault::Callback);
        return;
      }

      auto* samples = static_cast<float*>(output);
      const auto count = static_cast<std::size_t>(frames) * self.m_config.channels;

      if (frames > 65536) {
        self.fault(OutputFault::Callback);
      }

      if (!self.m_accepting.load() || self.m_fault.load() != OutputFault::None) {
        std::fill_n(samples, count, 0.0F);
        return;
      }

      const auto read = self.m_queue.read(samples, frames);
      std::uint64_t clipped = 0;

      for (std::size_t index = 0; index < count; ++index) {
        if (samples[index] < -1 || samples[index] > 1) {
          ++clipped;
          samples[index] = std::clamp(samples[index], -1.0F, 1.0F);
        }
      }

      self.m_consumed.fetch_add(read);
      self.m_callbackFrames.fetch_add(frames);
      self.m_underruns.fetch_add(frames - read);
      self.m_clipped.fetch_add(clipped);
    }

    static auto notification(const ma_device_notification* event) noexcept -> void {
      auto& self = *static_cast<Impl*>(event->pDevice->pUserData);

      switch (event->type) {
        case ma_device_notification_type_stopped:
          if (self.m_expectedRunning.load()) {
            self.fault(OutputFault::Stopped);
          }

          break;
        case ma_device_notification_type_rerouted:
          self.fault(OutputFault::Rerouted);
          break;
        case ma_device_notification_type_interruption_began:
          self.fault(OutputFault::Interrupted);
          break;
        default:
          break;
      }
    }

    auto check(bool healthy = true) const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Audio output requires its owner thread");
      }

      if (healthy && m_fault.load() != OutputFault::None) {
        return Core::fail(
            Core::ErrorCode::Unavailable,
            std::format("Audio output: {}; recreate the device", faultName(m_fault.load())));
      }

      return {};
    }
  };

  Output::Output(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Output::Output(Output&&) noexcept = default;

  auto Output::operator=(Output&&) noexcept -> Output& = default;

  Output::~Output() = default;

  auto Output::backendMemory() noexcept -> AudioMemoryStats {
    return {Detail::outputMemory.allocations.load(), Detail::outputMemory.bytes.load()};
  }

  auto Output::create(const OutputConfig& config) -> Core::Result<Output> {
    if ((config.channels != 1 && config.channels != 2) || config.sampleRate < 8000
        || config.sampleRate > 192000 || !std::has_single_bit(config.queueFrames)
        || config.queueFrames < 64 || config.queueFrames > 262144 || config.periodFrames < 64
        || config.periodFrames > 8192 || config.periodFrames > config.queueFrames
        || config.backendBytes == 0 || config.backendBytes > 1024ULL * 1024 * 1024
        || !std::isfinite(config.volume) || config.volume < 0 || config.volume > 1
        || (config.backend != AudioBackend::Default
            && nativeBackend(config.backend) == ma_backend_null)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid native audio output configuration");
    }

    try {
      auto impl = std::make_unique<Impl>(config);
      std::array<ma_backend, 3> backends{};
      ma_uint32 count = 1;

      if (config.backend != AudioBackend::Default) {
        backends[0] = nativeBackend(config.backend);
      } else {
#if defined(__APPLE__)
        backends[0] = ma_backend_coreaudio;
#elif defined(_WIN32)
        backends[0] = ma_backend_wasapi;
#else
        backends = {ma_backend_pulseaudio, ma_backend_alsa, ma_backend_jack};
        count = 3;
#endif
      }

      auto context = ma_context_config_init();
      context.allocationCallbacks = impl->m_heap.callbacks();
      auto status = ma_context_init(backends.data(), count, &context, &impl->m_context);

      if (status != MA_SUCCESS) {
        return failure(status, "Initialize native audio context");
      }

      impl->m_contextReady = true;
      auto device = ma_device_config_init(ma_device_type_playback);
      device.playback.format = ma_format_f32;
      device.playback.channels = config.channels;
      device.sampleRate = config.sampleRate;
      device.periodSizeInFrames = config.periodFrames;
      device.noClip = MA_TRUE;
      device.dataCallback = Impl::callback;
      device.notificationCallback = Impl::notification;
      device.pUserData = impl.get();
      status = ma_device_init(&impl->m_context, &device, &impl->m_device);

      if (status != MA_SUCCESS) {
        return failure(status, "Open native playback device");
      }

      impl->m_deviceReady = true;
      status = ma_device_set_master_volume(&impl->m_device, config.volume);

      if (status != MA_SUCCESS) {
        return failure(status, "Set native playback software volume");
      }

      const auto backend = publicBackend(impl->m_context.backend);

      if (backend == AudioBackend::Default || impl->m_device.playback.format != ma_format_f32
          || impl->m_device.playback.channels != config.channels
          || impl->m_device.sampleRate != config.sampleRate) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Native audio backend did not retain the requested callback format");
      }

      std::array<char, MA_MAX_DEVICE_NAME_LENGTH + 1> name{};
      std::size_t length = 0;
      status = ma_device_get_name(&impl->m_device, ma_device_type_playback, name.data(),
                                  name.size(), &length);

      if (status != MA_SUCCESS) {
        return failure(status, "Read playback device name");
      }

      if (length == 0 || length >= name.size()
          || !Core::isValidUtf8(std::string_view{name.data(), length})) {
        return Core::fail(Core::ErrorCode::Unavailable, "Invalid native playback device name");
      }

      impl->m_info = {backend,
                      std::string{name.data(), length},
                      config.channels,
                      config.sampleRate,
                      impl->m_device.playback.internalChannels,
                      impl->m_device.playback.internalSampleRate,
                      config.queueFrames};
      auto checked = impl->check();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      return Output{std::move(impl)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        std::format("Create native audio output: {}", error.what()));
    }
  }

  auto Output::info() const -> Core::Result<OutputInfo> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio output has been moved");
    }

    auto checked = m_impl->check(false);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->m_info;
  }

  auto Output::stats() const -> Core::Result<OutputStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio output has been moved");
    }

    auto checked = m_impl->check(false);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    const auto queued = m_impl->m_queue.queued();
    return OutputStats{ma_device_is_started(&m_impl->m_device) != 0,
                       m_impl->m_fault.load(),
                       queued,
                       m_impl->m_config.queueFrames - queued,
                       m_impl->m_submitted,
                       m_impl->m_consumed.load(),
                       m_impl->m_discarded,
                       m_impl->m_callbackFrames.load(),
                       m_impl->m_underruns.load(),
                       m_impl->m_callbacks.load(),
                       m_impl->m_clipped.load(),
                       {m_impl->m_heap.allocations.load(), m_impl->m_heap.bytes.load()}};
  }

  auto Output::submit(std::span<const float> samples) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio output has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    const auto channels = m_impl->m_config.channels;

    if (samples.empty() || samples.size() % channels != 0 || samples.size() / channels > 65536
        || samples.size() / channels
               > std::numeric_limits<std::uint64_t>::max() - m_impl->m_submitted
        || !std::ranges::all_of(samples, [](float value) { return std::isfinite(value); })) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid native audio PCM frame data");
    }

    if (!m_impl->m_queue.write(samples)) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Native audio queue is full; retry after consumption");
    }

    m_impl->m_submitted += samples.size() / channels;
    return {};
  }

  auto Output::mix(Mixer& mixer, std::uint32_t frames) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio output has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    auto source = mixer.config();

    if (!source) {
      return std::unexpected(source.error());
    }

    if (source->channels != m_impl->m_config.channels
        || source->sampleRate != m_impl->m_config.sampleRate || frames == 0 || frames > 65536
        || frames > std::numeric_limits<std::uint64_t>::max() - m_impl->m_submitted) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Audio mixer/output format or frame count mismatch");
    }

    if (frames > m_impl->m_queue.available()) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Native audio queue has insufficient mix capacity");
    }

    auto samples
        = std::span{m_impl->m_scratch}.first(static_cast<std::size_t>(frames) * source->channels);
    auto mixed = mixer.render(samples);

    if (!mixed) {
      return mixed;
    }

    return submit(samples);
  }

  auto Output::setVolume(float volume) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio output has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (!std::isfinite(volume) || volume < 0 || volume > 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid playback software volume");
    }

    const auto status = ma_device_set_master_volume(&m_impl->m_device, volume);
    return status == MA_SUCCESS ? Core::Result<void>{}
                                : failure(status, "Set playback software volume");
  }

  auto Output::start() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio output has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (ma_device_is_started(&m_impl->m_device)) {
      return {};
    }

    m_impl->m_expectedRunning.store(true);
    m_impl->m_accepting.store(true);
    const auto status = ma_device_start(&m_impl->m_device);

    if (status != MA_SUCCESS) {
      m_impl->m_expectedRunning.store(false);
      m_impl->fault(OutputFault::Start);
      return failure(status, "Start native audio playback");
    }

    return m_impl->check();
  }

  auto Output::stop() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio output has been moved");
    }

    auto checked = m_impl->check(false);

    if (!checked) {
      return checked;
    }

    m_impl->m_expectedRunning.store(false);
    const auto status = ma_device_stop(&m_impl->m_device);
    m_impl->m_accepting.store(false);

    if (status != MA_SUCCESS) {
      m_impl->fault(OutputFault::Stop);
      return failure(status, "Stop native audio playback");
    }

    return {};
  }

  auto Output::clear() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio output has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (ma_device_get_state(&m_impl->m_device) != ma_device_state_stopped) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Stop native audio playback before clearing its queue");
    }

    m_impl->m_discarded += m_impl->m_queue.queued();
    m_impl->m_queue.reset();
    return {};
  }
}  // namespace Dot::Audio

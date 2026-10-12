module;

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <expected>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <span>
#include <string_view>
#include <thread>
#include <utility>

#include "Backend.h"
#include "BackendMemory.hpp"

module Dot.Audio.Mixer;

import Dot.Audio.Clip;
import Dot.Audio.Spatial;
import Dot.Math;
import Dot.Core.Result;

namespace Dot::Audio {
  namespace {
    std::atomic<std::uint64_t> nextMixer{1};

    auto settingsValid(const VoiceSettings& settings) -> bool {
      return std::isfinite(settings.volume) && settings.volume >= 0 && settings.volume <= 1
             && std::isfinite(settings.pan) && settings.pan >= -1 && settings.pan <= 1
             && std::isfinite(settings.pitch) && settings.pitch >= 0.25F && settings.pitch <= 4;
    }

    auto failure(ma_result status, std::string_view operation) -> std::unexpected<Core::Error> {
      return Core::fail(Core::ErrorCode::External,
                        std::format("{}: {}", operation, ma_result_description(status)));
    }

    auto identity() -> Core::Result<std::uint64_t> {
      auto value = nextMixer.load();

      while (value != std::numeric_limits<std::uint64_t>::max()) {
        if (nextMixer.compare_exchange_weak(value, value + 1)) {
          return value;
        }
      }

      return Core::fail(Core::ErrorCode::InvalidState, "Audio mixer identity exhausted");
    }

    struct Voice {
      Clip clip;
      ClipInfo info;
      VoiceSettings settings;
      SpatialSettings spatial;
      ma_audio_buffer buffer{};
      ma_sound sound{};
      bool bufferReady{};
      bool soundReady{};

      Voice(const Clip& source, ClipInfo format, VoiceSettings properties)
          : clip(source), info(format), settings(properties) {
      }

      Voice(const Voice&) = delete;
      auto operator=(const Voice&) -> Voice& = delete;
      Voice(Voice&&) = delete;
      auto operator=(Voice&&) -> Voice& = delete;

      ~Voice() {
        if (soundReady) {
          ma_sound_uninit(&sound);
        }

        if (bufferReady) {
          ma_audio_buffer_uninit(&buffer);
        }
      }

      auto configure(VoiceSettings properties) -> Core::Result<void> {
        auto status = dotAudioSetLooping(&sound, properties.looping ? MA_TRUE : MA_FALSE);

        if (status != MA_SUCCESS) {
          return failure(status, "Set audio voice looping");
        }

        status = dotAudioSetPitch(&sound, properties.pitch);

        if (status != MA_SUCCESS) {
          return failure(status, "Set audio voice pitch");
        }

        ma_sound_set_volume(&sound, properties.volume);
        ma_sound_set_pan_mode(&sound, ma_pan_mode_pan);
        ma_sound_set_pan(&sound, properties.pan);
        settings = properties;
        return {};
      }
    };
  }  // namespace

  struct Mixer::Impl {
    MixerConfig m_config;
    Detail::BackendHeap m_heap;
    ma_engine m_engine{};
    bool m_ready{};
    bool m_faulted{};
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_identity;
    std::uint64_t m_nextVoice{1};
    std::uint64_t m_renderedFrames{};
    Listener m_listener{};
    std::map<std::uint64_t, std::unique_ptr<Voice>> m_voices;

    Impl(MixerConfig settings, std::uint64_t id)
        : m_config(settings), m_heap{settings.backendBytes, Detail::mixerMemory}, m_identity(id) {
    }

    ~Impl() {
      m_voices.clear();

      if (m_ready) {
        ma_engine_uninit(&m_engine);
      }
    }

    auto check(bool requireHealthy = true) const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Audio operation requires its owner thread");
      }

      if (requireHealthy && m_faulted) {
        return Core::fail(Core::ErrorCode::InvalidState, "Audio mixer is faulted; recreate it");
      }

      return {};
    }

    auto find(VoiceId id) const -> Core::Result<Voice*> {
      const auto found = m_voices.find(id.value);

      if (id.mixer != m_identity || found == m_voices.end()) {
        return Core::fail(Core::ErrorCode::NotFound,
                          "Audio voice is stale or belongs to another mixer");
      }

      return found->second.get();
    }
  };

  Mixer::Mixer(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Mixer::Mixer(Mixer&&) noexcept = default;

  auto Mixer::operator=(Mixer&&) noexcept -> Mixer& = default;

  Mixer::~Mixer() = default;

  auto Mixer::backendMemory() noexcept -> AudioMemoryStats {
    return {Detail::mixerMemory.allocations.load(), Detail::mixerMemory.bytes.load()};
  }

  auto Mixer::create(const MixerConfig& config) -> Core::Result<Mixer> {
    if ((config.channels != 1 && config.channels != 2) || config.sampleRate < 8000
        || config.sampleRate > 192000 || config.maxVoices == 0 || config.maxVoices > 1024
        || config.backendBytes == 0 || config.backendBytes > 1024ULL * 1024 * 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid audio mixer format/capacity/budget");
    }

    auto id = identity();

    if (!id) {
      return std::unexpected(id.error());
    }

    try {
      auto impl = std::make_unique<Impl>(config, *id);
      auto engine = ma_engine_config_init();
      engine.noDevice = MA_TRUE;
      engine.channels = config.channels;
      engine.sampleRate = config.sampleRate;
      engine.allocationCallbacks = impl->m_heap.callbacks();
      const auto status = ma_engine_init(&engine, &impl->m_engine);

      if (status != MA_SUCCESS) {
        return failure(status, "Initialize audio mixer");
      }

      impl->m_ready = true;
      return Mixer{std::move(impl)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        std::format("Create audio mixer: {}", error.what()));
    }
  }

  auto Mixer::createVoice(const Clip& clip, const VoiceSettings& settings)
      -> Core::Result<VoiceId> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio mixer has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto info = clip.info();
    auto samples = clip.samples();

    if (!info || !samples) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Audio clip has been moved");
    }

    if (!settingsValid(settings) || m_impl->m_voices.size() >= m_impl->m_config.maxVoices
        || m_impl->m_nextVoice == std::numeric_limits<std::uint64_t>::max()) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid voice settings or voice capacity");
    }

    try {
      auto entry = std::make_unique<Voice>(clip, *info, settings);
      auto buffer = ma_audio_buffer_config_init(ma_format_f32, info->channels, info->frames,
                                                samples->data(), nullptr);
      buffer.sampleRate = info->sampleRate;
      buffer.allocationCallbacks = m_impl->m_heap.callbacks();
      auto status = ma_audio_buffer_init(&buffer, &entry->buffer);

      if (status != MA_SUCCESS) {
        return failure(status, "Initialize audio voice buffer");
      }

      entry->bufferReady = true;
      status
          = ma_sound_init_from_data_source(&m_impl->m_engine, &entry->buffer,
                                           MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &entry->sound);

      if (status != MA_SUCCESS) {
        return failure(status, "Initialize audio voice");
      }

      entry->soundReady = true;
      auto configured = entry->configure(settings);

      if (!configured) {
        return std::unexpected(configured.error());
      }
      const VoiceId id{m_impl->m_identity, m_impl->m_nextVoice};
      m_impl->m_voices.emplace(id.value, std::move(entry));
      ++m_impl->m_nextVoice;
      return id;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        std::format("Create audio voice: {}", error.what()));
    }
  }

  auto Mixer::destroyVoice(VoiceId id) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio mixer has been moved");
    }

    auto checked = m_impl->check(false);

    if (!checked) {
      return checked;
    }

    auto found = m_impl->find(id);

    if (!found) {
      return std::unexpected(found.error());
    }

    m_impl->m_voices.erase(id.value);
    return {};
  }

  auto Mixer::play(VoiceId id) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio mixer has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    auto found = m_impl->find(id);

    if (!found) {
      return std::unexpected(found.error());
    }

    const auto status = ma_sound_start(&(*found)->sound);
    return status == MA_SUCCESS ? Core::Result<void>{} : failure(status, "Start audio voice");
  }

  auto Mixer::pause(VoiceId id) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio mixer has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    auto found = m_impl->find(id);

    if (!found) {
      return std::unexpected(found.error());
    }

    const auto status = ma_sound_stop(&(*found)->sound);
    return status == MA_SUCCESS ? Core::Result<void>{} : failure(status, "Pause audio voice");
  }

  auto Mixer::seek(VoiceId id, std::uint64_t frame) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio mixer has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    auto found = m_impl->find(id);

    if (!found) {
      return std::unexpected(found.error());
    }

    if (frame >= (*found)->info.frames) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Audio seek exceeds source frames");
    }

    const auto status = dotAudioSeek(&(*found)->sound, frame);
    return status == MA_SUCCESS ? Core::Result<void>{} : failure(status, "Seek audio voice");
  }

  auto Mixer::configureVoice(VoiceId id, const VoiceSettings& settings) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio mixer has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    auto found = m_impl->find(id);

    if (!found) {
      return std::unexpected(found.error());
    }

    if (!settingsValid(settings) || ((*found)->spatial.enabled && settings.pan != 0)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid audio voice settings");
    }

    return (*found)->configure(settings);
  }

  auto Mixer::voice(VoiceId id) const -> Core::Result<VoiceState> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio mixer has been moved");
    }

    auto checked = m_impl->check(false);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto found = m_impl->find(id);

    if (!found) {
      return std::unexpected(found.error());
    }

    const auto& entry = **found;
    return VoiceState{entry.info, entry.settings, ma_sound_is_playing(&entry.sound) != 0,
                      ma_sound_at_end(&entry.sound) != 0, entry.spatial};
  }

  auto Mixer::setVolume(float volume) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio mixer has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (!std::isfinite(volume) || volume < 0 || volume > 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid audio mixer volume");
    }

    const auto status = ma_engine_set_volume(&m_impl->m_engine, volume);
    return status == MA_SUCCESS ? Core::Result<void>{} : failure(status, "Set audio mixer volume");
  }

  auto Mixer::stats() const -> Core::Result<MixerStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio mixer has been moved");
    }

    auto checked = m_impl->check(false);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    std::size_t playing = 0;

    for (const auto& [id, entry] : m_impl->m_voices) {
      (void)id;
      playing += ma_sound_is_playing(&entry->sound) != 0 ? 1 : 0;
    }

    return MixerStats{m_impl->m_voices.size(),
                      playing,
                      m_impl->m_renderedFrames,
                      {m_impl->m_heap.allocations.load(), m_impl->m_heap.bytes.load()},
                      m_impl->m_faulted};
  }

  auto Mixer::config() const -> Core::Result<MixerConfig> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio mixer has been moved");
    }

    auto checked = m_impl->check(false);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->m_config;
  }

  auto Mixer::setListener(const Listener& input) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio mixer has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    auto listener = validatedListener(input);

    if (!listener) {
      return std::unexpected(listener.error());
    }

    auto matrix = Math::compose({}, listener->rotation);

    if (!matrix) {
      return std::unexpected(matrix.error());
    }

    auto forward = Math::transformVector(*matrix, {0, 0, -1});
    auto up = Math::transformVector(*matrix, {0, 1, 0});

    if (!forward || !up) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Audio listener basis is unrepresentable");
    }

    auto& engine = m_impl->m_engine;
    ma_engine_listener_set_position(&engine, 0, listener->position.x, listener->position.y,
                                    listener->position.z);
    ma_engine_listener_set_direction(&engine, 0, forward->x, forward->y, forward->z);
    ma_engine_listener_set_world_up(&engine, 0, up->x, up->y, up->z);
    ma_engine_listener_set_velocity(&engine, 0, listener->velocity.x, listener->velocity.y,
                                    listener->velocity.z);
    m_impl->m_listener = *listener;

    for (const auto& [id, entry] : m_impl->m_voices) {
      (void)id;
      const auto status = dotAudioSetPitch(&entry->sound, entry->settings.pitch);

      if (status != MA_SUCCESS) {
        m_impl->m_faulted = true;
        return failure(status, "Update listener-dependent audio pitch");
      }
    }

    return {};
  }

  auto Mixer::listener() const -> Core::Result<Listener> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio mixer has been moved");
    }

    auto checked = m_impl->check(false);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->m_listener;
  }

  auto Mixer::setSpatial(VoiceId id, const SpatialSettings& input) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio mixer has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    auto found = m_impl->find(id);

    if (!found) {
      return std::unexpected(found.error());
    }

    auto spatial = validatedSpatial(input);

    if (!spatial) {
      return std::unexpected(spatial.error());
    }

    auto& entry = **found;

    if (spatial->enabled && (entry.info.channels != 1 || entry.settings.pan != 0)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Spatial audio requires a mono clip and zero manual pan");
    }

    auto matrix = Math::compose({}, spatial->rotation);

    if (!matrix) {
      return std::unexpected(matrix.error());
    }

    auto forward = Math::transformVector(*matrix, {0, 0, -1});

    if (!forward) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Spatial audio direction is unrepresentable");
    }

    auto& sound = entry.sound;
    ma_sound_set_pinned_listener_index(&sound, 0);
    ma_sound_set_positioning(&sound, ma_positioning_absolute);
    ma_sound_set_position(&sound, spatial->position.x, spatial->position.y, spatial->position.z);
    ma_sound_set_direction(&sound, forward->x, forward->y, forward->z);
    ma_sound_set_velocity(&sound, spatial->velocity.x, spatial->velocity.y, spatial->velocity.z);
    const auto model = spatial->attenuation == Attenuation::Linear ? ma_attenuation_model_linear
                       : spatial->attenuation == Attenuation::Exponential
                           ? ma_attenuation_model_exponential
                           : ma_attenuation_model_inverse;
    ma_sound_set_attenuation_model(&sound, model);
    ma_sound_set_min_distance(&sound, spatial->minDistance);
    ma_sound_set_max_distance(&sound, spatial->maxDistance);
    ma_sound_set_rolloff(&sound, spatial->rolloff);
    ma_sound_set_min_gain(&sound, 0);
    ma_sound_set_max_gain(&sound, 1);
    ma_sound_set_directional_attenuation_factor(&sound, 1);
    constexpr auto radians = std::numbers::pi_v<float> / 180;
    ma_sound_set_cone(&sound, spatial->innerConeDegrees * radians,
                      spatial->outerConeDegrees * radians, spatial->outerConeGain);
    ma_sound_set_doppler_factor(&sound, spatial->dopplerFactor);
    ma_sound_set_spatialization_enabled(&sound, spatial->enabled ? MA_TRUE : MA_FALSE);
    const auto status = dotAudioSetPitch(&sound, entry.settings.pitch);

    if (status != MA_SUCCESS) {
      m_impl->m_faulted = true;
      return failure(status, "Configure spatial audio pitch");
    }

    entry.spatial = *spatial;
    return {};
  }

  auto Mixer::render(std::span<float> output) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio mixer has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    const auto channels = m_impl->m_config.channels;

    if (output.empty() || output.size() % channels != 0 || output.size() / channels > 65536
        || output.size() / channels
               > std::numeric_limits<std::uint64_t>::max() - m_impl->m_renderedFrames) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid audio output frame count");
    }

    const auto frames = output.size() / channels;
    ma_uint64 read = 0;
    const auto status = ma_engine_read_pcm_frames(&m_impl->m_engine, output.data(), frames, &read);
    /** The backend fills silent tails even when no active input frames exist.
     * 后端在没有活动输入帧时也填满静音尾部，读取计数不等于输出缓冲长度。 */
    const bool finite
        = status == MA_SUCCESS && read <= frames
          && std::ranges::all_of(output, [](float sample) { return std::isfinite(sample); });

    if (status != MA_SUCCESS || read > frames || !finite) {
      m_impl->m_faulted = true;
      return Core::fail(Core::ErrorCode::External,
                        std::format("Audio mix failed after advancement: {}; read {} of {} frames; "
                                    "verified finite output {}",
                                    ma_result_description(status), read, frames, finite));
    }

    const auto clock
        = ma_engine_set_time_in_pcm_frames(&m_impl->m_engine, m_impl->m_renderedFrames + frames);

    if (clock != MA_SUCCESS) {
      m_impl->m_faulted = true;
      return failure(clock, "Advance audio graph clock");
    }

    m_impl->m_renderedFrames += frames;
    return {};
  }
}  // namespace Dot::Audio

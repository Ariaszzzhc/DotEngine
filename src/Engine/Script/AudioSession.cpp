module;

#include <array>
#include <charconv>
#include <cstdint>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "ComponentJson.hpp"

module Dot.Script.AudioSession;

import Dot.Core.Result;
import Dot.Core.Text;
import Dot.Core.Files;
import Dot.World.Scene;
import Dot.Audio.SceneAudio;

namespace Dot::Script {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    using Reply = Core::Result<std::optional<std::string>>;

    constexpr std::array methods{"audio.voice", "audio.stats", "audio.listener", "audio.play",
                                 "audio.pause", "audio.stop",  "audio.seek",     "audio.setVolume"};

    auto frame(const Json& value) -> std::uint64_t {
      if (!value.is_string()) {
        throw std::invalid_argument("Audio frame requires a decimal string");
      }

      const auto& text = value.get_ref<const std::string&>();
      std::uint64_t result = 0;
      const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);

      if (text.empty() || text.size() > 20 || (text.size() != 1 && text.front() == '0')
          || error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument("Audio frame is not canonical uint64 decimal");
      }

      return result;
    }

    auto written(Core::Result<void> value) -> Reply {
      if (!value) {
        return std::unexpected(value.error());
      }

      return std::optional<std::string>{};
    }

    auto listenerJson(const Audio::Listener& value) -> Json {
      return {{"position", Detail::xyz(value.position)},
              {"rotation", Detail::xyzw(value.rotation)},
              {"velocity", Detail::xyz(value.velocity)}};
    }

    auto voiceJson(const Audio::VoiceState& value) -> Json {
      const auto& s = value.spatial;
      const auto attenuation = s.attenuation == Audio::Attenuation::Inverse  ? "inverse"
                               : s.attenuation == Audio::Attenuation::Linear ? "linear"
                                                                             : "exponential";
      return {{"clip",
               {{"channels", value.clip.channels},
                {"sampleRate", value.clip.sampleRate},
                {"frames", std::to_string(value.clip.frames)},
                {"durationSeconds", value.clip.durationSeconds}}},
              {"settings",
               {{"volume", value.settings.volume},
                {"pan", value.settings.pan},
                {"pitch", value.settings.pitch},
                {"looping", value.settings.looping}}},
              {"playing", value.playing},
              {"atEnd", value.atEnd},
              {"sourceFrame", std::to_string(value.sourceFrame)},
              {"spatial",
               {{"enabled", s.enabled},
                {"attenuation", attenuation},
                {"minDistance", s.minDistance},
                {"maxDistance", s.maxDistance},
                {"rolloff", s.rolloff},
                {"dopplerFactor", s.dopplerFactor},
                {"innerConeDegrees", s.innerConeDegrees},
                {"outerConeDegrees", s.outerConeDegrees},
                {"outerConeGain", s.outerConeGain},
                {"position", Detail::xyz(s.position)},
                {"rotation", Detail::xyzw(s.rotation)},
                {"velocity", Detail::xyz(s.velocity)}}}};
    }

    auto errorJson(const std::optional<Core::Error>& error) -> Json {
      if (!error) {
        return nullptr;
      }

      const auto names = std::array{"InvalidArgument", "NotFound",     "Io",
                                    "Unavailable",     "InvalidState", "External"};
      return {{"code", names.at(static_cast<std::size_t>(error->code))},
              {"message", error->message}};
    }

    auto playbackJson(const std::optional<Audio::PlaybackStats>& value) -> Json {
      if (!value) {
        return nullptr;
      }

      const auto& output = value->output;
      const auto state = value->state == Audio::PlaybackState::Prepared  ? "prepared"
                         : value->state == Audio::PlaybackState::Running ? "running"
                         : value->state == Audio::PlaybackState::Closed  ? "closed"
                                                                         : "faulted";
      constexpr std::array faults{"none",        "callback", "stopped", "rerouted",
                                  "interrupted", "start",    "stop"};
      return {{"state", state},
              {"error", errorJson(value->error)},
              {"output",
               {{"running", output.running},
                {"fault", faults.at(static_cast<std::size_t>(output.fault))},
                {"queuedFrames", output.queuedFrames},
                {"availableFrames", output.availableFrames},
                {"submittedFrames", std::to_string(output.submittedFrames)},
                {"consumedFrames", std::to_string(output.consumedFrames)},
                {"discardedFrames", std::to_string(output.discardedFrames)},
                {"callbackFrames", std::to_string(output.callbackFrames)},
                {"underrunFrames", std::to_string(output.underrunFrames)},
                {"callbacks", std::to_string(output.callbacks)},
                {"clippedSamples", std::to_string(output.clippedSamples)}}}};
    }
  }  // namespace

  struct AudioSession::Impl {
    std::shared_ptr<World::Scene> m_scene;
    Audio::SceneAudio m_audio;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_identity;
    std::uint64_t m_generation{1};
    float m_volume{1};
    bool m_playback;

    Impl(std::shared_ptr<World::Scene> scene, Audio::SceneAudio audio, bool playback)
        : m_scene(std::move(scene)),
          m_audio(std::move(audio)),
          m_identity(m_scene->id()),
          m_playback(playback) {
    }

    auto check(bool requireScene = true) const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id() || (requireScene && m_scene->id() != m_identity)) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Audio session requires original scene/owner thread");
      }

      return requireScene ? m_scene->name().transform([](const auto&) {}) : Core::Result<void>{};
    }

    auto dispatch(std::string_view method, const Json& args) -> Reply {
      const auto count = method == "audio.stats" || method == "audio.listener" ? 0U
                         : method == "audio.seek"                              ? 2U
                                                                               : 1U;

      if (args.size() != count) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Incorrect audio argument count");
      }

      if (method == "audio.stats") {
        auto stats = m_audio.stats();

        if (!stats) {
          return std::unexpected(stats.error());
        }

        return std::optional{Json{
            {"mode", m_playback ? "playback" : "offline"},
            {"generation", std::to_string(m_generation)},
            {"volume", m_volume},
            {"assets", stats->assets},
            {"sampleBytes", std::to_string(stats->sampleBytes)},
            {"voices", stats->mixer.voices},
            {"playing", stats->mixer.playing},
            {"renderedFrames", std::to_string(stats->mixer.renderedFrames)},
            {"faulted",
             stats->mixer.faulted
                 || (stats->playback && stats->playback->state == Audio::PlaybackState::Faulted)},
            {"playback", playbackJson(stats->playback)}}
                                 .dump()};
      }

      if (method == "audio.listener") {
        auto listener = m_audio.listener();

        if (!listener) {
          return std::unexpected(listener.error());
        }

        return std::optional{listenerJson(*listener).dump()};
      }

      if (method == "audio.setVolume") {
        const auto volume = Codec::number(args[0]);
        auto result = m_audio.setVolume(volume);

        if (result) {
          m_volume = volume;
        }

        return written(result);
      }

      Codec::requireFields(args[0], {"scene", "value"});
      const World::EntityId id{Codec::id(args[0]["scene"]), Codec::id(args[0]["value"])};
      auto owner = m_scene->entity(id);

      if (!owner) {
        return std::unexpected(owner.error());
      }

      if (method == "audio.voice") {
        auto value = m_audio.voice(id);

        if (!value) {
          return std::unexpected(value.error());
        }

        return std::optional{voiceJson(*value).dump()};
      }

      if (method == "audio.play") {
        return written(m_audio.play(id));
      }

      if (method == "audio.pause") {
        return written(m_audio.pause(id));
      }

      if (method == "audio.stop") {
        return written(m_audio.stop(id));
      }

      return written(m_audio.seek(id, frame(args[1])));
    }

    auto invoke(std::string_view method, std::string_view arguments) -> Reply {
      auto checked = check();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (arguments.empty() || arguments.size() > 64 * 1024 || !Core::isValidUtf8(arguments)
          || arguments.find('\0') != std::string_view::npos) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid audio host argument bytes");
      }

      try {
        auto args = Codec::parseJson(arguments, 16);

        if (!args.is_array()) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Audio arguments require an array");
        }

        return dispatch(method, args);
      } catch (const std::invalid_argument& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Audio JSON: " + std::string(error.what()));
      } catch (const Json::exception& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Audio JSON: " + std::string(error.what()));
      } catch (const std::exception& error) {
        return Core::fail(Core::ErrorCode::External,
                          "Audio host operation: " + std::string(error.what()));
      }
    }
  };

  AudioSession::AudioSession(std::shared_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  AudioSession::AudioSession(AudioSession&&) noexcept = default;

  auto AudioSession::operator=(AudioSession&&) noexcept -> AudioSession& = default;

  AudioSession::~AudioSession() = default;

  auto AudioSession::create(std::shared_ptr<World::Scene> scene, const Core::FileSystem& files,
                            const Audio::MixerConfig& config, const Audio::SceneAudioLimits& limits)
      -> Core::Result<AudioSession> {
    if (!scene || scene->id() == 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Audio session requires a live scene");
    }

    auto backend = Audio::SceneAudio::create(*scene, files, config, limits);

    if (!backend) {
      return std::unexpected(backend.error());
    }

    try {
      return AudioSession(std::make_shared<Impl>(std::move(scene), std::move(*backend), false));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create audio session: " + std::string(error.what()));
    }
  }

  auto AudioSession::createPlayback(std::shared_ptr<World::Scene> scene,
                                    const Core::FileSystem& files,
                                    const Audio::PlaybackConfig& config,
                                    const Audio::SceneAudioLimits& limits,
                                    Audio::Playback* preparedPlayback)
      -> Core::Result<AudioSession> {
    if (!scene || scene->id() == 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Audio session requires a live scene");
    }

    auto backend
        = Audio::SceneAudio::createPlayback(*scene, files, config, limits, preparedPlayback);

    if (!backend) {
      return std::unexpected(backend.error());
    }

    try {
      return AudioSession(std::make_shared<Impl>(std::move(scene), std::move(*backend), true));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create native audio session: " + std::string(error.what()));
    }
  }

  auto AudioSession::synchronize(double seconds) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio session was moved");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_audio.synchronize(*m_impl->m_scene, seconds) : checked;
  }

  auto AudioSession::reset() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (m_impl->m_generation == std::numeric_limits<std::uint64_t>::max() - 1) {
      return Core::fail(Core::ErrorCode::Unavailable, "Audio generation exhausted");
    }

    auto reset = m_impl->m_audio.reset(*m_impl->m_scene);

    if (reset) {
      ++m_impl->m_generation;
      m_impl->m_volume = 1;
    }

    return reset;
  }

  auto AudioSession::beginPlayback() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio session was moved");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_audio.beginPlayback()
                   : Core::Result<void>{std::unexpected(checked.error())};
  }

  auto AudioSession::pollPlayback() -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio session was moved");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_audio.pollPlayback()
                   : Core::Result<bool>{std::unexpected(checked.error())};
  }

  auto AudioSession::requestPlaybackClose() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio session was moved");
    }

    auto checked = m_impl->check(false);
    return checked ? m_impl->m_audio.requestPlaybackClose()
                   : Core::Result<void>{std::unexpected(checked.error())};
  }

  auto AudioSession::closePlayback() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio session was moved");
    }

    auto checked = m_impl->check(false);
    return checked ? m_impl->m_audio.closePlayback() : checked;
  }

  auto AudioSession::pollPlaybackClosed() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio session was moved");
    }

    auto checked = m_impl->check(false);
    return checked ? m_impl->m_audio.pollPlaybackClosed()
                   : Core::Result<bool>{std::unexpected(checked.error())};
  }

  auto AudioSession::startPlayback() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio session was moved");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_audio.startPlayback() : checked;
  }

  auto AudioSession::render(std::span<float> samples) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio session was moved");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_audio.render(samples) : checked;
  }

  auto AudioSession::bindings() const -> Core::Result<HostBindings> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      HostBindings bindings;
      bindings.reserve(methods.size());

      for (const auto* method : methods) {
        const bool read = std::string_view(method) == "audio.voice"
                          || std::string_view(method) == "audio.stats"
                          || std::string_view(method) == "audio.listener";
        bindings.push_back({method,
                            [impl = m_impl, method](std::string_view args) -> Reply {
                              return impl->invoke(method, args);
                            },
                            read ? HostAccess::ReadOnly : HostAccess::ReadWrite});
      }

      return bindings;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Bind audio session: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Script

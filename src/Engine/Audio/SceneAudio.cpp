module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

module Dot.Audio.SceneAudio;

import Dot.Audio.Clip;
import Dot.Audio.Mixer;
import Dot.Audio.Playback;
import Dot.Audio.Source;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Math;
import Dot.World.Scene;

namespace Dot::Audio {
  namespace {
    using EntityId = Dot::World::EntityId;
    using Scene = Dot::World::Scene;
    using Backend = std::variant<Mixer, Playback>;

    auto validateLimits(const SceneAudioLimits& limits) -> Core::Result<void> {
      constexpr auto maxBytes = 1024ULL * 1024 * 1024;

      if (limits.sampleBytes == 0 || limits.sampleBytes > maxBytes || limits.maxAssets == 0
          || limits.maxAssets > 1024 || limits.clip.sourceBytes == 0
          || limits.clip.sourceBytes > maxBytes || limits.clip.sampleBytes == 0
          || limits.clip.sampleBytes > maxBytes) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid scene audio asset limits");
      }

      return {};
    }

    template <typename T>
    auto context(Core::Result<T> result, EntityId entity, const std::string& asset = "")
        -> Core::Result<T> {
      if (!result) {
        auto error = std::move(result.error());
        error.message = std::format("Scene audio entity {} asset '{}': {}", entity.value, asset,
                                    error.message);
        return std::unexpected(std::move(error));
      }

      return result;
    }

    auto pose(const Scene& scene, EntityId entity) -> Core::Result<Math::TransformParts> {
      auto matrix = scene.worldTransform(entity);

      if (!matrix) {
        return std::unexpected(matrix.error());
      }

      auto value = Math::decomposeTrs(*matrix);

      if (!value) {
        return std::unexpected(value.error());
      }

      if (value->scale.x <= 0 || value->scale.y <= 0 || value->scale.z <= 0) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Audio world pose requires positive scale");
      }

      return value;
    }

    auto velocity(Math::Vec3 current, Math::Vec3 previous, double seconds) -> Math::Vec3 {
      if (seconds == 0) {
        return {};
      }

      return {static_cast<float>((static_cast<double>(current.x) - previous.x) / seconds),
              static_cast<float>((static_cast<double>(current.y) - previous.y) / seconds),
              static_cast<float>((static_cast<double>(current.z) - previous.z) / seconds)};
    }

    struct Binding {
      VoiceId voice;
      SourceProperties properties;
      SpatialSettings spatial;
    };
  }  // namespace

  struct SceneAudio::Impl {
    Backend m_backend;
    Core::FileSystem m_files;
    MixerConfig m_mixerConfig;
    std::optional<PlaybackConfig> m_playbackConfig;
    SceneAudioLimits m_limits;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_scene{};
    std::map<std::string, Clip> m_assets;
    std::uint64_t m_sampleBytes{};
    std::map<std::uint64_t, Binding> m_bindings;
    std::optional<EntityId> m_listenerEntity;
    Listener m_listener{};

    Impl(Backend backend, Core::FileSystem files, MixerConfig config,
         std::optional<PlaybackConfig> playback, SceneAudioLimits limits)
        : m_backend(std::move(backend)),
          m_files(std::move(files)),
          m_mixerConfig(config),
          m_playbackConfig(playback),
          m_limits(limits) {
    }

    auto check() const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Scene audio requires its owner thread");
      }

      return {};
    }

    auto find(EntityId entity) const -> Core::Result<VoiceId> {
      auto checked = check();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      const auto found = m_bindings.find(entity.value);

      if (entity.scene != m_scene || found == m_bindings.end()) {
        return Core::fail(Core::ErrorCode::NotFound, "Scene audio entity is stale or foreign");
      }

      return found->second.voice;
    }

    auto synchronize(const Scene& scene, double seconds) -> Core::Result<void> {
      auto checked = check();

      if (!checked) {
        return checked;
      }

      if (!std::isfinite(seconds) || seconds < 0 || seconds > 1 || scene.id() != m_scene) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid scene audio scope/timestep");
      }

      auto entities = scene.entities();

      if (!entities) {
        return std::unexpected(entities.error());
      }

      try {
        std::map<std::string, Clip> assets;
        std::map<std::uint64_t, Binding> bindings;
        std::uint64_t sampleBytes = 0;
        VoiceBatch batch;
        std::vector<std::uint64_t> createdEntities;
        std::optional<EntityId> listenerEntity;
        Listener listener;

        for (const auto& entity : *entities) {
          if (entity.audioListener && entity.audioListener->primary) {
            listenerEntity = entity.id;
            auto world = context(pose(scene, entity.id), entity.id);

            if (!world) {
              return std::unexpected(world.error());
            }

            listener.position = world->position;
            listener.rotation = world->rotation;
            listener.velocity = m_listenerEntity == listenerEntity
                                    ? velocity(listener.position, m_listener.position, seconds)
                                    : Math::Vec3{};
            auto normalized = context(validatedListener(listener), entity.id);

            if (!normalized) {
              return std::unexpected(normalized.error());
            }

            listener = *normalized;
          }
        }

        for (const auto& entity : *entities) {
          if (!entity.audioSource) {
            continue;
          }

          if (bindings.size() >= m_mixerConfig.maxVoices) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Scene audio exceeds voice capacity");
          }

          const auto& properties = *entity.audioSource;
          auto spatial = SpatialSettings{};
          static_cast<SpatialProperties&>(spatial) = properties.spatial;
          const auto old = m_bindings.find(entity.id.value);
          const bool retain
              = old != m_bindings.end() && old->second.properties.asset == properties.asset;

          if (spatial.enabled) {
            if (!listenerEntity) {
              return Core::fail(Core::ErrorCode::Unavailable,
                                std::format("Spatial audio entity {} requires a primary listener",
                                            entity.id.value));
            }

            auto world = context(pose(scene, entity.id), entity.id, properties.asset);

            if (!world) {
              return std::unexpected(world.error());
            }

            spatial.position = world->position;
            spatial.rotation = world->rotation;
            spatial.velocity
                = retain && old->second.spatial.enabled
                      ? velocity(spatial.position, old->second.spatial.position, seconds)
                      : Math::Vec3{};
            auto normalized = context(validatedSpatial(spatial), entity.id, properties.asset);

            if (!normalized) {
              return std::unexpected(normalized.error());
            }

            spatial = *normalized;
          }

          auto found = assets.find(properties.asset);

          if (found == assets.end()) {
            if (assets.size() >= m_limits.maxAssets) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Scene audio exceeds asset capacity");
            }

            const auto cached = m_assets.find(properties.asset);
            auto limits = m_limits.clip;
            limits.sampleBytes = std::min(limits.sampleBytes, m_limits.sampleBytes - sampleBytes);

            if (limits.sampleBytes == 0) {
              return Core::fail(Core::ErrorCode::InvalidArgument, "Scene audio exceeds PCM budget");
            }

            const std::u8string utf8Path(properties.asset.begin(), properties.asset.end());
            const std::filesystem::path path{utf8Path};

            auto source = cached != m_assets.end() ? Core::Result<Clip>{cached->second}
                                                   : context(loadClip(m_files, path, limits),
                                                             entity.id, properties.asset);

            if (!source) {
              return std::unexpected(source.error());
            }

            auto samples = source->samples();

            if (!samples || samples->size_bytes() > m_limits.sampleBytes - sampleBytes) {
              return Core::fail(Core::ErrorCode::InvalidArgument, "Scene audio exceeds PCM budget");
            }

            sampleBytes += samples->size_bytes();
            found = assets.emplace(properties.asset, std::move(*source)).first;
          }

          if (spatial.enabled && found->second.info()->channels != 1) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              std::format("Spatial audio entity {} asset '{}' requires mono PCM",
                                          entity.id.value, properties.asset));
          }

          Binding binding{{}, properties, spatial};

          if (retain) {
            binding.voice = old->second.voice;
            VoiceUpdate update{binding.voice};

            if (properties.voice != old->second.properties.voice) {
              update.settings = properties.voice;
            }

            if (spatial != old->second.spatial) {
              update.spatial = spatial;
            }

            if (update.settings || update.spatial) {
              batch.update.push_back(update);
            }
          } else {
            batch.create.push_back(
                {found->second, properties.voice, spatial, properties.playOnStart});
            createdEntities.push_back(entity.id.value);
          }

          bindings.emplace(entity.id.value, std::move(binding));
        }

        for (const auto& [entity, binding] : m_bindings) {
          const auto next = bindings.find(entity);

          if (next == bindings.end() || next->second.voice != binding.voice) {
            batch.remove.push_back(binding.voice);
          }
        }

        if (listener != m_listener) {
          batch.listener = listener;
        }

        auto applied
            = std::visit([&](auto& backend) { return backend.applyChanges(batch); }, m_backend);

        if (!applied) {
          return std::unexpected(applied.error());
        }

        for (std::size_t index = 0; index < createdEntities.size(); ++index) {
          bindings.at(createdEntities[index]).voice = (*applied)[index];
        }

        m_bindings.swap(bindings);
        m_assets.swap(assets);
        m_sampleBytes = sampleBytes;
        m_listenerEntity = listenerEntity;
        m_listener = listener;
        return {};
      } catch (const std::exception& error) {
        return Core::fail(Core::ErrorCode::External,
                          std::format("Synchronize scene audio: {}", error.what()));
      }
    }
  };

  SceneAudio::SceneAudio(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  SceneAudio::SceneAudio(SceneAudio&&) noexcept = default;

  auto SceneAudio::operator=(SceneAudio&&) noexcept -> SceneAudio& = default;

  SceneAudio::~SceneAudio() = default;

  auto SceneAudio::create(const Scene& scene, const Core::FileSystem& files,
                          const MixerConfig& config, const SceneAudioLimits& limits)
      -> Core::Result<SceneAudio> {
    auto checked = validateLimits(limits);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      auto backend = Mixer::create(config);

      if (!backend) {
        return std::unexpected(backend.error());
      }

      auto impl = std::make_unique<Impl>(Backend{std::move(*backend)}, files, config, std::nullopt,
                                         limits);
      impl->m_scene = scene.id();
      auto initialized = impl->synchronize(scene, 0);

      if (!initialized) {
        return std::unexpected(initialized.error());
      }

      return SceneAudio{std::move(impl)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        std::format("Create scene audio: {}", error.what()));
    }
  }

  auto SceneAudio::createPlayback(const Scene& scene, const Core::FileSystem& files,
                                  const PlaybackConfig& config, const SceneAudioLimits& limits,
                                  Playback* preparedPlayback) -> Core::Result<SceneAudio> {
    auto checked = validateLimits(limits);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      if (preparedPlayback) {
        auto actual = preparedPlayback->configuration();
        auto ready = preparedPlayback->pollReady();
        auto started = preparedPlayback->pollStarted();

        if (!actual || !ready || !started) {
          return std::unexpected(!actual  ? actual.error()
                                 : !ready ? ready.error()
                                          : started.error());
        }

        if (*actual != config || !config.deferredStart || !*ready || *started) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Scene playback requires a ready matching deferred endpoint");
        }
      }

      auto backend = preparedPlayback ? Core::Result<Playback>{std::move(*preparedPlayback)}
                                      : Playback::create(config);

      if (!backend) {
        return std::unexpected(backend.error());
      }

      auto impl = std::make_unique<Impl>(Backend{std::move(*backend)}, files, config.mixer, config,
                                         limits);
      impl->m_scene = scene.id();
      auto initialized = impl->synchronize(scene, 0);

      if (!initialized) {
        return std::unexpected(initialized.error());
      }

      return SceneAudio{std::move(impl)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        std::format("Create scene playback: {}", error.what()));
    }
  }

  auto SceneAudio::beginPlayback() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene audio has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (auto* playback = std::get_if<Playback>(&m_impl->m_backend)) {
      return playback->beginStart();
    }

    return {};
  }

  auto SceneAudio::pollPlayback() -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene audio has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (auto* playback = std::get_if<Playback>(&m_impl->m_backend)) {
      auto ready = playback->pollStarted();

      if (ready && *ready) {
        m_impl->m_playbackConfig->deferredStart = false;
      }

      return ready;
    }

    return true;
  }

  auto SceneAudio::requestPlaybackClose() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene audio has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (auto* playback = std::get_if<Playback>(&m_impl->m_backend)) {
      return playback->requestClose();
    }

    return {};
  }

  auto SceneAudio::pollPlaybackClosed() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene audio has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (auto* playback = std::get_if<Playback>(&m_impl->m_backend)) {
      auto closed = playback->pollClosed();

      if (closed && *closed) {
        auto result = playback->close();

        if (!result) {
          return std::unexpected(result.error());
        }
      }

      return closed;
    }

    return true;
  }

  auto SceneAudio::synchronize(const Scene& scene, double seconds) -> Core::Result<void> {
    return m_impl ? m_impl->synchronize(scene, seconds)
                  : Core::Result<void>{
                        Core::fail(Core::ErrorCode::InvalidState, "Scene audio has been moved")};
  }

  auto SceneAudio::startPlayback() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene audio has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (auto* playback = std::get_if<Playback>(&m_impl->m_backend)) {
      auto started = playback->start();

      if (!started) {
        return started;
      }

      m_impl->m_playbackConfig->deferredStart = false;
    }

    return {};
  }

  auto SceneAudio::reset(const Scene& scene) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene audio has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    auto candidate
        = m_impl->m_playbackConfig
              ? createPlayback(scene, m_impl->m_files, *m_impl->m_playbackConfig, m_impl->m_limits)
              : create(scene, m_impl->m_files, m_impl->m_mixerConfig, m_impl->m_limits);

    if (!candidate) {
      return std::unexpected(candidate.error());
    }

    m_impl.swap(candidate->m_impl);
    return {};
  }

  auto SceneAudio::voice(EntityId entity) const -> Core::Result<VoiceState> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene audio has been moved");
    }

    auto id = m_impl->find(entity);

    if (!id) {
      return std::unexpected(id.error());
    }

    return std::visit([&](auto& backend) { return backend.voice(*id); }, m_impl->m_backend);
  }

  auto SceneAudio::play(EntityId entity) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene audio has been moved");
    }

    auto id = m_impl->find(entity);
    return id ? std::visit([&](auto& backend) { return backend.play(*id); }, m_impl->m_backend)
              : Core::Result<void>{std::unexpected(id.error())};
  }

  auto SceneAudio::pause(EntityId entity) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene audio has been moved");
    }

    auto id = m_impl->find(entity);
    return id ? std::visit([&](auto& backend) { return backend.pause(*id); }, m_impl->m_backend)
              : Core::Result<void>{std::unexpected(id.error())};
  }

  auto SceneAudio::stop(EntityId entity) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene audio has been moved");
    }

    auto id = m_impl->find(entity);
    return id ? std::visit([&](auto& backend) { return backend.stop(*id); }, m_impl->m_backend)
              : Core::Result<void>{std::unexpected(id.error())};
  }

  auto SceneAudio::seek(EntityId entity, std::uint64_t sourceFrame) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene audio has been moved");
    }

    auto id = m_impl->find(entity);
    return id ? std::visit([&](auto& backend) { return backend.seek(*id, sourceFrame); },
                           m_impl->m_backend)
              : Core::Result<void>{std::unexpected(id.error())};
  }

  auto SceneAudio::setVolume(float volume) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene audio has been moved");
    }

    auto checked = m_impl->check();
    return checked ? std::visit([&](auto& backend) { return backend.setVolume(volume); },
                                m_impl->m_backend)
                   : checked;
  }

  auto SceneAudio::listener() const -> Core::Result<Listener> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene audio has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return std::visit([](auto& backend) { return backend.listener(); }, m_impl->m_backend);
  }

  auto SceneAudio::stats() const -> Core::Result<SceneAudioStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene audio has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (auto* backend = std::get_if<Playback>(&m_impl->m_backend)) {
      auto value = backend->stats();
      return value ? Core::Result<SceneAudioStats>{{m_impl->m_assets.size(), m_impl->m_sampleBytes,
                                                    value->mixer, *value}}
                   : Core::Result<SceneAudioStats>{std::unexpected(value.error())};
    }

    auto value = std::get<Mixer>(m_impl->m_backend).stats();
    return value ? Core::Result<SceneAudioStats>{{m_impl->m_assets.size(), m_impl->m_sampleBytes,
                                                  *value, std::nullopt}}
                 : Core::Result<SceneAudioStats>{std::unexpected(value.error())};
  }

  auto SceneAudio::render(std::span<float> interleaved) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene audio has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    auto* backend = std::get_if<Mixer>(&m_impl->m_backend);
    return backend ? backend->render(interleaved)
                   : Core::Result<void>{Core::fail(Core::ErrorCode::InvalidState,
                                                   "Native scene audio cannot render offline")};
  }
}  // namespace Dot::Audio

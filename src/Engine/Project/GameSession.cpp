module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

module Dot.Project.GameSession;

import Dot.Physics.Mesh;
import Dot.Physics.MeshFiles;
import Dot.Navigation.Files;
import Dot.Script.NavigationSource;

import Dot.UI.Files;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Project;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Platform.Input;
import Dot.Platform.Events;
import Dot.Script.AudioSession;
import Dot.Script.AnimationSession;
import Dot.Assets.SceneSkins;
import Dot.Project.ScenePreparation;
import Dot.Script.Compilation;
import Dot.Script.SceneFlow;
import Dot.Script.GameData;

namespace Dot::Project {
  namespace {
    auto validate(const GameSessionConfig& config) -> Core::Result<void> {
      if (config.audioMode != SessionAudio::Offline && config.audioMode != SessionAudio::Playback) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid game session audio mode");
      }

      if (config.pollInterval.count() < 0 || config.pollInterval.count() > 60000) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Game poll interval must be 0..60000 ms");
      }

      return {};
    }

    auto append(Script::ScriptReport& target, Script::ScriptReport source) -> void {
      target.failures.insert(target.failures.end(),
                             std::make_move_iterator(source.failures.begin()),
                             std::make_move_iterator(source.failures.end()));
      target.created += source.created;
      target.removed += source.removed;
      target.calls += source.calls;
      target.realmFaulted = target.realmFaulted || source.realmFaulted;
    }

    struct ActiveScope {
      bool& active;

      explicit ActiveScope(bool& value) : active(value) {
        active = true;
      }

      ~ActiveScope() {
        active = false;
      }
    };
  }  // namespace

  struct GameSession::Origin {
    std::string path;
    std::string bytes;
  };

  struct GameSession::Impl {
    struct Initial {
      std::optional<std::string> bytes;
      std::optional<ScenePreparation> resources;
      std::optional<PreparedScene> proof;
      std::optional<Script::CompilationJob> compilation;
      std::optional<Script::CompiledProgram> program;
      std::optional<Audio::Playback> playback;
      bool obsolete{};
    };

    std::optional<Initial> m_initial;
    std::optional<SessionPreparation> m_initialPhase;
    std::optional<Core::Error> m_initialError;
    std::uint64_t m_initialCompiles{};
    std::uint64_t m_discardedInitial{};
    std::shared_ptr<World::Scene> m_scene;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_identity{};
    Description m_description;
    SessionState m_state{SessionState::Ready};
    SessionAudio m_audioMode{SessionAudio::Offline};
    bool m_active{};
    std::optional<Core::FileSystem> m_files;
    GameSessionConfig m_config;
    std::optional<Script::TypeScriptCompiler> m_compiler;
    std::optional<Script::CompiledProgram> m_program;
    std::optional<Script::SceneFlow> m_flow;
    std::optional<Script::GameData> m_progress;
    std::optional<Script::Hud> m_hud;
    std::optional<Script::GameClock> m_clock;
    std::uint64_t m_fixedSteps{};
    std::uint64_t m_updates{};
    std::uint64_t m_lifetimeFixedSteps{};
    std::shared_ptr<const Origin> m_origin;
    std::optional<Script::InputSource> m_input;
    std::optional<Script::PhysicsSource> m_physics;
    std::optional<Script::NavigationSource> m_navigation;
    std::optional<Script::AudioSession> m_audio;
    std::optional<Script::AnimationSession> m_animations;
    /** Script callbacks retain services; destroy the realm before its dependencies.
     * 脚本回调持有服务，先销毁 realm 再销毁依赖。 */
    std::optional<Script::SceneScripts> m_scripts;
    std::optional<Script::LiveScripts> m_live;
    std::optional<ScenePreparation> m_preparation;
    std::optional<Script::SceneRequest> m_preparingRequest;
    /** Only the retired audio keeps its old scene; no old realm/physics/animation.
     * Gate the next preparation until reaped, bounding native endpoints to two.
     * 仅退役音频保留旧场景，不保留旧 realm/物理/动画；回收前不开始下次准备，端点至多两个。 */
    std::optional<Script::AudioSession> m_retiredAudio;

    ~Impl() {
      if (m_initial) {
        if (m_initial->resources) {
          m_initial->resources->requestStop();
        }

        if (m_initial->compilation) {
          m_initial->compilation->requestStop();
        }
      }
    }

    auto cancelInitial() -> Core::Result<void> {
      auto& initial = *m_initial;
      m_state = SessionState::Cancelling;
      m_initialPhase = SessionPreparation::Teardown;

      if (initial.resources) {
        initial.resources->requestStop();
      }

      if (initial.compilation) {
        initial.compilation->requestStop();
      }

      return initial.playback ? initial.playback->requestClose() : Core::Result<void>{};
    }

    auto retireServices() noexcept -> void {
      if (m_preparation) {
        m_preparation->requestStop();
      }

      if (m_live) {
        m_live->cancelCompilation();
      }

      if (m_flow) {
        m_flow->retire();
      }

      if (m_progress) {
        static_cast<void>(m_progress->retire());
      }

      if (m_hud) {
        m_hud->retire();
      }

      if (m_clock) {
        m_clock->retire();
      }
    }

    auto check(bool allowTerminal = false) const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id() || m_active) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Game session requires owner thread without reentry");
      }

      if (!allowTerminal
          && (m_state == SessionState::Closed || m_state == SessionState::Faulted
              || m_state == SessionState::Cancelling)) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Game session is closed, faulted or cancelling");
      }

      if (!allowTerminal && !m_scene) {
        return Core::fail(Core::ErrorCode::InvalidState, "Game scene services are not prepared");
      }

      if (!allowTerminal && m_scene->id() != m_identity) {
        return Core::fail(Core::ErrorCode::InvalidState, "Game session scene was moved/replaced");
      }

      return {};
    }

    auto scriptStats() const -> Core::Result<Script::SceneScriptsStats> {
      if (m_live) {
        return m_live->stats().transform([](const auto& stats) { return stats.scripts; });
      }

      return m_scripts->stats();
    }

    auto pollRetiredAudio() -> Core::Result<bool> {
      if (!m_retiredAudio) {
        return true;
      }

      auto closed = m_retiredAudio->pollPlaybackClosed();

      if (!closed || *closed) {
        m_retiredAudio.reset();
      }

      return closed;
    }

    auto shutdown(std::optional<Script::AudioSession>* retirement = nullptr)
        -> Core::Result<Script::ScriptReport> {
      auto result = m_state == SessionState::Cancelling
                        ? Core::Result<Script::ScriptReport>{Script::ScriptReport{}}
                    : m_live ? m_live->stop()
                             : m_scripts->stop();
      m_preparation.reset();
      m_preparingRequest.reset();
      m_live.reset();
      m_scripts.reset();
      m_animations.reset();

      if (m_hud) {
        m_hud->retire();
      }

      if (m_clock) {
        m_clock->retire();
      }

      if (m_audio) {
        auto closed = retirement && m_audioMode == SessionAudio::Playback
                          ? m_audio->requestPlaybackClose()
                          : m_audio->closePlayback();

        if (closed && retirement && m_audioMode == SessionAudio::Playback) {
          retirement->emplace(std::move(*m_audio));
        } else if (!closed && result) {
          result = std::unexpected(closed.error());
        }
      }

      m_audio.reset();

      if (m_retiredAudio) {
        auto closed = m_retiredAudio->closePlayback();

        if (!closed && result) {
          result = std::unexpected(closed.error());
        }

        m_retiredAudio.reset();
      }

      m_navigation.reset();
      m_physics.reset();
      m_input.reset();
      m_program.reset();
      m_origin.reset();
      m_compiler.reset();
      m_files.reset();
      m_config.scripts.services.clear();
      m_state = SessionState::Closed;
      return result;
    }

    auto frame(double seconds, bool fixed) -> Core::Result<Script::LiveScriptReport> {
      if (m_state != SessionState::Running) {
        return Core::fail(Core::ErrorCode::InvalidState, "Start game session before dispatch");
      }

      ActiveScope active{m_active};

      try {
        Core::Result<Script::LiveScriptReport> result{Script::LiveScriptReport{}};

        if (!fixed) {
          result = m_live ? m_live->frame(seconds)
                          : m_scripts->frame(seconds).transform([](auto report) {
                              return Script::LiveScriptReport{{}, std::move(report)};
                            });
        }

        if (!result || result->frame.realmFaulted) {
          m_state = SessionState::Faulted;
          retireServices();
          return result;
        }

        auto clock = m_clock->snapshot();

        if (!clock) {
          return std::unexpected(clock.error());
        }

        const auto paused = clock->paused;
        const bool pendingStep
            = !fixed && clock->step && clock->step->state == Script::GameStepState::Pending;
        bool stepping{};
        auto simulationSeconds = seconds;

        const bool fixedCounterFull
            = (fixed || pendingStep)
              && (m_fixedSteps == std::numeric_limits<std::uint64_t>::max()
                  || m_lifetimeFixedSteps == std::numeric_limits<std::uint64_t>::max());
        const bool updateCounterFull
            = !fixed && m_updates == std::numeric_limits<std::uint64_t>::max();

        if ((!paused || pendingStep) && (fixedCounterFull || updateCounterFull)) {
          return Core::fail(Core::ErrorCode::Unavailable, "Game simulation counter exhausted");
        }

        if (pendingStep) {
          auto begun = m_clock->beginStep();

          if (!begun) {
            return std::unexpected(begun.error());
          }

          stepping = *begun;
          simulationSeconds = 1.0 / m_description.fixedHz;
          auto fixedReport = m_live ? m_live->dispatchPrepared(Script::ScriptPhase::FixedUpdate,
                                                               simulationSeconds)
                                    : m_scripts->fixedUpdate(simulationSeconds);

          if (!fixedReport) {
            m_state = SessionState::Faulted;
            retireServices();
            return std::unexpected(fixedReport.error());
          }

          append(result->frame, std::move(*fixedReport));

          if (result->frame.realmFaulted) {
            m_state = SessionState::Faulted;
            retireServices();
            return result;
          }

          auto advanced = m_physics->advance(simulationSeconds);

          if (!advanced) {
            m_state = SessionState::Faulted;
            retireServices();
            return std::unexpected(advanced.error());
          }

          ++m_fixedSteps;
          ++m_lifetimeFixedSteps;
        }

        if (!paused || stepping) {
          if (fixed) {
            result = m_live ? m_live->fixedUpdate(seconds)
                            : m_scripts->fixedUpdate(seconds).transform([](auto report) {
                                return Script::LiveScriptReport{{}, std::move(report)};
                              });
          } else {
            auto game
                = m_live ? m_live->dispatchPrepared(Script::ScriptPhase::Update, simulationSeconds)
                         : m_scripts->update(simulationSeconds);

            if (!game) {
              m_state = SessionState::Faulted;
              retireServices();
              return std::unexpected(game.error());
            }

            append(result->frame, std::move(*game));
          }
        }

        if (!result || result->frame.realmFaulted) {
          m_state = SessionState::Faulted;
          retireServices();
          return result;
        }

        if (fixed && !paused) {
          auto advanced = m_physics->advance(seconds);

          if (!advanced) {
            m_state = SessionState::Faulted;
            retireServices();
            return std::unexpected(advanced.error());
          }
        }

        if (!fixed) {
          auto animated = m_animations->advance(paused && !stepping ? 0 : simulationSeconds);

          if (!animated) {
            m_state = SessionState::Faulted;
            retireServices();
            return std::unexpected(animated.error());
          }

          auto synchronized = m_audio->synchronize(seconds);

          if (!synchronized) {
            m_state = SessionState::Faulted;
            retireServices();
            return std::unexpected(synchronized.error());
          }
        }

        if (!paused || stepping) {
          ++(fixed ? m_fixedSteps : m_updates);

          if (fixed) {
            ++m_lifetimeFixedSteps;
          }
        }

        if (stepping) {
          auto completed = m_clock->finishStep(true);

          if (!completed) {
            m_state = SessionState::Faulted;
            retireServices();
            return std::unexpected(completed.error());
          }
        }

        return result;
      } catch (const std::exception& error) {
        m_state = SessionState::Faulted;
        retireServices();
        return Core::fail(Core::ErrorCode::External,
                          "Dispatch game session: " + std::string(error.what()));
      }
    }
  };

  GameSession::GameSession(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  GameSession::GameSession(GameSession&&) noexcept = default;

  auto GameSession::operator=(GameSession&& other) noexcept -> GameSession& {
    if (this != &other) {
      if (m_impl && m_impl->m_flow) {
        m_impl->retireServices();
      }

      m_impl = std::move(other.m_impl);
    }

    return *this;
  }

  GameSession::~GameSession() {
    if (m_impl && m_impl->m_flow) {
      m_impl->retireServices();
    }
  }

  auto GameSession::begin(const Project& project, const Script::TypeScriptCompiler& compiler,
                          const GameSessionConfig& config) -> Core::Result<GameSession> {
    auto valid = validate(config);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto description = project.description();
    auto files = project.files();
    auto scene = project.loadStartup();

    if (!description || !files || !scene) {
      return std::unexpected(!description ? description.error()
                             : !files     ? files.error()
                                          : scene.error());
    }

    auto bytes = World::encodeScene(*scene);
    return bytes ? beginPreparation(*files, std::move(*description), std::move(*bytes), config,
                                    &compiler, nullptr)
                 : std::unexpected(bytes.error());
  }

  auto GameSession::beginCompiled(const Core::FileSystem& assets, const Description& description,
                                  const World::Scene& scene, const Script::CompiledProgram& program,
                                  const GameSessionConfig& config) -> Core::Result<GameSession> {
    auto valid = validate(config);
    auto metadata = validateDescription(description);
    auto modules = program.modules();

    if (!valid || !metadata || !modules) {
      return std::unexpected(!valid      ? valid.error()
                             : !metadata ? metadata.error()
                                         : modules.error());
    }

    if (description.hotReload) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Compiled sessions require hotReload=false");
    }

    auto bytes = World::encodeScene(scene);
    return bytes
               ? beginPreparation(assets, description, std::move(*bytes), config, nullptr, &program)
               : std::unexpected(bytes.error());
  }

  auto GameSession::beginPreparation(const Core::FileSystem& assets, Description description,
                                     std::string sceneBytes, const GameSessionConfig& config,
                                     const Script::TypeScriptCompiler* compiler,
                                     const Script::CompiledProgram* program)
      -> Core::Result<GameSession> {
    try {
      auto impl = std::make_unique<Impl>();
      impl->m_description = std::move(description);
      impl->m_files = assets;
      impl->m_config = config;
      impl->m_audioMode = config.audioMode;
      impl->m_state = SessionState::Preparing;
      impl->m_initialPhase = SessionPreparation::Resources;
      impl->m_initial.emplace();
      impl->m_initial->bytes = std::move(sceneBytes);

      if (compiler) {
        impl->m_compiler = *compiler;
      }

      if (program) {
        impl->m_initial->program = *program;
      }

      return GameSession{std::move(impl)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Begin game preparation: " + std::string(error.what()));
    }
  }

  auto GameSession::pollPreparation() -> Core::Result<SessionState> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!m_impl->m_initial) {
      return m_impl->m_state;
    }

    /** Keep the old flag alive until its reentry guard releases after publication.
     * 发布替换后，须保留旧标记的所有者直到重入守卫退出。 */
    std::unique_ptr<Impl> retired;
    ActiveScope active{m_impl->m_active};
    auto& initial = *m_impl->m_initial;
    const auto failed = [&](Core::Error error) -> Core::Result<SessionState> {
      if (!m_impl->m_initialError) {
        m_impl->m_initialError = std::move(error);
      }

      auto closed = m_impl->cancelInitial();

      if (!closed) {
        m_impl->m_initialError->message += "; teardown: " + closed.error().message;
      }

      return std::unexpected(*m_impl->m_initialError);
    };

    try {
      if (m_impl->m_state == SessionState::Cancelling) {
        if (initial.resources) {
          auto ready = initial.resources->ready();

          if (!ready) {
            return failed(ready.error());
          }

          if (!*ready) {
            return SessionState::Cancelling;
          }

          initial.resources.reset();
        }

        if (initial.compilation) {
          auto ready = initial.compilation->ready();

          if (!ready) {
            return failed(ready.error());
          }

          if (!*ready) {
            return SessionState::Cancelling;
          }

          initial.compilation.reset();
        }

        if (initial.playback) {
          auto closed = initial.playback->pollClosed();

          if (closed && !*closed) {
            return SessionState::Cancelling;
          }

          auto result = closed ? initial.playback->close()
                               : Core::Result<void>{std::unexpected(closed.error())};

          if (!result && !m_impl->m_initialError) {
            m_impl->m_initialError = result.error();
          }
        }

        m_impl->m_initial.reset();
        m_impl->m_initialPhase.reset();
        m_impl->m_config.scripts.services.clear();
        m_impl->m_compiler.reset();
        m_impl->m_files.reset();
        m_impl->m_state = m_impl->m_initialError ? SessionState::Faulted : SessionState::Closed;
        return m_impl->m_state;
      }

      if (!initial.playback && m_impl->m_audioMode == SessionAudio::Playback) {
        auto config = m_impl->m_config.audio;
        config.deferredStart = true;
        auto endpoint = Audio::Playback::begin(config);

        if (!endpoint) {
          return failed(endpoint.error());
        }

        initial.playback.emplace(std::move(*endpoint));
      }

      if (!initial.resources && !initial.proof) {
        auto job = ScenePreparation::begin(*m_impl->m_files, m_impl->m_description.startScene,
                                           std::move(initial.bytes), ScenePreparationKind::Complete,
                                           m_impl->m_config.audioLimits);

        if (!job) {
          return failed(job.error());
        }

        initial.resources.emplace(std::move(*job));
      }

      if (initial.resources) {
        auto ready = initial.resources->ready();

        if (!ready) {
          return failed(ready.error());
        }

        if (*ready) {
          auto proof = initial.resources->take();
          initial.resources.reset();

          if (!proof) {
            return failed(proof.error());
          }

          initial.proof.emplace(std::move(*proof));
        }
      }

      if (m_impl->m_compiler) {
        auto captured = m_impl->m_compiler->captureProject(*m_impl->m_files,
                                                           m_impl->m_description.scriptsDirectory);

        if (!captured) {
          return failed(captured.error());
        }

        if (initial.compilation) {
          auto source = initial.compilation->source();
          auto ready = initial.compilation->ready();

          if (!source || !ready) {
            return failed(!source ? source.error() : ready.error());
          }

          if (!source->equals(*captured)) {
            initial.obsolete = true;
            initial.compilation->requestStop();
          }

          if (*ready) {
            auto program = initial.compilation->take();
            initial.compilation.reset();

            if (initial.obsolete) {
              ++m_impl->m_discardedInitial;
            } else if (!program) {
              return failed(program.error());
            } else {
              initial.program.emplace(std::move(*program));
            }
          }
        }

        if (initial.program) {
          auto source = initial.program->source();

          if (!source) {
            return failed(source.error());
          }

          if (!source->equals(*captured)) {
            initial.program.reset();
            ++m_impl->m_discardedInitial;
          }
        }

        if (!initial.compilation && !initial.program) {
          auto job = Script::CompilationJob::begin(*m_impl->m_compiler, *captured);

          if (!job) {
            return failed(job.error());
          }

          initial.obsolete = false;
          initial.compilation.emplace(std::move(*job));
          ++m_impl->m_initialCompiles;
        }
      }

      if (!initial.program) {
        m_impl->m_initialPhase = SessionPreparation::Scripts;
        return SessionState::Preparing;
      }

      if (!initial.proof) {
        m_impl->m_initialPhase = SessionPreparation::Resources;
        return SessionState::Preparing;
      }

      if (initial.playback) {
        auto ready = initial.playback->pollReady();

        if (!ready) {
          return failed(ready.error());
        }

        if (!*ready) {
          m_impl->m_initialPhase = SessionPreparation::Audio;
          return SessionState::Preparing;
        }
      }

      m_impl->m_initialPhase = SessionPreparation::Services;
      auto scene = initial.proof->scene();

      if (!scene) {
        return failed(scene.error());
      }

      auto prepared = prepareSession(
          *m_impl->m_files, m_impl->m_description, std::move(*scene), m_impl->m_config,
          m_impl->m_compiler ? &*m_impl->m_compiler : nullptr, &*initial.program,
          m_impl->m_description.startScene, nullptr, {}, nullptr,
          initial.playback ? &*initial.playback : nullptr, &*initial.proof);

      if (initial.playback && !initial.playback->configuration()) {
        initial.playback.reset();
      }

      if (!prepared) {
        return failed(prepared.error());
      }

      prepared->m_impl->m_initialCompiles = m_impl->m_initialCompiles;
      prepared->m_impl->m_discardedInitial = m_impl->m_discardedInitial;
      m_impl.swap(prepared->m_impl);
      retired = std::move(prepared->m_impl);
      return m_impl->m_state;
    } catch (const std::exception& error) {
      return failed(
          {Core::ErrorCode::External, "Poll game preparation: " + std::string(error.what()), {}});
    }
  }

  auto GameSession::create(const Project& project, const Script::TypeScriptCompiler& compiler,
                           const GameSessionConfig& config) -> Core::Result<GameSession> {
    auto valid = validate(config);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto loaded = project.loadStartup();
    auto description = project.description();

    if (!description) {
      return std::unexpected(description.error());
    }

    return loaded ? prepare(project, compiler, std::move(*loaded), config, description->startScene)
                  : std::unexpected(loaded.error());
  }

  auto GameSession::createFromScene(const Project& project,
                                    const Script::TypeScriptCompiler& compiler,
                                    const World::Scene& scene, const GameSessionConfig& config,
                                    std::optional<std::string> scenePath,
                                    Audio::Playback* preparedPlayback,
                                    const Script::CompiledProgram* preparedProgram,
                                    const PreparedScene* preflight) -> Core::Result<GameSession> {
    auto valid = validate(config);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    if (preparedPlayback && config.audioMode != SessionAudio::Playback) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Prepared playback requires native audio mode");
    }

    auto encoded = World::encodeScene(scene);

    if (!encoded) {
      return std::unexpected(encoded.error());
    }

    auto cloned = World::decodeScene(*encoded);
    return cloned ? prepare(project, compiler, std::move(*cloned), config, std::move(scenePath),
                            std::move(*encoded), preparedPlayback, preparedProgram, preflight)
                  : std::unexpected(cloned.error());
  }

  auto GameSession::prepare(const Project& project, const Script::TypeScriptCompiler& compiler,
                            World::Scene scene, const GameSessionConfig& config,
                            std::optional<std::string> scenePath,
                            std::optional<std::string> originBytes,
                            Audio::Playback* preparedPlayback,
                            const Script::CompiledProgram* preparedProgram,
                            const PreparedScene* preflight) -> Core::Result<GameSession> {
    try {
      auto description = project.description();
      auto files = project.files();

      if (!description) {
        return std::unexpected(description.error());
      }

      if (!files) {
        return std::unexpected(files.error());
      }

      if (preparedProgram) {
        auto source = preparedProgram->source();
        auto current = compiler.captureProject(*files, description->scriptsDirectory);

        if (!source || !current) {
          return std::unexpected(!source ? source.error() : current.error());
        }

        if (!source->equals(*current)) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Prepared scripts no longer match project source");
        }
      }

      std::shared_ptr<const Origin> origin;

      if (scenePath && originBytes) {
        origin = std::make_shared<const Origin>(Origin{*scenePath, std::move(*originBytes)});
      }

      return prepareSession(*files, std::move(*description), std::move(scene), config, &compiler,
                            preparedProgram, std::move(scenePath), nullptr, std::move(origin),
                            nullptr, preparedPlayback, preflight);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare project session: " + std::string(error.what()));
    }
  }

  auto GameSession::createCompiled(const Core::FileSystem& assets, const Description& description,
                                   const World::Scene& scene,
                                   const Script::CompiledProgram& program,
                                   const GameSessionConfig& config) -> Core::Result<GameSession> {
    auto checked = validate(config);
    auto metadata = validateDescription(description);
    auto modules = program.modules();

    if (!checked || !metadata || !modules) {
      return std::unexpected(!checked    ? checked.error()
                             : !metadata ? metadata.error()
                                         : modules.error());
    }

    if (description.hotReload) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Compiled sessions require hotReload=false");
    }

    auto encoded = World::encodeScene(scene);

    if (!encoded) {
      return std::unexpected(encoded.error());
    }

    auto cloned = World::decodeScene(*encoded);
    return cloned ? prepareSession(assets, description, std::move(*cloned), config, nullptr,
                                   &program, description.startScene)
                  : std::unexpected(cloned.error());
  }

  auto GameSession::prepareSession(
      const Core::FileSystem& assets, Description description, World::Scene scene,
      const GameSessionConfig& config, const Script::TypeScriptCompiler* compiler,
      const Script::CompiledProgram* precompiled, std::optional<std::string> scenePath,
      const Script::SceneFlow* sharedFlow, std::shared_ptr<const Origin> origin,
      const Script::GameData* sharedProgress, Audio::Playback* preparedPlayback,
      const PreparedScene* preflight) -> Core::Result<GameSession> {
    try {
      if (sharedFlow && !preflight) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Scene replacement requires completed preflight");
      }

      if (preflight) {
        auto accepted = preflight->accepts(assets, scene);

        if (!accepted || !*accepted) {
          return accepted ? Core::fail(Core::ErrorCode::InvalidArgument,
                                       "Scene preflight does not match candidate/root")
                          : std::unexpected(accepted.error());
        }
      }

      auto skins = preflight ? Core::Result<void>{} : Assets::validateSceneSkins(assets, scene);

      if (!skins) {
        return std::unexpected(skins.error());
      }

      auto impl = std::make_unique<Impl>();
      impl->m_description = std::move(description);
      impl->m_audioMode = config.audioMode;
      impl->m_scene = std::make_shared<World::Scene>(std::move(scene));
      impl->m_identity = impl->m_scene->id();
      impl->m_files = assets;
      impl->m_config = config;
      impl->m_origin = std::move(origin);

      if (compiler) {
        impl->m_compiler = *compiler;
      }

      /** Live generations belong to the watcher, not the fixed-program cache
       * used across compiled scene transitions; source transitions compile current files.
       * 活动源码代归监听器所有，不放入跨预编译场景使用的固定程序缓存；源码场景切换编译当前文件。 */
      if (precompiled && !impl->m_description.hotReload) {
        impl->m_program = *precompiled;
      }

      auto progress = sharedProgress ? Core::Result<Script::GameData>{*sharedProgress}
                                     : Script::GameData::create(config.progress);

      if (!progress) {
        return std::unexpected(progress.error());
      }

      impl->m_progress.emplace(std::move(*progress));
      auto flow = sharedFlow ? Core::Result<Script::SceneFlow>{*sharedFlow}
                             : Script::SceneFlow::create(impl->m_identity, scenePath);

      if (!flow) {
        return std::unexpected(flow.error());
      }

      impl->m_flow.emplace(std::move(*flow));
      auto ui = [&]() -> Core::Result<std::optional<UI::Asset>> {
        if (preflight) {
          return preflight->ui();
        }

        auto binding = impl->m_scene->ui();

        if (!binding) {
          return std::unexpected(binding.error());
        }

        if (!*binding) {
          return std::optional<UI::Asset>{};
        }

        auto loaded = UI::loadLayout(
            assets, std::filesystem::path{std::u8string((**binding).begin(), (**binding).end())});
        return loaded ? Core::Result<std::optional<UI::Asset>>{UI::Asset{**binding,
                                                                         std::move(*loaded)}}
                      : std::unexpected(loaded.error());
      }();

      if (!ui) {
        return std::unexpected(ui.error());
      }

      auto hud = Script::Hud::create(impl->m_identity, std::move(*ui));

      if (!hud) {
        return std::unexpected(hud.error());
      }

      impl->m_hud.emplace(std::move(*hud));
      auto clock = Script::GameClock::create();

      if (!clock) {
        return std::unexpected(clock.error());
      }

      impl->m_clock.emplace(std::move(*clock));
      auto input = Script::InputSource::create(config.input);

      if (!input) {
        return std::unexpected(input.error());
      }

      impl->m_input.emplace(std::move(*input));
      auto published = impl->m_input->publish(Platform::InputState{});

      if (!published) {
        return std::unexpected(published.error());
      }

      std::optional<std::vector<Physics::MeshAsset>> collisionMeshes;

      if (preflight) {
        auto prepared = preflight->collisionMeshes();

        if (!prepared) {
          return std::unexpected(prepared.error());
        }

        collisionMeshes.emplace(prepared->begin(), prepared->end());
      }

      auto physics
          = Script::PhysicsSource::create(impl->m_scene, config.physics, assets, collisionMeshes);

      if (!physics) {
        return std::unexpected(physics.error());
      }

      impl->m_physics.emplace(std::move(*physics));
      auto navigation = [&]() -> Core::Result<Script::NavigationSource> {
        auto surface = [&]() -> Core::Result<std::optional<Navigation::Surface>> {
          if (preflight) {
            return preflight->navigation();
          }

          auto binding = impl->m_scene->navigation();

          if (!binding) {
            return std::unexpected(binding.error());
          }

          if (!*binding) {
            return std::optional<Navigation::Surface>{};
          }

          auto loaded = Navigation::loadSurface(assets, **binding);
          return loaded ? Core::Result<std::optional<Navigation::Surface>>{std::move(*loaded)}
                        : std::unexpected(loaded.error());
        }();

        if (!surface) {
          return std::unexpected(surface.error());
        }

        std::vector<Physics::MeshAsset> meshes;

        if (*surface) {
          if (collisionMeshes) {
            meshes = *collisionMeshes;
          } else {
            auto loaded = Physics::loadSceneCollisionMeshes(assets, *impl->m_scene);

            if (!loaded) {
              return std::unexpected(loaded.error());
            }

            meshes = std::move(*loaded);
          }
        }

        return Script::NavigationSource::create(impl->m_scene, *impl->m_physics,
                                                std::move(*surface), std::move(meshes));
      }();

      if (!navigation) {
        return std::unexpected(navigation.error());
      }

      impl->m_navigation.emplace(std::move(*navigation));
      std::optional<std::span<const Audio::ClipAsset>> audioClips;

      if (preflight) {
        auto decoded = preflight->audioClips(config.audioLimits);

        if (!decoded) {
          return std::unexpected(decoded.error());
        }

        audioClips = *decoded;
      }

      auto playbackConfig = config.audio;
      playbackConfig.deferredStart = true;
      auto audio = config.audioMode == SessionAudio::Playback
                       ? Script::AudioSession::createPlayback(impl->m_scene, assets, playbackConfig,
                                                              config.audioLimits, preparedPlayback,
                                                              audioClips)
                       : Script::AudioSession::create(impl->m_scene, assets, config.audio.mixer,
                                                      config.audioLimits, audioClips);

      if (!audio) {
        return std::unexpected(audio.error());
      }

      impl->m_audio.emplace(std::move(*audio));
      auto animations = [&]() -> Core::Result<Script::AnimationSession> {
        if (!preflight) {
          return Script::AnimationSession::create(impl->m_scene, assets);
        }

        auto clips = preflight->animationClips();
        auto controllers = preflight->animationControllers();

        if (!clips || !controllers) {
          return std::unexpected(!clips ? clips.error() : controllers.error());
        }

        return Script::AnimationSession::createPrepared(impl->m_scene, assets, *clips,
                                                        *controllers);
      }();

      if (!animations) {
        return std::unexpected(animations.error());
      }

      impl->m_animations.emplace(std::move(*animations));
      auto scriptsConfig = config.scripts;

      for (auto bindings :
           {impl->m_input->bindings(), impl->m_physics->bindings(), impl->m_navigation->bindings(),
            impl->m_audio->bindings(), impl->m_animations->bindings(), impl->m_progress->bindings(),
            impl->m_hud->bindings(), impl->m_clock->bindings(),
            impl->m_flow->bindings(impl->m_identity, scenePath)}) {
        if (!bindings) {
          return std::unexpected(bindings.error());
        }

        scriptsConfig.services.insert(scriptsConfig.services.end(),
                                      std::make_move_iterator(bindings->begin()),
                                      std::make_move_iterator(bindings->end()));
      }

      if (impl->m_description.hotReload) {
        Script::LiveScriptsConfig liveConfig{
            std::move(scriptsConfig), impl->m_description.scriptsDirectory, config.pollInterval};
        auto scripts = precompiled ? Script::LiveScripts::createPrepared(
                                         impl->m_scene, assets, *compiler, *precompiled, liveConfig)
                                   : Script::LiveScripts::create(impl->m_scene, assets, *compiler,
                                                                 liveConfig);

        if (!scripts) {
          return std::unexpected(scripts.error());
        }

        impl->m_live.emplace(std::move(*scripts));
      } else {
        auto program = precompiled
                           ? Core::Result<Script::CompiledProgram>{*precompiled}
                           : compiler->compileProject(assets, impl->m_description.scriptsDirectory);

        if (!program) {
          return std::unexpected(program.error());
        }

        impl->m_program = *program;
        auto scripts = Script::SceneScripts::create(impl->m_scene, *program, scriptsConfig);

        if (!scripts) {
          return std::unexpected(scripts.error());
        }

        auto prepared = scripts->sync();

        if (!prepared) {
          return std::unexpected(prepared.error());
        }

        if (prepared->realmFaulted) {
          return Core::fail(Core::ErrorCode::InvalidState, "Prepare script realm faulted");
        }

        if (!prepared->failures.empty()) {
          return std::unexpected(prepared->failures.front().error);
        }

        impl->m_scripts.emplace(std::move(*scripts));
      }

      return GameSession(std::move(impl));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create game session: " + std::string(error.what()));
    }
  }

  auto GameSession::scene() const -> Core::Result<std::shared_ptr<World::Scene>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!m_impl->m_scene) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game scene services are not prepared");
    }

    if (m_impl->m_scene->id() != m_impl->m_identity) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session scene was moved/replaced");
    }

    return m_impl->m_scene;
  }

  auto GameSession::stats() const -> Core::Result<GameSessionStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    GameSessionStats result{m_impl->m_state,
                            m_impl->m_description.fixedHz,
                            m_impl->m_description.hotReload,
                            {},
                            {},
                            {}};
    result.preparation = m_impl->m_initialPhase;
    result.preparationError = m_impl->m_initialError;
    result.preparationCompileAttempts = m_impl->m_initialCompiles;
    result.discardedScriptCompilations = m_impl->m_discardedInitial;

    if (!m_impl->m_scene) {
      result.location.path = m_impl->m_description.startScene;
      return result;
    }

    auto location = m_impl->m_flow->location();
    auto latest = m_impl->m_flow->latest();

    if (!location || !latest) {
      return std::unexpected(!location ? location.error() : latest.error());
    }

    result.location = std::move(*location);
    result.transition = std::move(*latest);
    result.retiringAudio = m_impl->m_retiredAudio.has_value();
    auto clock = m_impl->m_clock->snapshot();

    if (!clock) {
      return std::unexpected(clock.error());
    }

    result.clock = *clock;
    result.simulatedFixedSteps = m_impl->m_fixedSteps;
    result.simulatedUpdates = m_impl->m_updates;
    result.lifetimeFixedSteps = m_impl->m_lifetimeFixedSteps;

    if (m_impl->m_state != SessionState::Closed) {
      if (m_impl->m_live) {
        auto source = m_impl->m_live->stats();

        if (!source) {
          return std::unexpected(source.error());
        }

        result.scripts = source->scripts;
        result.source = std::move(*source);
      } else {
        auto scripts = m_impl->scriptStats();

        if (!scripts) {
          return std::unexpected(scripts.error());
        }

        result.scripts = *scripts;
      }
    }

    return result;
  }

  auto GameSession::pollScripts(bool force) -> Core::Result<Script::ReloadStatus> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_state != SessionState::Running || !m_impl->m_live) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Source polling requires a running hot-reload session");
    }

    ActiveScope active{m_impl->m_active};
    auto result = m_impl->m_live->poll(force);
    auto state = m_impl->m_live->stats();

    if (!result || !state || state->scripts.faulted) {
      m_impl->m_state = SessionState::Faulted;
      m_impl->retireServices();
    }

    if (!state) {
      return std::unexpected(state.error());
    }

    return result;
  }

  auto GameSession::animationState(World::EntityId owner) const
      -> Core::Result<Animation::ScenePlayerState> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_state != SessionState::Running || !m_impl->m_animations) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Animation inspection requires a running session");
    }

    return m_impl->m_animations->state(owner);
  }

  auto GameSession::animationParameters(World::EntityId owner,
                                        std::span<const Animation::ControllerParameter> parameters)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_state != SessionState::Running || !m_impl->m_animations) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Animation control requires a running session");
    }

    return m_impl->m_animations->parameters(owner, parameters);
  }

  auto GameSession::resetAnimationController(World::EntityId owner) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_state != SessionState::Running || !m_impl->m_animations) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Animation control requires a running session");
    }

    return m_impl->m_animations->resetController(owner);
  }

  auto GameSession::controlAnimationLayer(World::EntityId owner, std::string_view name,
                                          const Animation::LayerControl& operation)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_state != SessionState::Running || !m_impl->m_animations) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Animation control requires a running session");
    }

    return m_impl->m_animations->controlLayer(owner, name, operation);
  }

  auto GameSession::inspectAnimation(World::EntityId owner) const -> Core::Result<std::string> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_state != SessionState::Running || !m_impl->m_animations) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Animation inspection requires a running session");
    }

    return m_impl->m_animations->inspect(owner);
  }

  auto GameSession::gameData() const -> Core::Result<Script::GameData> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check(true);
    return checked && m_impl->m_progress ? Core::Result<Script::GameData>{*m_impl->m_progress}
           : checked ? Core::fail(Core::ErrorCode::InvalidState, "Game data is not prepared")
                     : std::unexpected(checked.error());
  }

  auto GameSession::gameClock() const -> Core::Result<Script::GameClock> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check(true);
    return checked && m_impl->m_clock ? Core::Result<Script::GameClock>{*m_impl->m_clock}
           : checked ? Core::fail(Core::ErrorCode::InvalidState, "Game clock is not prepared")
                     : std::unexpected(checked.error());
  }

  auto GameSession::hud() const -> Core::Result<Script::Hud> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check(true);
    return checked && m_impl->m_hud ? Core::Result<Script::Hud>{*m_impl->m_hud}
           : checked ? Core::fail(Core::ErrorCode::InvalidState, "HUD is not prepared")
                     : std::unexpected(checked.error());
  }

  auto GameSession::requestScene(std::string_view path) -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_state != SessionState::Running) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene requests require a running session");
    }

    return m_impl->m_flow->request(path, m_impl->m_identity);
  }

  auto GameSession::appendScene(std::string_view path) -> Core::Result<std::uint64_t> {
    auto checked = stats();

    if (!checked || checked->state != SessionState::Running) {
      return checked ? Core::fail(Core::ErrorCode::InvalidState,
                                  "Scene append requires running gameplay")
                     : std::unexpected(checked.error());
    }

    return m_impl->m_flow->append(path, m_impl->m_identity);
  }

  auto GameSession::removeScene(World::EntityId root) -> Core::Result<std::uint64_t> {
    auto checked = stats();

    if (!checked || checked->state != SessionState::Running) {
      return checked ? Core::fail(Core::ErrorCode::InvalidState,
                                  "Scene removal requires running gameplay")
                     : std::unexpected(checked.error());
    }

    return m_impl->m_flow->remove(root);
  }

  auto GameSession::sceneInstances() const -> Core::Result<std::vector<Script::SceneInstance>> {
    auto checked = stats();
    return checked
               ? m_impl->m_flow->instances()
               : Core::Result<std::vector<Script::SceneInstance>>{std::unexpected(checked.error())};
  }

  auto GameSession::processContentRequest(const Script::SceneRequest& request,
                                          const PreparedScene* preflight,
                                          Script::ScriptReport& report) -> Core::Result<void> {
    bool published{};
    const auto failed = [&](Core::Error error) -> Core::Result<void> {
      error.message = published
                          ? "Scene content published; synchronization failed: " + error.message
                          : "Scene content preparation failed; gameplay retained: " + error.message;
      auto recorded = m_impl->m_flow->fail(request.id, error);

      if (published || !recorded) {
        m_impl->m_state = SessionState::Faulted;
        m_impl->retireServices();
        return recorded ? Core::Result<void>{std::unexpected(std::move(error))} : recorded;
      }

      return {};
    };

    try {
      ActiveScope active{m_impl->m_active};
      std::optional<World::SceneAttachment> attachment;
      std::span<const Animation::ClipAsset> clips;
      std::span<const Animation::ControllerAsset> controllers;
      std::span<const Physics::MeshAsset> meshes;
      std::span<const Audio::ClipAsset> audio;
      bool prepared = request.kind == Script::SceneRequestKind::Append;

      if (request.kind == Script::SceneRequestKind::Append) {
        auto decoded = preflight->animationClips();
        auto graphs = preflight->animationControllers();
        auto geometry = preflight->collisionMeshes();

        auto pcm = preflight->audioClips(m_impl->m_config.audioLimits);

        if (!pcm) {
          return failed(pcm.error());
        }

        if (!decoded || !graphs || !geometry) {
          return failed(!decoded ? decoded.error() : !graphs ? graphs.error() : geometry.error());
        }

        clips = *decoded;
        controllers = *graphs;
        meshes = *geometry;
        audio = *pcm;
        auto content = preflight->scene();

        if (!content) {
          return failed(content.error());
        }

        auto added = m_impl->m_scene->appendContent(*content);

        if (!added) {
          return failed(added.error());
        }

        attachment.emplace(std::move(*added));
      } else {
        auto members = m_impl->m_flow->members(*request.root);

        if (!members) {
          return failed(members.error());
        }

        auto removed = m_impl->m_scene->removeContent(*members);

        if (!removed) {
          return failed(removed.error());
        }
      }

      published = true;
      auto accepted = m_impl->m_flow->commitContent(request.id, std::move(attachment));

      if (!accepted) {
        return failed(accepted.error());
      }

      const auto synchronize = [&]() -> Core::Result<void> {
        auto physics = prepared ? m_impl->m_physics->synchronizePrepared(meshes)
                                : m_impl->m_physics->synchronize();

        if (!physics) {
          return std::unexpected(physics.error());
        }

        auto animation = prepared ? m_impl->m_animations->advancePrepared(0, clips, controllers)
                                  : m_impl->m_animations->advance(0);

        if (!animation) {
          return std::unexpected(animation.error());
        }

        return prepared ? m_impl->m_audio->synchronizePrepared(audio)
                        : m_impl->m_audio->synchronize();
      };
      auto ready = synchronize();
      prepared = false;

      if (!ready) {
        return failed(ready.error());
      }

      auto started = m_impl->m_live ? m_impl->m_live->start() : m_impl->m_scripts->start();

      if (!started) {
        return failed(started.error());
      }

      const auto faulted = started->realmFaulted || !started->failures.empty();
      std::optional<Core::Error> error;

      if (!started->failures.empty()) {
        error = started->failures.front().error;
      }

      append(report, std::move(*started));

      if (faulted) {
        return failed(error.value_or(
            Core::Error{Core::ErrorCode::InvalidState, "Content script realm faulted", {}}));
      }

      auto synchronized = synchronize();
      return synchronized ? Core::Result<void>{} : failed(synchronized.error());
    } catch (const std::exception& error) {
      return failed(
          {Core::ErrorCode::External, "Publish scene content: " + std::string(error.what()), {}});
    }
  }

  auto GameSession::sceneRequest(std::uint64_t id) const -> Core::Result<Script::SceneRequest> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check(true);
    return checked && m_impl->m_flow ? m_impl->m_flow->status(id)
           : checked ? Core::fail(Core::ErrorCode::InvalidState, "Scene requests are not prepared")
                     : std::unexpected(checked.error());
  }

  auto GameSession::processSceneRequest(Script::ScriptReport& report, bool beginPending)
      -> Core::Result<void> {
    auto retired = m_impl->pollRetiredAudio();

    if (!retired) {
      auto error = retired.error();
      error.message = "Retired scene audio teardown: " + error.message;
      m_impl->m_state = SessionState::Faulted;
      m_impl->retireServices();
      return std::unexpected(std::move(error));
    }

    if (!*retired || (!beginPending && !m_impl->m_preparation)) {
      return {};
    }

    if (!m_impl->m_preparation) {
      auto pending = m_impl->m_flow->pending();

      if (!pending) {
        return std::unexpected(pending.error());
      }

      if (!*pending) {
        return {};
      }

      auto request = std::move(**pending);
      auto begun = m_impl->m_flow->begin(request.id);

      if (!begun) {
        return begun;
      }

      if (request.kind == Script::SceneRequestKind::Remove) {
        return processContentRequest(request, nullptr, report);
      }

      try {
        auto initial = request.kind == Script::SceneRequestKind::Replace && m_impl->m_origin
                               && m_impl->m_origin->path == request.path
                           ? std::optional<std::string>{m_impl->m_origin->bytes}
                           : std::nullopt;
        auto started = ScenePreparation::begin(*m_impl->m_files, request.path, std::move(initial),
                                               request.kind == Script::SceneRequestKind::Append
                                                   ? ScenePreparationKind::Content
                                                   : ScenePreparationKind::Complete,
                                               m_impl->m_config.audioLimits);

        if (!started) {
          return m_impl->m_flow->fail(request.id, started.error());
        }

        m_impl->m_preparation.emplace(std::move(*started));
        m_impl->m_preparingRequest.emplace(std::move(request));
        return {};
      } catch (const std::exception& error) {
        m_impl->m_preparation.reset();
        m_impl->m_preparingRequest.reset();
        return m_impl->m_flow->fail(
            request.id, {Core::ErrorCode::External,
                         "Start scene preflight; old scene retained: " + std::string(error.what()),
                         {}});
      }
    }

    auto ready = m_impl->m_preparation->ready();

    if (!ready || !*ready) {
      return ready ? Core::Result<void>{} : std::unexpected(ready.error());
    }

    auto request = std::move(*m_impl->m_preparingRequest);
    auto preflight = m_impl->m_preparation->take();
    m_impl->m_preparation.reset();
    m_impl->m_preparingRequest.reset();

    if (request.kind == Script::SceneRequestKind::Append) {
      return preflight ? processContentRequest(request, &*preflight, report)
                       : m_impl->m_flow->fail(request.id, preflight.error());
    }

    bool committed = false;
    bool published = false;
    std::unique_ptr<Impl> replacement;
    const auto failed = [&](Core::Error error) -> Core::Result<void> {
      if (committed && !published) {
        m_impl.swap(replacement);
        published = true;
      }

      if (committed) {
        replacement.reset();
        m_impl->m_state = SessionState::Faulted;
        error.message = "Scene transition committed; target startup failed: " + error.message;
      } else {
        error.message = "Scene transition preparation failed; old scene retained: " + error.message;
      }

      auto recorded = m_impl->m_flow->fail(request.id, error);

      if (!recorded) {
        m_impl->m_state = SessionState::Faulted;
        m_impl->retireServices();
        return recorded;
      }

      if (committed) {
        m_impl->retireServices();
        return std::unexpected(std::move(error));
      }

      return {};
    };

    try {
      {
        ActiveScope active{m_impl->m_active};

        if (!preflight) {
          return failed(preflight.error());
        }

        auto loaded = preflight->scene();

        if (!loaded) {
          return failed(loaded.error());
        }

        auto prepared = prepareSession(
            *m_impl->m_files, m_impl->m_description, std::move(*loaded), m_impl->m_config,
            m_impl->m_compiler ? &*m_impl->m_compiler : nullptr,
            m_impl->m_program ? &*m_impl->m_program : nullptr, request.path, &*m_impl->m_flow,
            m_impl->m_origin, &*m_impl->m_progress, nullptr, &*preflight);

        if (!prepared) {
          return failed(prepared.error());
        }

        replacement = std::move(prepared->m_impl);
      }

      /** Stop hooks cannot be rolled back. Prepare all services/path ownership
       * before acceptance; retire old writers and allow the next Start to queue.
       * Stop 钩子不可回滚；先准备全部服务及路径所有权，再接受替换，使旧写入失效并允许
       * 新 Start 排队下一请求。 */
      replacement->m_lifetimeFixedSteps = m_impl->m_lifetimeFixedSteps;
      replacement->m_initialCompiles = m_impl->m_initialCompiles;
      replacement->m_discardedInitial = m_impl->m_discardedInitial;
      auto accepted
          = m_impl->m_flow->commit(request.id, replacement->m_identity, std::move(request.path));

      if (!accepted) {
        return failed(accepted.error());
      }

      committed = true;
      Core::Result<Script::ScriptReport> stopped;

      {
        ActiveScope active{m_impl->m_active};
        stopped = m_impl->shutdown(&replacement->m_retiredAudio);
      }

      m_impl.swap(replacement);
      published = true;
      replacement.reset();

      if (!stopped) {
        return failed(stopped.error());
      }

      append(report, std::move(*stopped));

      if (report.realmFaulted) {
        if (!report.failures.empty()) {
          auto error = report.failures.back().error;
          error.message = "Retired scene Stop: " + error.message;
          return failed(std::move(error));
        }

        return failed(Core::Error{Core::ErrorCode::InvalidState,
                                  "Retired script realm faulted during Stop",
                                  std::source_location::current()});
      }

      auto started = start();

      if (!started) {
        return failed(started.error());
      }

      append(report, std::move(*started));

      if (report.realmFaulted) {
        if (!report.failures.empty()) {
          auto error = report.failures.back().error;
          error.message = "Target scene Start: " + error.message;
          return failed(std::move(error));
        }

        return failed(Core::Error{Core::ErrorCode::InvalidState,
                                  "Target script realm faulted during Start",
                                  std::source_location::current()});
      }

      return {};
    } catch (const std::exception& error) {
      return failed(Core::Error{Core::ErrorCode::External,
                                "Replace game scene: " + std::string(error.what()),
                                std::source_location::current()});
    }
  }

  auto GameSession::publish(const Platform::InputState& input, bool pointerCaptured,
                            std::optional<GameInputArea> area, bool textInputActive)
      -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_state == SessionState::Starting) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game audio activation is pending");
    }

    if (area
        && (!std::isfinite(area->size.x) || !std::isfinite(area->size.y) || area->size.x < 0
            || area->size.y < 0 || area->size.x > 1000000 || area->size.y > 1000000
            || (area->enabled && (area->size.x == 0 || area->size.y == 0)))) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid logical game input area");
    }

    auto textRequested = m_impl->m_input->textInputRequested();

    if (!textRequested) {
      return std::unexpected(textRequested.error());
    }

    auto published = m_impl->m_input->publish(input, pointerCaptured, textInputActive);

    if (!published) {
      return std::unexpected(published.error());
    }

    Script::HudInput ui;
    ui.enabled = !pointerCaptured && (!area || area->enabled);

    if (area && ui.enabled) {
      const auto point = input.pointer();
      const Math::Vec2 normalized{point.x / area->size.x, point.y / area->size.y};

      if (normalized.x >= 0 && normalized.x < 1 && normalized.y >= 0 && normalized.y < 1) {
        ui.pointer = normalized;
      }
    }

    const auto button = input.button(Platform::MouseButton::Left);
    ui.down = button.down;
    ui.pressed = button.pressed;
    ui.released = button.released;
    const auto reverse
        = input.key(Platform::Key::LeftShift).down || input.key(Platform::Key::RightShift).down;
    ui.next = !*textRequested && input.key(Platform::Key::Tab).pressed && !reverse;
    ui.previous = !*textRequested && input.key(Platform::Key::Tab).pressed && reverse;
    ui.confirm = !*textRequested
                 && (input.key(Platform::Key::Enter).pressed
                     || input.key(Platform::Key::KeypadEnter).pressed
                     || input.key(Platform::Key::Space).pressed);
    auto handled = m_impl->m_hud->publish(ui);

    if (!handled) {
      m_impl->m_state = SessionState::Faulted;
      m_impl->retireServices();
      return Core::fail(handled.error().code,
                        "Input frame committed; HUD dispatch failed: " + handled.error().message);
    }

    return published;
  }

  auto GameSession::textInputRequested() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_input->textInputRequested()
                   : Core::Result<bool>{std::unexpected(checked.error())};
  }

  auto GameSession::pointerCaptureRequested() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_input->pointerCaptureRequested()
                   : Core::Result<bool>{std::unexpected(checked.error())};
  }

  auto GameSession::start() -> Core::Result<Script::ScriptReport> {
    auto begun = beginStart();

    if (!begun || begun->realmFaulted) {
      return begun;
    }

    auto activated = m_impl->m_audio->startPlayback();

    if (!activated) {
      m_impl->m_state = SessionState::Faulted;
      m_impl->retireServices();
      return std::unexpected(activated.error());
    }

    auto ready = pollStart();
    return ready ? std::move(begun)
                 : Core::Result<Script::ScriptReport>{std::unexpected(ready.error())};
  }

  auto GameSession::beginStart() -> Core::Result<Script::ScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_state == SessionState::Starting) {
      return Script::ScriptReport{};
    }

    ActiveScope active{m_impl->m_active};
    const bool starting = m_impl->m_state == SessionState::Ready;
    auto result = m_impl->m_live ? m_impl->m_live->start() : m_impl->m_scripts->start();
    m_impl->m_state
        = !result || result->realmFaulted ? SessionState::Faulted : SessionState::Running;

    if (m_impl->m_state == SessionState::Faulted) {
      m_impl->retireServices();
    }

    if (m_impl->m_state == SessionState::Running) {
      auto animated = m_impl->m_animations->advance(0);

      if (!animated) {
        m_impl->m_state = SessionState::Faulted;
        m_impl->retireServices();
        return std::unexpected(animated.error());
      }

      if (starting) {
        /** Start can place listeners/sources; establish their first motion sample.
         * Start 可定位听者/声源；零时长同步建立首次运动采样，不将初始化当作瞬间移动。 */
        auto synchronized = m_impl->m_audio->synchronize(0);

        if (!synchronized) {
          m_impl->m_state = SessionState::Faulted;
          m_impl->retireServices();
          return std::unexpected(synchronized.error());
        }

        auto activated = m_impl->m_audio->beginPlayback();

        if (!activated) {
          m_impl->m_state = SessionState::Faulted;
          m_impl->retireServices();
          return std::unexpected(activated.error());
        }

        m_impl->m_state = SessionState::Starting;
      }
    }

    return result;
  }

  auto GameSession::pollStart() -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_state == SessionState::Running) {
      return true;
    }

    if (m_impl->m_state != SessionState::Starting) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session is not starting");
    }

    auto ready = m_impl->m_audio->pollPlayback();

    if (!ready) {
      m_impl->m_state = SessionState::Faulted;
      m_impl->retireServices();
    } else if (*ready) {
      m_impl->m_state = SessionState::Running;
    }

    return ready;
  }

  auto GameSession::requestPreparationClose() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    if (m_impl->m_initial) {
      auto checked = m_impl->check(true);

      if (!checked) {
        return checked;
      }

      return m_impl->cancelInitial();
    }

    if (!m_impl->m_audio) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game audio is closed");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return checked;
    }

    if (m_impl->m_state == SessionState::Running) {
      return Core::fail(Core::ErrorCode::InvalidState, "Cannot cancel a running game preparation");
    }

    m_impl->m_state = SessionState::Cancelling;
    m_impl->retireServices();
    return m_impl->m_audio->requestPlaybackClose();
  }

  auto GameSession::pollPreparationClosed() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    if (m_impl->m_initial || !m_impl->m_scene) {
      auto checked = m_impl->check(true);
      return checked ? Core::Result<bool>{!m_impl->m_initial} : std::unexpected(checked.error());
    }

    if (!m_impl->m_audio) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game audio is closed");
    }

    auto checked = m_impl->check(true);
    return checked ? m_impl->m_audio->pollPlaybackClosed()
                   : Core::Result<bool>{std::unexpected(checked.error())};
  }

  auto GameSession::fixedUpdate() -> Core::Result<Script::LiveScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->frame(1.0 / m_impl->m_description.fixedHz, true);
  }

  auto GameSession::update(double seconds) -> Core::Result<Script::LiveScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Game frame seconds must be finite [0,1]");
    }

    auto result = m_impl->frame(seconds, false);

    if (!result || result->frame.realmFaulted) {
      if (m_impl->m_state == SessionState::Faulted) {
        m_impl->retireServices();
      }

      return result;
    }

    Script::ScriptReport transition;
    auto transitioned = processSceneRequest(transition, true);

    if (!transitioned) {
      return std::unexpected(transitioned.error());
    }

    append(result->frame, std::move(transition));
    return result;
  }

  auto GameSession::pollScene() -> Core::Result<Script::ScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_state != SessionState::Running) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene polling requires running session");
    }

    Script::ScriptReport report;

    auto processed = processSceneRequest(report, false);
    return processed ? Core::Result<Script::ScriptReport>{std::move(report)}
                     : std::unexpected(processed.error());
  }

  auto GameSession::renderAudio(std::span<float> samples) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (m_impl->m_audioMode == SessionAudio::Playback) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Project playback cannot render offline PCM");
    }

    auto result = m_impl->m_audio->render(samples);

    if (!result && result.error().code != Core::ErrorCode::InvalidArgument) {
      m_impl->m_state = SessionState::Faulted;
      m_impl->retireServices();
    }

    return result;
  }

  auto GameSession::close() -> Core::Result<Script::ScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_state == SessionState::Closed) {
      return Script::ScriptReport{};
    }

    if (m_impl->m_initial) {
      auto requested = requestPreparationClose();
      ActiveScope active{m_impl->m_active};
      auto& initial = *m_impl->m_initial;
      initial.resources.reset();
      initial.compilation.reset();
      auto closed = initial.playback ? initial.playback->close() : Core::Result<void>{};
      m_impl->m_initial.reset();
      m_impl->m_initialPhase.reset();
      m_impl->m_config.scripts.services.clear();
      m_impl->m_compiler.reset();
      m_impl->m_files.reset();
      m_impl->m_state = SessionState::Closed;

      if (!requested || !closed) {
        return std::unexpected(!requested ? requested.error() : closed.error());
      }

      return Script::ScriptReport{};
    }

    if (!m_impl->m_scene) {
      ActiveScope active{m_impl->m_active};
      m_impl->m_config.scripts.services.clear();
      m_impl->m_compiler.reset();
      m_impl->m_files.reset();
      m_impl->m_state = SessionState::Closed;
      return Script::ScriptReport{};
    }

    ActiveScope active{m_impl->m_active};
    m_impl->m_flow->retire();
    auto stopped = m_impl->shutdown();
    auto drained = m_impl->m_progress->retire();

    if (!drained) {
      return Core::fail(drained.error().code,
                        (stopped ? std::string{} : stopped.error().message + "; ")
                            + "Save shutdown: " + drained.error().message);
    }

    return stopped;
  }
}  // namespace Dot::Project

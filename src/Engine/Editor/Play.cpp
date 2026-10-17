module;

#include <algorithm>
#include <chrono>
#include <exception>
#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <source_location>
#include <string>
#include <thread>
#include <utility>

module Dot.Editor.Play;

import Dot.Core.Clock;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Core.Files;
import Dot.Project.GameSession;
import Dot.Script.SceneScripts;
import Dot.Script.Compilation;
import Dot.World.SceneFiles;

namespace Dot::Editor {
  namespace {
    /** Preserve the script cause before failed startup retires its reports.
     * 在启动失败并清理报告前保留脚本原因，避免只返回笼统的脚本域错误。 */
    auto realmFaultError(std::string context, const Script::ScriptReport& report) -> Core::Error {
      if (!report.failures.empty()) {
        const auto& failure = report.failures.front();
        context
            += "; entity " + std::to_string(failure.entity.value) + ": " + failure.error.message;
      }

      return {Core::ErrorCode::InvalidState, std::move(context), std::source_location::current()};
    }

    struct ActiveScope {
      bool& active;

      explicit ActiveScope(bool& flag) noexcept : active(flag) {
        active = true;
      }

      ~ActiveScope() {
        active = false;
      }
    };
  }  // namespace

  struct PlaySession::Impl {
    std::thread::id m_thread{std::this_thread::get_id()};
    std::optional<Core::StagedDirectory> m_saveStorage;
    std::optional<Project::GameSession> m_session;

    struct Preparation {
      Project::Project project;
      Script::TypeScriptCompiler compiler;
      World::Scene scene;
      PlayConfig config;
      std::optional<std::string> scenePath;
      std::optional<Audio::Playback> playback;
      std::optional<Script::CompilationJob> compilation{};
      std::optional<Script::CompiledProgram> program{};
      bool obsolete{};
    };

    std::unique_ptr<Preparation> m_preparation;
    Core::FrameStepper m_stepper;
    std::size_t m_maxUpdates;
    PlayStats m_stats;
    PlayFrame m_frame;
    bool m_active{};

    Impl(Project::GameSession session, std::uint32_t hz, std::size_t maxUpdates)
        : m_session(std::move(session)),
          m_stepper(std::chrono::nanoseconds{1'000'000'000 / hz}, maxUpdates),
          m_maxUpdates(maxUpdates),
          m_stats{PlayState::Running, hz, 0, 0, {}, {}, {}} {
    }

    Impl(std::uint32_t hz, std::size_t maxUpdates)
        : m_stepper(std::chrono::nanoseconds{1'000'000'000 / hz}, maxUpdates),
          m_maxUpdates(maxUpdates),
          m_stats{PlayState::Preparing, hz, 0, 0, {}, {}, {}} {
      m_stats.started = false;
      m_stats.preparation = PlayPreparation::Scene;
    }

    auto check() const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id() || m_active) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Play session requires owner thread without reentry");
      }

      return {};
    }

    auto fault() -> void {
      m_stats.state = PlayState::Faulted;
      auto data = m_session->gameData();

      if (data) {
        data->retire();
      }

      auto hud = m_session->hud();

      if (hud) {
        hud->retire();
      }

      auto clock = m_session->gameClock();

      if (clock) {
        clock->retire();
      }
    }

    auto record(Core::Result<Script::LiveScriptReport> result) -> Core::Result<void> {
      if (!result) {
        fault();
        return std::unexpected(result.error());
      }

      const bool faulted = result->frame.realmFaulted;
      m_frame.reports.push_back(std::move(*result));

      if (faulted) {
        fault();
        return std::unexpected(
            realmFaultError("Play script realm faulted", m_frame.reports.back().frame));
      }

      return {};
    }
  };

  PlayConfig::PlayConfig() noexcept {
    session.audioMode = Project::SessionAudio::Playback;
  }

  PlaySession::PlaySession(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  PlaySession::PlaySession(PlaySession&&) noexcept = default;

  auto PlaySession::operator=(PlaySession&&) noexcept -> PlaySession& = default;

  PlaySession::~PlaySession() = default;

  auto PlaySession::create(const Project::Project& project,
                           const Script::TypeScriptCompiler& compiler,
                           const World::Scene& authoring, const PlayConfig& config,
                           std::optional<std::string> scenePath) -> Core::Result<PlaySession> {
    if (config.maxUpdates == 0 || config.maxUpdates > 64) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Play update budget must be 1..64");
    }

    if (config.session.progress.saves) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Play owns an isolated temporary save directory");
    }

    try {
      auto description = project.description();

      if (!description) {
        return std::unexpected(description.error());
      }

      auto parent = config.temporarySavesParent
                        ? Core::Result<Core::FileSystem>{*config.temporarySavesParent}
                        : Core::FileSystem::open(std::filesystem::temp_directory_path());

      if (!parent) {
        return std::unexpected(parent.error());
      }

      auto storage = Core::StagedDirectory::create(*parent);

      if (!storage) {
        return std::unexpected(storage.error());
      }

      auto sessionConfig = config.session;
      sessionConfig.progress.saves = storage->files().value();
      auto session = Project::GameSession::createFromScene(project, compiler, authoring,
                                                           sessionConfig, std::move(scenePath));

      if (!session) {
        return std::unexpected(session.error());
      }

      auto impl
          = std::make_unique<Impl>(std::move(*session), description->fixedHz, config.maxUpdates);
      impl->m_saveStorage.emplace(std::move(*storage));
      impl->m_frame.reports.reserve(config.maxUpdates + 1);
      auto started = impl->m_session->start();

      if (!started || started->realmFaulted) {
        auto error = started ? realmFaultError("Start play script realm faulted", *started)
                             : started.error();
        auto closed = impl->m_session->close();

        if (!closed) {
          error.message += "; cleanup: " + closed.error().message;
        }

        return std::unexpected(std::move(error));
      }

      impl->m_frame.reports.push_back({{}, std::move(*started)});
      return PlaySession(std::move(impl));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create play session: " + std::string(error.what()));
    }
  }

  auto PlaySession::begin(const Project::Project& project,
                          const Script::TypeScriptCompiler& compiler, const World::Scene& authoring,
                          const PlayConfig& config, std::optional<std::string> scenePath)
      -> Core::Result<PlaySession> {
    if (config.maxUpdates == 0 || config.maxUpdates > 64 || config.session.progress.saves) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Play requires 1..64 updates and isolated temporary saves");
    }

    try {
      auto description = project.description();
      auto encoded = World::encodeScene(authoring);

      if (!description || !encoded) {
        return std::unexpected(!description ? description.error() : encoded.error());
      }

      auto cloned = World::decodeScene(*encoded);
      auto parent = config.temporarySavesParent
                        ? Core::Result<Core::FileSystem>{*config.temporarySavesParent}
                        : Core::FileSystem::open(std::filesystem::temp_directory_path());

      if (!cloned || !parent) {
        return std::unexpected(!cloned ? cloned.error() : parent.error());
      }

      auto storage = Core::StagedDirectory::create(*parent);

      if (!storage) {
        return std::unexpected(storage.error());
      }

      auto impl = std::make_unique<Impl>(description->fixedHz, config.maxUpdates);
      impl->m_saveStorage.emplace(std::move(*storage));
      auto ownedConfig = config;
      ownedConfig.session.progress.saves = impl->m_saveStorage->files().value();
      impl->m_preparation = std::make_unique<Impl::Preparation>(
          project, compiler, std::move(*cloned), std::move(ownedConfig), std::move(scenePath),
          std::nullopt);
      impl->m_frame.reports.reserve(config.maxUpdates + 1);

      if (config.session.audioMode == Project::SessionAudio::Playback) {
        impl->m_stats.preparation = PlayPreparation::Audio;
        auto properties = config.session.audio;
        properties.deferredStart = true;
        auto playback = Audio::Playback::begin(properties);

        if (!playback) {
          return std::unexpected(playback.error());
        }

        impl->m_preparation->playback.emplace(std::move(*playback));
      }

      return PlaySession{std::move(impl)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Begin play preparation: " + std::string(error.what()));
    }
  }

  auto PlaySession::pollPreparation() -> Core::Result<PlayState> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    ActiveScope active{m_impl->m_active};
    auto& state = m_impl->m_stats.state;

    if (state != PlayState::Preparing && state != PlayState::Cancelling) {
      return state;
    }

    auto& pending = *m_impl->m_preparation;
    const auto fail = [&](Core::Error error) -> Core::Result<PlayState> {
      if (!m_impl->m_stats.preparationError) {
        m_impl->m_stats.preparationError = error;
      } else if (m_impl->m_stats.preparationError->message.find(error.message)
                 == std::string::npos) {
        m_impl->m_stats.preparationError->message += "; teardown: " + error.message;
      }

      state = PlayState::Cancelling;
      m_impl->m_stats.preparation = PlayPreparation::Teardown;

      if (pending.compilation) {
        pending.compilation->requestStop();
      }

      auto closing = m_impl->m_session  ? m_impl->m_session->requestPreparationClose()
                     : pending.playback ? pending.playback->requestClose()
                                        : Core::Result<void>{};

      if (!closing) {
        m_impl->m_stats.preparationError->message += "; teardown: " + closing.error().message;
      }

      return std::unexpected(*m_impl->m_stats.preparationError);
    };

    try {
      if (state == PlayState::Cancelling) {
        if (pending.compilation) {
          auto ready = pending.compilation->ready();

          if (!ready) {
            return fail(ready.error());
          }

          if (!*ready) {
            return state;
          }

          pending.compilation.reset();
        }

        pending.program.reset();
        auto closed = m_impl->m_session  ? m_impl->m_session->pollPreparationClosed()
                      : pending.playback ? pending.playback->pollClosed()
                                         : Core::Result<bool>{true};

        if (closed && !*closed) {
          return state;
        }

        if (closed && pending.playback && !m_impl->m_session) {
          auto result = pending.playback->close();

          if (!result) {
            closed = std::unexpected(result.error());
          }
        }

        if (!closed) {
          if (!m_impl->m_stats.preparationError) {
            m_impl->m_stats.preparationError = closed.error();
          } else if (m_impl->m_stats.preparationError->message.find(closed.error().message)
                     == std::string::npos) {
            m_impl->m_stats.preparationError->message += "; teardown: " + closed.error().message;
          }
        }

        m_impl->m_session.reset();
        pending.playback.reset();

        if (m_impl->m_saveStorage) {
          auto cleaned = m_impl->m_saveStorage->discard();

          if (!cleaned) {
            return fail(cleaned.error());
          }

          m_impl->m_saveStorage.reset();
        }

        m_impl->m_stats.preparation.reset();
        state = m_impl->m_stats.preparationError ? PlayState::Faulted : PlayState::Stopped;
        return state;
      }

      if (!m_impl->m_session) {
        auto description = pending.project.description();
        auto files = pending.project.files();

        if (!description || !files) {
          return fail(!description ? description.error() : files.error());
        }

        auto captured = pending.compiler.captureProject(*files, description->scriptsDirectory);

        if (!captured) {
          return fail(captured.error());
        }

        if (pending.compilation) {
          auto source = pending.compilation->source();
          auto ready = pending.compilation->ready();

          if (!source || !ready) {
            return fail(!source ? source.error() : ready.error());
          }

          if (!source->equals(*captured)) {
            pending.obsolete = true;
            pending.compilation->requestStop();
          }

          if (*ready) {
            auto program = pending.compilation->take();
            pending.compilation.reset();

            if (pending.obsolete) {
              ++m_impl->m_stats.discardedScriptCompilations;
            } else if (!program) {
              return fail(program.error());
            } else {
              pending.program.emplace(std::move(*program));
            }
          }
        }

        if (pending.program) {
          auto source = pending.program->source();

          if (!source) {
            return fail(source.error());
          }

          if (!source->equals(*captured)) {
            pending.program.reset();
            ++m_impl->m_stats.discardedScriptCompilations;
          }
        }

        if (!pending.compilation && !pending.program) {
          auto job = Script::CompilationJob::begin(pending.compiler, *captured);

          if (!job) {
            return fail(job.error());
          }

          pending.obsolete = false;
          pending.compilation.emplace(std::move(*job));
          ++m_impl->m_stats.preparationCompileAttempts;
        }

        if (!pending.program) {
          m_impl->m_stats.preparation = PlayPreparation::Scripts;
          return state;
        }

        if (pending.playback) {
          auto ready = pending.playback->pollReady();

          if (!ready) {
            return fail(ready.error());
          }

          if (!*ready) {
            m_impl->m_stats.preparation = PlayPreparation::Audio;
            return state;
          }
        }

        m_impl->m_stats.preparation = PlayPreparation::Scene;
        auto session = Project::GameSession::createFromScene(
            pending.project, pending.compiler, pending.scene, pending.config.session,
            pending.scenePath, pending.playback ? &*pending.playback : nullptr, &*pending.program);
        pending.playback.reset();
        pending.program.reset();

        if (!session) {
          return fail(session.error());
        }

        m_impl->m_session.emplace(std::move(*session));
        auto started = m_impl->m_session->beginStart();

        if (!started || started->realmFaulted) {
          return fail(started ? realmFaultError("Start play script realm faulted", *started)
                              : started.error());
        }

        m_impl->m_frame.reports.push_back({{}, std::move(*started)});
        m_impl->m_stats.preparation = PlayPreparation::Activation;
        return state;
      }

      auto ready = m_impl->m_session->pollStart();

      if (!ready) {
        return fail(ready.error());
      }

      if (*ready) {
        m_impl->m_stats.started = true;
        m_impl->m_stats.preparation.reset();
        state = PlayState::Running;
        m_impl->m_preparation.reset();
      }

      return state;
    } catch (const std::exception& error) {
      return fail(Core::Error{Core::ErrorCode::External,
                              "Poll play preparation: " + std::string(error.what()),
                              std::source_location::current()});
    }
  }

  auto PlaySession::advance(const Platform::InputState& input, std::chrono::nanoseconds elapsed,
                            bool pointerCaptured, std::optional<Project::GameInputArea> area,
                            bool textInputActive) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (m_impl->m_stats.state != PlayState::Running) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session must be running");
    }

    if (elapsed.count() < 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Play elapsed time must be nonnegative");
    }

    if (m_impl->m_stats.frames == std::numeric_limits<std::uint64_t>::max()
        || m_impl->m_stats.fixedSteps
               > std::numeric_limits<std::uint64_t>::max() - m_impl->m_maxUpdates) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play frame counters exhausted");
    }

    ActiveScope active{m_impl->m_active};

    try {
      auto published = m_impl->m_session->publish(input, pointerCaptured, area, textInputActive);

      if (!published) {
        return std::unexpected(published.error());
      }

      auto timing = m_impl->m_stepper.advance(elapsed);
      m_impl->m_frame.elapsed = elapsed;
      m_impl->m_frame.dropped = timing.dropped;
      m_impl->m_stats.dropped = timing.dropped;
      m_impl->m_frame.reports.clear();
      ++m_impl->m_stats.frames;

      for (std::size_t step = 0; step < timing.updates; ++step) {
        auto clock = m_impl->m_session->gameClock();
        auto before = clock
                          ? clock->snapshot()
                          : Core::Result<Script::GameClockSnapshot>{std::unexpected(clock.error())};

        if (!before) {
          return std::unexpected(before.error());
        }

        auto dispatched = m_impl->record(m_impl->m_session->fixedUpdate());

        if (!dispatched) {
          return dispatched;
        }

        if (!before->paused) {
          ++m_impl->m_stats.fixedSteps;
        }
      }

      const auto seconds = std::min(1.0, std::chrono::duration<double>(elapsed).count());
      return m_impl->record(m_impl->m_session->update(seconds));
    } catch (const std::exception& error) {
      m_impl->fault();
      return Core::fail(Core::ErrorCode::External,
                        "Advance play session: " + std::string(error.what()));
    }
  }

  auto PlaySession::stop() -> Core::Result<Script::ScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    ActiveScope active{m_impl->m_active};

    if (!m_impl->m_stats.started) {
      if (m_impl->m_stats.state == PlayState::Stopped
          || m_impl->m_stats.state == PlayState::Faulted) {
        m_impl->m_stats.state = PlayState::Stopped;
        return Script::ScriptReport{};
      }

      auto& pending = *m_impl->m_preparation;

      if (pending.compilation) {
        pending.compilation->requestStop();
      }

      auto requested = m_impl->m_session  ? m_impl->m_session->requestPreparationClose()
                       : pending.playback ? pending.playback->requestClose()
                                          : Core::Result<void>{};

      if (!requested) {
        return std::unexpected(requested.error());
      }

      m_impl->m_stats.state = PlayState::Cancelling;
      m_impl->m_stats.preparation = PlayPreparation::Teardown;
      return Script::ScriptReport{};
    }

    auto result = m_impl->m_session->close();
    m_impl->m_stats.state = PlayState::Stopped;

    if (m_impl->m_saveStorage) {
      auto cleaned = m_impl->m_saveStorage->discard();

      if (!cleaned) {
        if (!result) {
          result.error().message += "; cleanup: " + cleaned.error().message;
          return result;
        }

        return std::unexpected(cleaned.error());
      }

      m_impl->m_saveStorage.reset();
    }

    return result;
  }

  auto PlaySession::scene() const -> Core::Result<const World::Scene*> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!m_impl->m_stats.started) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play scene has not been published");
    }

    return m_impl->m_session->scene().transform(
        [](const auto& scene) -> const World::Scene* { return scene.get(); });
  }

  auto PlaySession::animationState(World::EntityId owner) const
      -> Core::Result<Animation::ScenePlayerState> {
    auto live = scene();

    if (!live || !m_impl->m_session || m_impl->m_stats.state != PlayState::Running
        || (*live)->id() != owner.scene) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Animation inspection requires current running Play scope");
    }

    return m_impl->m_session->animationState(owner);
  }

  auto PlaySession::controlAnimationLayer(World::EntityId owner, std::string_view name,
                                          const Animation::LayerControl& operation)
      -> Core::Result<void> {
    auto live = scene();

    if (!live || !m_impl->m_session || m_impl->m_stats.state != PlayState::Running
        || (*live)->id() != owner.scene) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Animation control requires current running Play scope");
    }

    return m_impl->m_session->controlAnimationLayer(owner, name, operation);
  }

  auto PlaySession::inspectAnimation(World::EntityId owner) const -> Core::Result<std::string> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!m_impl->m_session || m_impl->m_stats.state != PlayState::Running) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Animation inspection requires running Play");
    }

    auto scene = m_impl->m_session->scene();
    return scene && (*scene)->id() == owner.scene
               ? m_impl->m_session->inspectAnimation(owner)
               : Core::fail(Core::ErrorCode::InvalidState, "Animation inspection scope is stale");
  }

  auto PlaySession::gameData() const -> Core::Result<Script::GameData> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    if (!m_impl->m_stats.started) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play has not started");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_session->gameData()
                   : Core::Result<Script::GameData>{std::unexpected(checked.error())};
  }

  auto PlaySession::gameClock() const -> Core::Result<Script::GameClock> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    auto checked = m_impl->check();
    return checked && m_impl->m_session ? m_impl->m_session->gameClock()
           : checked ? Core::fail(Core::ErrorCode::InvalidState, "Play clock is not prepared")
                     : std::unexpected(checked.error());
  }

  auto PlaySession::hud() const -> Core::Result<Script::Hud> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    auto checked = m_impl->check();
    return checked && m_impl->m_session ? m_impl->m_session->hud()
           : checked ? Core::fail(Core::ErrorCode::InvalidState, "Play HUD is not prepared")
                     : std::unexpected(checked.error());
  }

  auto PlaySession::requestScene(std::string_view path) -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    if (!m_impl->m_stats.started) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play has not started");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_session->requestScene(path)
                   : Core::Result<std::uint64_t>{std::unexpected(checked.error())};
  }

  auto PlaySession::sceneRequest(std::uint64_t id) const -> Core::Result<Script::SceneRequest> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    if (!m_impl->m_stats.started) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play has not started");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_session->sceneRequest(id)
                   : Core::Result<Script::SceneRequest>{std::unexpected(checked.error())};
  }

  auto PlaySession::textInputRequested() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    if (!m_impl->m_stats.started) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play has not started");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_session->textInputRequested()
                   : Core::Result<bool>{std::unexpected(checked.error())};
  }

  auto PlaySession::pointerCaptureRequested() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    if (!m_impl->m_stats.started) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play has not started");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_session->pointerCaptureRequested()
                   : Core::Result<bool>{std::unexpected(checked.error())};
  }

  auto PlaySession::stats() const -> Core::Result<PlayStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    auto checked = m_impl->check();
    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!m_impl->m_stats.started) {
      return m_impl->m_stats;
    }

    auto game = m_impl->m_session->stats();

    if (!game) {
      return std::unexpected(game.error());
    }

    auto result = m_impl->m_stats;
    result.location = std::move(game->location);
    result.transition = std::move(game->transition);
    result.source = std::move(game->source);
    result.retiringAudio = game->retiringAudio;
    result.clock = game->clock;
    result.simulatedUpdates = game->simulatedUpdates;
    return result;
  }

  auto PlaySession::pollScripts(bool force) -> Core::Result<Script::ReloadStatus> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    if (!m_impl->m_stats.started) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play has not started");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto result = m_impl->m_session->pollScripts(force);
    auto state = m_impl->m_session->stats();

    if (!state) {
      return std::unexpected(state.error());
    }

    if (state->state == Project::SessionState::Faulted) {
      m_impl->m_stats.state = PlayState::Faulted;
    }

    return result;
  }

  auto PlaySession::lastFrame() const -> Core::Result<PlayFrame> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      return m_impl->m_frame;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Read play reports: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Editor

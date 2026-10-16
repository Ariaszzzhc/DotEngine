module;

#include <cmath>
#include <exception>
#include <expected>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

module Dot.Project.GameSession;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Project;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Platform.Input;
import Dot.Script.AudioSession;
import Dot.Script.AnimationSession;
import Dot.Assets.SceneSkins;

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

  struct GameSession::Impl {
    std::shared_ptr<World::Scene> m_scene;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_identity{};
    Description m_description;
    SessionState m_state{SessionState::Ready};
    SessionAudio m_audioMode{SessionAudio::Offline};
    bool m_active{};
    std::optional<Script::InputSource> m_input;
    std::optional<Script::PhysicsSource> m_physics;
    std::optional<Script::AudioSession> m_audio;
    std::optional<Script::AnimationSession> m_animations;
    /** Script callbacks retain services; destroy the realm before its dependencies.
     * 脚本回调持有服务，先销毁 realm 再销毁依赖。 */
    std::optional<Script::SceneScripts> m_scripts;
    std::optional<Script::LiveScripts> m_live;

    auto check(bool allowTerminal = false) const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id() || m_active) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Game session requires owner thread without reentry");
      }

      if (!allowTerminal && (m_state == SessionState::Closed || m_state == SessionState::Faulted)) {
        return Core::fail(Core::ErrorCode::InvalidState, "Game session is closed or faulted");
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

    auto frame(double seconds, bool fixed) -> Core::Result<Script::LiveScriptReport> {
      if (m_state != SessionState::Running) {
        return Core::fail(Core::ErrorCode::InvalidState, "Start game session before dispatch");
      }

      ActiveScope active{m_active};

      try {
        auto result = m_live
                          ? (fixed ? m_live->fixedUpdate(seconds) : m_live->update(seconds))
                          : (fixed ? m_scripts->fixedUpdate(seconds) : m_scripts->update(seconds))
                                .transform([](auto report) {
                                  return Script::LiveScriptReport{{}, std::move(report)};
                                });

        if (!result || result->frame.realmFaulted) {
          m_state = SessionState::Faulted;
          return result;
        }

        if (fixed) {
          auto advanced = m_physics->advance(seconds);

          if (!advanced) {
            m_state = SessionState::Faulted;
            return std::unexpected(advanced.error());
          }
        }

        if (!fixed) {
          auto animated = m_animations->advance(seconds);

          if (!animated) {
            m_state = SessionState::Faulted;
            return std::unexpected(animated.error());
          }

          auto synchronized = m_audio->synchronize(seconds);

          if (!synchronized) {
            m_state = SessionState::Faulted;
            return std::unexpected(synchronized.error());
          }
        }

        return result;
      } catch (const std::exception& error) {
        m_state = SessionState::Faulted;
        return Core::fail(Core::ErrorCode::External,
                          "Dispatch game session: " + std::string(error.what()));
      }
    }
  };

  GameSession::GameSession(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  GameSession::GameSession(GameSession&&) noexcept = default;

  auto GameSession::operator=(GameSession&&) noexcept -> GameSession& = default;

  GameSession::~GameSession() = default;

  auto GameSession::create(const Project& project, const Script::TypeScriptCompiler& compiler,
                           const GameSessionConfig& config) -> Core::Result<GameSession> {
    auto valid = validate(config);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto loaded = project.loadStartup();
    return loaded ? prepare(project, compiler, std::move(*loaded), config)
                  : std::unexpected(loaded.error());
  }

  auto GameSession::createFromScene(const Project& project,
                                    const Script::TypeScriptCompiler& compiler,
                                    const World::Scene& scene, const GameSessionConfig& config)
      -> Core::Result<GameSession> {
    auto valid = validate(config);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto encoded = World::encodeScene(scene);

    if (!encoded) {
      return std::unexpected(encoded.error());
    }

    auto cloned = World::decodeScene(*encoded);
    return cloned ? prepare(project, compiler, std::move(*cloned), config)
                  : std::unexpected(cloned.error());
  }

  auto GameSession::prepare(const Project& project, const Script::TypeScriptCompiler& compiler,
                            World::Scene scene, const GameSessionConfig& config)
      -> Core::Result<GameSession> {
    try {
      auto description = project.description();
      auto files = project.files();

      if (!description) {
        return std::unexpected(description.error());
      }

      if (!files) {
        return std::unexpected(files.error());
      }

      return prepareSession(*files, std::move(*description), std::move(scene), config, &compiler,
                            nullptr);
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
    return cloned
               ? prepareSession(assets, description, std::move(*cloned), config, nullptr, &program)
               : std::unexpected(cloned.error());
  }

  auto GameSession::prepareSession(const Core::FileSystem& assets, Description description,
                                   World::Scene scene, const GameSessionConfig& config,
                                   const Script::TypeScriptCompiler* compiler,
                                   const Script::CompiledProgram* precompiled)
      -> Core::Result<GameSession> {
    try {
      auto skins = Assets::validateSceneSkins(assets, scene);

      if (!skins) {
        return std::unexpected(skins.error());
      }

      auto impl = std::make_unique<Impl>();
      impl->m_description = std::move(description);
      impl->m_audioMode = config.audioMode;
      impl->m_scene = std::make_shared<World::Scene>(std::move(scene));
      impl->m_identity = impl->m_scene->id();
      auto input = Script::InputSource::create(config.input);

      if (!input) {
        return std::unexpected(input.error());
      }

      impl->m_input.emplace(std::move(*input));
      auto published = impl->m_input->publish(Platform::InputState{});

      if (!published) {
        return std::unexpected(published.error());
      }

      auto physics = Script::PhysicsSource::create(impl->m_scene, config.physics);

      if (!physics) {
        return std::unexpected(physics.error());
      }

      impl->m_physics.emplace(std::move(*physics));
      auto audio = config.audioMode == SessionAudio::Playback
                       ? Script::AudioSession::createPlayback(impl->m_scene, assets, config.audio,
                                                              config.audioLimits)
                       : Script::AudioSession::create(impl->m_scene, assets, config.audio.mixer,
                                                      config.audioLimits);

      if (!audio) {
        return std::unexpected(audio.error());
      }

      impl->m_audio.emplace(std::move(*audio));
      auto animations = Script::AnimationSession::create(impl->m_scene, assets);

      if (!animations) {
        return std::unexpected(animations.error());
      }

      impl->m_animations.emplace(std::move(*animations));
      auto scriptsConfig = config.scripts;

      for (auto bindings : {impl->m_input->bindings(), impl->m_physics->bindings(),
                            impl->m_audio->bindings(), impl->m_animations->bindings()}) {
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
        auto scripts = Script::LiveScripts::create(impl->m_scene, assets, *compiler, liveConfig);

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

    GameSessionStats result{
        m_impl->m_state, m_impl->m_description.fixedHz, m_impl->m_description.hotReload, {}};

    if (m_impl->m_state != SessionState::Closed) {
      auto scripts = m_impl->scriptStats();

      if (!scripts) {
        return std::unexpected(scripts.error());
      }

      result.scripts = *scripts;
    }

    return result;
  }

  auto GameSession::publish(const Platform::InputState& input, bool pointerCaptured)
      -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->m_input->publish(input, pointerCaptured);
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
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    ActiveScope active{m_impl->m_active};
    auto result = m_impl->m_live ? m_impl->m_live->start() : m_impl->m_scripts->start();
    m_impl->m_state
        = !result || result->realmFaulted ? SessionState::Faulted : SessionState::Running;

    if (m_impl->m_state == SessionState::Running) {
      auto animated = m_impl->m_animations->advance(0);

      if (!animated) {
        m_impl->m_state = SessionState::Faulted;
        return std::unexpected(animated.error());
      }
    }

    return result;
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

    return m_impl->frame(seconds, false);
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

    ActiveScope active{m_impl->m_active};
    auto result = m_impl->m_live ? m_impl->m_live->stop() : m_impl->m_scripts->stop();
    m_impl->m_live.reset();
    m_impl->m_scripts.reset();
    m_impl->m_animations.reset();
    m_impl->m_audio.reset();
    m_impl->m_physics.reset();
    m_impl->m_input.reset();
    m_impl->m_state = SessionState::Closed;
    return result;
  }
}  // namespace Dot::Project

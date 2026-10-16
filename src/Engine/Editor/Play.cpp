module;

#include <algorithm>
#include <chrono>
#include <exception>
#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>

module Dot.Editor.Play;

import Dot.Core.Clock;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Core.Files;
import Dot.Project.GameSession;

namespace Dot::Editor {
  namespace {
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
    Project::GameSession m_session;
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

    auto check() const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id() || m_active) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Play session requires owner thread without reentry");
      }

      return {};
    }

    auto fault() -> void {
      m_stats.state = PlayState::Faulted;
      auto data = m_session.gameData();

      if (data) {
        data->retire();
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
        return Core::fail(Core::ErrorCode::InvalidState, "Play script realm faulted");
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
      auto started = impl->m_session.start();

      if (!started || started->realmFaulted) {
        auto error = started ? Core::Error{Core::ErrorCode::InvalidState,
                                           "Start play script realm faulted",
                                           {}}
                             : started.error();
        auto closed = impl->m_session.close();

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

  auto PlaySession::advance(const Platform::InputState& input, std::chrono::nanoseconds elapsed,
                            bool pointerCaptured) -> Core::Result<void> {
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
      auto published = m_impl->m_session.publish(input, pointerCaptured);

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
        auto dispatched = m_impl->record(m_impl->m_session.fixedUpdate());

        if (!dispatched) {
          return dispatched;
        }

        ++m_impl->m_stats.fixedSteps;
      }

      const auto seconds = std::min(1.0, std::chrono::duration<double>(elapsed).count());
      return m_impl->record(m_impl->m_session.update(seconds));
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
    auto result = m_impl->m_session.close();
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

    return m_impl->m_session.scene().transform(
        [](const auto& scene) -> const World::Scene* { return scene.get(); });
  }

  auto PlaySession::gameData() const -> Core::Result<Script::GameData> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_session.gameData()
                   : Core::Result<Script::GameData>{std::unexpected(checked.error())};
  }

  auto PlaySession::requestScene(std::string_view path) -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_session.requestScene(path)
                   : Core::Result<std::uint64_t>{std::unexpected(checked.error())};
  }

  auto PlaySession::sceneRequest(std::uint64_t id) const -> Core::Result<Script::SceneRequest> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_session.sceneRequest(id)
                   : Core::Result<Script::SceneRequest>{std::unexpected(checked.error())};
  }

  auto PlaySession::pointerCaptureRequested() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Play session was moved");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_session.pointerCaptureRequested()
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

    auto game = m_impl->m_session.stats();

    if (!game) {
      return std::unexpected(game.error());
    }

    auto result = m_impl->m_stats;
    result.location = std::move(game->location);
    result.transition = std::move(game->transition);
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

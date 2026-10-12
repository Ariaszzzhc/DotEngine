module;

#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

module Dot.Script.LiveScripts;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.World.Scene;
import Dot.Script.Compiler;
import Dot.Script.SceneScripts;

namespace Dot::Script {
  namespace {
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

  struct LiveScripts::Impl {
    using Clock = std::chrono::steady_clock;
    std::shared_ptr<World::Scene> m_scene;
    Core::FileSystem m_project;
    TypeScriptCompiler m_compiler;
    LiveScriptsConfig m_config;
    SourceSnapshot m_attempt;
    SceneScripts m_scripts;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_sceneId;
    std::uint64_t m_compileAttempts{1};
    std::uint64_t m_reloads{};
    std::optional<Core::Error> m_error;
    Clock::time_point m_next;
    bool m_active{};
    bool m_stopped{};
    bool m_captureError{};

    Impl(std::shared_ptr<World::Scene> scene, Core::FileSystem project, TypeScriptCompiler compiler,
         LiveScriptsConfig config, SourceSnapshot attempt, SceneScripts scripts)
        : m_scene(std::move(scene)),
          m_project(std::move(project)),
          m_compiler(std::move(compiler)),
          m_config(std::move(config)),
          m_attempt(std::move(attempt)),
          m_scripts(std::move(scripts)),
          m_sceneId(m_scene->id()),
          m_next(Clock::now() + m_config.pollInterval) {
    }

    auto check(bool allowStopped = false) const -> Core::Result<void> {
      if (m_active || m_thread != std::this_thread::get_id() || m_scene->id() != m_sceneId
          || (!allowStopped && m_stopped)) {
        return Core::fail(
            Core::ErrorCode::InvalidState,
            "Live scripts require their original owner thread/scene without reentry or stop");
      }

      return m_scene->name().transform([](const auto&) {});
    }

    auto poll(bool force) -> Core::Result<ReloadStatus> {
      ReloadStatus status;
      status.error = m_error;

      if (!force && Clock::now() < m_next) {
        return status;
      }

      m_next = Clock::now() + m_config.pollInterval;
      status.checked = true;
      auto captured = m_compiler.captureProject(m_project, m_config.directory);

      if (!captured) {
        m_captureError = true;
        m_error = captured.error();
        status.error = m_error;
        return status;
      }

      if (!force && captured->equals(m_attempt)) {
        if (m_captureError) {
          m_error.reset();
          status.error.reset();
          m_captureError = false;
        }

        return status;
      }

      m_captureError = false;

      status.changed = !captured->equals(m_attempt);
      m_attempt = std::move(*captured);
      ++m_compileAttempts;
      auto program = m_compiler.compileSnapshot(m_attempt);

      if (!program) {
        m_error = program.error();
        status.error = m_error;
        return status;
      }

      auto reloaded = m_scripts.reload(*program);

      if (!reloaded) {
        m_error = reloaded.error();
        status.error = m_error;
        return status;
      }

      ++m_reloads;
      m_error.reset();
      status.error.reset();
      status.committed = true;
      status.reload = *reloaded;
      return status;
    }

    auto frame(double seconds, bool fixed) -> Core::Result<LiveScriptReport> {
      auto polled = poll(false);

      if (!polled) {
        return std::unexpected(polled.error());
      }

      auto dispatched = fixed ? m_scripts.fixedUpdate(seconds) : m_scripts.update(seconds);

      if (!dispatched) {
        return std::unexpected(dispatched.error());
      }

      return LiveScriptReport{std::move(*polled), std::move(*dispatched)};
    }
  };

  LiveScripts::LiveScripts(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  LiveScripts::LiveScripts(LiveScripts&&) noexcept = default;

  auto LiveScripts::operator=(LiveScripts&&) noexcept -> LiveScripts& = default;

  LiveScripts::~LiveScripts() = default;

  auto LiveScripts::create(std::shared_ptr<World::Scene> scene, const Core::FileSystem& project,
                           const TypeScriptCompiler& compiler, const LiveScriptsConfig& config)
      -> Core::Result<LiveScripts> {
    if (!scene || scene->id() == 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Live scripts require a live owned scene");
    }

    auto owner = scene->name();

    if (!owner) {
      return std::unexpected(owner.error());
    }

    if (config.pollInterval.count() < 0 || config.pollInterval > std::chrono::minutes{1}) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid live-script poll interval");
    }

    auto snapshot = compiler.captureProject(project, config.directory);

    if (!snapshot) {
      return std::unexpected(snapshot.error());
    }

    auto program = compiler.compileSnapshot(*snapshot);

    if (!program) {
      return std::unexpected(program.error());
    }

    auto scripts = SceneScripts::create(scene, *program, config.scripts);

    if (!scripts) {
      return std::unexpected(scripts.error());
    }

    auto prepared = scripts->sync();

    if (!prepared) {
      return std::unexpected(prepared.error());
    }

    if (!prepared->failures.empty()) {
      return std::unexpected(prepared->failures.front().error);
    }

    try {
      return LiveScripts(std::make_unique<Impl>(std::move(scene), project, compiler, config,
                                                std::move(*snapshot), std::move(*scripts)));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create live scripts: " + std::string(error.what()));
    }
  }

  auto LiveScripts::poll(bool force) -> Core::Result<ReloadStatus> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Live scripts were moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    ActiveScope active{m_impl->m_active};

    try {
      return m_impl->poll(force);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Poll live scripts: " + std::string(error.what()));
    }
  }

  auto LiveScripts::start() -> Core::Result<ScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Live scripts were moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    ActiveScope active{m_impl->m_active};
    return m_impl->m_scripts.start();
  }

  auto LiveScripts::update(double seconds) -> Core::Result<LiveScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Live scripts were moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Live frame seconds must be finite in [0,1]");
    }

    ActiveScope active{m_impl->m_active};

    try {
      return m_impl->frame(seconds, false);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Update live scripts: " + std::string(error.what()));
    }
  }

  auto LiveScripts::fixedUpdate(double seconds) -> Core::Result<LiveScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Live scripts were moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Live fixed seconds must be finite in [0,1]");
    }

    ActiveScope active{m_impl->m_active};

    try {
      return m_impl->frame(seconds, true);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Fixed update live scripts: " + std::string(error.what()));
    }
  }

  auto LiveScripts::stop() -> Core::Result<ScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Live scripts were moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    ActiveScope active{m_impl->m_active};
    m_impl->m_stopped = true;
    return m_impl->m_scripts.stop();
  }

  auto LiveScripts::stats() const -> Core::Result<LiveScriptsStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Live scripts were moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto scripts = m_impl->m_scripts.stats();

    if (!scripts) {
      return std::unexpected(scripts.error());
    }

    return LiveScriptsStats{m_impl->m_compileAttempts, m_impl->m_reloads, m_impl->m_error,
                            *scripts};
  }
}  // namespace Dot::Script

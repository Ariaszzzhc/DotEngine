module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.Project.PlayerControl;

import Dot.Core.Text;
import Dot.Platform.AgentInput;
import Dot.World.SceneFiles;

namespace Dot::Project {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;

    auto errorJson(const Core::Error& error) -> Json {
      constexpr std::array names{"InvalidArgument", "NotFound",     "Io",
                                 "Unavailable",     "InvalidState", "External"};
      return {{"code", names.at(static_cast<std::size_t>(error.code))}, {"message", error.message}};
    }

    auto transitionJson(const Script::SceneRequest& request) -> Json {
      constexpr std::array states{"pending", "loading", "succeeded", "failed", "cancelled"};
      return {{"id", std::to_string(request.id)},
              {"path", request.path},
              {"state", states.at(static_cast<std::size_t>(request.state))},
              {"committed", request.committed},
              {"error", request.error ? errorJson(*request.error) : Json(nullptr)}};
    }
  }  // namespace

  struct PlayerControl::Impl {
    GameSession* m_session;
    std::thread::id m_thread{std::this_thread::get_id()};
    Platform::AgentInput m_input;
    Platform::InputState m_previous;
    std::uint64_t m_scope{};
    std::vector<Json> m_diagnostics;
    std::uint64_t m_failures{};
    bool m_closing{};
    bool m_agent{};

    explicit Impl(GameSession& value) : m_session(&value) {
      m_diagnostics.reserve(32);
    }

    auto check() const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Player control requires its owner thread");
      }

      return {};
    }

    auto current() -> Core::Result<GameSessionStats> {
      auto checked = check();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      auto stats = m_session->stats();

      if (stats && stats->location.scope != m_scope) {
        m_scope = stats->location.scope;
        m_input.reset();
        m_previous = Platform::InputState{};
        m_agent = false;
      }

      return stats;
    }

    auto status(const GameSessionStats& stats) const -> Json {
      constexpr std::array states{"preparing", "running",  "faulted",
                                  "closed",    "starting", "cancelling"};
      const auto held = m_input.stats();
      Json source = nullptr;

      if (stats.source) {
        source = {{"compiling", stats.source->compiling},
                  {"compileAttempts", std::to_string(stats.source->compileAttempts)},
                  {"reloads", std::to_string(stats.source->reloads)},
                  {"error", stats.source->error ? errorJson(*stats.source->error) : Json(nullptr)}};
      }

      return {{"state", states.at(static_cast<std::size_t>(stats.state))},
              {"closing", m_closing},
              {"scope", std::to_string(m_scope)},
              {"path", stats.location.path ? Json(*stats.location.path) : Json(nullptr)},
              {"input",
               {{"source", m_agent ? "agent" : "native"},
                {"active", held.active},
                {"remaining", held.remaining},
                {"published", held.published}}},
              {"scripts", stats.scripts ? Json{{"running", stats.scripts->running},
                                               {"failed", stats.scripts->failed},
                                               {"faulted", stats.scripts->faulted}}
                                        : Json(nullptr)},
              {"source", std::move(source)},
              {"transition", stats.transition ? transitionJson(*stats.transition) : Json(nullptr)},
              {"failureCount", std::to_string(m_failures)},
              {"diagnostics", m_diagnostics}};
    }
  };

  PlayerControl::PlayerControl(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  PlayerControl::PlayerControl(PlayerControl&&) noexcept = default;

  auto PlayerControl::operator=(PlayerControl&&) noexcept -> PlayerControl& = default;

  PlayerControl::~PlayerControl() = default;

  auto PlayerControl::create(GameSession& session) -> Core::Result<PlayerControl> {
    try {
      auto impl = std::make_unique<Impl>(session);
      auto current = impl->current();
      return current ? Core::Result<PlayerControl>{PlayerControl{std::move(impl)}}
                     : std::unexpected(current.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create player control: " + std::string(error.what()));
    }
  }

  auto PlayerControl::perform(std::string_view request) -> Core::Result<std::string> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Player control was moved");
    }

    auto stats = m_impl->current();

    if (!stats) {
      return std::unexpected(stats.error());
    }

    if (request.empty() || request.size() > 1024 * 1024
        || request.find('\0') != std::string_view::npos || !Core::isValidUtf8(request)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid player control request bytes");
    }

    bool effects{};

    try {
      const auto value = Codec::parseJson(request);
      const auto command = value.at("command").get<std::string>();

      if (command == "close") {
        Codec::requireFields(value, {"command"});
        effects = true;
        m_impl->m_closing = true;
        m_impl->m_input.reset();
        return Json{{"ok", true}, {"closing", true}}.dump();
      }

      if (command == "inspect") {
        Codec::requireFields(value, {"command"});
        auto scene = m_impl->m_session->scene();
        auto encoded = scene ? World::encodeScene(**scene)
                             : Core::Result<std::string>{std::unexpected(scene.error())};
        return encoded ? Core::Result<std::string>{Json{{"ok", true},
                                                        {"player", m_impl->status(*stats)},
                                                        {"scene", Codec::parseJson(*encoded)}}
                                                       .dump()}
                       : std::unexpected(encoded.error());
      }

      if (command == "game-data") {
        Codec::requireFields(value, {"command", "value"});
        const auto inner = value["value"].at("command").get<std::string>();

        if ((m_impl->m_closing || stats->state != SessionState::Running) && inner != "snapshot"
            && inner != "get") {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Game data mutations/disk require running player");
        }

        auto data = m_impl->m_session->gameData();
        effects = true;
        auto result = data ? data->perform(value["value"].dump())
                           : Core::Result<std::string>{std::unexpected(data.error())};
        return result ? Core::Result<std::string>{Json{{"ok", true},
                                                       {"result", Codec::parseJson(*result)}}
                                                      .dump()}
                      : std::unexpected(result.error());
      }

      if (command == "scene-status") {
        Codec::requireFields(value, {"command", "request"});
        auto result = m_impl->m_session->sceneRequest(Codec::id(value["request"]));
        return result ? Core::Result<std::string>{Json{
                            {"ok", true},
                            {"transition",
                             transitionJson(*result)}}.dump()}
                      : std::unexpected(result.error());
      }

      if (command == "input") {
        Codec::requireFields(value, {"command", "scope", "value"});
      } else if (command == "cancel-input") {
        Codec::requireFields(value, {"command", "scope"});
      } else if (command == "request-scene") {
        Codec::requireFields(value, {"command", "scope", "path"});
      } else {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown player control command");
      }

      if (m_impl->m_closing || stats->state != SessionState::Running
          || Codec::id(value["scope"]) != m_impl->m_scope) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Player operation requires its current running scope");
      }

      if (command == "request-scene") {
        effects = true;
        auto result = m_impl->m_session->requestScene(value["path"].get<std::string>());
        return result ? Core::Result<std::string>{Json{
                            {"ok", true},
                            {"request",
                             std::to_string(*result)}}.dump()}
                      : std::unexpected(result.error());
      }

      if (command == "input") {
        auto staged = m_impl->m_input.submit(value["value"].dump(), m_impl->m_previous);

        if (!staged) {
          return std::unexpected(staged.error());
        }
      } else {
        m_impl->m_input.cancel();
      }

      effects = true;
      return Json{{"ok", true}, {"player", m_impl->status(*stats)}}.dump();
    } catch (const std::exception& error) {
      return Core::fail(
          effects ? Core::ErrorCode::External : Core::ErrorCode::InvalidArgument,
          std::string(effects ? "Player request may have committed: " : "Player control: ")
              + error.what());
    }
  }

  auto PlayerControl::next(const Platform::InputState& native)
      -> Core::Result<Platform::InputState> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Player control was moved");
    }

    auto stats = m_impl->current();

    if (!stats) {
      return std::unexpected(stats.error());
    }

    if (m_impl->m_closing || stats->state != SessionState::Running) {
      return Core::fail(Core::ErrorCode::InvalidState, "Input publication requires running player");
    }

    try {
      auto staged = m_impl->m_input.next();

      if (!staged) {
        return std::unexpected(staged.error());
      }

      auto output = *staged ? std::move(**staged) : native;
      m_impl->m_previous = output;
      m_impl->m_agent = staged->has_value();
      return output;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Publish player input: " + std::string(error.what()));
    }
  }

  auto PlayerControl::record(const Script::ScriptReport& report) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Player control was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    try {
      for (const auto& failure : report.failures) {
        if (m_impl->m_failures == std::numeric_limits<std::uint64_t>::max()) {
          return Core::fail(Core::ErrorCode::Unavailable, "Player diagnostic sequence exhausted");
        }

        auto message = failure.error.message.substr(0, 8192);

        while (!message.empty() && !Core::isValidUtf8(message)) {
          message.pop_back();
        }

        auto error = errorJson(failure.error);
        error["message"] = message;
        Json diagnostic{{"sequence", std::to_string(m_impl->m_failures + 1)},
                        {"scope", std::to_string(failure.entity.scene)},
                        {"entity", std::to_string(failure.entity.value)},
                        {"phase", static_cast<int>(failure.phase)},
                        {"error", std::move(error)},
                        {"truncated", message.size() != failure.error.message.size()}};

        if (m_impl->m_diagnostics.size() == 32) {
          m_impl->m_diagnostics.erase(m_impl->m_diagnostics.begin());
        }

        m_impl->m_diagnostics.push_back(std::move(diagnostic));
        ++m_impl->m_failures;
      }

      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Record player diagnostics: " + std::string(error.what()));
    }
  }

  auto PlayerControl::closing() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Player control was moved");
    }

    auto checked = m_impl->check();
    return checked ? Core::Result<bool>{m_impl->m_closing} : std::unexpected(checked.error());
  }
}  // namespace Dot::Project

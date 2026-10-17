module;

#include <array>
#include <exception>
#include <expected>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "../Serialization/Json.hpp"

module Dot.Script.GameClock;

import Dot.Core.Text;

namespace Dot::Script {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;

    auto snapshotJson(const GameClockSnapshot& value) -> Json {
      return {{"revision", std::to_string(value.revision)},
              {"paused", value.paused},
              {"step", value.step ? Json{{"id", std::to_string(value.step->id)},
                                         {"state", gameStepStateName(value.step->state)}}
                                  : Json(nullptr)}};
    }
  }  // namespace

  auto gameStepStateName(GameStepState state) -> std::string_view {
    constexpr std::array states{"pending", "running", "completed", "cancelled", "failed"};
    return states.at(static_cast<std::size_t>(state));
  }

  struct GameClock::Impl {
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_revision{1};
    bool m_paused{};
    bool m_retired{};
    std::optional<GameStepSnapshot> m_step;
  };

  GameClock::GameClock(std::shared_ptr<Impl> impl) : m_impl(std::move(impl)) {
  }

  auto GameClock::create() -> Core::Result<GameClock> {
    try {
      return GameClock(std::make_shared<Impl>());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create game clock: " + std::string(error.what()));
    }
  }

  auto GameClock::snapshot() const -> Core::Result<GameClockSnapshot> {
    if (!m_impl || m_impl->m_thread != std::this_thread::get_id()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game clock requires its owner thread");
    }

    return GameClockSnapshot{m_impl->m_revision, m_impl->m_paused, m_impl->m_step};
  }

  auto GameClock::retire() noexcept -> void {
    if (m_impl) {
      m_impl->m_retired = true;

      if (m_impl->m_step
          && (m_impl->m_step->state == GameStepState::Pending
              || m_impl->m_step->state == GameStepState::Running)) {
        m_impl->m_step->state = m_impl->m_step->state == GameStepState::Pending
                                    ? GameStepState::Cancelled
                                    : GameStepState::Failed;
        ++m_impl->m_revision;
      }
    }
  }

  auto GameClock::perform(std::string_view request) -> Core::Result<std::string> {
    auto current = snapshot();

    if (!current) {
      return std::unexpected(current.error());
    }

    if (request.empty() || request.size() > 1024 || !Core::isValidUtf8(request)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid game clock request bytes");
    }

    try {
      const auto value = Codec::parseJson(request, 4);
      const auto command = value.at("command").get<std::string>();

      if (command == "snapshot") {
        Codec::requireFields(value, {"command"});
        return snapshotJson(*current).dump();
      }

      if (command != "set-paused" && command != "step") {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown game clock operation");
      }

      Codec::requireFields(
          value, command == "step" ? std::initializer_list<std::string_view>{"command"}
                                   : std::initializer_list<std::string_view>{"command", "paused"});

      if (m_impl->m_retired) {
        return Core::fail(Core::ErrorCode::InvalidState, "Game clock is retired");
      }

      if (current->step && current->step->state == GameStepState::Running) {
        return Core::fail(Core::ErrorCode::InvalidState, "Game clock single step is running");
      }

      if (command == "step") {
        if (!current->paused || (current->step && current->step->state == GameStepState::Pending)) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Single step requires paused simulation without a pending step");
        }

        /** Reserve all three lifecycle revisions before accepting work.
         * 接受请求前保留请求、开始及完成的三个状态修订。 */
        if (current->revision > std::numeric_limits<std::uint64_t>::max() - 4
            || (current->step
                && current->step->id == std::numeric_limits<std::uint64_t>::max() - 1)) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Game clock single step identity exhausted");
        }

        auto next = *current;
        ++next.revision;
        next.step
            = GameStepSnapshot{current->step ? current->step->id + 1 : 1, GameStepState::Pending};
        auto reply = snapshotJson(next).dump();
        m_impl->m_revision = next.revision;
        m_impl->m_step = next.step;
        return reply;
      }

      const auto paused = value["paused"].get<bool>();

      if (paused != current->paused
          && current->revision == std::numeric_limits<std::uint64_t>::max() - 1) {
        return Core::fail(Core::ErrorCode::Unavailable, "Game clock revision exhausted");
      }

      auto next = *current;
      next.revision += paused != current->paused ? 1U : 0U;
      next.paused = paused;

      if (!paused && next.step && next.step->state == GameStepState::Pending) {
        next.step->state = GameStepState::Cancelled;
      }

      auto reply = snapshotJson(next).dump();
      m_impl->m_paused = paused;
      m_impl->m_revision = next.revision;
      m_impl->m_step = next.step;
      return reply;
    } catch (const std::exception& error) {
      return Core::fail(dynamic_cast<const std::invalid_argument*>(&error)
                                || dynamic_cast<const Json::exception*>(&error)
                            ? Core::ErrorCode::InvalidArgument
                            : Core::ErrorCode::External,
                        "Game clock: " + std::string(error.what()));
    }
  }

  auto GameClock::beginStep() -> Core::Result<bool> {
    auto current = snapshot();

    if (!current) {
      return std::unexpected(current.error());
    }

    if (m_impl->m_retired || (current->step && current->step->state == GameStepState::Running)) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game clock cannot begin a single step");
    }

    if (!current->step || current->step->state != GameStepState::Pending) {
      return false;
    }

    m_impl->m_step->state = GameStepState::Running;
    ++m_impl->m_revision;
    return true;
  }

  auto GameClock::finishStep(bool succeeded) -> Core::Result<void> {
    auto current = snapshot();

    if (!current) {
      return std::unexpected(current.error());
    }

    if (m_impl->m_retired || !current->step || current->step->state != GameStepState::Running) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game clock has no running single step");
    }

    m_impl->m_step->state = succeeded ? GameStepState::Completed : GameStepState::Failed;
    ++m_impl->m_revision;
    return {};
  }

  auto GameClock::bindings() const -> Core::Result<HostBindings> {
    auto current = snapshot();

    if (!current) {
      return std::unexpected(current.error());
    }

    try {
      HostBindings result;

      for (const auto* name : {"snapshot", "setPaused", "step"}) {
        auto service = *this;
        const auto setting = std::string_view(name) == "setPaused";
        const auto stepping = std::string_view(name) == "step";
        result.push_back(
            {"gameClock." + std::string(name),
             [service, setting,
              stepping](std::string_view args) mutable -> Core::Result<std::optional<std::string>> {
               if (args.empty() || args.size() > 1024 || !Core::isValidUtf8(args)) {
                 return Core::fail(Core::ErrorCode::InvalidArgument,
                                   "Invalid game clock arguments");
               }

               try {
                 const auto values = Codec::parseJson(args, 4);

                 if (!values.is_array() || values.size() != (setting ? 1U : 0U)) {
                   return Core::fail(Core::ErrorCode::InvalidArgument,
                                     "Incorrect game clock arguments");
                 }

                 Json request{{"command", setting ? "set-paused" : stepping ? "step" : "snapshot"}};

                 if (setting) {
                   request["paused"] = values[0];
                 }

                 auto reply = service.perform(request.dump());
                 return reply ? Core::Result<std::optional<std::string>>{std::optional{
                                    std::move(*reply)}}
                              : std::unexpected(reply.error());
               } catch (const std::exception& error) {
                 return Core::fail(Core::ErrorCode::InvalidArgument,
                                   "Game clock arguments: " + std::string(error.what()));
               }
             },
             setting || stepping ? HostAccess::ReadWrite : HostAccess::ReadOnly});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Bind game clock: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Script

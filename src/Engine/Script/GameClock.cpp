module;

#include <exception>
#include <expected>
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
  }  // namespace

  struct GameClock::Impl {
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_revision{1};
    bool m_paused{};
    bool m_retired{};
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

    return GameClockSnapshot{m_impl->m_revision, m_impl->m_paused};
  }

  auto GameClock::retire() noexcept -> void {
    if (m_impl) {
      m_impl->m_retired = true;
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
        return Json{{"revision", std::to_string(current->revision)}, {"paused", current->paused}}
            .dump();
      }

      if (command != "set-paused") {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown game clock operation");
      }

      Codec::requireFields(value, {"command", "paused"});
      const auto paused = value["paused"].get<bool>();

      if (m_impl->m_retired) {
        return Core::fail(Core::ErrorCode::InvalidState, "Game clock is retired");
      }

      if (paused != current->paused
          && current->revision == std::numeric_limits<std::uint64_t>::max() - 1) {
        return Core::fail(Core::ErrorCode::Unavailable, "Game clock revision exhausted");
      }

      const auto next = current->revision + (paused != current->paused ? 1U : 0U);
      auto reply = Json{{"revision", std::to_string(next)}, {"paused", paused}}.dump();
      m_impl->m_paused = paused;
      m_impl->m_revision = next;
      return reply;
    } catch (const std::exception& error) {
      return Core::fail(dynamic_cast<const std::invalid_argument*>(&error)
                                || dynamic_cast<const Json::exception*>(&error)
                            ? Core::ErrorCode::InvalidArgument
                            : Core::ErrorCode::External,
                        "Game clock: " + std::string(error.what()));
    }
  }

  auto GameClock::bindings() const -> Core::Result<HostBindings> {
    auto current = snapshot();

    if (!current) {
      return std::unexpected(current.error());
    }

    try {
      HostBindings result;

      for (const auto* name : {"snapshot", "setPaused"}) {
        auto service = *this;
        const auto setting = std::string_view(name) == "setPaused";
        result.push_back(
            {"gameClock." + std::string(name),
             [service,
              setting](std::string_view args) mutable -> Core::Result<std::optional<std::string>> {
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

                 Json request{{"command", setting ? "set-paused" : "snapshot"}};

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
             setting ? HostAccess::ReadWrite : HostAccess::ReadOnly});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Bind game clock: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Script

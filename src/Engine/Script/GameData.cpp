module;

#include <array>
#include <cstddef>
#include <exception>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "../Serialization/Json.hpp"

module Dot.Script.GameData;

import Dot.Core.Text;
import Dot.Game.State;
import Dot.Game.Saves;

namespace Dot::Script {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;

    auto snapshotJson(const Game::StateSnapshot& snapshot) -> Json {
      return {{"revision", std::to_string(snapshot.revision)},
              {"data", Codec::parseJson(snapshot.data, 32)}};
    }

    auto slotJson(const Game::SaveSlot& slot) -> Json {
      return {{"name", slot.name}, {"bytes", std::to_string(slot.bytes)}};
    }

    auto revisionReply(Core::Result<std::uint64_t> result) -> Core::Result<std::string> {
      return result ? Core::Result<std::string>{Json{{"revision", std::to_string(*result)}}.dump()}
                    : Core::Result<std::string>{std::unexpected(result.error())};
    }

    auto failure(const std::exception& error) -> std::unexpected<Core::Error> {
      return Core::fail(dynamic_cast<const std::invalid_argument*>(&error)
                                || dynamic_cast<const Json::exception*>(&error)
                            ? Core::ErrorCode::InvalidArgument
                            : Core::ErrorCode::External,
                        "Game data: " + std::string(error.what()));
    }
  }  // namespace

  struct GameData::Impl {
    Game::State m_state;
    std::optional<Game::Saves> m_saves;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::size_t m_bytes;
    bool m_retired{};

    Impl(Game::State value, std::size_t limit) : m_state(std::move(value)), m_bytes(limit) {
    }
  };

  GameData::GameData(std::shared_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  GameData::GameData(const GameData&) noexcept = default;

  auto GameData::operator=(const GameData&) noexcept -> GameData& = default;

  GameData::GameData(GameData&&) noexcept = default;

  auto GameData::operator=(GameData&&) noexcept -> GameData& = default;

  GameData::~GameData() = default;

  auto GameData::create(const GameDataConfig& config) -> Core::Result<GameData> {
    auto state = Game::State::create(config.initial, config.limits.state);

    if (!state) {
      return std::unexpected(state.error());
    }

    try {
      auto impl = std::make_shared<Impl>(std::move(*state), config.limits.state.bytes);

      if (config.saves) {
        auto saves = Game::Saves::open(*config.saves, config.limits);

        if (!saves) {
          return std::unexpected(saves.error());
        }

        impl->m_saves.emplace(std::move(*saves));
      } else if (config.limits.slots == 0 || config.limits.slots > 1024) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid game data save capacity");
      }

      return GameData(std::move(impl));
    } catch (const std::exception& error) {
      return failure(error);
    }
  }

  auto GameData::retire() noexcept -> void {
    if (m_impl) {
      m_impl->m_retired = true;
    }
  }

  auto GameData::perform(std::string_view request) -> Core::Result<std::string> {
    if (!m_impl || m_impl->m_thread != std::this_thread::get_id()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game data requires its owner thread");
    }

    if (request.empty() || request.size() > m_impl->m_bytes + 8192 || !Core::isValidUtf8(request)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid game data request bytes");
    }

    try {
      const auto value = Codec::parseJson(request, 36);
      const auto command = value.at("command").get<std::string>();

      if (command == "snapshot") {
        Codec::requireFields(value, {"command"});
        auto snapshot = m_impl->m_state.snapshot();
        return snapshot ? Core::Result<std::string>{snapshotJson(*snapshot).dump()}
                        : Core::Result<std::string>{std::unexpected(snapshot.error())};
      }

      if (command == "get") {
        Codec::requireFields(value, {"command", "key"});
        auto result = m_impl->m_state.get(value["key"].get<std::string>());

        if (!result) {
          return std::unexpected(result.error());
        }

        return Json{{"present", result->has_value()},
                    {"value", *result ? Codec::parseJson(**result, 32) : Json(nullptr)}}
            .dump();
      }

      if (m_impl->m_retired) {
        return Core::fail(Core::ErrorCode::InvalidState, "Game data session is retired");
      }

      if (command == "set") {
        Codec::requireFields(value, {"command", "key", "value"});
        return revisionReply(
            m_impl->m_state.set(value["key"].get<std::string>(), value["value"].dump()));
      }

      if (command == "remove") {
        Codec::requireFields(value, {"command", "key"});
        return revisionReply(m_impl->m_state.remove(value["key"].get<std::string>()));
      }

      if (command == "replace") {
        Codec::requireFields(value, {"command", "data", "revision"});
        return revisionReply(
            m_impl->m_state.replace(value["data"].dump(), Codec::id(value["revision"])));
      }

      if (command == "list-saves") {
        Codec::requireFields(value, {"command"});
      } else if (command == "read-save" || command == "delete-save") {
        Codec::requireFields(value, {"command", "slot"});
      } else if (command == "save") {
        Codec::requireFields(value, {"command", "slot", "overwrite"});
      } else if (command == "load") {
        Codec::requireFields(value, {"command", "slot", "revision"});
      } else {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown game data operation");
      }

      const auto slot = command == "list-saves" ? std::string{} : value["slot"].get<std::string>();
      const bool overwrite = command == "save" && value["overwrite"].get<bool>();
      const auto expectedRevision = command == "load" ? Codec::id(value["revision"]) : 0;

      if (!m_impl->m_saves) {
        return Core::fail(Core::ErrorCode::Unavailable, "Game session has no save directory");
      }

      if (command == "list-saves") {
        auto slots = m_impl->m_saves->list();

        if (!slots) {
          return std::unexpected(slots.error());
        }

        auto result = Json::array();

        for (const auto& entry : *slots) {
          result.push_back(slotJson(entry));
        }

        return result.dump();
      }

      if (command == "read-save" || command == "load") {
        auto loaded = m_impl->m_saves->read(slot);

        if (!loaded) {
          return std::unexpected(loaded.error());
        }

        return command == "load"
                   ? revisionReply(m_impl->m_state.replace(loaded->data, expectedRevision))
                   : Core::Result<std::string>{snapshotJson(*loaded).dump()};
      }

      if (command == "delete-save") {
        auto removed = m_impl->m_saves->remove(slot);
        return removed ? Core::Result<std::string>{"null"}
                       : Core::Result<std::string>{std::unexpected(removed.error())};
      }

      auto snapshot = m_impl->m_state.snapshot();

      if (!snapshot) {
        return std::unexpected(snapshot.error());
      }

      auto saved = m_impl->m_saves->write(slot, *snapshot, overwrite);
      return saved ? Core::Result<std::string>{slotJson(*saved).dump()}
                   : Core::Result<std::string>{std::unexpected(saved.error())};
    } catch (const std::exception& error) {
      return failure(error);
    }
  }

  auto GameData::bindings() const -> Core::Result<HostBindings> {
    if (!m_impl || m_impl->m_thread != std::this_thread::get_id() || m_impl->m_retired) {
      return Core::fail(Core::ErrorCode::InvalidState, "Cannot bind unavailable game data");
    }

    struct Method {
      const char* name;
      const char* command;
      std::array<const char*, 2> fields;
      HostAccess access{HostAccess::ReadWrite};
    };

    /** Disk reads are effectful too; preparation only observes in-memory data.
     * 磁盘读取也有外部副作用，准备阶段仅允许观察内存数据。 */
    constexpr Method methods[]{{"gameState.snapshot", "snapshot", {}, HostAccess::ReadOnly},
                               {"gameState.get", "get", {"key"}, HostAccess::ReadOnly},
                               {"gameState.set", "set", {"key", "value"}},
                               {"gameState.remove", "remove", {"key"}},
                               {"gameState.replace", "replace", {"data", "revision"}},
                               {"saves.list", "list-saves", {}},
                               {"saves.read", "read-save", {"slot"}},
                               {"saves.write", "save", {"slot", "overwrite"}},
                               {"saves.load", "load", {"slot", "revision"}},
                               {"saves.remove", "delete-save", {"slot"}}};

    try {
      HostBindings result;

      for (const auto& method : methods) {
        result.push_back(
            {method.name,
             [data = *this, method](
                 std::string_view arguments) mutable -> Core::Result<std::optional<std::string>> {
               if (arguments.empty() || arguments.size() > data.m_impl->m_bytes + 8192
                   || !Core::isValidUtf8(arguments)) {
                 return Core::fail(Core::ErrorCode::InvalidArgument,
                                   "Invalid game host argument bytes");
               }

               try {
                 auto args = Codec::parseJson(arguments, 35);
                 const std::size_t count = !method.fields[0] ? 0 : !method.fields[1] ? 1 : 2;

                 if (!args.is_array() || args.size() != count) {
                   return Core::fail(Core::ErrorCode::InvalidArgument,
                                     "Invalid game host argument shape");
                 }

                 Json request{{"command", method.command}};

                 for (std::size_t i = 0; i < count; ++i) {
                   request[method.fields[i]] = std::move(args[i]);
                 }

                 return data.perform(request.dump()).transform([](auto reply) {
                   return std::optional<std::string>{std::move(reply)};
                 });
               } catch (const std::exception& error) {
                 return failure(error);
               }
             },
             method.access});
      }

      return result;
    } catch (const std::exception& error) {
      return failure(error);
    }
  }
}  // namespace Dot::Script

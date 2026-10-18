module;

#include <array>
#include <cstddef>
#include <exception>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

#include "../Serialization/Json.hpp"

module Dot.Script.GameData;

import Dot.Core.Text;
import Dot.Game.State;
import Dot.Game.SaveQueue;

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
    std::optional<Game::SaveQueue> m_saves;
    std::map<std::uint64_t, std::uint64_t> m_loads;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::size_t m_bytes;
    bool m_retired{};

    auto operation(const Game::SaveJob& job) const -> std::string {
      if (m_loads.contains(job.id)) {
        return "load";
      }

      constexpr std::array names{"list", "read", "write", "remove"};
      return names[static_cast<std::size_t>(job.operation)];
    }

    auto jobJson(const Game::SaveJob& job) const -> Json {
      constexpr std::array states{"pending", "running", "succeeded", "failed", "cancelled"};
      constexpr std::array codes{"InvalidArgument", "NotFound",     "Io",
                                 "Unavailable",     "InvalidState", "External"};
      return {{"id", std::to_string(job.id)},
              {"operation", operation(job)},
              {"state", m_loads.contains(job.id) && job.state == Game::SaveJobState::Succeeded
                            ? "ready"
                            : states[static_cast<std::size_t>(job.state)]},
              {"error", job.error ? Json{{"code", codes[static_cast<std::size_t>(job.error->code)]},
                                         {"message", job.error->message}}
                                  : Json(nullptr)}};
    }

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
    auto valid = Game::validateSaveQueueLimits(config.limits);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto state = Game::State::create(config.initial, config.limits.saves.state);

    if (!state) {
      return std::unexpected(state.error());
    }

    try {
      auto impl = std::make_shared<Impl>(std::move(*state), config.limits.saves.state.bytes);

      if (config.saves) {
        auto saves = Game::SaveQueue::create(*config.saves, config.limits);

        if (!saves) {
          return std::unexpected(saves.error());
        }

        impl->m_saves.emplace(std::move(*saves));
      }

      return GameData(std::move(impl));
    } catch (const std::exception& error) {
      return failure(error);
    }
  }

  auto GameData::retire() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game data was moved");
    }

    m_impl->m_retired = true;
    return m_impl->m_saves ? m_impl->m_saves->finish() : Core::Result<void>{};
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

      if (command == "save-jobs" || command == "save-status" || command == "take-save"
          || command == "cancel-save") {
        if (command == "save-jobs") {
          Codec::requireFields(value, {"command"});
        } else {
          Codec::requireFields(value, {"command", "id", "operation"});
        }

        if (!m_impl->m_saves) {
          return Core::fail(Core::ErrorCode::Unavailable, "Game session has no save directory");
        }

        if (command == "save-jobs") {
          auto jobs = m_impl->m_saves->jobs();

          if (!jobs) {
            return std::unexpected(jobs.error());
          }

          auto result = Json::array();

          for (const auto& job : *jobs) {
            result.push_back(m_impl->jobJson(job));
          }

          return result.dump();
        }

        const auto id = Codec::id(value["id"]);
        auto job = m_impl->m_saves->status(id);

        if (!job) {
          return std::unexpected(job.error());
        }

        if (value["operation"].get<std::string>() != m_impl->operation(*job)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Save job operation does not match its identity");
        }

        if (command == "cancel-save") {
          if (m_impl->m_retired) {
            return Core::fail(Core::ErrorCode::InvalidState, "Game data session is retired");
          }

          auto cancelled = m_impl->m_saves->cancel(id);
          return cancelled ? Core::Result<std::string>{"null"} : std::unexpected(cancelled.error());
        }

        if (command == "save-status") {
          return m_impl->jobJson(*job).dump();
        }

        job = m_impl->m_saves->take(id);

        if (!job) {
          return std::unexpected(job.error());
        }

        const bool terminal = job->state != Game::SaveJobState::Pending
                              && job->state != Game::SaveJobState::Running;
        std::optional<std::uint64_t> loadedRevision;
        const auto load = m_impl->m_loads.find(id);

        if (job->state == Game::SaveJobState::Succeeded && load != m_impl->m_loads.end()) {
          auto applied = m_impl->m_retired
                             ? Core::Result<std::uint64_t>{Core::fail(
                                   Core::ErrorCode::InvalidState,
                                   "Game data retired before load publication")}
                             : m_impl->m_state.replace(
                                   std::get<Game::StateSnapshot>(job->output).data, load->second);

          if (applied) {
            loadedRevision = *applied;
          } else {
            job->state = Game::SaveJobState::Failed;
            job->error = applied.error();
          }
        }

        auto result = m_impl->jobJson(*job);

        if (job->state == Game::SaveJobState::Succeeded) {
          if (loadedRevision) {
            result["state"] = "succeeded";
            result["result"] = {{"revision", std::to_string(*loadedRevision)}};
          } else if (auto* slots = std::get_if<std::vector<Game::SaveSlot>>(&job->output)) {
            result["result"] = Json::array();

            for (const auto& slot : *slots) {
              result["result"].push_back(slotJson(slot));
            }
          } else if (auto* snapshot = std::get_if<Game::StateSnapshot>(&job->output)) {
            result["result"] = snapshotJson(*snapshot);
          } else if (auto* slot = std::get_if<Game::SaveSlot>(&job->output)) {
            result["result"] = slotJson(*slot);
          } else {
            result["result"] = nullptr;
          }
        }

        if (terminal) {
          m_impl->m_loads.erase(id);
        }

        return result.dump();
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
        Codec::requireFields(value, {"command", "slot", "overwrite"}, {"snapshot"});
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

      Game::SaveRequest job{command == "list-saves"    ? Game::SaveOperation::List
                            : command == "delete-save" ? Game::SaveOperation::Remove
                            : command == "save"        ? Game::SaveOperation::Write
                                                       : Game::SaveOperation::Read,
                            slot,
                            {},
                            overwrite};

      if (command == "save") {
        if (value.contains("snapshot")) {
          const auto& snapshot = value["snapshot"];
          Codec::requireFields(snapshot, {"revision", "data"});
          job.snapshot
              = Game::StateSnapshot{Codec::id(snapshot["revision"]), snapshot["data"].dump()};
        } else {
          auto snapshot = m_impl->m_state.snapshot();

          if (!snapshot) {
            return std::unexpected(snapshot.error());
          }

          job.snapshot = std::move(*snapshot);
        }
      }

      if (command == "load") {
        auto current = m_impl->m_state.snapshot();

        if (!current || current->revision != expectedRevision) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Load requires current game state revision");
        }
      }

      std::map<std::uint64_t, std::uint64_t> metadata;

      if (command == "load") {
        metadata.emplace(0, expectedRevision);
      }

      auto submitted = m_impl->m_saves->submit(std::move(job));

      if (!submitted) {
        return std::unexpected(submitted.error());
      }

      if (command == "load") {
        auto node = metadata.extract(0);
        node.key() = *submitted;
        m_impl->m_loads.insert(std::move(node));
      }

      auto status = m_impl->m_saves->status(*submitted);
      return Json{{"id", std::to_string(*submitted)}, {"operation", m_impl->operation(*status)}}
          .dump();
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
      std::array<const char*, 3> fields;
      HostAccess access{HostAccess::ReadWrite};
      std::size_t optional{};
    };

    /** Disk reads are effectful too; preparation only observes in-memory data.
     * 磁盘读取也有外部副作用，准备阶段仅允许观察内存数据。 */
    constexpr Method methods[]{
        {"gameState.snapshot", "snapshot", {}, HostAccess::ReadOnly},
        {"gameState.get", "get", {"key"}, HostAccess::ReadOnly},
        {"gameState.set", "set", {"key", "value"}},
        {"gameState.remove", "remove", {"key"}},
        {"gameState.replace", "replace", {"data", "revision"}},
        {"saves.list", "list-saves", {}},
        {"saves.read", "read-save", {"slot"}},
        {"saves.write", "save", {"slot", "overwrite", "snapshot"}, HostAccess::ReadWrite, 1},
        {"saves.load", "load", {"slot", "revision"}},
        {"saves.remove", "delete-save", {"slot"}},
        {"saves.jobs", "save-jobs", {}, HostAccess::ReadOnly},
        {"saves.status", "save-status", {"id", "operation"}, HostAccess::ReadOnly},
        {"saves.take", "take-save", {"id", "operation"}},
        {"saves.cancel", "cancel-save", {"id", "operation"}}};

    try {
      HostBindings result;

      for (const auto& method : methods) {
        result.push_back({method.name,
                          [data = *this, method](std::string_view arguments) mutable
                              -> Core::Result<std::optional<std::string>> {
                            if (arguments.empty() || arguments.size() > data.m_impl->m_bytes + 8192
                                || !Core::isValidUtf8(arguments)) {
                              return Core::fail(Core::ErrorCode::InvalidArgument,
                                                "Invalid game host argument bytes");
                            }

                            try {
                              auto args = Codec::parseJson(arguments, 35);
                              std::size_t count{};

                              while (count < method.fields.size() && method.fields[count]) {
                                ++count;
                              }

                              if (!args.is_array() || args.size() < count - method.optional
                                  || args.size() > count) {
                                return Core::fail(Core::ErrorCode::InvalidArgument,
                                                  "Invalid game host argument shape");
                              }

                              Json request{{"command", method.command}};

                              for (std::size_t i = 0; i < args.size(); ++i) {
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

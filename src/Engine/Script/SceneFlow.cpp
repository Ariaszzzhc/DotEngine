module;

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "../Serialization/Json.hpp"

module Dot.Script.SceneFlow;

import Dot.Core.Result;
import Dot.Core.Paths;
import Dot.Core.Text;

namespace Dot::Script {
  namespace {
    using Json = Serialization::Detail::Json;
    using Reply = Core::Result<std::optional<std::string>>;

    auto validPath(std::string_view path) -> bool {
      if (!Core::isPortableRelativePath(path) || !path.ends_with(".dotscene")) {
        return false;
      }

      while (!path.empty()) {
        const auto end = path.find('/');

        if (path.substr(0, end).starts_with(".dot-stage-")) {
          return false;
        }

        if (end == std::string_view::npos) {
          break;
        }

        path.remove_prefix(end + 1);
      }

      return true;
    }

    auto locationJson(const SceneLocation& value) -> Json {
      return {{"scope", std::to_string(value.scope)},
              {"path", value.path ? Json(*value.path) : Json(nullptr)}};
    }

    auto requestJson(const SceneRequest& value) -> Json {
      constexpr std::array states{"pending", "loading", "succeeded", "failed", "cancelled"};
      constexpr std::array codes{"InvalidArgument", "NotFound",     "Io",
                                 "Unavailable",     "InvalidState", "External"};
      Json error = nullptr;

      if (value.error) {
        error = {{"code", codes.at(static_cast<std::size_t>(value.error->code))},
                 {"message", value.error->message}};
      }

      return {{"id", std::to_string(value.id)},
              {"path", value.path},
              {"state", states.at(static_cast<std::size_t>(value.state))},
              {"committed", value.committed},
              {"error", std::move(error)}};
    }

    auto identifier(const Json& value) -> Core::Result<std::uint64_t> {
      if (!value.is_string()) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Scene request id must be a decimal string");
      }

      const auto& text = value.get_ref<const std::string&>();
      std::uint64_t id{};
      const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), id);

      if (text.empty() || text.size() > 20 || text.front() == '0' || error != std::errc{}
          || end != text.data() + text.size()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid canonical scene request id");
      }

      return id;
    }
  }  // namespace

  struct SceneFlow::Impl {
    std::thread::id m_thread{std::this_thread::get_id()};
    SceneLocation m_location;
    std::array<std::optional<SceneRequest>, 16> m_records;
    std::uint64_t m_next{1};
    std::optional<std::uint64_t> m_pending;
    std::optional<std::uint64_t> m_loading;
    bool m_retired{};

    explicit Impl(SceneLocation location) : m_location(std::move(location)) {
    }

    auto check(bool live = false) const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id() || (live && m_retired)) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Scene flow requires its live owner thread");
      }

      return {};
    }

    auto find(std::uint64_t id) -> SceneRequest* {
      auto& record = m_records[id % m_records.size()];
      return record && record->id == id ? &*record : nullptr;
    }
  };

  SceneFlow::SceneFlow(std::shared_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  SceneFlow::SceneFlow(const SceneFlow&) noexcept = default;

  auto SceneFlow::operator=(const SceneFlow&) noexcept -> SceneFlow& = default;

  SceneFlow::SceneFlow(SceneFlow&&) noexcept = default;

  auto SceneFlow::operator=(SceneFlow&&) noexcept -> SceneFlow& = default;

  SceneFlow::~SceneFlow() = default;

  auto SceneFlow::create(std::uint64_t scope, std::optional<std::string> path)
      -> Core::Result<SceneFlow> {
    if (scope == 0 || (path && !validPath(*path))) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid initial scene location");
    }

    try {
      return SceneFlow(std::make_shared<Impl>(SceneLocation{scope, std::move(path)}));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create scene flow: " + std::string(error.what()));
    }
  }

  auto SceneFlow::location() const -> Core::Result<SceneLocation> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene flow was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      return m_impl->m_location;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Read scene location: " + std::string(error.what()));
    }
  }

  auto SceneFlow::request(std::string_view path, std::uint64_t scope)
      -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene flow was moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (scope != m_impl->m_location.scope || m_impl->m_pending || m_impl->m_loading) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Scene request is stale or another request is active");
    }

    if (!validPath(path)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Scene path must be a portable relative .dotscene file");
    }

    if (m_impl->m_next == std::numeric_limits<std::uint64_t>::max()) {
      return Core::fail(Core::ErrorCode::Unavailable, "Scene request ids exhausted");
    }

    try {
      const auto id = m_impl->m_next;
      SceneRequest record{id, std::string(path), SceneRequestState::Pending, false, {}};
      m_impl->m_records[id % m_impl->m_records.size()] = std::move(record);
      m_impl->m_pending = id;
      ++m_impl->m_next;
      return id;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Queue scene request: " + std::string(error.what()));
    }
  }

  auto SceneFlow::status(std::uint64_t id) const -> Core::Result<SceneRequest> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene flow was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto* record = m_impl->find(id);

    if (!record) {
      return Core::fail(id == 0 ? Core::ErrorCode::InvalidArgument : Core::ErrorCode::NotFound,
                        "Unknown or expired scene request id");
    }

    try {
      return *record;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Read scene request: " + std::string(error.what()));
    }
  }

  auto SceneFlow::latest() const -> Core::Result<std::optional<SceneRequest>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene flow was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_next == 1) {
      return std::optional<SceneRequest>{};
    }

    return status(m_impl->m_next - 1).transform([](auto value) {
      return std::optional<SceneRequest>{std::move(value)};
    });
  }

  auto SceneFlow::pending() const -> Core::Result<std::optional<SceneRequest>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene flow was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!m_impl->m_pending) {
      return std::optional<SceneRequest>{};
    }

    return status(*m_impl->m_pending).transform([](auto value) {
      return std::optional<SceneRequest>{std::move(value)};
    });
  }

  auto SceneFlow::begin(std::uint64_t id) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene flow was moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return checked;
    }

    if (m_impl->m_pending != id || m_impl->m_loading) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene request is not pending");
    }

    m_impl->find(id)->state = SceneRequestState::Loading;
    m_impl->m_pending.reset();
    m_impl->m_loading = id;
    return {};
  }

  auto SceneFlow::commit(std::uint64_t id, std::uint64_t scope, std::string path)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene flow was moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return checked;
    }

    auto* record = m_impl->find(id);

    if (!record || m_impl->m_loading != id || scope == 0 || scope == m_impl->m_location.scope
        || record->path != path) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Scene request does not match prepared publication");
    }

    m_impl->m_location = {scope, std::move(path)};
    record->state = SceneRequestState::Succeeded;
    record->committed = true;
    m_impl->m_loading.reset();
    return {};
  }

  auto SceneFlow::fail(std::uint64_t id, Core::Error error) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene flow was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    auto* record = m_impl->find(id);

    if (!record
        || (record->state != SceneRequestState::Loading
            && record->state != SceneRequestState::Succeeded)) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene request cannot fail in this state");
    }

    record->state = SceneRequestState::Failed;
    record->error = std::move(error);

    if (m_impl->m_loading == id) {
      m_impl->m_loading.reset();
    }

    return {};
  }

  auto SceneFlow::retire() noexcept -> void {
    if (!m_impl) {
      return;
    }

    for (auto& record : m_impl->m_records) {
      if (record
          && (record->state == SceneRequestState::Pending
              || record->state == SceneRequestState::Loading)) {
        record->state = SceneRequestState::Cancelled;
      }
    }

    m_impl->m_pending.reset();
    m_impl->m_loading.reset();
    m_impl->m_retired = true;
  }

  auto SceneFlow::bindings(std::uint64_t scope, std::optional<std::string> path) const
      -> Core::Result<HostBindings> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene flow was moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (scope == 0 || (path && !validPath(*path))) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid bound scene location");
    }

    try {
      HostBindings result;

      for (const auto* name : {"scenes.current", "scenes.request", "scenes.status"}) {
        result.push_back(
            {name,
             [flow = *this, location = SceneLocation{scope, path},
              name](std::string_view arguments) mutable -> Reply {
               auto checked = flow.m_impl->check();

               if (!checked) {
                 return std::unexpected(checked.error());
               }

               if (arguments.empty() || arguments.size() > 8192 || !Core::isValidUtf8(arguments)
                   || arguments.find('\0') != std::string_view::npos) {
                 return Core::fail(Core::ErrorCode::InvalidArgument,
                                   "Invalid scene host argument bytes");
               }

               try {
                 const auto args = Serialization::Detail::parseJson(arguments, 8);
                 const std::string_view method{name};
                 const auto count = method == "scenes.current" ? 0U : 1U;

                 if (!args.is_array() || args.size() != count) {
                   return Core::fail(Core::ErrorCode::InvalidArgument,
                                     "Invalid scene host argument shape");
                 }

                 if (method == "scenes.current") {
                   return std::optional{locationJson(location).dump()};
                 }

                 if (method == "scenes.request") {
                   if (!args[0].is_string()) {
                     return Core::fail(Core::ErrorCode::InvalidArgument,
                                       "Scene path must be a string");
                   }

                   auto requested = flow.request(args[0].get<std::string>(), location.scope);
                   return requested ? Reply{std::optional{Json(std::to_string(*requested)).dump()}}
                                    : Reply{std::unexpected(requested.error())};
                 }

                 auto id = identifier(args[0]);

                 if (!id) {
                   return std::unexpected(id.error());
                 }

                 auto record = flow.status(*id);
                 return record ? Reply{std::optional{requestJson(*record).dump()}}
                               : Reply{std::unexpected(record.error())};
               } catch (const std::invalid_argument& error) {
                 return Core::fail(Core::ErrorCode::InvalidArgument,
                                   "Scene JSON: " + std::string(error.what()));
               } catch (const Json::exception& error) {
                 return Core::fail(Core::ErrorCode::InvalidArgument,
                                   "Scene JSON: " + std::string(error.what()));
               } catch (const std::exception& error) {
                 return Core::fail(Core::ErrorCode::External,
                                   "Scene host: " + std::string(error.what()));
               }
             },
             std::string_view(name) == "scenes.request" ? HostAccess::ReadWrite
                                                        : HostAccess::ReadOnly});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Bind scene flow: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Script

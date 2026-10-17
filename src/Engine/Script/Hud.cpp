module;

#include <algorithm>
#include <array>
#include <exception>
#include <expected>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "ComponentJson.hpp"

module Dot.Script.Hud;

import Dot.Core.Text;
import Dot.Math;

namespace Dot::Script {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;

    auto xy(const Json& value) -> Math::Vec2 {
      Codec::requireFields(value, {"x", "y"});
      return {Codec::number(value["x"]), Codec::number(value["y"])};
    }

    auto box(const Json& value) -> Graphics::TextBox {
      Codec::requireFields(value, {"text", "position", "size", "fontSize", "padding", "color",
                                   "background", "visible"});
      return {value["text"].get<std::string>(),
              xy(value["position"]),
              xy(value["size"]),
              Codec::number(value["fontSize"]),
              Codec::number(value["padding"]),
              Detail::color<Math::Vec4>(value["color"]),
              Detail::color<Math::Vec4>(value["background"]),
              value["visible"].get<bool>()};
    }

    auto json(const Graphics::TextBox& value) -> Json {
      return {{"text", value.text},
              {"position", {{"x", value.position.x}, {"y", value.position.y}}},
              {"size", {{"x", value.size.x}, {"y", value.size.y}}},
              {"fontSize", value.fontSize},
              {"padding", value.padding},
              {"color", Detail::xyzw(value.color)},
              {"background", Detail::xyzw(value.background)},
              {"visible", value.visible}};
    }

    auto validKey(std::string_view key) -> bool {
      return !key.empty() && key.size() <= 64 && std::ranges::all_of(key, [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
               || c == '_' || c == '-';
      });
    }
  }  // namespace

  struct Hud::Impl {
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_scope;
    std::uint64_t m_revision{1};
    std::map<std::string, Graphics::TextBox> m_labels;
    bool m_retired{};

    explicit Impl(std::uint64_t scope) : m_scope(scope) {
    }
  };

  Hud::Hud(std::shared_ptr<Impl> impl) : m_impl(std::move(impl)) {
  }

  auto Hud::create(std::uint64_t scope) -> Core::Result<Hud> {
    if (!scope || scope == std::numeric_limits<std::uint64_t>::max()) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "HUD requires a valid scene scope");
    }

    try {
      return Hud(std::make_shared<Impl>(scope));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Create HUD: " + std::string(error.what()));
    }
  }

  auto Hud::snapshot() const -> Core::Result<HudSnapshot> {
    if (!m_impl || m_impl->m_thread != std::this_thread::get_id()) {
      return Core::fail(Core::ErrorCode::InvalidState, "HUD requires its owner thread");
    }

    try {
      HudSnapshot result{m_impl->m_scope, m_impl->m_revision, {}};

      for (const auto& [key, value] : m_impl->m_labels) {
        result.labels.push_back({key, value});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Snapshot HUD: " + std::string(error.what()));
    }
  }

  auto Hud::retire() noexcept -> void {
    if (m_impl) {
      m_impl->m_retired = true;
    }
  }

  auto Hud::perform(std::string_view request) -> Core::Result<std::string> {
    if (!m_impl || m_impl->m_thread != std::this_thread::get_id()) {
      return Core::fail(Core::ErrorCode::InvalidState, "HUD requires its owner thread");
    }

    if (request.empty() || request.size() > 65536 || !Core::isValidUtf8(request)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid HUD request bytes");
    }

    try {
      const auto value = Codec::parseJson(request, 8);
      const auto command = value.at("command").get<std::string>();

      if (command == "snapshot") {
        Codec::requireFields(value, {"command"});
        auto entries = Json::array();

        for (const auto& [key, label] : m_impl->m_labels) {
          entries.push_back({{"key", key}, {"value", json(label)}});
        }

        return Json{{"scope", std::to_string(m_impl->m_scope)},
                    {"revision", std::to_string(m_impl->m_revision)},
                    {"labels", std::move(entries)}}
            .dump();
      }

      if (m_impl->m_retired) {
        return Core::fail(Core::ErrorCode::InvalidState, "HUD scene is retired");
      }

      auto candidate = m_impl->m_labels;

      if (command == "set" || command == "remove") {
        const auto setting = command == "set";

        if (setting) {
          Codec::requireFields(value, {"command", "key", "value"});
        } else {
          Codec::requireFields(value, {"command", "key"});
        }

        const auto key = value["key"].get<std::string>();

        if (!validKey(key)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid HUD label key");
        }

        if (setting) {
          auto label = box(value["value"]);
          auto valid = Graphics::validateTextBox(label);

          if (!valid) {
            return std::unexpected(valid.error());
          }

          candidate.insert_or_assign(key, std::move(label));
        } else {
          candidate.erase(key);
        }
      } else if (command == "clear") {
        Codec::requireFields(value, {"command"});
        candidate.clear();
      } else {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown HUD operation");
      }

      std::size_t bytes{};

      for (const auto& [key, label] : candidate) {
        bytes += label.text.size();
      }

      if (candidate.size() > 32 || bytes > 32768) {
        return Core::fail(Core::ErrorCode::Unavailable, "HUD exceeds 32 labels/32768 text bytes");
      }

      const auto changed = candidate != m_impl->m_labels;

      if (changed && m_impl->m_revision == std::numeric_limits<std::uint64_t>::max() - 1) {
        return Core::fail(Core::ErrorCode::Unavailable, "HUD revision exhausted");
      }

      const auto next = m_impl->m_revision + (changed ? 1U : 0U);
      auto reply = Json{{"revision", std::to_string(next)}}.dump();

      if (changed) {
        m_impl->m_labels.swap(candidate);
        m_impl->m_revision = next;
      }

      return reply;
    } catch (const std::exception& error) {
      return Core::fail(dynamic_cast<const std::invalid_argument*>(&error)
                                || dynamic_cast<const Json::exception*>(&error)
                            ? Core::ErrorCode::InvalidArgument
                            : Core::ErrorCode::External,
                        "HUD: " + std::string(error.what()));
    }
  }

  auto Hud::bindings() const -> Core::Result<HostBindings> {
    auto valid = snapshot();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    try {
      HostBindings result;

      for (const auto* name : {"snapshot", "set", "remove", "clear"}) {
        auto service = *this;
        const auto method = std::string(name);
        result.push_back(
            {"hud." + method,
             [service,
              method](std::string_view args) mutable -> Core::Result<std::optional<std::string>> {
               if (args.empty() || args.size() > 65536 || !Core::isValidUtf8(args)) {
                 return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid HUD argument bytes");
               }

               try {
                 const auto values = Codec::parseJson(args, 8);
                 const auto count = method == "set" ? 2U : method == "remove" ? 1U : 0U;

                 if (!values.is_array() || values.size() != count) {
                   return Core::fail(Core::ErrorCode::InvalidArgument, "Incorrect HUD arguments");
                 }

                 Json request{{"command", method}};

                 if (count) {
                   request["key"] = values[0];
                 }

                 if (count == 2) {
                   request["value"] = values[1];
                 }

                 auto reply = service.perform(request.dump());
                 return reply ? Core::Result<std::optional<std::string>>{std::optional{
                                    std::move(*reply)}}
                              : std::unexpected(reply.error());
               } catch (const std::exception& error) {
                 return Core::fail(Core::ErrorCode::InvalidArgument,
                                   "HUD arguments: " + std::string(error.what()));
               }
             },
             method == "snapshot" ? HostAccess::ReadOnly : HostAccess::ReadWrite});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "HUD bindings: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Script

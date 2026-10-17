module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

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

    auto box(const Json& value, bool button) -> Graphics::TextBox {
      if (button) {
        Codec::requireFields(value, {"text", "position", "size", "fontSize", "padding", "color",
                                     "background", "visible", "enabled"});
      } else {
        Codec::requireFields(value, {"text", "position", "size", "fontSize", "padding", "color",
                                     "background", "visible"});
      }

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

    auto keyJson(const std::optional<std::string>& key) -> Json {
      return key ? Json(*key) : Json(nullptr);
    }
  }  // namespace

  struct Hud::Impl {
    struct Element {
      Graphics::TextBox box;
      bool button{};
      bool enabled{true};
      auto operator==(const Element&) const -> bool = default;
    };

    struct Action {
      std::uint64_t sequence;
      std::string key;
      auto operator==(const Action&) const -> bool = default;
    };

    struct State {
      std::map<std::string, Element> elements;
      std::vector<Action> actions;
      std::uint64_t sequence{};
      std::optional<std::string> focused;
      std::optional<std::string> hovered;
      std::optional<std::string> armed;
      std::optional<std::string> pressed;
      auto operator==(const State&) const -> bool = default;
    };

    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_scope;
    std::uint64_t m_revision{1};
    std::uint64_t m_buttonRevision{1};
    State m_state;
    HudInputResult m_input;
    bool m_retired{};

    explicit Impl(std::uint64_t scope) : m_scope(scope) {
    }

    auto check(bool writing = false) const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id() || (writing && m_retired)) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "HUD requires its owner thread/live writer");
      }

      return {};
    }

    static auto eligible(const Element& value) -> bool {
      return value.button && value.enabled && value.box.visible;
    }

    static auto eligible(const State& state, std::string_view key) -> bool {
      const auto found = state.elements.find(std::string(key));
      return found != state.elements.end() && eligible(found->second);
    }

    static auto sanitize(State& state) -> void {
      for (auto* key : {&state.focused, &state.hovered, &state.armed, &state.pressed}) {
        if (*key && !eligible(state, **key)) {
          key->reset();
        }
      }
    }

    static auto activate(State& state, const std::string& key) -> Core::Result<void> {
      if (!eligible(state, key)) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "HUD activation requires a visible enabled button");
      }

      if (state.sequence == std::numeric_limits<std::uint64_t>::max() - 1) {
        return Core::fail(Core::ErrorCode::Unavailable, "HUD action sequence exhausted");
      }

      state.actions.push_back({++state.sequence, key});

      if (state.actions.size() > 64) {
        state.actions.erase(state.actions.begin());
      }

      return {};
    }

    static auto sameButtons(const State& first, const State& second) noexcept -> bool {
      auto a = first.elements.begin();
      auto b = second.elements.begin();

      while (true) {
        while (a != first.elements.end() && !a->second.button) {
          ++a;
        }

        while (b != second.elements.end() && !b->second.button) {
          ++b;
        }

        if (a == first.elements.end() || b == second.elements.end()) {
          return a == first.elements.end() && b == second.elements.end();
        }

        if (*a != *b) {
          return false;
        }

        ++a;
        ++b;
      }
    }

    auto revision(const State& candidate) const -> Core::Result<std::uint64_t> {
      const auto changed = candidate != m_state;

      if (changed && m_revision == std::numeric_limits<std::uint64_t>::max() - 1) {
        return Core::fail(Core::ErrorCode::Unavailable, "HUD revision exhausted");
      }

      if (!sameButtons(candidate, m_state)
          && m_buttonRevision == std::numeric_limits<std::uint64_t>::max() - 1) {
        return Core::fail(Core::ErrorCode::Unavailable, "HUD button revision exhausted");
      }

      return m_revision + (changed ? 1U : 0U);
    }

    auto commit(State& candidate, std::uint64_t next) noexcept -> void {
      static_assert(std::is_nothrow_swappable_v<State>);
      m_buttonRevision += sameButtons(candidate, m_state) ? 0U : 1U;
      std::swap(m_state, candidate);
      m_revision = next;
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
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "HUD was moved");
    }

    auto valid = m_impl->check();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    try {
      const auto& state = m_impl->m_state;
      HudSnapshot result{m_impl->m_scope,
                         m_impl->m_revision,
                         {},
                         {},
                         state.focused,
                         state.hovered,
                         state.pressed,
                         state.sequence,
                         m_impl->m_buttonRevision};

      for (const auto& [key, value] : state.elements) {
        if (value.button) {
          result.buttons.push_back({key, value.box, value.enabled});
        } else {
          result.labels.push_back({key, value.box});
        }
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Snapshot HUD: " + std::string(error.what()));
    }
  }

  auto Hud::buttonRevision() const -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "HUD was moved");
    }

    auto valid = m_impl->check();
    return valid ? Core::Result<std::uint64_t>{m_impl->m_buttonRevision}
                 : std::unexpected(valid.error());
  }

  auto Hud::draws() const -> Core::Result<std::vector<Graphics::TextLayer>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "HUD was moved");
    }

    auto valid = m_impl->check();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    try {
      std::vector<Graphics::TextLayer> result;
      const auto& state = m_impl->m_state;

      for (const auto& [key, element] : state.elements) {
        auto value = element.box;

        if (element.button) {
          if (!element.enabled) {
            value.color.x *= .45F;
            value.color.y *= .45F;
            value.color.z *= .45F;
          } else if (state.pressed == key) {
            value.background.x *= .6F;
            value.background.y *= .6F;
            value.background.z *= .6F;
          } else if (state.focused == key) {
            value.background = {.12F, .32F, .43F, value.background.w};
          } else if (state.hovered == key) {
            value.background.x = std::min(1.F, value.background.x + .08F);
            value.background.y = std::min(1.F, value.background.y + .08F);
            value.background.z = std::min(1.F, value.background.z + .08F);
          }
        }

        result.push_back({key, std::move(value)});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare HUD draws: " + std::string(error.what()));
    }
  }

  auto Hud::publish(const HudInput& input) -> Core::Result<HudInputResult> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "HUD was moved");
    }

    auto valid = m_impl->check(true);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    if (input.pointer
        && ((!std::isfinite(input.pointer->x) || !std::isfinite(input.pointer->y))
            || input.pointer->x < 0 || input.pointer->y < 0 || input.pointer->x >= 1
            || input.pointer->y >= 1)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "HUD pointer must be normalized [0,1)");
    }

    try {
      HudInputResult handled;

      if (std::ranges::none_of(m_impl->m_state.elements,
                               [](const auto& entry) { return entry.second.button; })) {
        m_impl->m_input = handled;
        return handled;
      }

      auto candidate = m_impl->m_state;
      candidate.hovered.reset();
      candidate.pressed.reset();

      if (!input.enabled) {
        candidate.armed.reset();
      } else {
        bool hitButton{};

        if (input.pointer) {
          /** Match ordered button drawing; disabled top buttons block lower hits.
           * Labels remain passive. 按按钮绘制顺序命中，禁用的顶层按钮阻挡下层；文字不截获指针。 */
          for (auto i = candidate.elements.rbegin(); i != candidate.elements.rend(); ++i) {
            const auto& value = i->second;
            const auto p = value.box.position;
            const auto s = value.box.size;

            if (value.button && value.box.visible && input.pointer->x >= p.x
                && input.pointer->y >= p.y && input.pointer->x < p.x + s.x
                && input.pointer->y < p.y + s.y) {
              hitButton = true;

              if (value.enabled) {
                candidate.hovered = i->first;
              }

              break;
            }
          }
        }

        handled.pointer = hitButton || candidate.armed.has_value();

        if (input.pressed) {
          candidate.armed = candidate.hovered;

          if (candidate.hovered) {
            candidate.focused = candidate.hovered;
          }
        }

        bool activated{};

        if (input.released) {
          if (candidate.armed && candidate.armed == candidate.hovered) {
            valid = Impl::activate(candidate, *candidate.armed);

            if (!valid) {
              return std::unexpected(valid.error());
            }

            activated = true;
          }

          candidate.armed.reset();
        } else if (!input.down) {
          candidate.armed.reset();
        }

        if (input.next || input.previous) {
          std::vector<std::string> keys;

          for (const auto& [key, value] : candidate.elements) {
            if (Impl::eligible(value)) {
              keys.push_back(key);
            }
          }

          if (!keys.empty()) {
            const auto found = std::ranges::find(keys, candidate.focused.value_or(""));
            const auto old = found == keys.end() ? keys.size()
                                                 : static_cast<std::size_t>(found - keys.begin());
            const auto next = input.previous
                                  ? (old == keys.size() ? keys.size() - 1
                                                        : (old + keys.size() - 1) % keys.size())
                                  : (old == keys.size() ? 0 : (old + 1) % keys.size());
            candidate.focused = keys[next];
            handled.keyboard = true;
          }
        }

        if (input.confirm && candidate.focused) {
          handled.keyboard = true;

          if (!activated) {
            valid = Impl::activate(candidate, *candidate.focused);

            if (!valid) {
              return std::unexpected(valid.error());
            }
          }
        }

        if (input.down && candidate.armed == candidate.hovered) {
          candidate.pressed = candidate.armed;
        }
      }

      auto next = m_impl->revision(candidate);

      if (!next) {
        return std::unexpected(next.error());
      }

      m_impl->commit(candidate, *next);
      m_impl->m_input = handled;
      return handled;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Publish HUD input: " + std::string(error.what()));
    }
  }

  auto Hud::retire() noexcept -> void {
    if (m_impl) {
      m_impl->m_retired = true;
    }
  }

  auto Hud::perform(std::string_view request) -> Core::Result<std::string> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "HUD was moved");
    }

    auto valid = m_impl->check();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    if (request.empty() || request.size() > 65536 || !Core::isValidUtf8(request)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid HUD request bytes");
    }

    try {
      const auto value = Codec::parseJson(request, 8);
      const auto command = value.at("command").get<std::string>();
      const auto& state = m_impl->m_state;

      if (command == "snapshot") {
        Codec::requireFields(value, {"command"});
        auto labels = Json::array();
        auto buttons = Json::array();

        for (const auto& [key, element] : state.elements) {
          auto description = json(element.box);

          if (element.button) {
            description["enabled"] = element.enabled;
          }

          (element.button ? buttons : labels)
              .push_back({{"key", key}, {"value", std::move(description)}});
        }

        return Json{{"scope", std::to_string(m_impl->m_scope)},
                    {"revision", std::to_string(m_impl->m_revision)},
                    {"labels", std::move(labels)},
                    {"buttons", std::move(buttons)},
                    {"focused", keyJson(state.focused)},
                    {"hovered", keyJson(state.hovered)},
                    {"pressed", keyJson(state.pressed)},
                    {"sequence", std::to_string(state.sequence)},
                    {"buttonRevision", std::to_string(m_impl->m_buttonRevision)}}
            .dump();
      }

      if (command == "input-state") {
        Codec::requireFields(value, {"command"});
        return Json{{"pointer", m_impl->m_input.pointer}, {"keyboard", m_impl->m_input.keyboard}}
            .dump();
      }

      if (command == "events") {
        Codec::requireFields(value, {"command", "after"});
        const auto after = value["after"] == "0" ? 0 : Codec::id(value["after"]);

        if (after > state.sequence) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "HUD event cursor is in the future");
        }

        const auto first
            = state.actions.empty() ? state.sequence + 1 : state.actions.front().sequence;
        auto events = Json::array();

        for (const auto& action : state.actions) {
          if (action.sequence > after) {
            events.push_back({{"sequence", std::to_string(action.sequence)}, {"key", action.key}});
          }
        }

        return Json{{"sequence", std::to_string(state.sequence)},
                    {"lost", after < first - 1},
                    {"events", std::move(events)}}
            .dump();
      }

      valid = m_impl->check(true);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      auto candidate = state;

      if (command == "set" || command == "set-button" || command == "remove") {
        const auto setting = command != "remove";
        Codec::requireFields(
            value, setting ? std::initializer_list<std::string_view>{"command", "key", "value"}
                           : std::initializer_list<std::string_view>{"command", "key"});
        const auto key = value["key"].get<std::string>();

        if (!validKey(key)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid HUD element key");
        }

        if (setting) {
          const auto button = command == "set-button";
          auto label = box(value["value"], button);
          valid = Graphics::validateTextBox(label);

          if (!valid) {
            return std::unexpected(valid.error());
          }

          Impl::Element next{std::move(label), button,
                             !button || value["value"]["enabled"].get<bool>()};
          const auto found = candidate.elements.find(key);

          if (found == candidate.elements.end() || found->second != next) {
            if (candidate.armed == key) {
              candidate.armed.reset();
              candidate.pressed.reset();
            }

            if (candidate.hovered == key) {
              candidate.hovered.reset();
            }
          }

          candidate.elements.insert_or_assign(key, std::move(next));
        } else {
          candidate.elements.erase(key);
        }
      } else if (command == "clear") {
        Codec::requireFields(value, {"command"});
        candidate.elements.clear();
      } else if (command == "focus") {
        Codec::requireFields(value, {"command", "key"});

        if (value["key"].is_null()) {
          candidate.focused.reset();
        } else {
          const auto key = value["key"].get<std::string>();

          if (!validKey(key) || !Impl::eligible(candidate, key)) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "HUD focus requires a visible enabled button");
          }

          candidate.focused = key;
        }
      } else if (command == "activate") {
        Codec::requireFields(value, {"command", "key"});
        const auto key = value["key"].get<std::string>();

        if (!validKey(key)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid HUD button key");
        }

        valid = Impl::activate(candidate, key);

        if (!valid) {
          return std::unexpected(valid.error());
        }
      } else {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown HUD operation");
      }

      Impl::sanitize(candidate);
      std::size_t bytes{};

      for (const auto& [key, element] : candidate.elements) {
        bytes += element.box.text.size();
      }

      if (candidate.elements.size() > 32 || bytes > 32768) {
        return Core::fail(Core::ErrorCode::Unavailable, "HUD exceeds 32 elements/32768 text bytes");
      }

      auto next = m_impl->revision(candidate);

      if (!next) {
        return std::unexpected(next.error());
      }

      auto reply = Json{{"revision", std::to_string(*next)},
                        {"sequence", std::to_string(candidate.sequence)}}
                       .dump();
      m_impl->commit(candidate, *next);
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

      for (const auto* name : {"snapshot", "set", "remove", "clear", "setButton", "focus",
                               "activate", "events", "inputState"}) {
        auto service = *this;
        const auto method = std::string(name);
        const auto readOnly = method == "snapshot" || method == "events" || method == "inputState";
        result.push_back(
            {"hud." + method,
             [service,
              method](std::string_view args) mutable -> Core::Result<std::optional<std::string>> {
               if (args.empty() || args.size() > 65536 || !Core::isValidUtf8(args)) {
                 return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid HUD argument bytes");
               }

               try {
                 const auto values = Codec::parseJson(args, 8);
                 const auto setting = method == "set" || method == "setButton";
                 const auto count = setting ? 2U
                                    : method == "remove" || method == "focus"
                                            || method == "activate" || method == "events"
                                        ? 1U
                                        : 0U;

                 if (!values.is_array() || values.size() != count) {
                   return Core::fail(Core::ErrorCode::InvalidArgument, "Incorrect HUD arguments");
                 }

                 Json request{{"command", method == "setButton"    ? "set-button"
                                          : method == "inputState" ? "input-state"
                                                                   : method}};

                 if (count) {
                   request[method == "events" ? "after" : "key"] = values[0];
                 }

                 if (setting) {
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
             readOnly ? HostAccess::ReadOnly : HostAccess::ReadWrite});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "HUD bindings: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Script

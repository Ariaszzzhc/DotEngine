module;

#include <algorithm>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>

#include "../UI/TextJson.hpp"

module Dot.Editor.UiSession;

import Dot.UI.Files;
import Dot.Math;
import Dot.Script.Hud;
import Dot.Core.Text;

namespace Dot::Editor {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
  }  // namespace

  struct UiSession::Impl {
    struct Gesture {
      std::uint64_t id;
      std::string key;
    };

    UiDocument m_document;
    Script::Hud m_preview;
    UI::Layout m_layout;
    Json m_original;
    Json m_form;
    std::string m_path;
    std::uint64_t m_buffer;
    std::uint64_t m_generation{1};
    std::uint64_t m_nextGesture{1};
    std::optional<std::string> m_selected;
    std::optional<Gesture> m_gesture;
    bool m_showHidden{};
    std::thread::id m_thread{std::this_thread::get_id()};

    Impl(UiDocument document, Script::Hud preview, UI::Layout layout, Json form, std::string path,
         std::uint64_t buffer)
        : m_document(std::move(document)),
          m_preview(std::move(preview)),
          m_layout(std::move(layout)),
          m_original(form),
          m_form(std::move(form)),
          m_path(std::move(path)),
          m_buffer(buffer) {
    }

    ~Impl() {
      m_preview.retire();
    }

    auto check() const -> Core::Result<void> {
      return m_thread == std::this_thread::get_id()
                 ? Core::Result<void>{}
                 : Core::fail(Core::ErrorCode::InvalidState,
                              "UI session requires its owner thread");
    }

    auto preview(const UI::Layout& value, bool showHidden) const -> Core::Result<Script::Hud> {
      auto hud = Script::Hud::create(m_buffer, UI::Asset{m_path, value});

      if (!hud) {
        return std::unexpected(hud.error());
      }

      if (showHidden) {
        auto patches = Json::array();

        for (const auto& node : value.description().value()->nodes) {
          patches.push_back({{"key", node.key}, {"visible", true}});
        }

        auto changed
            = hud->perform(Json{{"command", "ui-update"}, {"changes", std::move(patches)}}.dump());

        if (!changed) {
          return std::unexpected(changed.error());
        }
      }

      return hud;
    }

    auto replacePreview(Script::Hud preview, UI::Layout layout) noexcept -> void {
      m_preview.retire();
      m_preview = std::move(preview);
      m_layout = std::move(layout);
      ++m_generation;
    }

    auto publish(UI::Layout layout, std::string_view operation, std::uint64_t revision)
        -> Core::Result<void> {
      auto encoded = UI::encodeLayout(layout);

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      auto form = Codec::parseJson(*encoded);

      if (form == m_original) {
        m_form.swap(form);
        return {};
      }

      auto hud = preview(layout, m_showHidden);

      if (!hud) {
        return std::unexpected(hud.error());
      }

      auto original = form;
      auto changed = operation == "undo-ui"   ? m_document.undo(revision)
                     : operation == "redo-ui" ? m_document.redo(revision)
                                              : m_document.replace(layout, revision);

      if (!changed) {
        return changed;
      }

      m_form.swap(form);
      m_original.swap(original);
      replacePreview(std::move(*hud), std::move(layout));

      if (m_selected
          && std::ranges::none_of(m_layout.description().value()->nodes,
                                  [&](const auto& node) { return node.key == *m_selected; })) {
        m_selected.reset();
      }

      return {};
    }

    auto stage(Json form) -> void {
      auto selected = m_selected;

      if (selected && std::ranges::count_if(form["nodes"], [&](const auto& node) {
                        return node["key"] == *selected;
                      }) != 1) {
        selected.reset();
      }

      m_form.swap(form);
      m_selected.swap(selected);
    }

    auto status() -> Json {
      const auto state = m_document.snapshot().value();
      return {
          {"buffer", std::to_string(m_buffer)},
          {"revision", std::to_string(state.revision)},
          {"path", state.path},
          {"dirty", state.dirty},
          {"canUndo", state.canUndo},
          {"canRedo", state.canRedo},
          {"historyStates", state.historyStates},
          {"historyBytes", state.historyBytes},
          {"value", m_original},
          {"form", m_form},
          {"pendingForm", m_form != m_original},
          {"selected", m_selected ? Json(*m_selected) : Json(nullptr)},
          {"showHidden", m_showHidden},
          {"generation", std::to_string(m_generation)},
          {"gesture", m_gesture ? Json(std::to_string(m_gesture->id)) : Json(nullptr)},
          {"preview", Codec::parseJson(m_preview.perform(R"({"command":"ui-snapshot"})").value())}};
    }
  };

  UiSession::UiSession(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  UiSession::UiSession(UiSession&&) noexcept = default;

  auto UiSession::operator=(UiSession&&) noexcept -> UiSession& = default;

  UiSession::~UiSession() = default;

  auto UiSession::handles(std::string_view command) noexcept -> bool {
    constexpr std::string_view commands[]{"inspect-ui",
                                          "stage-ui",
                                          "apply-ui",
                                          "reset-ui-form",
                                          "undo-ui",
                                          "redo-ui",
                                          "save-ui",
                                          "close-ui",
                                          "select-ui-node",
                                          "pick-ui-node",
                                          "ui-preview-options",
                                          "begin-ui-rect",
                                          "update-ui-rect",
                                          "finish-ui-rect",
                                          "add-ui-node",
                                          "remove-ui-node",
                                          "rename-ui-node",
                                          "reparent-ui-node",
                                          "reorder-ui-node",
                                          "set-ui-node-kind"};
    return std::ranges::find(commands, command) != std::ranges::end(commands);
  }

  auto UiSession::open(const Project::Project& project, std::string_view path, std::uint64_t buffer)
      -> Core::Result<UiSession> {
    if (!buffer || buffer == std::numeric_limits<std::uint64_t>::max()) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "UI session requires a valid buffer");
    }

    try {
      auto document = UiDocument::open(project, path);

      if (!document) {
        return std::unexpected(document.error());
      }

      const auto state = document->snapshot().value();
      auto preview = Script::Hud::create(buffer, UI::Asset{state.path, state.value});

      if (!preview) {
        return std::unexpected(preview.error());
      }

      auto form = Codec::parseJson(UI::encodeLayout(state.value).value());
      return UiSession(std::make_unique<Impl>(std::move(*document), std::move(*preview),
                                              state.value, std::move(form), state.path, buffer));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Open UI session: " + std::string(error.what()));
    }
  }

  auto UiSession::snapshot() const -> Core::Result<std::string> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "UI session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      return m_impl->status().dump();
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Inspect UI session: " + std::string(error.what()));
    }
  }

  auto UiSession::draws() const -> Core::Result<std::vector<Graphics::TextLayer>> {
    return m_impl ? m_impl->m_preview.draws()
                  : Core::fail(Core::ErrorCode::InvalidState, "UI session was moved");
  }

  auto UiSession::generation() const -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "UI session was moved");
    }

    auto checked = m_impl->check();
    return checked ? Core::Result<std::uint64_t>{m_impl->m_generation}
                   : std::unexpected(checked.error());
  }

  auto UiSession::pending() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "UI session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    const auto state = m_impl->m_document.snapshot();
    return state ? Core::Result<bool>{state->dirty || m_impl->m_form != m_impl->m_original
                                      || m_impl->m_gesture.has_value()}
                 : std::unexpected(state.error());
  }

  auto UiSession::perform(std::string_view request) -> Core::Result<std::string> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "UI session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (request.empty() || request.size() > 1024 * 1024 || !Core::isValidUtf8(request)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid UI authoring request bytes");
    }

    try {
      auto& impl = *m_impl;
      const auto value = Codec::parseJson(request, 16);
      const auto command = value.at("command").get<std::string>();
      const auto state = impl.m_document.snapshot().value();

      if (Codec::id(value.at("buffer")) != impl.m_buffer
          || Codec::id(value.at("revision")) != state.revision) {
        return Core::fail(Core::ErrorCode::InvalidState, "UI buffer/revision is stale");
      }

      if (command == "stage-ui") {
        Codec::requireFields(value, {"command", "buffer", "revision", "value"});
      } else if (command == "close-ui") {
        Codec::requireFields(value, {"command", "buffer", "revision", "discard"});
      } else if (command == "finish-ui-rect") {
        Codec::requireFields(value, {"command", "buffer", "revision", "gesture", "commit"});
      } else if (command == "add-ui-node") {
        Codec::requireFields(value, {"command", "buffer", "revision", "kind", "parent"});
      } else if (command == "rename-ui-node") {
        Codec::requireFields(value, {"command", "buffer", "revision", "key", "name"});
      } else if (command == "reparent-ui-node") {
        Codec::requireFields(value, {"command", "buffer", "revision", "key", "parent"});
      } else if (command == "reorder-ui-node") {
        Codec::requireFields(value, {"command", "buffer", "revision", "key", "direction"});
      } else if (command == "set-ui-node-kind") {
        Codec::requireFields(value, {"command", "buffer", "revision", "key", "kind"});
      } else if (command == "select-ui-node" || command == "begin-ui-rect"
                 || command == "remove-ui-node") {
        Codec::requireFields(value, {"command", "buffer", "revision", "key"});
      } else if (command == "pick-ui-node") {
        Codec::requireFields(value, {"command", "buffer", "revision", "generation", "uv"});
      } else if (command == "update-ui-rect") {
        Codec::requireFields(value, {"command", "buffer", "revision", "gesture", "value"});
      } else if (command == "ui-preview-options") {
        Codec::requireFields(value, {"command", "buffer", "revision", "showHidden"});
      } else {
        Codec::requireFields(value, {"command", "buffer", "revision"});
      }

      if (impl.m_gesture && command != "inspect-ui" && command != "update-ui-rect"
          && command != "finish-ui-rect") {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Finish or cancel the UI rectangle gesture first");
      }

      if (command != "inspect-ui"
          && impl.m_generation >= std::numeric_limits<std::uint64_t>::max() - 1) {
        return Core::fail(Core::ErrorCode::Unavailable, "UI preview generation exhausted");
      }

      if (command == "add-ui-node" || command == "remove-ui-node" || command == "rename-ui-node"
          || command == "reparent-ui-node" || command == "reorder-ui-node"
          || command == "set-ui-node-kind") {
        auto form = impl.m_form;
        auto& nodes = form["nodes"];
        auto selected = impl.m_selected;
        const auto find = [&](const Json& key) {
          return std::ranges::find_if(nodes, [&](const auto& node) { return node["key"] == key; });
        };
        const auto parentValid = [&](const Json& parent) {
          if (parent.is_null()) {
            return true;
          }

          const auto text = parent.get<std::string>();
          const auto matches
              = std::ranges::count_if(nodes, [&](const auto& node) { return node["key"] == text; });
          return matches == 1 && (*find(parent))["kind"] == "panel";
        };
        const auto kindValue = [&]() -> UI::Kind {
          const auto kind = value["kind"].get<std::string>();

          if (kind == "panel") {
            return UI::Kind::Panel;
          }

          if (kind == "label") {
            return UI::Kind::Label;
          }

          if (kind == "button") {
            return UI::Kind::Button;
          }

          throw std::invalid_argument("Unknown UI node kind");
        };

        if (command == "add-ui-node") {
          if (nodes.size() >= 32 || !parentValid(value["parent"])) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "UI add requires capacity and a panel/canvas parent");
          }

          std::size_t next{};
          std::string key;

          do {
            key = "node-" + std::to_string(++next);
          } while (find(key) != nodes.end());

          const auto kind = kindValue();
          UI::Node node{key, {}, kind};

          if (kind == UI::Kind::Panel) {
            node.box.position = {.25F, .25F};
            node.box.size = {.5F, .5F};
          } else if (!value["parent"].is_null()) {
            node.box.position = {.08F, .1F};
            node.box.size = {.84F, .3F};
          }

          node.box.text = kind == UI::Kind::Label    ? "Label"
                          : kind == UI::Kind::Button ? "Button"
                                                     : "";
          node.blocksPointer = kind != UI::Kind::Label;
          nodes.push_back({{"key", key},
                           {"parent", value["parent"]},
                           {"kind", value["kind"]},
                           {"box", UI::Detail::textJson(node.box)},
                           {"enabled", true},
                           {"blocksPointer", node.blocksPointer}});
          selected = key;
        } else {
          const auto key = value["key"].get<std::string>();

          if (std::ranges::count_if(nodes, [&](const auto& node) { return node["key"] == key; })
              != 1) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "UI node operation requires one matching key");
          }

          const auto found = find(key);

          if (command == "rename-ui-node") {
            const auto name = value["name"].get<std::string>();

            if (!UI::validKey(name) || (name != key && find(name) != nodes.end())) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "UI rename requires a unique valid key");
            }

            (*found)["key"] = name;

            for (auto& node : nodes) {
              if (node["parent"] == key) {
                node["parent"] = name;
              }
            }

            if (selected == key) {
              selected = name;
            }
          } else if (command == "remove-ui-node") {
            std::set<std::string> removed{key};
            bool changed;

            do {
              changed = false;

              for (const auto& node : nodes) {
                if (!node["parent"].is_null()
                    && removed.contains(node["parent"].get<std::string>())) {
                  changed = removed.insert(node["key"].get<std::string>()).second || changed;
                }
              }
            } while (changed);

            std::erase_if(nodes.get_ref<Json::array_t&>(), [&](const auto& node) {
              return removed.contains(node["key"].template get<std::string>());
            });

            if (selected && removed.contains(*selected)) {
              selected.reset();
            }
          } else if (command == "reparent-ui-node") {
            if (!parentValid(value["parent"])) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "UI parent must be a unique panel/canvas");
            }

            auto parent = value["parent"];
            std::set<std::string> ancestors{key};

            while (!parent.is_null()) {
              const auto name = parent.get<std::string>();

              if (!ancestors.insert(name).second) {
                return Core::fail(Core::ErrorCode::InvalidArgument,
                                  "UI parenting would create a cycle");
              }

              const auto ancestor = find(parent);

              if (ancestor == nodes.end()) {
                return Core::fail(Core::ErrorCode::InvalidArgument, "UI ancestor is missing");
              }

              parent = (*ancestor)["parent"];
            }

            (*found)["parent"] = value["parent"];
          } else if (command == "set-ui-node-kind") {
            const auto kind = kindValue();

            if (kind != UI::Kind::Panel && std::ranges::any_of(nodes, [&](const auto& node) {
                  return node["parent"] == key;
                })) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "A UI parent with children must remain a panel");
            }

            (*found)["kind"] = value["kind"];

            if (kind == UI::Kind::Panel) {
              (*found)["box"]["text"] = "";
            }

            if (kind == UI::Kind::Button) {
              (*found)["blocksPointer"] = true;
            }
          } else {
            const auto direction = value["direction"].get<std::string>();

            if (direction != "earlier" && direction != "later") {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "UI order direction must be earlier/later");
            }

            const auto index = static_cast<std::size_t>(found - nodes.begin());
            auto sibling = index;

            while (direction == "earlier" ? sibling > 0 : sibling + 1 < nodes.size()) {
              sibling = direction == "earlier" ? sibling - 1 : sibling + 1;

              if (nodes[sibling]["parent"] == (*found)["parent"]) {
                std::swap(nodes[sibling], nodes[index]);
                break;
              }
            }
          }
        }

        impl.m_form.swap(form);
        impl.m_selected.swap(selected);
      } else if (command == "stage-ui") {
        auto form = UI::decodeLayoutForm(value["value"].dump());

        if (!form) {
          return std::unexpected(form.error());
        }

        impl.stage(value["value"]);
      } else if (command == "reset-ui-form") {
        impl.stage(impl.m_original);
      } else if (command == "apply-ui" || command == "undo-ui" || command == "redo-ui") {
        if (command != "apply-ui" && impl.m_form != impl.m_original) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Apply or reset the UI form before history");
        }

        auto layout = command == "apply-ui"
                          ? UI::decodeLayout(impl.m_form.dump())
                          : impl.m_document.historyValue(command == "redo-ui", state.revision);

        if (!layout) {
          return std::unexpected(layout.error());
        }

        checked = impl.publish(std::move(*layout), command, state.revision);
      } else if (command == "save-ui") {
        if (impl.m_form != impl.m_original) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Apply or reset the UI form before saving");
        }

        checked = impl.m_document.save(state.revision);
      } else if (command == "close-ui") {
        if (pending().value() && !value["discard"].get<bool>()) {
          return Core::fail(Core::ErrorCode::InvalidState, "Save or explicitly discard UI changes");
        }

        (void)value["discard"].get<bool>();
      } else if (command == "select-ui-node" || command == "begin-ui-rect") {
        const auto key = value["key"].is_null() ? std::optional<std::string>{}
                                                : std::optional{value["key"].get<std::string>()};

        if (key && std::ranges::count_if(impl.m_form["nodes"], [&](const auto& node) {
                     return node["key"] == *key;
                   }) != 1) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown UI node");
        }

        if (command == "begin-ui-rect") {
          if (!key || impl.m_form != impl.m_original
              || impl.m_nextGesture >= std::numeric_limits<std::uint64_t>::max() - 1) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "UI gesture requires an applied node/current form");
          }

          impl.m_gesture = Impl::Gesture{impl.m_nextGesture++, *key};
        }

        impl.m_selected = key;
      } else if (command == "ui-preview-options") {
        const auto visible = value["showHidden"].get<bool>();

        if (visible != impl.m_showHidden) {
          auto preview = impl.preview(impl.m_layout, visible);

          if (!preview) {
            return std::unexpected(preview.error());
          }

          impl.replacePreview(std::move(*preview), impl.m_layout);
          impl.m_showHidden = visible;
        }
      } else if (command == "pick-ui-node") {
        if (Codec::id(value["generation"]) != impl.m_generation) {
          return Core::fail(Core::ErrorCode::InvalidState, "UI pick generation is stale");
        }

        const auto uv = UI::Detail::xy<Math::Vec2>(value["uv"]);
        auto layout = impl.m_layout;

        if (impl.m_showHidden) {
          std::vector<UI::NodeOverride> changes;

          for (const auto& node : layout.description().value()->nodes) {
            changes.push_back({node.key, {}, true, {}});
          }

          layout = layout.updated(changes).value();
        }

        auto hit = layout.pick(uv);

        if (!hit) {
          return std::unexpected(hit.error());
        }

        impl.m_selected = std::move(*hit);
      } else if (command == "update-ui-rect" || command == "finish-ui-rect") {
        if (!impl.m_gesture || Codec::id(value["gesture"]) != impl.m_gesture->id) {
          return Core::fail(Core::ErrorCode::InvalidState, "UI gesture is stale");
        }

        if (command == "update-ui-rect") {
          Codec::requireFields(value["value"], {"position", "size"});
          auto form = impl.m_form;
          auto found = std::ranges::find_if(
              form["nodes"], [&](const auto& node) { return node["key"] == impl.m_gesture->key; });
          (*found)["box"]["position"] = value["value"]["position"];
          (*found)["box"]["size"] = value["value"]["size"];
          auto layout = UI::decodeLayout(form.dump());

          if (!layout) {
            return std::unexpected(layout.error());
          }

          if (form != impl.m_form) {
            auto preview = impl.preview(*layout, impl.m_showHidden);

            if (!preview) {
              return std::unexpected(preview.error());
            }

            impl.replacePreview(std::move(*preview), std::move(*layout));
            impl.m_form.swap(form);
          }
        } else {
          const auto commit = value.at("commit").get<bool>();
          auto layout = UI::decodeLayout((commit ? impl.m_form : impl.m_original).dump());

          if (!layout) {
            return std::unexpected(layout.error());
          }

          if (commit) {
            checked = impl.publish(std::move(*layout), "apply-ui", state.revision);
          } else {
            auto preview = impl.preview(*layout, impl.m_showHidden);

            if (!preview) {
              return std::unexpected(preview.error());
            }

            auto form = impl.m_original;
            impl.replacePreview(std::move(*preview), std::move(*layout));
            impl.m_form.swap(form);
          }

          if (checked) {
            impl.m_gesture.reset();
          }
        }
      } else if (command != "inspect-ui") {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown UI authoring operation");
      }

      return checked ? snapshot() : std::unexpected(checked.error());
    } catch (const std::exception& error) {
      return Core::fail(dynamic_cast<const std::invalid_argument*>(&error)
                                || dynamic_cast<const Json::exception*>(&error)
                            ? Core::ErrorCode::InvalidArgument
                            : Core::ErrorCode::External,
                        "UI authoring: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Editor

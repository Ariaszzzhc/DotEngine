module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Dot.UI.Layout;

import Dot.Core.Text;

namespace Dot::UI {
  struct Layout::Data {
    Description description;
    std::map<std::string, std::size_t, std::less<>> indices;
    std::vector<std::size_t> order;
    std::vector<Graphics::TextBox> rectangles;
  };

  struct Layout::Values {
    struct Entry {
      std::string text;
      bool visible;
      bool enabled;
      auto operator==(const Entry&) const -> bool = default;
    };

    std::vector<Entry> entries;
    auto operator==(const Values&) const -> bool = default;
  };

  auto validKey(std::string_view key) noexcept -> bool {
    return !key.empty() && key.size() <= 64 && std::ranges::all_of(key, [](unsigned char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'
             || c == '-';
    });
  }

  Layout::Layout(std::shared_ptr<const Data> data, std::shared_ptr<const Values> values) noexcept
      : m_data(std::move(data)), m_values(std::move(values)) {
  }

  Layout::Layout(const Layout&) = default;

  auto Layout::operator=(const Layout&) -> Layout& = default;

  Layout::Layout(Layout&&) noexcept = default;

  auto Layout::operator=(Layout&& other) noexcept -> Layout& {
    if (this != &other) {
      m_data = std::move(other.m_data);
      m_values = std::move(other.m_values);
    }

    return *this;
  }

  Layout::~Layout() = default;

  auto Layout::create(Description description) -> Core::Result<Layout> {
    if (description.name.empty() || description.name.size() > 256
        || description.name.find('\0') != std::string::npos || !Core::isValidUtf8(description.name)
        || description.nodes.size() > 32) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid UI name/node capacity");
    }

    try {
      auto data = std::make_shared<Data>();
      auto values = std::make_shared<Values>();
      std::size_t bytes{};

      for (const auto& node : description.nodes) {
        if (!validKey(node.key) || !data->indices.emplace(node.key, values->entries.size()).second
            || node.kind < Kind::Panel || node.kind > Kind::Button
            || (node.kind == Kind::Panel && !node.box.text.empty())
            || (node.kind == Kind::Button && !node.blocksPointer)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid/duplicate UI node: " + node.key);
        }

        auto valid = Graphics::validateTextBox(node.box);

        if (!valid) {
          return Core::fail(valid.error().code,
                            "UI node " + node.key + ": " + valid.error().message);
        }

        bytes += node.box.text.size();
        values->entries.push_back({node.box.text, node.box.visible, node.enabled});
      }

      if (bytes > 32768) {
        return Core::fail(Core::ErrorCode::Unavailable, "UI exceeds 32768 text bytes");
      }

      std::vector<std::vector<std::size_t>> children(description.nodes.size());
      std::vector<std::size_t> roots;

      for (std::size_t i = 0; i < description.nodes.size(); ++i) {
        const auto& node = description.nodes[i];

        if (!node.parent) {
          roots.push_back(i);
          continue;
        }

        const auto parent = data->indices.find(*node.parent);

        if (parent == data->indices.end() || parent->second == i
            || description.nodes[parent->second].kind != Kind::Panel) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid UI parent for: " + node.key);
        }

        children[parent->second].push_back(i);
      }

      std::array<bool, 32> visited{};
      data->rectangles.resize(description.nodes.size());
      std::function<Core::Result<void>(std::size_t, std::size_t, const Graphics::TextBox&)> visit;
      visit = [&](std::size_t index, std::size_t depth,
                  const Graphics::TextBox& parent) -> Core::Result<void> {
        if (visited[index] || depth > 8) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "UI exceeds eight levels or has a cycle");
        }

        visited[index] = true;
        const auto& node = description.nodes[index];
        auto box = node.box;
        const auto left = static_cast<double>(parent.position.x)
                          + static_cast<double>(parent.size.x) * box.position.x;
        const auto top = static_cast<double>(parent.position.y)
                         + static_cast<double>(parent.size.y) * box.position.y;
        const auto right = static_cast<double>(parent.position.x)
                           + static_cast<double>(parent.size.x)
                                 * (static_cast<double>(box.position.x) + box.size.x);
        const auto bottom = static_cast<double>(parent.position.y)
                            + static_cast<double>(parent.size.y)
                                  * (static_cast<double>(box.position.y) + box.size.y);
        box.position = {static_cast<float>(left), static_cast<float>(top)};
        box.size = {static_cast<float>(right) - box.position.x,
                    static_cast<float>(bottom) - box.position.y};
        auto valid = Graphics::validateTextBox(box);

        if (!valid) {
          return Core::fail(valid.error().code,
                            "Unrepresentable resolved UI rectangle: " + node.key);
        }

        data->rectangles[index] = box;
        data->order.push_back(index);

        for (const auto child : children[index]) {
          auto nested = visit(child, depth + 1, box);

          if (!nested) {
            return nested;
          }
        }

        return {};
      };
      Graphics::TextBox canvas;
      canvas.position = {0, 0};
      canvas.size = {1, 1};

      for (const auto root : roots) {
        auto valid = visit(root, 1, canvas);

        if (!valid) {
          return std::unexpected(valid.error());
        }
      }

      if (data->order.size() != description.nodes.size()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "UI contains a parent cycle");
      }

      data->description = std::move(description);
      return Layout(std::move(data), std::move(values));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create UI layout: " + std::string(error.what()));
    }
  }

  auto Layout::description() const -> Core::Result<const Description*> {
    return m_data ? Core::Result<const Description*>{&m_data->description}
                  : Core::fail(Core::ErrorCode::InvalidState, "UI layout is empty or moved");
  }

  auto Layout::resolve() const -> Core::Result<std::vector<ResolvedNode>> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "UI layout is empty or moved");
    }

    try {
      std::vector<ResolvedNode> result;
      std::array<bool, 32> visible{}, enabled{};
      result.reserve(m_data->order.size());

      for (const auto index : m_data->order) {
        auto local = m_data->description.nodes[index];
        const auto& value = m_values->entries[index];
        local.box.text = value.text;
        local.box.visible = value.visible;
        local.enabled = value.enabled;
        auto box = m_data->rectangles[index];
        box.text = value.text;
        visible[index] = value.visible;
        enabled[index] = value.enabled;

        if (local.parent) {
          const auto parent = m_data->indices.at(*local.parent);
          visible[index] = visible[index] && visible[parent];
          enabled[index] = enabled[index] && enabled[parent];
        }

        box.visible = visible[index];
        result.push_back({std::move(local), std::move(box), enabled[index]});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Resolve UI layout: " + std::string(error.what()));
    }
  }

  auto Layout::updated(std::span<const NodeOverride> changes) const -> Core::Result<Layout> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "UI layout is empty or moved");
    }

    if (changes.size() > 32) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "UI override capacity exceeded");
    }

    try {
      std::array<bool, 32> assigned{};
      bool changed{};

      for (const auto& patch : changes) {
        const auto found = m_data->indices.find(patch.key);

        if (found == m_data->indices.end() || assigned[found->second]) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Unknown/duplicate UI override: " + patch.key);
        }

        assigned[found->second] = true;
        const auto& old = m_values->entries[found->second];
        changed = changed || (patch.text && *patch.text != old.text)
                  || (patch.visible && *patch.visible != old.visible)
                  || (patch.enabled && *patch.enabled != old.enabled);
      }

      if (!changed) {
        return *this;
      }

      auto values = std::make_shared<Values>(*m_values);

      for (const auto& patch : changes) {
        const auto index = m_data->indices.at(patch.key);
        auto& value = values->entries[index];

        if (patch.text) {
          auto box = m_data->description.nodes[index].box;
          box.text = *patch.text;
          auto valid = Graphics::validateTextBox(box);

          if (!valid
              || (m_data->description.nodes[index].kind == Kind::Panel && !patch.text->empty())) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Invalid UI text override: " + patch.key);
          }

          value.text = *patch.text;
        }

        if (patch.visible) {
          value.visible = *patch.visible;
        }

        if (patch.enabled) {
          value.enabled = *patch.enabled;
        }
      }

      std::size_t bytes{};

      for (const auto& value : values->entries) {
        bytes += value.text.size();
      }

      if (bytes > 32768) {
        return Core::fail(Core::ErrorCode::Unavailable, "UI overrides exceed 32768 text bytes");
      }

      return Layout(m_data, std::move(values));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Override UI layout: " + std::string(error.what()));
    }
  }

  auto Layout::reset() const -> Core::Result<Layout> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "UI layout is empty or moved");
    }

    try {
      auto values = std::make_shared<Values>();

      for (const auto& node : m_data->description.nodes) {
        values->entries.push_back({node.box.text, node.box.visible, node.enabled});
      }

      return *values == *m_values ? Core::Result<Layout>{*this}
                                  : Core::Result<Layout>{Layout(m_data, std::move(values))};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Reset UI layout: " + std::string(error.what()));
    }
  }

  auto Layout::pick(Math::Vec2 point) const -> Core::Result<std::optional<std::string>> {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0 || point.y < 0
        || point.x >= 1 || point.y >= 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "UI pick point requires normalized [0,1)");
    }

    auto nodes = resolve();

    if (!nodes) {
      return std::unexpected(nodes.error());
    }

    for (auto node = nodes->rbegin(); node != nodes->rend(); ++node) {
      const auto& box = node->box;

      if (box.visible && point.x >= box.position.x && point.y >= box.position.y
          && point.x < box.position.x + box.size.x && point.y < box.position.y + box.size.y) {
        return std::optional{node->local.key};
      }
    }

    return std::optional<std::string>{};
  }

  auto Layout::operator==(const Layout& other) const -> bool {
    if (!m_data || !other.m_data) {
      return m_data == other.m_data;
    }

    return (m_data == other.m_data || m_data->description == other.m_data->description)
           && (m_values == other.m_values || *m_values == *other.m_values);
  }
}  // namespace Dot::UI

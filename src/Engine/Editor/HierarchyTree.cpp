module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

module Dot.Editor.HierarchyTree;

import Dot.Core.Text;

namespace Dot::Editor {
  struct HierarchyTree::Impl {
    std::vector<HierarchyNode> nodes;
    std::vector<std::size_t> roots;
    std::vector<std::optional<std::size_t>> parents;
    std::vector<std::size_t> order;
  };

  HierarchyTree::HierarchyTree() noexcept = default;

  HierarchyTree::HierarchyTree(std::shared_ptr<const Impl> impl) noexcept
      : m_impl(std::move(impl)) {
  }

  HierarchyTree::HierarchyTree(const HierarchyTree&) noexcept = default;

  auto HierarchyTree::operator=(const HierarchyTree&) noexcept -> HierarchyTree& = default;

  HierarchyTree::HierarchyTree(HierarchyTree&&) noexcept = default;

  auto HierarchyTree::operator=(HierarchyTree&&) noexcept -> HierarchyTree& = default;

  HierarchyTree::~HierarchyTree() = default;

  auto HierarchyTree::create(std::vector<HierarchyItem> items) -> Core::Result<HierarchyTree> {
    if (items.size() > 10000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Hierarchy exceeds 10000 nodes");
    }

    try {
      auto impl = std::make_shared<Impl>();
      impl->nodes.reserve(items.size());
      impl->parents.resize(items.size());
      impl->order.reserve(items.size());
      std::unordered_map<std::uint64_t, std::size_t> lookup;
      lookup.reserve(items.size());

      for (auto& item : items) {
        if (!item.id || item.name.empty() || item.name.size() > 256
            || item.name.find('\0') != std::string::npos || !Core::isValidUtf8(item.name)
            || !lookup.emplace(item.id, impl->nodes.size()).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid hierarchy ID/name");
        }

        auto key = std::to_string(item.id);
        impl->nodes.push_back({std::move(item), std::move(key), {}});
      }

      for (std::size_t i = 0; i < impl->nodes.size(); ++i) {
        const auto parent = impl->nodes[i].item.parent;

        if (!parent) {
          impl->roots.push_back(i);
          continue;
        }

        const auto found = lookup.find(*parent);

        if (found == lookup.end() || found->second == i) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Missing/self hierarchy parent");
        }

        impl->parents[i] = found->second;
        impl->nodes[found->second].children.push_back(i);
      }

      std::vector<std::pair<std::size_t, std::size_t>> pending;
      pending.reserve(items.size());

      for (auto root = impl->roots.rbegin(); root != impl->roots.rend(); ++root) {
        pending.emplace_back(*root, 0);
      }

      while (!pending.empty()) {
        const auto [index, depth] = pending.back();
        pending.pop_back();

        if (depth >= 256) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Hierarchy exceeds 256 levels");
        }

        impl->order.push_back(index);
        const auto& children = impl->nodes[index].children;

        for (auto child = children.rbegin(); child != children.rend(); ++child) {
          pending.emplace_back(*child, depth + 1);
        }
      }

      if (impl->order.size() != impl->nodes.size()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Hierarchy contains a cycle");
      }

      return HierarchyTree(std::move(impl));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Build hierarchy: " + std::string(error.what()));
    }
  }

  auto HierarchyTree::nodes() const noexcept -> std::span<const HierarchyNode> {
    return m_impl ? std::span<const HierarchyNode>(m_impl->nodes)
                  : std::span<const HierarchyNode>{};
  }

  auto HierarchyTree::roots() const noexcept -> std::span<const std::size_t> {
    return m_impl ? std::span<const std::size_t>(m_impl->roots) : std::span<const std::size_t>{};
  }

  auto HierarchyTree::filter(std::string_view query) const -> Core::Result<std::vector<bool>> {
    if (query.size() > 128 || query.find('\0') != std::string_view::npos
        || !Core::isValidUtf8(query)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid hierarchy search");
    }

    try {
      std::vector<bool> result(nodes().size(), query.empty());

      if (!m_impl || query.empty()) {
        return result;
      }

      const auto folded = [](char value) {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
      };

      for (auto iterator = m_impl->order.rbegin(); iterator != m_impl->order.rend(); ++iterator) {
        const auto index = *iterator;
        const auto& node = m_impl->nodes[index];
        result[index] = result[index]
                        || std::ranges::search(
                               node.item.name, query,
                               [&](char a, char b) {
                                 return folded(a) == folded(b);
                               }).size()
                               == query.size();

        if (result[index] && m_impl->parents[index]) {
          result[*m_impl->parents[index]] = true;
        }
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Filter hierarchy: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Editor

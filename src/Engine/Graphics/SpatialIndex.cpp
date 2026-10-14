module;

#include <algorithm>
#include <array>
#include <exception>
#include <expected>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Dot.Graphics.SpatialIndex;

import Dot.Math;

namespace Dot::Graphics {
  namespace {
    auto valid(const Bounds& bounds) -> bool {
      return Math::isFinite(bounds.minimum) && Math::isFinite(bounds.maximum)
             && bounds.minimum.x <= bounds.maximum.x && bounds.minimum.y <= bounds.maximum.y
             && bounds.minimum.z <= bounds.maximum.z;
    }

    auto merge(const Bounds& a, const Bounds& b) -> Bounds {
      return {{std::min(a.minimum.x, b.minimum.x), std::min(a.minimum.y, b.minimum.y),
               std::min(a.minimum.z, b.minimum.z)},
              {std::max(a.maximum.x, b.maximum.x), std::max(a.maximum.y, b.maximum.y),
               std::max(a.maximum.z, b.maximum.z)}};
    }

    auto same(const Bounds& a, const Bounds& b) -> bool {
      return a.minimum == b.minimum && a.maximum == b.maximum;
    }
  }  // namespace

  auto SpatialIndex::update(std::span<const Bounds> bounds) -> Core::Result<SpatialUpdate> {
    if (bounds.size() > 10000 || !std::ranges::all_of(bounds, valid)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid spatial bounds/count");
    }

    try {
      SpatialUpdate result;

      if (bounds.size() != m_bounds.size()) {
        SpatialIndex next;
        next.m_bounds.assign(bounds.begin(), bounds.end());
        next.m_order.resize(bounds.size());
        next.m_leaves.resize(bounds.size());
        std::iota(next.m_order.begin(), next.m_order.end(), 0U);
        next.m_nodes.reserve(bounds.size() * 2);
        const auto build = [&](auto&& self, std::uint32_t first, std::uint32_t count,
                               std::uint32_t parent) -> std::uint32_t {
          auto box = next.m_bounds[next.m_order[first]];

          for (std::uint32_t i = 1; i < count; ++i) {
            box = merge(box, next.m_bounds[next.m_order[first + i]]);
          }

          const auto index = static_cast<std::uint32_t>(next.m_nodes.size());
          next.m_nodes.push_back({box, first, count, 0, 0, parent});

          if (count <= 8) {
            for (std::uint32_t i = 0; i < count; ++i) {
              next.m_leaves[next.m_order[first + i]] = index;
            }

            return index;
          }

          const std::array extents{static_cast<double>(box.maximum.x) - box.minimum.x,
                                   static_cast<double>(box.maximum.y) - box.minimum.y,
                                   static_cast<double>(box.maximum.z) - box.minimum.z};
          const auto axis
              = static_cast<std::size_t>(std::ranges::max_element(extents) - extents.begin());
          const auto center = [&](std::uint32_t item) {
            const auto& b = next.m_bounds[item];
            const std::array values{static_cast<double>(b.minimum.x) + b.maximum.x,
                                    static_cast<double>(b.minimum.y) + b.maximum.y,
                                    static_cast<double>(b.minimum.z) + b.maximum.z};
            return values[axis];
          };
          const auto middle = first + count / 2;
          std::nth_element(next.m_order.begin() + first, next.m_order.begin() + middle,
                           next.m_order.begin() + first + count, [&](auto a, auto b) {
                             return center(a) == center(b) ? a < b : center(a) < center(b);
                           });
          const auto left = self(self, first, count / 2, index);
          const auto right = self(self, middle, count - count / 2, index);
          next.m_nodes[index].left = left;
          next.m_nodes[index].right = right;
          return index;
        };

        if (!bounds.empty()) {
          build(build, 0, static_cast<std::uint32_t>(bounds.size()), 0);
        }

        result.rebuilt = true;
        result.changedBounds = static_cast<std::uint32_t>(bounds.size());
        *this = std::move(next);
        return result;
      }

      std::vector<bool> dirty(m_nodes.size());

      for (std::size_t i = 0; i < bounds.size(); ++i) {
        if (same(bounds[i], m_bounds[i])) {
          continue;
        }

        ++result.changedBounds;
        auto node = m_leaves[i];

        while (!dirty[node]) {
          dirty[node] = true;

          if (node == 0) {
            break;
          }

          node = m_nodes[node].parent;
        }
      }

      if (!result.changedBounds) {
        return result;
      }

      auto nodes = m_nodes;
      std::vector<Bounds> nextBounds(bounds.begin(), bounds.end());

      for (std::size_t i = nodes.size(); i-- > 0;) {
        if (!dirty[i]) {
          continue;
        }

        auto& node = nodes[i];

        if (node.count <= 8) {
          node.bounds = nextBounds[m_order[node.first]];

          for (std::uint32_t j = 1; j < node.count; ++j) {
            node.bounds = merge(node.bounds, nextBounds[m_order[node.first + j]]);
          }
        } else {
          node.bounds = merge(nodes[node.left].bounds, nodes[node.right].bounds);
        }

        ++result.refittedNodes;
      }

      m_nodes.swap(nodes);
      m_bounds.swap(nextBounds);
      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Update spatial index: " + std::string(error.what()));
    }
  }

  auto SpatialIndex::query(const Frustum& frustum) const -> Core::Result<SpatialQuery> {
    try {
      SpatialQuery result;
      const auto visit = [&](auto&& self, std::uint32_t index) -> Core::Result<void> {
        const auto& node = m_nodes[index];
        ++result.nodeTests;
        auto state = frustum.classify(node.bounds);

        if (!state) {
          return std::unexpected(state.error());
        }

        if (*state == Containment::Outside) {
          return {};
        }

        if (*state == Containment::Inside) {
          for (std::uint32_t i = 0; i < node.count; ++i) {
            result.hits.push_back({m_order[node.first + i], true});
          }

          return {};
        }

        if (node.count <= 8) {
          for (std::uint32_t i = 0; i < node.count; ++i) {
            const auto item = m_order[node.first + i];
            ++result.boundsTests;
            auto visible = frustum.classify(m_bounds[item]);

            if (!visible) {
              return std::unexpected(visible.error());
            }

            if (*visible != Containment::Outside) {
              result.hits.push_back({item, *visible == Containment::Inside});
            }
          }

          return {};
        }

        auto left = self(self, node.left);
        return left ? self(self, node.right) : left;
      };

      if (!m_nodes.empty()) {
        auto checked = visit(visit, 0);

        if (!checked) {
          return std::unexpected(checked.error());
        }
      }

      std::ranges::sort(result.hits, {}, &SpatialHit::index);
      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Query spatial index: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Graphics

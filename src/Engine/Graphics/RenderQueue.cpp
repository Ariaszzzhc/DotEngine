module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Dot.Graphics.RenderQueue;

namespace Dot::Graphics {
  auto validateCoverage(AlphaMode mode, float cutoff) -> Core::Result<void> {
    if ((mode != AlphaMode::Opaque && mode != AlphaMode::Mask && mode != AlphaMode::Blend)
        || !std::isfinite(cutoff) || cutoff < 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid alpha mode/cutoff");
    }

    return {};
  }

  auto buildRenderQueue(std::span<const QueueItem> items) -> Core::Result<RenderQueue> {
    if (items.size() > 10000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Render queue exceeds 10000 inputs");
    }

    try {
      std::vector<BatchItem> solid, blended;
      std::vector<std::uint32_t> order;
      solid.reserve(items.size());

      for (std::uint32_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];

        if (!item.binding.mesh || !std::isfinite(item.depth) || !validateCoverage(item.alpha, 0)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid render queue item");
        }

        auto binding = item.binding;
        binding.enabled = binding.enabled && item.alpha != AlphaMode::Blend;
        solid.push_back(binding);

        if (item.binding.enabled && item.alpha == AlphaMode::Blend) {
          order.push_back(i);
        }
      }

      std::stable_sort(order.begin(), order.end(),
                       [&](auto a, auto b) { return items[a].depth > items[b].depth; });

      for (auto index : order) {
        blended.push_back(items[index].binding);
      }

      auto s = buildDrawBatches(solid);
      auto b = buildDrawBatches(blended);

      if (!s || !b) {
        return std::unexpected(!s ? s.error() : b.error());
      }

      for (auto& index : b->indices) {
        index = order[index];
      }

      return RenderQueue{std::move(*s), std::move(*b)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Build render queue: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Graphics

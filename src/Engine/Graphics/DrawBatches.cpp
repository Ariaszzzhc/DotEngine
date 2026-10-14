module;

#include <array>
#include <expected>
#include <optional>
#include <span>
#include <vector>

module Dot.Graphics.DrawBatches;

namespace Dot::Graphics {
  auto buildDrawBatches(std::span<const BatchItem> items) -> Core::Result<DrawBatches> {
    if (items.size() > 10000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Instance inputs exceed 10000");
    }

    DrawBatches result;
    result.indices.reserve(items.size());
    result.batches.reserve(items.size());
    std::optional<BatchItem> previous;

    for (std::uint32_t index = 0; index < items.size(); ++index) {
      const auto& item = items[index];

      if (!item.mesh) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Instance mesh identity is zero");
      }

      if (!item.enabled) {
        continue;
      }

      if (previous && previous->mesh == item.mesh && previous->textures == item.textures) {
        ++result.batches.back().count;
      } else {
        result.batches.push_back({static_cast<std::uint32_t>(result.indices.size()), 1});
      }

      result.indices.push_back(index);
      previous = item;
    }

    return result;
  }
}  // namespace Dot::Graphics

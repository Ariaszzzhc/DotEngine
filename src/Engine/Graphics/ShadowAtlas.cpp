module;

#include <algorithm>
#include <bit>
#include <cstdint>
#include <exception>
#include <span>
#include <string>
#include <vector>

module Dot.Graphics.ShadowAtlas;

namespace Dot::Graphics {
  auto planShadowAtlas(std::span<const ShadowAtlasRequest> requests, std::uint64_t maxBytes,
                       std::uint32_t maxLayers) -> Core::Result<ShadowAtlas> {
    if (requests.size() > 64 || !maxBytes || maxBytes > 512 * 1024 * 1024 || !maxLayers) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid shadow atlas bounds");
    }

    try {
      struct Map {
        std::size_t source;
        std::uint32_t face;
        std::uint32_t size;
      };

      std::vector<Map> maps;
      std::uint32_t dimension{};

      for (std::size_t source = 0; source < requests.size(); ++source) {
        const auto& request = requests[source];

        if (!std::has_single_bit(request.resolution) || request.resolution < 128
            || request.resolution > 2048 || !request.maps || request.maps > 6) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid shadow atlas request");
        }

        dimension = std::max(dimension, request.resolution);

        for (std::uint32_t face = 0; face < request.maps; ++face) {
          maps.push_back({source, face, request.resolution});
        }
      }

      std::stable_sort(maps.begin(), maps.end(),
                       [](const Map& left, const Map& right) { return left.size > right.size; });

      if (maps.empty()) {
        return ShadowAtlas{};
      }

      for (;;) {
        ShadowAtlas result;
        result.dimension = dimension;
        result.sources.resize(requests.size());
        std::vector<ShadowTile> free;

        for (const auto& map : maps) {
          auto best = free.end();

          for (auto item = free.begin(); item != free.end(); ++item) {
            if (item->size >= map.size && (best == free.end() || item->size < best->size)) {
              best = item;
            }
          }

          ShadowTile tile;

          if (best == free.end()) {
            tile = {0, 0, result.layers++, dimension};
          } else {
            tile = *best;
            free.erase(best);
          }

          while (tile.size > map.size) {
            tile.size /= 2;
            free.push_back({tile.x + tile.size, tile.y, tile.layer, tile.size});
            free.push_back({tile.x, tile.y + tile.size, tile.layer, tile.size});
            free.push_back({tile.x + tile.size, tile.y + tile.size, tile.layer, tile.size});
          }

          result.sources[map.source][map.face] = tile;
        }

        if (result.layers > maxLayers) {
          if (dimension == 2048) {
            return Core::fail(Core::ErrorCode::Unavailable, "Shadow atlas exceeds layer limit");
          }

          dimension *= 2;
          continue;
        }

        result.bytes = static_cast<std::uint64_t>(dimension) * dimension * result.layers * 4;

        if (result.bytes > maxBytes) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Shadow atlas exceeds depth storage budget");
        }

        return result;
      }
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Plan shadow atlas: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Graphics

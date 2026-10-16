module;

#include <array>
#include <cstdint>
#include <span>
#include <vector>

export module Dot.Graphics.ShadowAtlas;

import Dot.Core.Result;

export namespace Dot::Graphics {
  struct ShadowAtlasRequest {
    std::uint32_t resolution;
    std::uint32_t maps;
  };

  struct ShadowTile {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t layer{};
    std::uint32_t size{};
    auto operator==(const ShadowTile&) const -> bool = default;
  };

  struct ShadowAtlas {
    std::uint32_t dimension{};
    std::uint32_t layers{};
    std::uint64_t bytes{};
    std::vector<std::array<ShadowTile, 6>> sources;
  };

  /** Deterministic CPU packing of at most 64 requests, each 1..6 square maps at
   * power-of-two resolution 128..2048. Unused map slots are zero. Largest-first
   * quadtree splitting preserves resolution; each source/face retains input order.
   * Pages grow only to satisfy the layer limit. Budget is 1 byte..512 MiB; dimensions,
   * padding and all layers count as four-byte depth texels. No GPU allocation.
   * 确定性 CPU 排布：最多 64 组，每组 1..6 张二次幂 128..2048 方图，未用槽为零。
   * 按尺寸降序四叉分割，保留分辨率及输入光源/面顺序；仅为满足层数上限增大页尺寸。
   * 预算为 1 字节至 512 MiB，含页填充及全部图层的四字节深度纹素；不分配 GPU 资源。 */
  [[nodiscard]] auto planShadowAtlas(std::span<const ShadowAtlasRequest> requests,
                                     std::uint64_t maxBytes = 256 * 1024 * 1024,
                                     std::uint32_t maxLayers = 256) -> Core::Result<ShadowAtlas>;
}  // namespace Dot::Graphics

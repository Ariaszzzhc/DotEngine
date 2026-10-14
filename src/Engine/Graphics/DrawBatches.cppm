module;

#include <array>
#include <cstdint>
#include <span>
#include <vector>

export module Dot.Graphics.DrawBatches;

import Dot.Core.Result;

export namespace Dot::Graphics {
  struct BatchItem {
    std::uint64_t mesh;
    std::array<std::uint64_t, 4> textures{};
    bool enabled{true};
  };

  struct DrawBatch {
    std::uint32_t first;
    std::uint32_t count;
    auto operator==(const DrawBatch&) const -> bool = default;
  };

  struct DrawBatches {
    std::vector<std::uint32_t> indices;
    std::vector<DrawBatch> batches;
  };

  /** Stable consecutive batching, skipping disabled entries without reordering
   * enabled ones. Mesh/texture identities are caller-owned numeric tokens; scalar
   * material/transform attributes remain per-instance. At most 10000 inputs; zero
   * mesh tokens are invalid even when disabled. Result owns all index/run storage.
   * 稳定相邻批次，跳过禁用项而不重排启用项；网格/纹理为调用者拥有的身份令牌，
   * 材质标量和变换仍按实例保存。最多 10000 项，禁用项的零网格身份也非法；结果独立拥有索引。 */
  [[nodiscard]] auto buildDrawBatches(std::span<const BatchItem> items)
      -> Core::Result<DrawBatches>;
}  // namespace Dot::Graphics

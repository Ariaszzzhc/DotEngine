module;

#include <span>

export module Dot.Graphics.RenderQueue;

import Dot.Core.Result;
export import Dot.Graphics.DrawBatches;

export namespace Dot::Graphics {
  enum class AlphaMode { Opaque, Mask, Blend };

  struct QueueItem {
    BatchItem binding;
    AlphaMode alpha{AlphaMode::Opaque};
    double depth{};
  };

  struct RenderQueue {
    DrawBatches solid;
    DrawBatches blended;
  };

  /** Validate the current coverage contract: finite nonnegative cutoff (values
   * above one deliberately mask everything). 不透明度模式有效，裁剪阈值有限非负；
   * 超过一的阈值明确裁掉所有片元。 */
  [[nodiscard]] auto validateCoverage(AlphaMode mode, float cutoff) -> Core::Result<void>;
  /** Stable solid source order, then back-to-front blended instance depth with
   * source-order ties. Own original IDs/batches; never renumber source identity.
   * Object-center sorting is approximate for intersecting/large transparent geometry.
   * At most 10000 inputs, finite depths and nonzero mesh tokens even when disabled.
   * 保持固体源顺序，混合实例按深度从后向前，同深度保留源顺序；结果拥有原始身份与批次。
   * 物体中心排序对交叉/大型透明几何只是近似，最多 10000 输入并完整校验。 */
  [[nodiscard]] auto buildRenderQueue(std::span<const QueueItem> items)
      -> Core::Result<RenderQueue>;
}  // namespace Dot::Graphics

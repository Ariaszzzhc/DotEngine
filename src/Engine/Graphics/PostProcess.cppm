module;

export module Dot.Graphics.PostProcess;

import Dot.Core.Result;
import Dot.Graphics.Image;

export namespace Dot::Graphics {
  enum class ToneMap { LinearClamp, Reinhard, AcesFitted };

  /** Exposure in stops [-16,16], applied once before tone mapping. Display output
   * normally uses sRGB encoding; Linear is for linear diagnostics/intermediates.
   * AcesFitted uses the published Stephen Hill RRT/ODT fit, not full ACES processing.
   * 曝光以档位 [-16,16] 指定，在色调映射前应用一次；显示通常使用 sRGB 编码，Linear
   * 用于线性诊断/中间结果。AcesFitted 使用 Stephen Hill 的 RRT/ODT 拟合，并非完整 ACES。 */
  struct PostProcessSettings {
    float exposureEv{};
    ToneMap toneMap{ToneMap::AcesFitted};
    ColorSpace outputEncoding{ColorSpace::Srgb};
    auto operator==(const PostProcessSettings&) const -> bool = default;
  };

  [[nodiscard]] auto validatePostProcess(const PostProcessSettings& settings) -> Core::Result<void>;
}  // namespace Dot::Graphics

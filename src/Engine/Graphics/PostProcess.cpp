module;

#include <cmath>

module Dot.Graphics.PostProcess;

import Dot.Core.Result;
import Dot.Graphics.Image;

namespace Dot::Graphics {
  auto validatePostProcess(const PostProcessSettings& settings) -> Core::Result<void> {
    if (!std::isfinite(settings.exposureEv) || settings.exposureEv < -16 || settings.exposureEv > 16
        || (settings.toneMap != ToneMap::LinearClamp && settings.toneMap != ToneMap::Reinhard
            && settings.toneMap != ToneMap::AcesFitted)
        || (settings.outputEncoding != ColorSpace::Linear
            && settings.outputEncoding != ColorSpace::Srgb)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid exposure, tone mapper or output encoding");
    }

    return {};
  }
}  // namespace Dot::Graphics

module;

#include <cmath>

module Dot.Audio.Voice;

import Dot.Core.Result;

namespace Dot::Audio {
  auto validateVoiceSettings(const VoiceSettings& settings) -> Core::Result<void> {
    if (!std::isfinite(settings.volume) || settings.volume < 0 || settings.volume > 1
        || !std::isfinite(settings.pan) || settings.pan < -1 || settings.pan > 1
        || !std::isfinite(settings.pitch) || settings.pitch < 0.25F || settings.pitch > 4) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid audio voice settings");
    }

    return {};
  }
}  // namespace Dot::Audio

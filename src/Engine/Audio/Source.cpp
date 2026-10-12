module;

#include <algorithm>
#include <expected>
#include <string>
#include <string_view>

module Dot.Audio.Source;

import Dot.Core.Result;
import Dot.Core.Text;
import Dot.Audio.Voice;
import Dot.Audio.Spatial;

namespace Dot::Audio {
  auto validateSourceProperties(const SourceProperties& properties) -> Core::Result<void> {
    const auto& asset = properties.asset;

    if (asset.empty() || asset.size() > 4096 || asset.front() == '/' || asset.back() == '/'
        || asset.find('\\') != std::string::npos || asset.find(':') != std::string::npos
        || asset.find_first_of("<>\"|?*") != std::string::npos
        || asset.find('\0') != std::string::npos || !Core::isValidUtf8(asset)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid portable audio asset path");
    }

    if (std::ranges::any_of(asset, [](unsigned char character) { return character < 32; })) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Audio asset path contains control characters");
    }

    std::size_t begin = 0;

    while (begin < asset.size()) {
      const auto end = asset.find('/', begin);
      const auto component
          = std::string_view{asset}.substr(begin, end == std::string::npos ? end : end - begin);

      if (component.empty() || component == "." || component == ".." || component.back() == '.'
          || component.back() == ' ') {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Audio asset path requires canonical relative components");
      }

      if (end == std::string::npos) {
        break;
      }

      begin = end + 1;
    }

    const auto dot = asset.rfind('.');
    std::string extension = dot == std::string::npos ? "" : asset.substr(dot);

    for (auto& character : extension) {
      if (character >= 'A' && character <= 'Z') {
        character = static_cast<char>(character + ('a' - 'A'));
      }
    }

    if (extension != ".wav" && extension != ".flac" && extension != ".mp3") {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Audio asset extension must be .wav/.flac/.mp3");
    }

    auto voice = validateVoiceSettings(properties.voice);

    if (!voice) {
      return voice;
    }

    auto spatial = validateSpatialProperties(properties.spatial);

    if (!spatial) {
      return spatial;
    }

    if (properties.spatial.enabled && properties.voice.pan != 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Spatial audio source requires zero manual pan");
    }

    return {};
  }
}  // namespace Dot::Audio

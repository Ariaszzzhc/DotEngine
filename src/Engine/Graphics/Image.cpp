module;

#include <cmath>
#include <cstddef>
#include <cstdint>

module Dot.Graphics.Image;

import Dot.Core.Result;

namespace Dot::Graphics {
  namespace {
    auto dimensions(std::uint32_t width, std::uint32_t height, std::size_t size)
        -> Core::Result<void> {
      const auto pixels = static_cast<std::uint64_t>(width) * height;

      if (width == 0 || height == 0 || width > 8192 || height > 8192 || pixels > 16'777'216
          || size != pixels * 4) {
        return Core::fail(
            Core::ErrorCode::InvalidArgument,
            "Image requires dimensions 1..8192, at most 16777216 pixels and exact RGBA storage");
      }

      return {};
    }
  }  // namespace

  auto validateImage(const Image& image) -> Core::Result<void> {
    return dimensions(image.width, image.height, image.pixels.size());
  }

  auto validateImage(const HdrImage& image) -> Core::Result<void> {
    auto checked = dimensions(image.width, image.height, image.pixels.size());

    if (!checked) {
      return checked;
    }

    for (std::size_t index = 0; index < image.pixels.size(); ++index) {
      const auto value = image.pixels[index];

      if (!std::isfinite(value) || value < 0 || (index % 4 == 3 && value > 1)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "HDR pixels require finite nonnegative RGB and alpha in [0,1]");
      }
    }

    return {};
  }
}  // namespace Dot::Graphics

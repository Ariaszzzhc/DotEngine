module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <string>

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

  auto downsampleImage(const Image& image, ColorSpace colorSpace) -> Core::Result<Image> {
    auto checked = validateImage(image);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (colorSpace != ColorSpace::Linear && colorSpace != ColorSpace::Srgb) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid mip color space");
    }

    try {
      if (image.width == 1 && image.height == 1) {
        return image;
      }

      /** RGBA8 has only 256 possible inputs. Preserve the exact transfer function
       * while sharing immutable decode values across textures and threads.
       * RGBA8 仅有 256 种输入值；保留精确传递函数，跨纹理和线程共享不可变解码值。 */
      static const auto decodedSrgb = [] {
        std::array<double, 256> values{};

        for (std::size_t index = 0; index < values.size(); ++index) {
          const auto value = static_cast<double>(index) / 255.0;
          values[index] = value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        }

        return values;
      }();
      Image next{std::max(1U, image.width / 2), std::max(1U, image.height / 2), {}};
      next.pixels.resize(static_cast<std::size_t>(next.width) * next.height * 4);

      for (std::uint32_t y = 0; y < next.height; ++y) {
        for (std::uint32_t x = 0; x < next.width; ++x) {
          const auto beginX = x * image.width / next.width;
          const auto endX = (x + 1) * image.width / next.width;
          const auto beginY = y * image.height / next.height;
          const auto endY = (y + 1) * image.height / next.height;
          const auto count = static_cast<double>((endX - beginX) * (endY - beginY));

          for (std::size_t channel = 0; channel < 4; ++channel) {
            double sum{};
            const auto srgb = channel != 3 && colorSpace == ColorSpace::Srgb;

            for (auto sy = beginY; sy < endY; ++sy) {
              for (auto sx = beginX; sx < endX; ++sx) {
                const auto pixel
                    = image.pixels[(static_cast<std::size_t>(sy) * image.width + sx) * 4 + channel];
                sum += srgb ? decodedSrgb[pixel] : pixel / 255.0;
              }
            }

            auto value = sum / count;

            if (srgb) {
              value
                  = value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
            }

            next.pixels[(static_cast<std::size_t>(y) * next.width + x) * 4 + channel]
                = static_cast<std::uint8_t>(std::clamp(std::round(value * 255), 0.0, 255.0));
          }
        }
      }

      return next;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Downsample image: " + std::string(error.what()));
    }
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

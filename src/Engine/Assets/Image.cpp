module;

#define STBI_NO_STDIO
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <source_location>
#include <string>

module Dot.Assets.Image;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Graphics.Image;

namespace Dot::Assets {
  namespace {
    constexpr std::size_t maxFileBytes = 64 * 1024 * 1024;

    auto failure(std::string_view context) -> Core::Error {
      const auto* reason = stbi_failure_reason();
      return {Core::ErrorCode::InvalidArgument,
              std::format("{}: {}", context, reason ? reason : "image decoding failed"),
              std::source_location::current()};
    }

    struct Encoded {
      const stbi_uc* bytes;
      int size;
      int width;
      int height;
    };

    auto preflight(std::span<const std::byte> source, bool hdr) -> Core::Result<Encoded> {
      if (source.empty() || source.size() > maxFileBytes) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Image source must be 1..64 MiB");
      }

      const auto* bytes = reinterpret_cast<const stbi_uc*>(source.data());
      const auto size = static_cast<int>(source.size());
      const std::array<stbi_uc, 8> png{137, 80, 78, 71, 13, 10, 26, 10};
      const auto isPng = source.size() >= png.size() && std::equal(png.begin(), png.end(), bytes);
      const auto isJpeg
          = source.size() >= 3 && bytes[0] == 255 && bytes[1] == 216 && bytes[2] == 255;
      const auto isHdr = stbi_is_hdr_from_memory(bytes, size) != 0;

      if ((hdr && !isHdr) || (!hdr && !isPng && !isJpeg)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          hdr ? "HDR input must be Radiance RGBE" : "Image input must be PNG/JPEG");
      }

      if (!hdr && stbi_is_16_bit_from_memory(bytes, size)) {
        return Core::fail(Core::ErrorCode::Unavailable, "16-bit image import is unsupported");
      }

      int width{};
      int height{};
      int channels{};

      if (!stbi_info_from_memory(bytes, size, &width, &height, &channels)) {
        return std::unexpected(failure("Read image dimensions"));
      }

      if (width <= 0 || height <= 0 || width > 8192 || height > 8192
          || static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > 16'777'216) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Image exceeds dimension/pixel bounds (8192 per axis, 16777216 pixels)");
      }

      return Encoded{bytes, size, width, height};
    }

    template <typename T>
    auto load(const Core::FileSystem& files, const std::filesystem::path& relative,
              Core::Result<T> (*decode)(std::span<const std::byte>)) -> Core::Result<T> {
      auto bytes = files.readBytes(relative, maxFileBytes);

      if (!bytes) {
        return std::unexpected(bytes.error());
      }

      auto decoded = decode(*bytes);

      if (!decoded) {
        return Core::fail(
            decoded.error().code,
            std::format("Load image {}: {}", relative.generic_string(), decoded.error().message));
      }

      return decoded;
    }
  }  // namespace

  auto decodeImage(std::span<const std::byte> source) -> Core::Result<Graphics::Image> {
    try {
      auto encoded = preflight(source, false);

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      int width{};
      int height{};
      int channels{};
      std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
          stbi_load_from_memory(encoded->bytes, encoded->size, &width, &height, &channels, 4),
          stbi_image_free);

      if (!pixels || width != encoded->width || height != encoded->height) {
        return std::unexpected(failure("Decode PNG/JPEG"));
      }

      const auto size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
      return Graphics::Image{static_cast<std::uint32_t>(width),
                             static_cast<std::uint32_t>(height),
                             {pixels.get(), pixels.get() + size}};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Decode image: " + std::string(error.what()));
    }
  }

  auto decodeHdrImage(std::span<const std::byte> source) -> Core::Result<Graphics::HdrImage> {
    try {
      auto encoded = preflight(source, true);

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      int width{};
      int height{};
      int channels{};
      std::unique_ptr<float, decltype(&stbi_image_free)> pixels(
          stbi_loadf_from_memory(encoded->bytes, encoded->size, &width, &height, &channels, 4),
          stbi_image_free);

      if (!pixels || width != encoded->width || height != encoded->height) {
        return std::unexpected(failure("Decode Radiance RGBE"));
      }

      const auto size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
      Graphics::HdrImage image{static_cast<std::uint32_t>(width),
                               static_cast<std::uint32_t>(height),
                               {pixels.get(), pixels.get() + size}};
      auto checked = Graphics::validateImage(image);

      if (!checked) {
        return std::unexpected(checked.error());
      }

      return image;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Decode HDR image: " + std::string(error.what()));
    }
  }

  auto loadImage(const Core::FileSystem& files, const std::filesystem::path& relative)
      -> Core::Result<Graphics::Image> {
    return load(files, relative, decodeImage);
  }

  auto loadHdrImage(const Core::FileSystem& files, const std::filesystem::path& relative)
      -> Core::Result<Graphics::HdrImage> {
    return load(files, relative, decodeHdrImage);
  }
}  // namespace Dot::Assets

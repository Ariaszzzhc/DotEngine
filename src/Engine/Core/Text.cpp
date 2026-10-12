module;

#include <cstddef>
#include <cstdint>
#include <format>
#include <string_view>

module Dot.Core.Text;

import Dot.Core.Result;

namespace Dot::Core {
  namespace {
    struct ScanResult {
      std::size_t count{};
      std::size_t errorOffset{};
      bool valid{true};
    };

    auto scan(std::string_view text) noexcept -> ScanResult {
      ScanResult result;
      std::size_t offset = 0;

      while (offset < text.size()) {
        const auto lead = static_cast<unsigned char>(text[offset]);
        std::size_t width = 0;
        std::uint32_t scalar = 0;
        std::uint32_t minimum = 0;

        if (lead <= 0x7F) {
          width = 1;
          scalar = lead;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
          width = 2;
          scalar = lead & 0x1FU;
          minimum = 0x80;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
          width = 3;
          scalar = lead & 0x0FU;
          minimum = 0x800;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
          width = 4;
          scalar = lead & 0x07U;
          minimum = 0x10000;
        }

        if (width == 0 || width > text.size() - offset) {
          return {result.count, offset, false};
        }

        for (std::size_t index = 1; index < width; ++index) {
          const auto continuation = static_cast<unsigned char>(text[offset + index]);

          if (continuation < 0x80 || continuation > 0xBF) {
            return {result.count, offset + index, false};
          }

          scalar = (scalar << 6U) | (continuation & 0x3FU);
        }

        if (scalar < minimum || scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF)) {
          return {result.count, offset, false};
        }

        ++result.count;
        offset += width;
      }

      return result;
    }
  }  // namespace

  auto isValidUtf8(std::string_view text) noexcept -> bool {
    return scan(text).valid;
  }

  auto utf8CodePoints(std::string_view text) -> Result<std::size_t> {
    const auto result = scan(text);

    if (!result.valid) {
      return fail(ErrorCode::InvalidArgument,
                  std::format("Invalid UTF-8 at byte {}", result.errorOffset));
    }

    return result.count;
  }
}  // namespace Dot::Core

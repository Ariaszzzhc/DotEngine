module;

#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Dot.Graphics.Text;

import Dot.Core.Text;

namespace Dot::Graphics {
  namespace {
    struct Face {
      FT_Library library{};
      FT_Face face{};
      Face() = default;
      Face(const Face&) = delete;
      auto operator=(const Face&) -> Face& = delete;

      ~Face() {
        if (face) {
          FT_Done_Face(face);
        }

        if (library) {
          FT_Done_FreeType(library);
        }
      }

      auto open(std::span<const std::byte> bytes) -> Core::Result<void> {
        if (FT_Init_FreeType(&library)
            || FT_New_Memory_Face(library, reinterpret_cast<const FT_Byte*>(bytes.data()),
                                  static_cast<FT_Long>(bytes.size()), 0, &face)
            || !FT_IS_SCALABLE(face) || FT_Select_Charmap(face, FT_ENCODING_UNICODE)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid scalable Unicode text font");
        }

        return {};
      }
    };

    auto points(std::string_view text) -> std::vector<std::uint32_t> {
      std::vector<std::uint32_t> result;

      for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        auto value = static_cast<std::uint32_t>(first);
        const auto count = first < 128 ? 0 : first < 224 ? 1 : first < 240 ? 2 : 3;

        if (count) {
          value &= (1U << (6 - count)) - 1;
        }

        for (int n = 0; n < count; ++n) {
          value = (value << 6) | (static_cast<unsigned char>(text[i++]) & 63U);
        }

        result.push_back(value);
      }

      return result;
    }

    auto supported(std::uint32_t cp) -> bool {
      return cp == 10 || (cp >= 32 && cp <= 126) || (cp >= 160 && cp <= 591)
             || (cp >= 880 && cp <= 1327) || (cp >= 0x2000 && cp <= 0x200a)
             || (cp >= 0x2010 && cp <= 0x2027) || (cp >= 0x2030 && cp <= 0x205e)
             || (cp >= 0x3000 && cp <= 0x3098) || (cp >= 0x309b && cp <= 0x30ff)
             || (cp >= 0x3400 && cp <= 0x9fff) || (cp >= 0xac00 && cp <= 0xd7a3)
             || (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0xff01 && cp <= 0xff60);
    }

    auto colorValid(Math::Vec4 c) -> bool {
      return Math::isFinite(c) && c.x >= 0 && c.x <= 1 && c.y >= 0 && c.y <= 1 && c.z >= 0
             && c.z <= 1 && c.w >= 0 && c.w <= 1;
    }
  }  // namespace

  auto validateTextBox(const TextBox& box) -> Core::Result<void> {
    const auto p = box.position;
    const auto s = box.size;

    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(s.x) || !std::isfinite(s.y)
        || p.x < 0 || p.y < 0 || s.x <= 0 || s.y <= 0 || p.x + s.x > 1 || p.y + s.y > 1
        || !std::isfinite(box.fontSize) || box.fontSize < 8 || box.fontSize > 64
        || !std::isfinite(box.padding) || box.padding < 0 || box.padding > 32
        || !colorValid(box.color) || !colorValid(box.background) || box.text.size() > 4096
        || !Core::isValidUtf8(box.text)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid text box bounds/color/UTF-8");
    }

    try {
      const auto decoded = points(box.text);

      if (decoded.size() > 1024
          || std::ranges::any_of(decoded, [](auto cp) { return !supported(cp); })) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Text exceeds 1024 scalars or supported horizontal-script ranges");
      }

      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Validate text box: " + std::string(error.what()));
    }
  }

  struct TextFont::Data {
    std::vector<std::byte> bytes;
  };

  TextFont::TextFont(std::shared_ptr<const Data> data) : m_data(std::move(data)) {
  }

  auto TextFont::create(std::span<const std::byte> bytes) -> Core::Result<TextFont> {
    if (bytes.empty() || bytes.size() > 32 * 1024 * 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Text font bytes must be in [1,32 MiB]");
    }

    try {
      auto data = std::make_shared<Data>(Data{{bytes.begin(), bytes.end()}});
      Face reader;
      auto valid = reader.open(data->bytes);
      return valid ? Core::Result<TextFont>{TextFont(std::move(data))}
                   : std::unexpected(valid.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Load text font: " + std::string(error.what()));
    }
  }

  auto TextFont::rasterize(const TextBox& box, std::uint32_t width, std::uint32_t height,
                           std::uint32_t viewportHeight) const -> Core::Result<TextRaster> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Text font was moved");
    }

    auto valid = validateTextBox(box);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    if (!width || !height || width > 8192 || height > 8192 || !viewportHeight
        || viewportHeight > 8192 || static_cast<std::uint64_t>(width) * height > 4194304) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Text raster dimensions exceed bounds");
    }

    try {
      Face reader;
      valid = reader.open(m_data->bytes);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      const auto scale = static_cast<float>(viewportHeight) / 720.F;
      const auto pixels = static_cast<unsigned>(std::max(1.F, std::round(box.fontSize * scale)));

      if (FT_Set_Pixel_Sizes(reader.face, 0, pixels)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Set text font size failed");
      }

      const auto bounded = [](FT_Pos value) { return value >= -8192 * 64 && value <= 8192 * 64; };

      if (!bounded(reader.face->size->metrics.height)
          || !bounded(reader.face->size->metrics.ascender)) {
        return Core::fail(Core::ErrorCode::Unavailable, "Text font line metrics exceed bounds");
      }

      struct Glyph {
        std::uint32_t point;
        FT_UInt index;
        int advance;
      };

      std::vector<Glyph> glyphs;
      auto ascent = std::max(0, static_cast<int>((reader.face->size->metrics.ascender + 63) / 64));
      int descent{};

      for (const auto cp : points(box.text)) {
        const auto index = cp == 10 ? 0 : FT_Get_Char_Index(reader.face, cp);

        if (cp != 10 && (!index || FT_Load_Glyph(reader.face, index, FT_LOAD_DEFAULT))) {
          return Core::fail(Core::ErrorCode::NotFound,
                            "Text font has no loadable glyph for codepoint " + std::to_string(cp));
        }

        const auto& metrics = reader.face->glyph->metrics;

        if (cp != 10
            && (!bounded(reader.face->glyph->advance.x) || !bounded(metrics.width)
                || !bounded(metrics.height) || !bounded(metrics.horiBearingX)
                || !bounded(metrics.horiBearingY))) {
          return Core::fail(Core::ErrorCode::Unavailable, "Text font glyph metrics exceed bounds");
        }

        if (cp != 10) {
          ascent = std::max(ascent, static_cast<int>((metrics.horiBearingY + 63) / 64));
          descent = std::max(descent,
                             static_cast<int>((metrics.height - metrics.horiBearingY + 63) / 64));
        }

        glyphs.push_back(
            {cp, index, cp == 10 ? 0 : static_cast<int>(reader.face->glyph->advance.x / 64)});
      }

      TextRaster result{
          {width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)}};
      const auto byte
          = [](float value) { return static_cast<std::uint8_t>(std::lround(value * 255)); };
      const std::array background{byte(box.background.x), byte(box.background.y),
                                  byte(box.background.z), byte(box.background.w)};

      for (std::size_t i = 0; i < result.image.pixels.size(); i += 4) {
        std::copy(background.begin(), background.end(),
                  result.image.pixels.begin() + static_cast<std::ptrdiff_t>(i));
      }

      const auto padding = static_cast<int>(std::round(box.padding * scale));
      const auto right = static_cast<int>(width) - padding;
      const auto bottom = static_cast<int>(height) - padding;
      /** Hinted glyph ink can exceed nominal face ascenders at small sizes.
       * Keep it inside padding and prevent adjacent lines from overlapping.
       * 小字号提示后的字形墨迹可能超出字体名义上升部；保留留白并避免相邻行重叠。 */
      const auto lineHeight = std::max(
          {1, static_cast<int>(reader.face->size->metrics.height / 64), ascent + descent});
      int x = padding;
      int baseline = padding + ascent;
      FT_UInt previous{};
      result.lines = glyphs.empty() ? 0 : 1;
      const auto newline = [&] {
        x = padding;
        baseline += lineHeight;
        previous = 0;
        ++result.lines;
      };

      for (std::size_t i = 0; i < glyphs.size(); ++i) {
        const auto& glyph = glyphs[i];

        if (glyph.point == 10) {
          newline();
          continue;
        }

        if (glyph.point != 32 && (i == 0 || glyphs[i - 1].point == 32)) {
          int word{};

          for (auto j = i; j < glyphs.size() && glyphs[j].point != 32 && glyphs[j].point != 10;
               ++j) {
            word += glyphs[j].advance;
          }

          if (x > padding && word <= right - padding && x + word > right) {
            newline();
          }
        }

        FT_Vector kern{};

        if (previous && glyph.index && FT_HAS_KERNING(reader.face)) {
          if (FT_Get_Kerning(reader.face, previous, glyph.index, FT_KERNING_DEFAULT, &kern)) {
            return Core::fail(Core::ErrorCode::External, "Text glyph kerning failed");
          }

          if (!bounded(kern.x)) {
            return Core::fail(Core::ErrorCode::Unavailable, "Text font kerning exceeds bounds");
          }
        }

        if (x > padding && x + static_cast<int>(kern.x / 64) + glyph.advance > right) {
          newline();
          kern.x = 0;
        }

        if (glyph.point == 32 && x == padding) {
          continue;
        }

        x += static_cast<int>(kern.x / 64);

        if (FT_Load_Glyph(reader.face, glyph.index, FT_LOAD_DEFAULT)) {
          return Core::fail(Core::ErrorCode::External, "Load text glyph failed");
        }

        /** Validate all glyphs above, including clipped ones. Avoid bitmap work
         * proportional to invisible text rather than the bounded visible box.
         * 先校验全部字形，包括裁剪外字形；位图工作量不随不可见文字面积增长。 */
        const auto& metrics = reader.face->glyph->metrics;
        const auto inkLeft = x + static_cast<int>(metrics.horiBearingX / 64) - 2;
        const auto inkTop = baseline - static_cast<int>(metrics.horiBearingY / 64) - 2;

        if (inkLeft >= right || inkTop >= bottom
            || inkLeft + static_cast<int>(metrics.width / 64) + 4 <= padding
            || inkTop + static_cast<int>(metrics.height / 64) + 4 <= padding) {
          result.overflow = true;
          x += glyph.advance;
          previous = glyph.index;
          ++result.glyphs;
          continue;
        }

        if (FT_Render_Glyph(reader.face->glyph, FT_RENDER_MODE_NORMAL)) {
          return Core::fail(Core::ErrorCode::External, "Rasterize text glyph failed");
        }

        const auto& bitmap = reader.face->glyph->bitmap;

        if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY || bitmap.pitch < 0 || bitmap.width > 8192
            || bitmap.rows > 8192 || static_cast<unsigned>(bitmap.pitch) < bitmap.width) {
          return Core::fail(Core::ErrorCode::Unavailable, "Text font requires grayscale glyphs");
        }

        const auto originX = x + reader.face->glyph->bitmap_left;
        const auto originY = baseline - reader.face->glyph->bitmap_top;
        const auto firstRow = std::max(0, padding - originY);
        const auto lastRow = std::min(static_cast<int>(bitmap.rows), bottom - originY);
        const auto firstColumn = std::max(0, padding - originX);
        const auto lastColumn = std::min(static_cast<int>(bitmap.width), right - originX);

        if (firstRow || firstColumn || lastRow < static_cast<int>(bitmap.rows)
            || lastColumn < static_cast<int>(bitmap.width)) {
          result.overflow = true;
        }

        for (int row = firstRow; row < lastRow; ++row) {
          const auto y = originY + row;

          for (int column = firstColumn; column < lastColumn; ++column) {
            const auto px = originX + column;

            const auto coverage
                = bitmap.buffer[static_cast<std::size_t>(row) * static_cast<unsigned>(bitmap.pitch)
                                + static_cast<unsigned>(column)]
                  / 255.F;
            const auto alpha = coverage * box.color.w;
            const auto at = (static_cast<std::size_t>(y) * width + static_cast<unsigned>(px)) * 4;
            auto& image = result.image.pixels;
            const auto oldAlpha = image[at + 3] / 255.F;
            const auto nextAlpha = alpha + oldAlpha * (1 - alpha);
            const std::array foreground{box.color.x, box.color.y, box.color.z};

            for (std::size_t c = 0; c < 3; ++c) {
              image[at + c] = byte(
                  nextAlpha == 0
                      ? 0
                      : (foreground[c] * alpha + image[at + c] / 255.F * oldAlpha * (1 - alpha))
                            / nextAlpha);
            }

            image[at + 3] = byte(nextAlpha);
          }
        }

        x += glyph.advance;
        previous = glyph.index;
        ++result.glyphs;
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Rasterize text: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Graphics

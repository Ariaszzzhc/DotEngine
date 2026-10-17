module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

export module Dot.Graphics.Text;

import Dot.Core.Result;
import Dot.Graphics.Image;
import Dot.Math;

export namespace Dot::Graphics {
  /** Normalized top-left viewport box; font/padding use a 720-pixel reference
   * height. Display RGB, straight alpha. Up to 4096 UTF-8 bytes/1024 scalars.
   * 视口左上归一化矩形；字体/留白按 720 像素参考高度缩放；显示 RGB、直通 alpha。
   * 最多 4096 UTF-8 字节/1024 标量。 */
  struct TextBox {
    std::string text;
    Math::Vec2 position{.02F, .02F};
    Math::Vec2 size{.6F, .12F};
    float fontSize{20};
    float padding{8};
    Math::Vec4 color{.9F, .96F, 1, 1};
    Math::Vec4 background{.035F, .045F, .065F, .9F};
    bool visible{true};
    auto operator==(const TextBox&) const -> bool = default;
  };

  struct TextLayer {
    std::string key;
    TextBox value;
    auto operator==(const TextLayer&) const -> bool = default;
  };

  struct TextRaster {
    Image image;
    std::uint32_t glyphs{};
    std::uint32_t lines{};
    bool overflow{};
  };

  [[nodiscard]] auto validateTextBox(const TextBox& box) -> Core::Result<void>;

  /** Own scalable Unicode font bytes (<=32 MiB), never platform fallback fonts.
   * Each raster owns its FreeType face/library; copies safely cross threads.
   * Layout is horizontal with kerning/word wrapping, not general shaping/bidi.
   * Unsupported combining/joining/directional scripts fail, as do missing glyphs.
   * 拥有至多 32 MiB 可缩放 Unicode 字体，不回退系统字体；每次栅格化拥有独立
   * FreeType 句柄，副本可跨线程。横向字距/换行不等于完整塑形/双向排版；
   * 不支持的组合/连写/方向脚本及缺字明确失败。 */
  class TextFont {
  public:
    [[nodiscard]] static auto create(std::span<const std::byte> bytes) -> Core::Result<TextFont>;
    [[nodiscard]] auto rasterize(const TextBox& box, std::uint32_t width, std::uint32_t height,
                                 std::uint32_t viewportHeight) const -> Core::Result<TextRaster>;

  private:
    struct Data;
    explicit TextFont(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> m_data;
  };
}  // namespace Dot::Graphics

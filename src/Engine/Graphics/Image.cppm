module;

#include <cstdint>
#include <vector>

export module Dot.Graphics.Image;

import Dot.Core.Result;

export namespace Dot::Graphics {
  enum class ColorSpace { Linear, Srgb };

  /** Owned RGBA8 pixels in top-to-bottom row order without padding. Color space
   * is specified at upload; data textures use Linear, base colors normally Srgb.
   * 拥有的 RGBA8 像素，行从上到下且无填充；上传时指定色彩空间，数据贴图使用
   * Linear，基础颜色通常使用 Srgb。 */
  struct Image {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels;
  };

  /** Linear floating-point RGBA radiance; RGB may exceed one, alpha is one.
   * 线性浮点 RGBA 辐射亮度，RGB 可超过一，alpha 为一。 */
  struct HdrImage {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<float> pixels;
  };

  /** Dimensions 1..8192, at most 16,777,216 pixels, and exact RGBA storage.
   * 每维 1..8192，最多 16,777,216 像素且 RGBA 存储长度须精确匹配。 */
  [[nodiscard]] auto validateImage(const Image& image) -> Core::Result<void>;
  /** Apply the same dimensions/storage limits and require finite nonnegative RGB
   * and alpha in [0,1]. 相同尺寸/存储限制，RGB 须有限非负，alpha 须在 [0,1]。 */
  [[nodiscard]] auto validateImage(const HdrImage& image) -> Core::Result<void>;
}  // namespace Dot::Graphics

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

  /** Linear floating-point RGBA radiance; RGB may exceed one, alpha is in [0,1].
   * 线性浮点 RGBA 辐射亮度，RGB 可超过一，alpha 在 [0,1]。 */
  struct HdrImage {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<float> pixels;
  };

  /** Dimensions 1..8192, at most 16,777,216 pixels, and exact RGBA storage.
   * 每维 1..8192，最多 16,777,216 像素且 RGBA 存储长度须精确匹配。 */
  [[nodiscard]] auto validateImage(const Image& image) -> Core::Result<void>;

  /** Produce an owned floor-half mip (minimum 1x1), averaging every source pixel
   * in its integer bin in linear light; alpha is always linear. A 1x1 source is
   * copied. Input is unchanged; image limits and color space are validated.
   * 生成独立拥有的向下取半 mip（最小 1x1），在线性光空间平均整数分区内的全部源像素；
   * alpha 始终线性。1x1 源会复制，输入不变，并校验图像限制及色彩空间。 */
  [[nodiscard]] auto downsampleImage(const Image& image, ColorSpace colorSpace)
      -> Core::Result<Image>;

  /** Apply the same dimensions/storage limits and require finite nonnegative RGB
   * and alpha in [0,1]. 相同尺寸/存储限制，RGB 须有限非负，alpha 须在 [0,1]。 */
  [[nodiscard]] auto validateImage(const HdrImage& image) -> Core::Result<void>;
}  // namespace Dot::Graphics

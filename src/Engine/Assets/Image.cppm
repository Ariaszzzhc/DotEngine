module;

#include <cstddef>
#include <filesystem>
#include <span>

export module Dot.Assets.Image;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Graphics.Image;

export namespace Dot::Assets {
  /** Decode PNG/JPEG only from at most 64 MiB, preflight dimensions before pixel
   * allocation, return owned top-to-bottom RGBA8 without implicit color conversion.
   * Reject 16-bit PNG instead of silently reducing precision.
   * 仅解码最多 64 MiB 的 PNG/JPEG，在像素分配前校验尺寸；返回拥有的从上到下
   * RGBA8，不隐式转换色彩；拒绝 16 位 PNG，避免静默降低精度。 */
  [[nodiscard]] auto decodeImage(std::span<const std::byte> source)
      -> Core::Result<Graphics::Image>;
  /** Radiance RGBE only, retaining linear radiance above one. Same file/pixel
   * bounds as LDR; at most 256 MiB of returned float pixels. EXR is not supported.
   * 仅 Radiance RGBE，保留超过一的线性辐射亮度；使用相同文件/像素限制，返回
   * 浮点像素最多 256 MiB；尚不支持 EXR。 */
  [[nodiscard]] auto decodeHdrImage(std::span<const std::byte> source)
      -> Core::Result<Graphics::HdrImage>;
  /** Load relative to the validated project root; codec errors retain path context.
   * 相对已校验项目根目录加载，编解码错误保留路径上下文。 */
  [[nodiscard]] auto loadImage(const Core::FileSystem& files, const std::filesystem::path& relative)
      -> Core::Result<Graphics::Image>;
  [[nodiscard]] auto loadHdrImage(const Core::FileSystem& files,
                                  const std::filesystem::path& relative)
      -> Core::Result<Graphics::HdrImage>;
}  // namespace Dot::Assets

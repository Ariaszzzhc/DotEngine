#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

import Dot.Core.Files;
import Dot.Core.Result;

namespace DotTest::Environment {
  /** Tiny 4x2 opaque linear RGBE panorama; exponent 130 gives RGB (2,1,.5).
   * Width below eight uses the Radiance uncompressed scanline representation.
   * 微型 4x2 不透明线性 RGBE 全景；指数 130 对应 RGB (2,1,.5)，宽度不足八时使用
   * Radiance 无压缩扫描行格式。 */
  inline auto write(const Dot::Core::FileSystem& files, std::string_view name = "assets/studio.hdr",
                    std::uint8_t exponent = 130) -> Dot::Core::Result<void> {
    const auto path = std::filesystem::path(std::u8string(name.begin(), name.end()));
    auto created = files.createDirectories(path.parent_path());

    if (!created) {
      return std::unexpected(created.error());
    }

    std::string source = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";

    for (int pixel = 0; pixel < 8; ++pixel) {
      source.push_back(static_cast<char>(128));
      source.push_back(static_cast<char>(64));
      source.push_back(static_cast<char>(32));
      source.push_back(static_cast<char>(exponent));
    }

    return files.writeTextAtomic(std::string(name), source);
  }
}  // namespace DotTest::Environment

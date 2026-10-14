module;

#include <bit>
#include <cmath>
#include <cstddef>
#include <expected>

module Dot.Graphics.Environment;

namespace Dot::Graphics {
  auto validateEnvironment(const HdrImage& panorama, const EnvironmentConfig& config)
      -> Core::Result<EnvironmentInfo> {
    auto valid = validateImage(panorama);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    const auto size = [](std::uint32_t value, std::uint32_t minimum, std::uint32_t maximum) {
      return value >= minimum && value <= maximum && std::has_single_bit(value);
    };

    if (panorama.width != panorama.height * 2 || panorama.height < 2
        || !size(config.radianceSize, 16, 512) || !size(config.specularSize, 16, 512)
        || !size(config.diffuseSize, 4, 128) || !size(config.brdfSize, 16, 256)
        || config.samples < 32 || config.samples > 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid HDR panorama/environment bounds");
    }

    for (std::size_t index = 0; index < panorama.pixels.size(); ++index) {
      if ((index % 4 == 3 && panorama.pixels[index] != 1)
          || (index % 4 != 3 && panorama.pixels[index] > 65504)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Environment requires opaque radiance representable in RGBA16F");
      }
    }

    const auto cubeBytes = [](std::uint32_t side) {
      std::uint64_t bytes{};

      while (side) {
        bytes += static_cast<std::uint64_t>(side) * side * 6 * 8;
        side /= 2;
      }

      return bytes;
    };
    const auto bytes = cubeBytes(config.radianceSize) + cubeBytes(config.specularSize)
                       + static_cast<std::uint64_t>(config.diffuseSize) * config.diffuseSize * 6 * 8
                       + static_cast<std::uint64_t>(config.brdfSize) * config.brdfSize * 8;
    return EnvironmentInfo{config, static_cast<std::uint32_t>(std::bit_width(config.radianceSize)),
                           static_cast<std::uint32_t>(std::bit_width(config.specularSize)), bytes};
  }

  auto environmentDirection(std::uint32_t face, Math::Vec2 uv) -> Core::Result<Math::Vec3> {
    if (face >= 6 || !std::isfinite(uv.x) || !std::isfinite(uv.y) || uv.x < 0 || uv.x > 1
        || uv.y < 0 || uv.y > 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid environment cube face/UV");
    }

    const auto x = uv.x * 2 - 1;
    const auto y = uv.y * 2 - 1;
    Math::Vec3 direction;

    switch (face) {
      case 0:
        direction = {1, -y, -x};
        break;
      case 1:
        direction = {-1, -y, x};
        break;
      case 2:
        direction = {x, 1, y};
        break;
      case 3:
        direction = {x, -1, -y};
        break;
      case 4:
        direction = {x, -y, 1};
        break;
      case 5:
        direction = {-x, -y, -1};
        break;
    }

    const auto length = std::hypot(direction.x, direction.y, direction.z);
    return Math::Vec3{direction.x / length, direction.y / length, direction.z / length};
  }
}  // namespace Dot::Graphics

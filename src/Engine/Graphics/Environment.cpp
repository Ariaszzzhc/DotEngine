module;

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <exception>
#include <expected>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

module Dot.Graphics.Environment;

namespace Dot::Graphics {
  namespace {
    auto describe(const EnvironmentConfig& config) -> EnvironmentInfo {
      const auto cubeBytes = [](std::uint32_t side) {
        std::uint64_t bytes{};

        while (side) {
          bytes += static_cast<std::uint64_t>(side) * side * 6 * 8;
          side /= 2;
        }

        return bytes;
      };

      const auto bytes
          = cubeBytes(config.radianceSize) + cubeBytes(config.specularSize)
            + static_cast<std::uint64_t>(config.diffuseSize) * config.diffuseSize * 6 * 8
            + static_cast<std::uint64_t>(config.brdfSize) * config.brdfSize * 8;
      return EnvironmentInfo{
          config, static_cast<std::uint32_t>(std::bit_width(config.radianceSize)),
          static_cast<std::uint32_t>(std::bit_width(config.specularSize)), bytes};
    }
  }  // namespace

  auto validateEnvironmentConfig(const EnvironmentConfig& config) -> Core::Result<void> {
    const auto size = [](std::uint32_t value, std::uint32_t minimum, std::uint32_t maximum) {
      return value >= minimum && value <= maximum && std::has_single_bit(value);
    };

    if (!size(config.radianceSize, 16, 512) || !size(config.specularSize, 16, 512)
        || !size(config.diffuseSize, 4, 128) || !size(config.brdfSize, 16, 256)
        || config.samples < 32 || config.samples > 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid environment processing bounds");
    }

    return {};
  }

  auto validateEnvironment(const HdrImage& panorama, const EnvironmentConfig& config)
      -> Core::Result<EnvironmentInfo> {
    auto valid = validateEnvironmentConfig(config);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    valid = validateImage(panorama);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    if (panorama.width != panorama.height * 2 || panorama.height < 2) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid HDR panorama dimensions");
    }

    for (std::size_t index = 0; index < panorama.pixels.size(); ++index) {
      if ((index % 4 == 3 && panorama.pixels[index] != 1)
          || (index % 4 != 3 && panorama.pixels[index] > 65504)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Environment requires opaque radiance representable in RGBA16F");
      }
    }

    return describe(config);
  }

  struct EnvironmentSource::Impl {
    std::vector<HdrImage> levels;
  };

  EnvironmentSource::EnvironmentSource(std::shared_ptr<const Impl> impl) noexcept
      : m_impl(std::move(impl)) {
  }

  EnvironmentSource::EnvironmentSource(const EnvironmentSource&) noexcept = default;

  auto EnvironmentSource::operator=(const EnvironmentSource&) noexcept
      -> EnvironmentSource& = default;

  EnvironmentSource::EnvironmentSource(EnvironmentSource&&) noexcept = default;

  auto EnvironmentSource::operator=(EnvironmentSource&&) noexcept -> EnvironmentSource& = default;

  EnvironmentSource::~EnvironmentSource() = default;

  auto EnvironmentSource::prepare(HdrImage panorama, std::stop_token stop)
      -> Core::Result<EnvironmentSource> {
    try {
      if (stop.stop_requested()) {
        return Core::fail(Core::ErrorCode::Unavailable, "Environment source preparation canceled");
      }

      auto valid = validateEnvironment(panorama);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      auto impl = std::make_shared<Impl>();
      impl->levels.reserve(14);
      impl->levels.push_back(std::move(panorama));

      while (impl->levels.back().width > 1) {
        if (stop.stop_requested()) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Environment source preparation canceled");
        }

        const auto& image = impl->levels.back();
        HdrImage next{std::max(1U, image.width / 2), std::max(1U, image.height / 2), {}};
        next.pixels.resize(static_cast<std::size_t>(next.width) * next.height * 4);

        for (std::uint32_t y = 0; y < next.height; ++y) {
          for (std::uint32_t x = 0; x < next.width; ++x) {
            const auto beginX = x * image.width / next.width;
            const auto endX = (x + 1) * image.width / next.width;
            const auto beginY = y * image.height / next.height;
            const auto endY = (y + 1) * image.height / next.height;
            std::array<double, 3> sum{};
            double weight{};

            for (auto sy = beginY; sy < endY; ++sy) {
              const auto solidAngle
                  = std::sin((static_cast<double>(sy) + 0.5) * 3.141592653589793 / image.height);

              for (auto sx = beginX; sx < endX; ++sx) {
                const auto offset = (static_cast<std::size_t>(sy) * image.width + sx) * 4;

                for (std::size_t channel = 0; channel < 3; ++channel) {
                  sum[channel] += image.pixels[offset + channel] * solidAngle;
                }

                weight += solidAngle;
              }
            }

            const auto offset = (static_cast<std::size_t>(y) * next.width + x) * 4;

            for (std::size_t channel = 0; channel < 3; ++channel) {
              next.pixels[offset + channel] = static_cast<float>(sum[channel] / weight);
            }

            next.pixels[offset + 3] = 1;
          }
        }

        impl->levels.push_back(std::move(next));
      }

      return EnvironmentSource(std::move(impl));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare environment source: " + std::string(error.what()));
    }
  }

  auto EnvironmentSource::levels() const -> Core::Result<std::span<const HdrImage>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Environment source was moved from");
    }

    return std::span<const HdrImage>(m_impl->levels);
  }

  auto EnvironmentSource::description(const EnvironmentConfig& config) const
      -> Core::Result<EnvironmentInfo> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Environment source was moved from");
    }

    auto checked = validateEnvironmentConfig(config);
    return checked ? Core::Result<EnvironmentInfo>{describe(config)}
                   : std::unexpected(checked.error());
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

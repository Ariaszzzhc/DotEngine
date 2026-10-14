#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <print>
#include <string>
#include <vector>

import Dot.Core.Files;
import Dot.Graphics.Renderer;
import Dot.Assets.Image;

namespace Dot::Runtime {
  /** Run actual GPU preprocessing and save a diagnostic Reinhard/sRGB cube cross.
   * This tests environment resources, not a fully lit scene or native presentation.
   * 运行真实 GPU 预处理并保存诊断 Reinhard/sRGB 立方体展开图；验证环境资源，不声称
   * 完整受光场景或原生呈现。 */
  auto testEnvironment(const std::filesystem::path& source, const std::filesystem::path& output)
      -> int {
    auto files = Core::FileSystem::open(source.parent_path());
    auto destination = Core::FileSystem::open(output);

    if (!files || !destination) {
      std::println(stderr, "{}", files ? destination.error().message : files.error().message);
      return 1;
    }

    auto image = Assets::loadHdrImage(*files, source.filename());

    if (!image) {
      std::println(stderr, "{}", image.error().message);
      return 1;
    }

    auto renderer = Graphics::Renderer::create();

    if (!renderer) {
      std::println(stderr, "{}", renderer.error().message);
      return 1;
    }

    auto map = renderer->bakeEnvironment(*image, {128, 32, 128, 128, 512});

    if (!map) {
      std::println(stderr, "{}", map.error().message);
      return 1;
    }

    constexpr std::uint32_t side = 128;
    const std::array<std::array<std::uint32_t, 2>, 6> tiles{
        {{2, 1}, {0, 1}, {1, 0}, {1, 2}, {1, 1}, {3, 1}}};
    std::string ppm = std::format("P6\n{} {}\n255\n", side * 4, side * 3);
    const auto header = ppm.size();
    ppm.resize(header + side * 4 * side * 3 * 3, '\0');
    float maximum{};

    for (std::uint32_t face = 0; face < 6; ++face) {
      auto pixels = renderer->readEnvironment(*map, Graphics::EnvironmentSurface::Radiance, face);

      if (!pixels) {
        std::println(stderr, "{}", pixels.error().message);
        return 1;
      }

      for (std::uint32_t y = 0; y < side; ++y) {
        for (std::uint32_t x = 0; x < side; ++x) {
          const auto input = (static_cast<std::size_t>(y) * side + x) * 4;
          const auto offset = header
                              + ((static_cast<std::size_t>(tiles[face][1]) * side + y) * side * 4
                                 + tiles[face][0] * side + x)
                                    * 3;

          for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto radiance = pixels->pixels[input + channel];
            maximum = std::max(maximum, radiance);
            const auto linear = radiance / (1 + radiance);
            const auto display = linear <= 0.0031308F
                                     ? linear * 12.92F
                                     : 1.055F * std::pow(linear, 1 / 2.4F) - 0.055F;
            ppm[offset + channel] = static_cast<char>(std::lround(display * 255));
          }
        }
      }
    }

    auto written = destination->writeTextAtomic("environment-radiance.ppm", ppm);

    if (!written || maximum <= 1) {
      std::println(stderr, "{}", written ? "HDR highlight was lost" : written.error().message);
      return 1;
    }

    auto brdf = renderer->readEnvironment(*map, Graphics::EnvironmentSurface::Brdf);

    if (!brdf) {
      std::println(stderr, "{}", brdf.error().message);
      return 1;
    }

    auto shared = *map;
    map = Graphics::EnvironmentMap{};

    if (renderer->resourceStats()->environments != 1) {
      return 1;
    }

    shared = {};

    if (renderer->resourceStats()->environmentBytes != 0) {
      return 1;
    }

    std::println("Environment GPU acceptance passed; peak radiance {}", maximum);
    return 0;
  }
}  // namespace Dot::Runtime

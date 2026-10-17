#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

import Dot.Core.Result;
import Dot.Graphics.Image;

namespace {
  using namespace Dot::Graphics;

  auto referenceMip(const Image& image, ColorSpace space) -> Image {
    Image result{std::max(1U, image.width / 2), std::max(1U, image.height / 2), {}};
    result.pixels.resize(static_cast<std::size_t>(result.width) * result.height * 4);

    for (std::uint32_t y = 0; y < result.height; ++y) {
      for (std::uint32_t x = 0; x < result.width; ++x) {
        std::array<double, 4> sum{};
        std::size_t count{};

        for (auto sy = y * image.height / result.height;
             sy < (y + 1) * image.height / result.height; ++sy) {
          for (auto sx = x * image.width / result.width; sx < (x + 1) * image.width / result.width;
               ++sx) {
            ++count;

            for (std::size_t channel = 0; channel < 4; ++channel) {
              double value
                  = image.pixels[(static_cast<std::size_t>(sy) * image.width + sx) * 4 + channel]
                    / 255.0;

              if (space == ColorSpace::Srgb && channel < 3) {
                value = value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
              }

              sum[channel] += value;
            }
          }
        }

        for (std::size_t channel = 0; channel < 4; ++channel) {
          auto value = sum[channel] / static_cast<double>(count);

          if (space == ColorSpace::Srgb && channel < 3) {
            value = value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
          }

          result.pixels[(static_cast<std::size_t>(y) * result.width + x) * 4 + channel]
              = static_cast<std::uint8_t>(std::clamp(std::round(value * 255), 0.0, 255.0));
        }
      }
    }

    return result;
  }
}  // namespace

TEST_CASE("image.mip_preserves_every_byte_pair_and_linear_alpha") {
  Image image{512, 512, {}};
  image.pixels.resize(512 * 512 * 4);

  for (std::uint32_t y = 0; y < 512; ++y) {
    for (std::uint32_t x = 0; x < 512; ++x) {
      const auto offset = (static_cast<std::size_t>(y) * 512 + x) * 4;
      const auto a = static_cast<std::uint8_t>(x / 2);
      const auto b = static_cast<std::uint8_t>(y / 2);
      const auto value = x % 2 ? a : b;
      image.pixels[offset] = value;
      image.pixels[offset + 1] = 255 - value;
      image.pixels[offset + 2] = y % 2 ? a : b;
      image.pixels[offset + 3] = value;
    }
  }

  const auto original = image.pixels;

  for (auto space : {ColorSpace::Linear, ColorSpace::Srgb}) {
    auto mip = downsampleImage(image, space);
    REQUIRE(mip);
    CHECK(mip->width == 256);
    CHECK(mip->height == 256);
    CHECK(mip->pixels == referenceMip(image, space).pixels);
  }

  CHECK(image.pixels == original);
  auto srgb = downsampleImage(Image{2, 1, {0, 0, 0, 0, 255, 255, 255, 255}}, ColorSpace::Srgb);
  REQUIRE(srgb);
  CHECK(srgb->pixels == std::vector<std::uint8_t>{188, 188, 188, 128});
}

TEST_CASE("image.mip_odd_edges_and_complete_chains_keep_owned_results") {
  for (const auto dimensions : {std::array{1U, 1U}, std::array{1U, 17U}, std::array{19U, 1U},
                                std::array{3U, 3U}, std::array{17U, 11U}, std::array{8192U, 1U}}) {
    for (auto space : {ColorSpace::Linear, ColorSpace::Srgb}) {
      Image source{dimensions[0], dimensions[1], {}};
      source.pixels.resize(static_cast<std::size_t>(source.width) * source.height * 4);

      for (std::size_t i = 0; i < source.pixels.size(); ++i) {
        source.pixels[i] = static_cast<std::uint8_t>((i * 73 + i / 7) % 256);
      }

      do {
        auto expected = referenceMip(source, space);
        const auto original = source.pixels;
        auto actual = downsampleImage(source, space);
        REQUIRE(actual);
        CHECK(source.pixels == original);
        CHECK(actual->width == expected.width);
        CHECK(actual->height == expected.height);
        CHECK(actual->pixels == expected.pixels);
        source.pixels.clear();
        CHECK(actual->pixels == expected.pixels);
        source = std::move(*actual);
      } while (source.width != 1 || source.height != 1);
    }
  }

  auto odd = downsampleImage(Image{3, 1, {0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 0}},
                             ColorSpace::Linear);
  REQUIRE(odd);
  CHECK(odd->pixels == std::vector<std::uint8_t>{85, 85, 85, 170});
}

TEST_CASE("image.mip_rejects_invalid_boundaries_and_recovers") {
  for (const auto& image : {Image{}, Image{1, 1, {1, 2, 3}}, Image{8193, 1, {}},
                            Image{8192, 8192, {}}, Image{1, 1, {1, 2, 3, 4, 5}}}) {
    auto mip = downsampleImage(image, ColorSpace::Srgb);
    REQUIRE_FALSE(mip);
    CHECK(mip.error().code == Dot::Core::ErrorCode::InvalidArgument);
    CHECK_FALSE(mip.error().message.empty());
  }

  CHECK_FALSE(downsampleImage(Image{1, 1, {1, 2, 3, 4}}, static_cast<ColorSpace>(99)));
  auto valid = downsampleImage(Image{1, 1, {1, 2, 3, 4}}, ColorSpace::Srgb);
  REQUIRE(valid);
  CHECK(valid->pixels == std::vector<std::uint8_t>{1, 2, 3, 4});
}

TEST_CASE("image.mip_parallel_calls_share_no_mutable_pixels") {
  const Image image{3, 1, {0, 8, 255, 1, 9, 10, 128, 128, 11, 255, 0, 255}};
  std::array<bool, 8> success{};
  std::vector<std::jthread> threads;

  for (std::size_t index = 0; index < success.size(); ++index) {
    threads.emplace_back([&, index] {
      for (std::size_t run = 0; run < 100; ++run) {
        auto result = downsampleImage(image, ColorSpace::Srgb);

        if (!result || result->pixels != referenceMip(image, ColorSpace::Srgb).pixels) {
          return;
        }

        result->pixels[0] = 0;
      }

      success[index] = true;
    });
  }

  threads.clear();

  for (auto passed : success) {
    CHECK(passed);
  }
}

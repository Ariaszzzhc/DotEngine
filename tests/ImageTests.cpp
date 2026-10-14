#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <thread>
#include <vector>

import Dot.Assets.Image;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Graphics.Image;

namespace {
  auto fixture(std::string_view name) -> std::vector<std::byte> {
    auto files = Dot::Core::FileSystem::open(DOT_IMAGE_FIXTURE_DIRECTORY).value();
    return files.readBytes(name).value();
  }
}  // namespace

TEST_CASE("image.png_retains_owned_rgba_row_order_and_alpha") {
  auto bytes = fixture("surface.png");
  auto image = Dot::Assets::decodeImage(bytes);
  REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
  CHECK(image->width == 3);
  CHECK(image->height == 2);
  CHECK(image->pixels == std::vector<std::uint8_t>{255, 0,   0,   255, 0,   255, 0,   128,
                                                   0,   0,   255, 255, 255, 255, 0,   255,
                                                   0,   255, 255, 0,   255, 0,   255, 255});
  bytes.clear();
  CHECK(image->pixels[0] == 255);
  CHECK(Dot::Graphics::validateImage(*image));
}

TEST_CASE("image.jpeg_and_hdr_use_distinct_explicit_decode_paths") {
  auto jpeg = fixture("surface.jpg");
  auto image = Dot::Assets::decodeImage(jpeg);
  REQUIRE(image);
  CHECK(image->width == 3);
  CHECK(image->height == 2);
  CHECK(image->pixels.size() == 24);

  for (std::size_t index = 3; index < image->pixels.size(); index += 4) {
    CHECK(image->pixels[index] == 255);
  }

  auto hdr = fixture("radiance.hdr");
  auto radiance = Dot::Assets::decodeHdrImage(hdr);
  REQUIRE_MESSAGE(radiance, (radiance ? "" : radiance.error().message));
  CHECK(radiance->width == 2);
  CHECK(radiance->height == 2);
  REQUIRE(radiance->pixels.size() == 16);
  CHECK(radiance->pixels[0] == 8);
  CHECK(radiance->pixels[1] == 4);
  CHECK(radiance->pixels[2] == 2);
  CHECK(radiance->pixels[3] == 1);
  CHECK(radiance->pixels[8] == 0.5F);
  CHECK(Dot::Graphics::validateImage(*radiance));
  CHECK_FALSE(Dot::Assets::decodeImage(hdr));
  CHECK_FALSE(Dot::Assets::decodeHdrImage(jpeg));
}

TEST_CASE("image.corrupt_unsupported_precision_and_oversized_inputs_fail") {
  CHECK_FALSE(Dot::Assets::decodeImage({}));
  const std::array<std::byte, 4> garbage{};
  CHECK_FALSE(Dot::Assets::decodeImage(garbage));
  CHECK_FALSE(Dot::Assets::decodeHdrImage(garbage));
  auto png = fixture("surface.png");
  CHECK_FALSE(Dot::Assets::decodeImage(std::span(png).first(12)));
  auto precision = Dot::Assets::decodeImage(fixture("precision.png"));
  REQUIRE_FALSE(precision);
  CHECK(precision.error().code == Dot::Core::ErrorCode::Unavailable);
  png[16] = std::byte{0};
  png[17] = std::byte{0};
  png[18] = std::byte{0x20};
  png[19] = std::byte{1};
  CHECK_FALSE(Dot::Assets::decodeImage(png));
  auto hdr = fixture("radiance.hdr");
  CHECK_FALSE(Dot::Assets::decodeHdrImage(std::span(hdr).first(10)));
  std::vector<std::byte> oversized(64 * 1024 * 1024 + 1);
  CHECK_FALSE(Dot::Assets::decodeImage(oversized));
  CHECK_FALSE(Dot::Assets::decodeHdrImage(oversized));
}

TEST_CASE("image.pixel_storage_and_hdr_values_validate_without_allocating_fake_results") {
  using namespace Dot::Graphics;
  CHECK_FALSE(validateImage(Image{}));
  CHECK_FALSE(validateImage(Image{1, 1, {1, 2, 3}}));
  CHECK_FALSE(validateImage(Image{8193, 1, {}}));
  CHECK_FALSE(validateImage(Image{8192, 8192, {}}));
  HdrImage hdr{1, 1, {8, 4, 2, 1}};
  CHECK(validateImage(hdr));
  hdr.pixels[0] = -1;
  CHECK_FALSE(validateImage(hdr));
  hdr.pixels[0] = std::numeric_limits<float>::infinity();
  CHECK_FALSE(validateImage(hdr));
  hdr.pixels[0] = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(validateImage(hdr));
  hdr.pixels[0] = 8;
  hdr.pixels[3] = 2;
  CHECK_FALSE(validateImage(hdr));
}

TEST_CASE("image.project_paths_and_codec_errors_keep_context_and_recover") {
  auto files = Dot::Core::FileSystem::open(DOT_IMAGE_FIXTURE_DIRECTORY).value();
  auto image = Dot::Assets::loadImage(files, "surface.png");
  REQUIRE(image);
  CHECK(Dot::Assets::loadHdrImage(files, "radiance.hdr"));
  CHECK_FALSE(Dot::Assets::loadImage(files, "../surface.png"));
  auto missing = Dot::Assets::loadImage(files, "missing.png");
  REQUIRE_FALSE(missing);
  CHECK(missing.error().code == Dot::Core::ErrorCode::NotFound);
  auto wrong = Dot::Assets::loadImage(files, "radiance.hdr");
  REQUIRE_FALSE(wrong);
  CHECK(wrong.error().message.find("radiance.hdr") != std::string::npos);
  CHECK(Dot::Assets::loadImage(files, "surface.png"));
}

TEST_CASE("image.parallel_decodes_keep_independent_state_and_row_orientation") {
  auto source = fixture("surface.png");
  std::array<bool, 4> success{};
  std::vector<std::jthread> workers;

  for (std::size_t index = 0; index < success.size(); ++index) {
    workers.emplace_back([&, index] {
      auto decoded = Dot::Assets::decodeImage(source);
      success[index] = decoded && decoded->pixels[0] == 255 && decoded->pixels[1] == 0
                       && decoded->pixels[12] == 255 && decoded->pixels[13] == 255;
    });
  }

  workers.clear();

  for (const auto passed : success) {
    CHECK(passed);
  }
}

TEST_CASE("image.png_encoding_is_lossless_owned_and_rejects_invalid_storage") {
  Dot::Graphics::Image image{2, 1, {1, 2, 3, 4, 5, 6, 7, 255}};
  auto encoded = Dot::Assets::encodeImage(image);
  REQUIRE(encoded);
  auto decoded = Dot::Assets::decodeImage(*encoded);
  REQUIRE(decoded);
  CHECK(decoded->pixels == image.pixels);
  image.pixels.clear();
  CHECK(Dot::Assets::decodeImage(*encoded)->pixels == decoded->pixels);
  CHECK_FALSE(Dot::Assets::encodeImage(image));
}

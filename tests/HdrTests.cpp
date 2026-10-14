#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

import Dot.Graphics.Renderer;
import Dot.Core.Result;

using namespace Dot::Graphics;

namespace {
  auto rgba(const Image& image, std::uint32_t x, std::uint32_t y) -> std::array<std::uint8_t, 4> {
    const auto index = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {image.pixels.at(index), image.pixels.at(index + 1), image.pixels.at(index + 2),
            image.pixels.at(index + 3)};
  }

  auto rgba(const HdrImage& image, std::uint32_t x, std::uint32_t y) -> std::array<float, 4> {
    const auto index = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {image.pixels.at(index), image.pixels.at(index + 1), image.pixels.at(index + 2),
            image.pixels.at(index + 3)};
  }
}  // namespace

TEST_CASE("hdr.raw_readback_retains_radiance_padding_alpha_and_completed_frame") {
  auto renderer = Renderer::create().value();
  auto target = renderer.createTarget(13, 7).value();
  CHECK_FALSE(renderer.readHdrTarget(target));
  REQUIRE(renderer.renderMeshesInto(target, {}, {}, {8, 4, 2, 0.5F}));
  auto raw = renderer.readHdrTarget(target);
  REQUIRE_MESSAGE(raw, (raw ? "" : raw.error().message));
  CHECK(raw->pixels.size() == 13 * 7 * 4);

  for (std::uint32_t y = 0; y < 7; ++y) {
    for (std::uint32_t x = 0; x < 13; ++x) {
      CHECK(rgba(*raw, x, y) == std::array<float, 4>{8, 4, 2, 0.5F});
    }
  }

  CHECK(renderer.resourceStats()->targetBytes == 13 * 7 * 16);
  CHECK(target.info()->frames == 1);
  const auto mapped = renderer.readTarget(target).value();
  CHECK(rgba(mapped, 6, 3)[0] > rgba(mapped, 6, 3)[2]);
  CHECK(rgba(mapped, 6, 3)[3] == 128);
  REQUIRE(renderer.setPostProcess({2, ToneMap::Reinhard, ColorSpace::Linear}));
  CHECK(renderer.readTarget(target)->pixels == mapped.pixels);
  CHECK(renderer.readHdrTarget(target)->pixels == raw->pixels);
}

TEST_CASE("hdr.reinhard_exposure_and_encoding_apply_once_without_clipping_source") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::Reinhard, ColorSpace::Linear}})
            .value();
  auto target = renderer.createTarget(1, 1).value();
  REQUIRE(renderer.renderMeshesInto(target, {}, {}, {1, 1, 1, 1}));
  CHECK(rgba(renderer.readTarget(target).value(), 0, 0)[0] == 128);
  REQUIRE(renderer.renderMeshesInto(target, {}, {}, {8, 8, 8, 1}));
  CHECK(rgba(renderer.readTarget(target).value(), 0, 0)[0] == 227);
  CHECK(rgba(renderer.readHdrTarget(target).value(), 0, 0)[0] == 8);
  REQUIRE(renderer.setPostProcess({1, ToneMap::Reinhard, ColorSpace::Linear}));
  REQUIRE(renderer.renderMeshesInto(target, {}, {}, {1, 1, 1, 1}));
  CHECK(rgba(renderer.readTarget(target).value(), 0, 0)[0] == 170);
  CHECK(rgba(renderer.readHdrTarget(target).value(), 0, 0)[0] == 1);
  REQUIRE(renderer.setPostProcess({0, ToneMap::Reinhard, ColorSpace::Srgb}));
  REQUIRE(renderer.renderMeshesInto(target, {}, {}, {1, 1, 1, 1}));
  CHECK(rgba(renderer.readTarget(target).value(), 0, 0)[0] == 188);
}

TEST_CASE("hdr.aces_fitted_default_matches_gray_references_and_retains_highlight_separation") {
  auto renderer = Renderer::create().value();
  CHECK(renderer.postProcess()->toneMap == ToneMap::AcesFitted);
  CHECK(renderer.postProcess()->outputEncoding == ColorSpace::Srgb);
  auto target = renderer.createTarget(1, 1).value();
  std::array<std::uint8_t, 3> levels{};
  const std::array<float, 3> values{0.18F, 1, 8};
  const std::array<unsigned, 3> reference{91, 206, 249};

  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto value = values[index];
    REQUIRE(renderer.renderMeshesInto(target, {}, {}, {value, value, value, 1}));
    const auto pixel = rgba(renderer.readTarget(target).value(), 0, 0);
    levels[index] = pixel[0];
    CHECK(pixel[0] == doctest::Approx(reference[index]).epsilon(0.015));
    CHECK(pixel[1] == pixel[0]);
    CHECK(pixel[2] == pixel[0]);
  }

  CHECK(levels[0] < levels[1]);
  CHECK(levels[1] < levels[2]);
  REQUIRE(renderer.setPostProcess({0, ToneMap::LinearClamp, ColorSpace::Srgb}));
  REQUIRE(renderer.renderMeshesInto(target, {}, {}, {0.0031308F, 0.25F, 0.5F, 0.25F}));
  const auto pixel = rgba(renderer.readTarget(target).value(), 0, 0);
  CHECK(pixel[0] == 10);
  CHECK(pixel[1] == 137);
  CHECK(pixel[2] == 188);
  CHECK(pixel[3] == 64);
}

TEST_CASE("hdr.emissive_materials_preserve_per_draw_data_and_depth_occlusion") {
  auto renderer = Renderer::create().value();
  auto mesh = renderer.upload(quad({2, 2}).value()).value();
  auto target = renderer.createTarget(16, 16).value();
  std::array draws{MeshDraw{&mesh}, MeshDraw{&mesh}};
  draws[0].transform.data[14] = 0.2F;
  draws[0].material.baseColor = {0, 0, 0, 1};
  draws[0].material.emissive = {8, 0, 0};
  draws[1].transform.data[14] = 0.8F;
  draws[1].material.baseColor = {0, 0, 0, 1};
  draws[1].material.emissive = {0, 0, 16};
  REQUIRE(renderer.renderMeshesInto(target, draws, {}));
  CHECK(rgba(renderer.readHdrTarget(target).value(), 8, 8) == std::array<float, 4>{8, 0, 0, 1});
  auto raw = renderer.readHdrTarget(target).value();
  auto display = renderer.readTarget(target).value();
  draws[0].material.emissive.x = 65504;
  CHECK_FALSE(renderer.renderMeshesInto(target, draws, {}));
  draws[0].material.emissive.x = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(renderer.renderMeshesInto(target, draws, {}));
  CHECK_FALSE(renderer.renderMeshesInto(target, {}, {}, {65505, 0, 0, 1}));
  CHECK(renderer.readHdrTarget(target)->pixels == raw.pixels);
  CHECK(renderer.readTarget(target)->pixels == display.pixels);
  CHECK(target.info()->frames == 1);
  draws[0].material.emissive = {65503, 0, 0};
  draws[0].material.baseColor.x = 1;
  REQUIRE(renderer.renderMeshesInto(target, draws, {}));
  CHECK(rgba(renderer.readHdrTarget(target).value(), 8, 8)[0] == 65504);
}

TEST_CASE("hdr.srgb_texture_decodes_once_and_final_output_encodes_once") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Srgb}})
            .value();
  auto texture = renderer.uploadTexture(Image{1, 1, {128, 128, 128, 128}}).value();
  auto mesh = renderer.upload(quad({2, 2}).value()).value();
  auto target = renderer.createTarget(16, 16).value();
  std::array draws{MeshDraw{&mesh, {}, {1, 1, 1, 1}, Material{{1, 1, 1, 1}, texture}}};
  REQUIRE(renderer.renderMeshesInto(target, draws, {}));
  const auto raw = rgba(renderer.readHdrTarget(target).value(), 8, 8);
  CHECK(raw[0] == doctest::Approx(0.21586).epsilon(0.002));
  CHECK(raw[3] == 1);
  CHECK(rgba(renderer.readTarget(target).value(), 8, 8)
        == std::array<std::uint8_t, 4>{128, 128, 128, 255});
  draws[0].material.alphaMode = AlphaMode::Blend;
  REQUIRE(renderer.renderMeshesInto(target, draws, {}, {0, 0, 0, 0}));
  const auto blended = rgba(renderer.readHdrTarget(target).value(), 8, 8);
  CHECK(blended[0] == doctest::Approx(0.21586 * 128 / 255).epsilon(0.002));
  CHECK(blended[3] == doctest::Approx(128.0 / 255).epsilon(0.002));
}

TEST_CASE("hdr.settings_and_resources_reject_invalid_owners_and_reuse_working_storage") {
  CHECK_FALSE(Renderer::create(nullptr, {.postProcess = {17}}));
  auto renderer = Renderer::create().value();
  const auto settings = renderer.postProcess().value();
  CHECK_FALSE(renderer.setPostProcess({std::numeric_limits<float>::infinity()}));
  CHECK(renderer.postProcess().value() == settings);
  auto target = renderer.createTarget(13, 7).value();
  REQUIRE(renderer.renderMeshesInto(target, {}, {}));
  auto foreign = Renderer::create().value();
  CHECK_FALSE(foreign.readHdrTarget(target));
  bool rejected{};
  std::jthread worker([&] {
    rejected = !renderer.setPostProcess({}) && !renderer.postProcess()
               && !renderer.readHdrTarget(target);
  });
  worker.join();
  CHECK(rejected);
  CHECK(renderer.render({}, {}, 13, 7));
  CHECK(renderer.resourceStats()->frameBytes == 13 * 7 * 12);
  CHECK(renderer.render({}, {}, 13, 7));
  CHECK(renderer.resourceStats()->frameBytes == 13 * 7 * 12);
  CHECK(renderer.render({}, {}, 3, 5));
  CHECK(renderer.resourceStats()->frameBytes == 3 * 5 * 12);
  auto moved = std::move(renderer);
  CHECK_FALSE(renderer.setPostProcess({}));
  CHECK_FALSE(renderer.readHdrTarget(target));
  CHECK(moved.readHdrTarget(target));
  CHECK(moved.postProcess().value() == settings);
}

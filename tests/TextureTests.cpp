#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

import Dot.Graphics.Renderer;
import Dot.Core.Result;
import Dot.Math;

using namespace Dot::Graphics;

namespace {
  auto uvQuad(Dot::Math::Vec2 uv) -> MeshData {
    auto mesh = quad({2, 2}).value();

    for (auto& vertex : mesh.vertices) {
      vertex.texCoord = uv;
    }

    return mesh;
  }

  auto center(const Image& image) -> std::array<std::uint8_t, 4> {
    const auto offset
        = (static_cast<std::size_t>(image.height / 2) * image.width + image.width / 2) * 4;
    return {image.pixels.at(offset), image.pixels.at(offset + 1), image.pixels.at(offset + 2),
            image.pixels.at(offset + 3)};
  }

  auto sampled(Renderer& renderer, const Texture& texture, Dot::Math::Vec2 uv,
               Dot::Math::Vec4 factor = {1, 1, 1, 1}) -> Image {
    auto mesh = renderer.upload(uvQuad(uv)).value();
    const std::array draws{MeshDraw{&mesh, {}, {1, 1, 1, 1}, Material{factor, texture}}};
    return renderer.renderMeshes(draws, {}, 16, 16).value();
  }
}  // namespace

TEST_CASE("texture.precomputed_levels_validate_before_allocation_and_preserve_authored_mips") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}})
            .value();
  TextureOptions options{.colorSpace = ColorSpace::Linear};
  std::vector<Image> levels{{2, 2, std::vector<std::uint8_t>(16, 255)}, {1, 1, {0, 255, 0, 255}}};
  CHECK_FALSE(renderer.uploadTextureLevels({}));
  auto invalid = levels;
  invalid[1] = {2, 1, std::vector<std::uint8_t>(8, 255)};
  CHECK_FALSE(renderer.uploadTextureLevels(invalid, options));
  invalid = levels;
  invalid[1].pixels.pop_back();
  CHECK_FALSE(renderer.uploadTextureLevels(invalid, options));
  CHECK_FALSE(renderer.uploadTextureLevels(std::span(levels).first(1), options));
  auto single = options;
  single.mipmaps = false;
  CHECK_FALSE(renderer.uploadTextureLevels(levels, single));
  CHECK(renderer.resourceStats()->textures == 0);
  auto texture = renderer.uploadTextureLevels(levels, options).value();
  CHECK(texture.info()->mipLevels == 2);
  CHECK(renderer.resourceStats()->textureBytes == 20);
  auto mesh = renderer.upload(quad({2, 2}).value()).value();
  std::array draws{MeshDraw{&mesh, {}, {1, 1, 1, 1}, Material{{1, 1, 1, 1}, texture}}};
  auto pixel = renderer.renderMeshes(draws, {}, 1, 1).value();
  CHECK(pixel.pixels == std::vector<std::uint8_t>{0, 255, 0, 255});
}

TEST_CASE("texture.gpu_sampling_retains_owned_rgba_top_left_uv_and_material_factor") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}})
            .value();
  Image image{2, 2, {255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 255, 255, 255, 255, 255}};
  TextureOptions options{ColorSpace::Linear, TextureFilter::Nearest};
  auto texture = renderer.uploadTexture(image, options);
  REQUIRE_MESSAGE(texture, (texture ? "" : texture.error().message));
  image.pixels.clear();
  CHECK(center(sampled(renderer, *texture, {0.25F, 0.25F}))
        == std::array<std::uint8_t, 4>{255, 0, 0, 255});
  CHECK(center(sampled(renderer, *texture, {0.75F, 0.25F}))
        == std::array<std::uint8_t, 4>{0, 255, 0, 255});
  CHECK(center(sampled(renderer, *texture, {0.25F, 0.75F}))
        == std::array<std::uint8_t, 4>{0, 0, 255, 255});
  CHECK(center(sampled(renderer, *texture, {0.75F, 0.75F}, {0.5F, 0.25F, 0.75F, 0.5F}))
        == std::array<std::uint8_t, 4>{128, 64, 191, 255});
  CHECK(renderer.resourceStats()->textures == 1);
  CHECK(renderer.resourceStats()->textureBytes == 20);
}

TEST_CASE("texture.color_space_linear_filtering_and_address_modes_are_explicit") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}})
            .value();
  Image gray{1, 1, {128, 128, 128, 128}};
  auto linear = renderer.uploadTexture(gray, {.colorSpace = ColorSpace::Linear}).value();
  auto srgb = renderer.uploadTexture(gray).value();
  CHECK(center(sampled(renderer, linear, {}))[0] == 128);
  CHECK(center(sampled(renderer, srgb, {}))[0] == 55);
  CHECK(center(sampled(renderer, srgb, {}))[3] == 255);
  Image pair{2, 1, {255, 0, 0, 255, 0, 255, 0, 255}};

  for (const auto address :
       {TextureAddress::Clamp, TextureAddress::Repeat, TextureAddress::Mirror}) {
    TextureOptions options{ColorSpace::Linear, TextureFilter::Nearest, address, address, false};
    auto texture = renderer.uploadTexture(pair, options).value();
    auto pixel = center(sampled(renderer, texture, {1.25F, 0.5F}));
    CHECK(pixel[0] == (address == TextureAddress::Repeat ? 255 : 0));
    CHECK(pixel[1] == (address == TextureAddress::Repeat ? 0 : 255));
    pixel = center(sampled(renderer, texture, {-0.25F, 0.5F}));
    CHECK(pixel[0] == (address == TextureAddress::Repeat ? 0 : 255));
  }

  auto filtered = renderer.uploadTexture(pair, {.colorSpace = ColorSpace::Linear}).value();
  auto pixel = center(sampled(renderer, filtered, {0.5F, 0.5F}));
  CHECK(pixel[0] == 128);
  CHECK(pixel[1] == 128);
}

TEST_CASE("texture.mip_sampling_averages_linear_light_and_preserves_odd_edges") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}})
            .value();
  Image image{3, 1, {0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255}};
  auto texture = renderer.uploadTexture(image).value();
  CHECK(texture.info()->mipLevels == 2);
  CHECK(renderer.resourceStats()->textureBytes == 16);
  auto geometry = uvQuad({});
  geometry.vertices[0].texCoord = {0, 256};
  geometry.vertices[1].texCoord = {256, 256};
  geometry.vertices[2].texCoord = {256, 0};
  geometry.vertices[3].texCoord = {0, 0};
  auto mesh = renderer.upload(geometry).value();
  const std::array draws{MeshDraw{&mesh, {}, {1, 1, 1, 1}, Material{{1, 1, 1, 1}, texture}}};
  auto imageOut = renderer.renderMeshes(draws, {}, 16, 16);
  REQUIRE_MESSAGE(imageOut, (imageOut ? "" : imageOut.error().message));
  const auto pixel = center(*imageOut);
  CHECK(pixel[0] == doctest::Approx(85).epsilon(0.02));
  CHECK(pixel[1] == pixel[0]);
  CHECK(pixel[2] == pixel[0]);
}

TEST_CASE("texture.shared_resources_survive_moves_and_release_once") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}})
            .value();
  auto texture = renderer.uploadTexture(Image{1, 1, {255, 0, 0, 255}}).value();
  auto copy = texture;
  auto moved = std::move(texture);
  CHECK_FALSE(texture.info());
  CHECK(moved.info());
  CHECK(renderer.resourceStats()->textures == 1);
  copy = {};
  CHECK(renderer.resourceStats()->textures == 1);
  Material material{{1, 1, 1, 1}, moved};
  moved = {};
  CHECK(renderer.resourceStats()->textures == 1);
  material.baseColorTexture.reset();
  CHECK(renderer.resourceStats()->textures == 0);
  CHECK(renderer.resourceStats()->textureBytes == 0);
  Texture orphan;

  {
    auto temporary
        = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}})
              .value();
    orphan = temporary.uploadTexture(Image{1, 1, {255, 255, 255, 255}}).value();
  }

  CHECK_FALSE(orphan.info());
  orphan = {};
}

TEST_CASE("texture.foreign_empty_and_invalid_materials_preserve_the_last_target") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}})
            .value();
  auto other
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}})
            .value();
  auto texture = other.uploadTexture(Image{1, 1, {255, 0, 0, 255}}).value();
  auto mesh = renderer.upload(uvQuad({})).value();
  auto target = renderer.createTarget(16, 16).value();
  std::array draws{MeshDraw{&mesh}};
  REQUIRE(renderer.renderMeshesInto(target, draws, {}));
  auto before = renderer.readTarget(target).value();
  draws[0].material.baseColorTexture = texture;
  CHECK_FALSE(renderer.renderMeshesInto(target, draws, {}));
  draws[0].material.baseColorTexture = Texture{};
  CHECK_FALSE(renderer.renderMeshesInto(target, draws, {}));
  draws[0].material.baseColorTexture.reset();
  draws[0].material.baseColor.x = -1;
  CHECK_FALSE(renderer.renderMeshesInto(target, draws, {}));
  draws[0].material.baseColor.x = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(renderer.renderMeshesInto(target, draws, {}));
  CHECK(renderer.readTarget(target)->pixels == before.pixels);
  CHECK(target.info()->frames == 1);
  CHECK(renderer.resourceStats()->textures == 0);
  draws[0].material.baseColor = {1, 1, 1, 1};
  REQUIRE(renderer.renderMeshesInto(target, draws, {}));
}

TEST_CASE("texture.boundaries_thread_ownership_and_recovery_are_enforced") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}})
            .value();
  const Image valid{1, 1, {255, 255, 255, 255}};
  CHECK_FALSE(renderer.uploadTexture({}));
  CHECK_FALSE(renderer.uploadTexture(Image{1, 1, {1, 2, 3}}));
  CHECK_FALSE(renderer.uploadTexture(valid, {.colorSpace = static_cast<ColorSpace>(99)}));
  CHECK_FALSE(renderer.uploadTexture(valid, {.filter = static_cast<TextureFilter>(99)}));
  CHECK_FALSE(renderer.uploadTexture(valid, {.addressU = static_cast<TextureAddress>(99)}));
  CHECK_FALSE(renderer.uploadTexture(valid, {.maxAnisotropy = 0}));
  CHECK_FALSE(renderer.uploadTexture(valid, {.maxAnisotropy = 17}));
  CHECK_FALSE(
      renderer.uploadTexture(valid, {.filter = TextureFilter::Nearest, .maxAnisotropy = 2}));
  CHECK(renderer.resourceStats()->textures == 0);
  auto texture = renderer.uploadTexture(valid, {.maxAnisotropy = 16}).value();
  bool queryRejected{};
  bool uploadRejected{};
  std::jthread worker([&] {
    queryRejected = !texture.info();
    uploadRejected = !renderer.uploadTexture(valid);
  });
  worker.join();
  CHECK(queryRejected);
  CHECK(uploadRejected);
  CHECK(texture.info());
  auto moved = std::move(renderer);
  CHECK_FALSE(renderer.uploadTexture(valid));
  CHECK(texture.info());
  CHECK(center(sampled(moved, texture, {}))[0] == 255);
}

TEST_CASE("texture.residency_limits_fail_before_allocation_and_recover_after_release") {
  CHECK_FALSE(Renderer::create(nullptr, {.maxTextures = 0}));
  CHECK_FALSE(Renderer::create(nullptr, {.maxTextures = 4097}));
  CHECK_FALSE(Renderer::create(nullptr, {.maxTextureBytes = 0}));
  CHECK_FALSE(Renderer::create(nullptr, {.maxTextureBytes = 512 * 1024 * 1024 + 1}));
  auto renderer = Renderer::create(nullptr, {.maxTextures = 2, .maxTextureBytes = 20}).value();
  const Image image{1, 1, {255, 255, 255, 255}};
  auto first = renderer.uploadTexture(image).value();
  auto second = renderer.uploadTexture(image).value();
  auto full = renderer.uploadTexture(image);
  REQUIRE_FALSE(full);
  CHECK(full.error().code == Dot::Core::ErrorCode::Unavailable);
  CHECK(renderer.resourceStats()->textures == 2);
  CHECK(renderer.resourceStats()->textureBytes == 8);
  second = {};
  const Image larger{2, 2, std::vector<std::uint8_t>(16, 255)};
  auto overBytes = renderer.uploadTexture(larger);
  REQUIRE_FALSE(overBytes);
  CHECK(overBytes.error().code == Dot::Core::ErrorCode::Unavailable);
  CHECK(renderer.resourceStats()->textureBytes == 4);
  first = {};
  auto exact = renderer.uploadTexture(larger);
  REQUIRE(exact);
  CHECK(renderer.resourceStats()->textureBytes == 20);
  exact = {};
  CHECK(renderer.resourceStats()->textureBytes == 0);
  CHECK(renderer.uploadTexture(image));
}

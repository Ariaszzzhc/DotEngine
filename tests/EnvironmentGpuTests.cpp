#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <optional>
#include <thread>

import Dot.Graphics.Renderer;
import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Assets.Image;

using namespace Dot::Graphics;

namespace {
  const EnvironmentConfig small{16, 4, 16, 16, 128};

  auto constantPanorama(float value = 2) -> HdrImage {
    HdrImage image{8, 4, std::vector<float>(8 * 4 * 4, 1)};

    for (std::size_t index = 0; index < image.pixels.size(); index += 4) {
      image.pixels[index] = value;
      image.pixels[index + 1] = value * 0.5F;
      image.pixels[index + 2] = value * 0.25F;
    }

    return image;
  }
}  // namespace

TEST_CASE("environment.gpu_constant_energy_hdr_diffuse_specular_mips_and_brdf_are_real") {
  auto renderer = Renderer::create().value();
  auto baked = renderer.bakeEnvironment(constantPanorama(), small);
  REQUIRE_MESSAGE(baked, (baked ? "" : baked.error().message));
  const auto map = *baked;
  CHECK(renderer.resourceStats()->environments == 1);
  CHECK(renderer.resourceStats()->environmentBytes == map.info()->bytes);

  for (const auto surface :
       {EnvironmentSurface::Radiance, EnvironmentSurface::Diffuse, EnvironmentSurface::Specular}) {
    const auto levels = surface == EnvironmentSurface::Diffuse ? 1U : 5U;

    for (std::uint32_t level = 0; level < levels; ++level) {
      for (std::uint32_t face = 0; face < 6; ++face) {
        auto read = renderer.readEnvironment(map, surface, face, level);
        INFO("surface ", static_cast<unsigned>(surface), " face ", face, " level ", level);
        REQUIRE_MESSAGE(read, (read ? "" : read.error().message));
        const auto image = std::move(*read);

        for (std::size_t pixel = 0; pixel < image.pixels.size(); pixel += 4) {
          CHECK(image.pixels[pixel] == doctest::Approx(2).epsilon(0.003));
          CHECK(image.pixels[pixel + 1] == doctest::Approx(1).epsilon(0.003));
          CHECK(image.pixels[pixel + 2] == doctest::Approx(0.5).epsilon(0.003));
          CHECK(image.pixels[pixel + 3] == 1);
        }
      }
    }
  }

  auto brdf = renderer.readEnvironment(map, EnvironmentSurface::Brdf).value();
  const auto offset = ((brdf.height - 1) * brdf.width + brdf.width - 1) * 4;
  CHECK(brdf.pixels[offset] > 0.25F);
  CHECK(brdf.pixels[offset] < 0.4F);
  CHECK(brdf.pixels[offset + 1] < 0.01F);
  CHECK_FALSE(renderer.readEnvironment(map, EnvironmentSurface::Brdf, 1));
  CHECK_FALSE(renderer.readEnvironment(map, EnvironmentSurface::Specular, 0, 5));
}

TEST_CASE("environment.gpu_budget_failure_foreign_empty_thread_and_shared_lifetime") {
  auto renderer = Renderer::create(nullptr, {.maxTextureBytes = 70000}).value();
  EnvironmentMap held;

  {
    auto result = renderer.bakeEnvironment(constantPanorama(), small);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    held = *result;
    CHECK_FALSE(renderer.bakeEnvironment(constantPanorama(), small));
    CHECK(renderer.resourceStats()->environments == 1);
    CHECK_FALSE(renderer.readEnvironment({}, EnvironmentSurface::Diffuse));
    auto other = Renderer::create().value();
    CHECK_FALSE(other.readEnvironment(held, EnvironmentSurface::Diffuse));
    bool rejected{};
    std::thread thread(
        [&] { rejected = !held.info() && !renderer.bakeEnvironment(constantPanorama(), small); });
    thread.join();
    CHECK(rejected);
  }

  CHECK(renderer.resourceStats()->environments == 1);
  held = {};
  CHECK(renderer.resourceStats()->environments == 0);
  CHECK(renderer.resourceStats()->environmentBytes == 0);
  auto invalid = constantPanorama();
  invalid.pixels[0] = -1;
  CHECK_FALSE(renderer.bakeEnvironment(invalid, small));
  CHECK(renderer.resourceStats()->environments == 0);

  {
    auto temporary = Renderer::create().value();
    held = temporary.bakeEnvironment(constantPanorama(), small).value();
  }

  CHECK_FALSE(held.info());
  held = {};
}

TEST_CASE(
    "environment.gpu_directional_field_faces_diffuse_and_roughness_match_independent_moments") {
  HdrImage image{256, 128, std::vector<float>(256 * 128 * 4, 1)};

  for (std::uint32_t y = 0; y < image.height; ++y) {
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const auto theta = (static_cast<double>(y) + 0.5) * 3.141592653589793 / image.height;
      const auto phi = ((static_cast<double>(x) + 0.5) / image.width - 0.5) * 6.283185307179586;
      const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
      image.pixels[offset] = static_cast<float>(0.5 + 0.5 * std::sin(phi) * std::sin(theta));
      image.pixels[offset + 1] = static_cast<float>(0.5 + 0.5 * std::cos(theta));
      image.pixels[offset + 2] = static_cast<float>(0.5 - 0.5 * std::cos(phi) * std::sin(theta));
    }
  }

  auto renderer = Renderer::create().value();
  auto map = renderer.bakeEnvironment(image, small).value();

  for (std::uint32_t face = 0; face < 6; ++face) {
    auto radiance = renderer.readEnvironment(map, EnvironmentSurface::Radiance, face).value();
    const auto uv
        = (static_cast<float>(radiance.width / 2) + 0.5F) / static_cast<float>(radiance.width);
    const auto normal = environmentDirection(face, {uv, uv}).value();
    const auto offset
        = (static_cast<std::size_t>(radiance.height / 2) * radiance.width + radiance.width / 2) * 4;
    CHECK(radiance.pixels[offset] == doctest::Approx(0.5F + 0.5F * normal.x).epsilon(0.04));
    CHECK(radiance.pixels[offset + 1] == doctest::Approx(0.5F + 0.5F * normal.y).epsilon(0.04));
    CHECK(radiance.pixels[offset + 2] == doctest::Approx(0.5F + 0.5F * normal.z).epsilon(0.04));
    auto diffuse = renderer.readEnvironment(map, EnvironmentSurface::Diffuse, face).value();
    const auto diffuseUv
        = (static_cast<float>(diffuse.width / 2) + 0.5F) / static_cast<float>(diffuse.width);
    const auto diffuseNormal = environmentDirection(face, {diffuseUv, diffuseUv}).value();
    const auto pixel
        = (static_cast<std::size_t>(diffuse.height / 2) * diffuse.width + diffuse.width / 2) * 4;
    CHECK(diffuse.pixels[pixel] == doctest::Approx(0.5F + diffuseNormal.x / 3).epsilon(0.05));
    CHECK(diffuse.pixels[pixel + 1] == doctest::Approx(0.5F + diffuseNormal.y / 3).epsilon(0.05));
    CHECK(diffuse.pixels[pixel + 2] == doctest::Approx(0.5F + diffuseNormal.z / 3).epsilon(0.05));
  }

  auto mirror = renderer.readEnvironment(map, EnvironmentSurface::Specular).value();
  const auto middle
      = (static_cast<std::size_t>(mirror.height / 2) * mirror.width + mirror.width / 2) * 4;
  auto rough = renderer.readEnvironment(map, EnvironmentSurface::Specular, 0, 4).value();
  CHECK(mirror.pixels[middle] > 0.98F);
  CHECK(rough.pixels[0] > 0.78F);
  CHECK(rough.pixels[0] < 0.88F);
}

TEST_CASE("environment.gpu_real_polyhaven_hdr_preserves_radiance_and_integrated_brdf_limit") {
  auto files = Dot::Core::FileSystem::open(DOT_ENVIRONMENT_ASSETS).value();
  auto image = Dot::Assets::loadHdrImage(files, "studio_small_09_1k.hdr");
  REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
  CHECK(image->width == 1024);
  CHECK(image->height == 512);
  auto renderer = Renderer::create().value();
  auto result = renderer.bakeEnvironment(*image, {64, 16, 64, 128, 512});
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  auto map = std::move(*result);
  float maximum{};

  for (std::uint32_t face = 0; face < 6; ++face) {
    auto pixels = renderer.readEnvironment(map, EnvironmentSurface::Radiance, face).value();
    maximum = std::max(maximum, *std::max_element(pixels.pixels.begin(), pixels.pixels.end()));
  }

  CHECK(maximum > 1);
  const auto lut = renderer.readEnvironment(map, EnvironmentSurface::Brdf).value();
  const auto offset = (static_cast<std::size_t>(lut.height - 1) * lut.width + lut.width - 1) * 4;
  CHECK(lut.pixels[offset] == doctest::Approx(1 - std::log(2.0)).epsilon(0.06));
  CHECK(lut.pixels[offset + 1] < 0.001F);
  map = {};
  CHECK(renderer.resourceStats()->environmentBytes == 0);
}

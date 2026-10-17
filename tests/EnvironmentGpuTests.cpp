#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <thread>
#include <vector>

import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.World.Scene;
import Dot.Math;
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

TEST_CASE("environment.gpu_async_preparation_reuses_pipeline_without_publishing_maps") {
  auto renderer = Renderer::create().value();
  CHECK_FALSE(renderer.environmentPipelinesReady().value());
  auto ready = renderer.prepareEnvironmentPipelines();
  REQUIRE(ready);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

  while (!*ready && std::chrono::steady_clock::now() < deadline) {
    CHECK(renderer.resourceStats()->environments == 0);
    CHECK(renderer.resourceStats()->environmentBytes == 0);
    CHECK_FALSE(renderer.environment().value());
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ready = renderer.prepareEnvironmentPipelines();
    REQUIRE_MESSAGE(ready, (ready ? "" : ready.error().message));
  }

  REQUIRE(*ready);
  CHECK(renderer.environmentPipelinesReady().value());
  CHECK(renderer.prepareEnvironmentPipelines().value());
  CHECK(renderer.resourceStats()->environments == 0);
  const auto map = renderer.bakeEnvironment(constantPanorama(), small).value();
  auto pixels = renderer.readEnvironment(map, EnvironmentSurface::Diffuse).value();
  CHECK(pixels.pixels[0] == doctest::Approx(2).epsilon(.003));
  CHECK(renderer.prepareEnvironmentPipelines().value());
  CHECK(renderer.resourceStats()->environments == 1);
  CHECK_FALSE(renderer.environment().value());
}

TEST_CASE("environment.gpu_bake_only_then_async_surfaces_preserve_map_and_render_real_sky") {
  auto renderer = Renderer::create().value();
  auto invalid = constantPanorama();
  invalid.pixels[0] = -1;
  CHECK_FALSE(renderer.bakeEnvironment(invalid, small));
  CHECK_FALSE(renderer.environmentPipelinesReady().value());
  CHECK(renderer.resourceStats()->environments == 0);
  const auto map = renderer.bakeEnvironment(constantPanorama(), small).value();
  const auto baked = renderer.readEnvironment(map, EnvironmentSurface::Diffuse).value();
  CHECK_FALSE(renderer.environmentPipelinesReady().value());
  auto target = renderer.createTarget(3, 3).value();
  auto ready = renderer.prepareEnvironmentPipelines();
  REQUIRE(ready);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

  while (!*ready && std::chrono::steady_clock::now() < deadline) {
    CHECK_FALSE(renderer.environmentPipelinesReady().value());
    CHECK(renderer.resourceStats()->environments == 1);
    CHECK(renderer.resourceStats()->environmentBytes == map.info()->bytes);
    CHECK_FALSE(renderer.environment().value());
    CHECK(target.info()->frames == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ready = renderer.prepareEnvironmentPipelines();
    REQUIRE_MESSAGE(ready, (ready ? "" : ready.error().message));
  }

  REQUIRE(*ready);
  CHECK(renderer.environmentPipelinesReady().value());
  CHECK_FALSE(renderer.environment().value());
  CHECK(target.info()->frames == 0);
  CHECK(renderer.readEnvironment(map, EnvironmentSurface::Diffuse)->pixels == baked.pixels);
  REQUIRE(renderer.setEnvironment(EnvironmentLighting{map, .5F}));
  const auto camera = Dot::Math::multiply(Dot::Math::perspective(60, 1, .1F, 100).value(),
                                          Dot::Math::lookAt({0, 0, 1}, {}).value())
                          .value();
  REQUIRE(renderer.renderMeshesInto(target, {}, camera));
  const auto pixels = renderer.readHdrTarget(target).value();

  for (std::size_t index = 0; index < pixels.pixels.size(); index += 4) {
    CHECK(pixels.pixels[index] == doctest::Approx(1).epsilon(.003));
    CHECK(pixels.pixels[index + 1] == doctest::Approx(.5F).epsilon(.003));
    CHECK(pixels.pixels[index + 2] == doctest::Approx(.25F).epsilon(.003));
    CHECK(pixels.pixels[index + 3] == 1);
  }

  const auto binding = renderer.environment().value();
  CHECK(renderer.prepareEnvironmentPipelines().value());
  CHECK(renderer.environment()->value().intensity == binding->intensity);
  CHECK(renderer.resourceStats()->environments == 1);
}

TEST_CASE("environment.gpu_async_preparation_move_thread_and_abandoned_job_are_safe") {
  for (int index = 0; index < 3; ++index) {
    auto renderer = Renderer::create().value();
    REQUIRE(renderer.prepareEnvironmentPipelines());
    bool rejected{};
    std::thread thread([&] {
      rejected = !renderer.prepareEnvironmentPipelines() && !renderer.environmentPipelinesReady();
    });
    thread.join();
    CHECK(rejected);
    auto moved = std::move(renderer);
    CHECK_FALSE(renderer.prepareEnvironmentPipelines());
    CHECK_FALSE(renderer.environmentPipelinesReady());

    if (index == 1) {
      const auto baked = moved.bakeEnvironment(constantPanorama(), small).value();
      CHECK(moved.readEnvironment(baked, EnvironmentSurface::Radiance)->pixels[0]
            == doctest::Approx(2).epsilon(.003));
    }
  }

  auto renderer = Renderer::create().value();
  CHECK(renderer.bakeEnvironment(constantPanorama(), small));
}

TEST_CASE("environment.gpu_scene_preparation_without_image_io_preserves_bindings_and_cache") {
  auto renderer = Renderer::create().value();
  auto drawing = SceneRenderer::create(renderer).value();
  auto scene = Dot::World::Scene::create().value();
  CHECK(drawing.preparePipelines(scene).value() == ScenePipelineStage::Ready);
  REQUIRE(scene.setEnvironment(Dot::World::Environment{"assets/missing.hdr"}));
  auto ready = drawing.preparePipelines(scene);
  REQUIRE(ready);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

  while (*ready != ScenePipelineStage::Ready && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ready = drawing.preparePipelines(scene);
    REQUIRE(ready);
  }

  REQUIRE(*ready == ScenePipelineStage::Ready);
  CHECK(drawing.stats()->bytes == 0);
  CHECK_FALSE(renderer.environment().value());
  auto target = renderer.createTarget(4, 4).value();
  CHECK_FALSE(drawing.renderInto(scene, target, {}, View{}));
  CHECK(drawing.stats()->bytes == 0);
  CHECK(renderer.resourceStats()->environments == 0);
  auto moved = std::move(drawing);
  CHECK_FALSE(drawing.preparePipelines(scene));
  CHECK(moved.preparePipelines(scene).value() == ScenePipelineStage::Ready);
  auto invalid = std::move(scene);
  CHECK_FALSE(moved.preparePipelines(scene));
  REQUIRE(invalid.setEnvironment(std::nullopt));
  CHECK(moved.preparePipelines(invalid).value() == ScenePipelineStage::Ready);
}

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

TEST_CASE("environment.lighting_constant_hdr_matches_analytic_diffuse_and_rough_metal_energy") {
  auto renderer = Renderer::create().value();
  auto map = renderer.bakeEnvironment(constantPanorama(), {16, 4, 16, 128, 512}).value();
  auto ready = renderer.prepareEnvironmentPipelines();
  REQUIRE(ready);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

  while (!*ready && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ready = renderer.prepareEnvironmentPipelines();
    REQUIRE(ready);
  }

  REQUIRE(*ready);
  REQUIRE(renderer.setEnvironment(EnvironmentLighting{map, 1, 0, false}));
  auto mesh = renderer.upload(quad({2, 2}).value()).value();
  auto target = renderer.createTarget(1, 1).value();
  Material surface;
  surface.model = SurfaceModel::MetallicRoughness;
  surface.baseColor = {0.5F, 0.25F, 0.1F, 1};
  surface.roughness = 1;
  std::array draws{MeshDraw{&mesh, {}, {1, 1, 1, 1}, surface}};
  const std::array<float, 3> energy{2, 1, 0.5F};
  const std::array<float, 3> base{0.5F, 0.25F, 0.1F};
  const auto smithIntegral = 1 - std::log(2.0);
  auto rendered = renderer.renderMeshesInto(target, draws, {});
  REQUIRE_MESSAGE(rendered, (rendered ? "" : rendered.error().message));
  auto pixels = renderer.readHdrTarget(target).value();

  for (std::size_t channel = 0; channel < 3; ++channel) {
    const auto reference = energy[channel] * (base[channel] * 0.96 + 0.04 * smithIntegral);
    CHECK(pixels.pixels[channel] == doctest::Approx(reference).epsilon(0.015));
  }

  draws[0].material.metallic = 1;
  REQUIRE(renderer.renderMeshesInto(target, draws, {}));
  const auto metal = renderer.readHdrTarget(target).value();

  for (std::size_t channel = 0; channel < 3; ++channel) {
    CHECK(metal.pixels[channel]
          == doctest::Approx(energy[channel] * base[channel] * smithIntegral).epsilon(0.03));
  }

  REQUIRE(renderer.setEnvironment(EnvironmentLighting{map, 4, 0, false}));
  REQUIRE(renderer.renderMeshesInto(target, draws, {}));
  const auto bright = renderer.readHdrTarget(target).value();
  CHECK(bright.pixels[0] == doctest::Approx(metal.pixels[0] * 4).epsilon(0.002));
  CHECK(bright.pixels[0] > 1);
  REQUIRE(renderer.setEnvironment(std::nullopt));
  REQUIRE(renderer.renderMeshesInto(target, draws, {}));
  CHECK(renderer.readHdrTarget(target)->pixels[0] == 0);
  REQUIRE(renderer.setEnvironment(EnvironmentLighting{map, 4, 0, false}));
  Lighting coincident;
  coincident.viewPosition = {};
  REQUIRE(renderer.setLighting(coincident));
  REQUIRE(renderer.renderMeshesInto(target, draws, {}));
  CHECK(renderer.readHdrTarget(target)->pixels[0] == 0);
  REQUIRE(renderer.setLighting(Lighting{}));
  draws[0].material.model = SurfaceModel::Unlit;
  REQUIRE(renderer.setEnvironment(EnvironmentLighting{map, 4, 0, false}));
  REQUIRE(renderer.renderMeshesInto(target, draws, {}));
  CHECK(renderer.readHdrTarget(target)->pixels[0] == doctest::Approx(0.5F));
}

TEST_CASE("environment.lighting_binding_is_transactional_shared_thread_and_device_bound") {
  auto renderer = Renderer::create().value();
  CHECK_FALSE(renderer.environment().value());
  auto map = renderer.bakeEnvironment(constantPanorama(), small).value();
  CHECK_FALSE(renderer.environmentPipelinesReady().value());
  REQUIRE(renderer.setEnvironment(EnvironmentLighting{map, 2, 45, false}));
  CHECK(renderer.environmentPipelinesReady().value());
  CHECK(renderer.prepareEnvironmentPipelines().value());
  CHECK_FALSE(renderer.setEnvironment(EnvironmentLighting{}));
  auto invalid = EnvironmentLighting{map, -1};
  CHECK_FALSE(renderer.setEnvironment(invalid));
  invalid.intensity = std::numeric_limits<float>::infinity();
  CHECK_FALSE(renderer.setEnvironment(invalid));
  invalid.intensity = 65;
  CHECK_FALSE(renderer.setEnvironment(invalid));
  invalid.intensity = 1;
  invalid.rotationDegrees = 361;
  CHECK_FALSE(renderer.setEnvironment(invalid));
  invalid.rotationDegrees = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(renderer.setEnvironment(invalid));
  auto foreign = Renderer::create().value();
  CHECK_FALSE(foreign.setEnvironment(EnvironmentLighting{map}));
  bool rejected{};
  std::thread worker(
      [&] { rejected = !renderer.setEnvironment(std::nullopt) && !renderer.environment(); });
  worker.join();
  CHECK(rejected);
  CHECK(renderer.environment()->value().intensity == 2);
  CHECK(renderer.environment()->value().rotationDegrees == 45);
  invalid.map = {};
  auto snapshot = renderer.environment().value();
  map = {};
  CHECK(renderer.resourceStats()->environments == 1);
  REQUIRE(renderer.setEnvironment(std::nullopt));
  CHECK(renderer.resourceStats()->environments == 1);
  CHECK(snapshot->map.info());
  snapshot.reset();
  CHECK(renderer.resourceStats()->environments == 0);
}

TEST_CASE("environment.sky_camera_yaw_orthographic_rays_depth_and_hdr_display_are_real") {
  HdrImage image{128, 64, std::vector<float>(128 * 64 * 4, 1)};

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
  auto map = renderer.bakeEnvironment(image, {64, 4, 16, 16, 128}).value();
  REQUIRE(renderer.setEnvironment(EnvironmentLighting{map}));
  const auto camera = Dot::Math::multiply(Dot::Math::perspective(60, 1, 0.1F, 100).value(),
                                          Dot::Math::lookAt({0, 0, 1}, {}).value())
                          .value();
  auto target = renderer.createTarget(3, 3).value();
  REQUIRE(renderer.renderMeshesInto(target, {}, camera));
  auto pixels = renderer.readHdrTarget(target).value();
  CHECK(pixels.pixels[16] == doctest::Approx(0.5F).epsilon(0.01));
  CHECK(pixels.pixels[17] == doctest::Approx(0.5F).epsilon(0.01));
  CHECK(pixels.pixels[18] < 0.001F);
  const auto center = pixels.pixels[16];
  CHECK(pixels.pixels[0] < center);
  CHECK(pixels.pixels[8] > center);
  CHECK(pixels.pixels[1] > pixels.pixels[25]);
  REQUIRE(renderer.setEnvironment(EnvironmentLighting{map, 1, 90}));
  REQUIRE(renderer.renderMeshesInto(target, {}, camera));
  pixels = renderer.readHdrTarget(target).value();
  CHECK(pixels.pixels[16] > 0.99F);
  CHECK(pixels.pixels[18] == doctest::Approx(0.5F).epsilon(0.01));
  Lighting lighting;
  lighting.viewDirection = Dot::Math::Vec3{0, 1, 0};
  REQUIRE(renderer.setLighting(lighting));
  REQUIRE(renderer.setEnvironment(EnvironmentLighting{map}));
  REQUIRE(renderer.renderMeshesInto(target, {}, camera));
  pixels = renderer.readHdrTarget(target).value();

  for (std::size_t index = 0; index < pixels.pixels.size(); index += 4) {
    CHECK(pixels.pixels[index + 1] > 0.99F);
  }

  REQUIRE(renderer.setLighting(Lighting{}));
  auto mesh = renderer.upload(quad({0.5F, 0.5F}).value()).value();
  std::array draws{MeshDraw{&mesh, {}, {1, 0, 0, 1}}};
  REQUIRE(renderer.renderMeshesInto(target, draws, camera));
  pixels = renderer.readHdrTarget(target).value();
  CHECK(pixels.pixels[16] == 1);
  CHECK(pixels.pixels[17] == 0);
  CHECK(pixels.pixels[18] == 0);
  CHECK(pixels.pixels[2] < 0.1F);
  auto constant = renderer.bakeEnvironment(constantPanorama(), small).value();
  REQUIRE(renderer.setEnvironment(EnvironmentLighting{constant, 2}));
  REQUIRE(renderer.setPostProcess({.toneMap = ToneMap::Reinhard}));
  REQUIRE(renderer.renderMeshesInto(target, {}, camera));
  pixels = renderer.readHdrTarget(target).value();
  CHECK(pixels.pixels[16] == 4);
  const auto display = renderer.readTarget(target).value();
  const auto srgb = 1.055 * std::pow(0.8, 1.0 / 2.4) - 0.055;
  CHECK(display.pixels[16] == doctest::Approx(std::lround(srgb * 255)).epsilon(0.005));
  Dot::Math::Mat4 singular;
  singular.data.fill(0);
  const auto frame = target.info()->frames;
  CHECK_FALSE(renderer.renderMeshesInto(target, {}, singular));
  CHECK(target.info()->frames == frame);
  Dot::Math::Mat4 horizon;
  horizon.data[3] = 2;
  CHECK_FALSE(renderer.renderMeshesInto(target, {}, horizon));
  CHECK(target.info()->frames == frame);
  CHECK(renderer.readHdrTarget(target)->pixels[16] == 4);
  Lighting onPlane;
  onPlane.viewPosition = {0, 0, 0.5F};
  REQUIRE(renderer.setLighting(onPlane));
  CHECK_FALSE(renderer.renderMeshesInto(target, {}, {}));
  CHECK(target.info()->frames == frame);
  REQUIRE(renderer.setLighting(Lighting{}));
  REQUIRE(renderer.setEnvironment(EnvironmentLighting{constant, 1, 0, false}));
  REQUIRE(renderer.renderMeshesInto(target, {}, camera, {0.25F, 0, 0, 1}));
  CHECK(renderer.readHdrTarget(target)->pixels[16] == 0.25F);
}

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Result;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.World.Scene;
import Dot.Math;

using namespace Dot::Graphics;

namespace {
  struct Fixture {
    Renderer renderer;
    Mesh ground;
    Mesh caster;
    RenderTarget target;
    Dot::Math::Mat4 camera;
    Lighting lights;
    std::array<MeshDraw, 2> draws;

    Fixture()
        : renderer(Renderer::create().value()),
          ground(renderer.upload(quad({8, 8}).value()).value()),
          caster(renderer.upload(cube({0.5F, 0.5F, 0.5F}).value()).value()),
          target(renderer.createTarget(129, 129).value()),
          camera(Dot::Math::multiply(Dot::Math::orthographic(-4, 4, -4, 4, 0.1F, 12).value(),
                                     Dot::Math::lookAt({0, 6, 0}, {}, {0, 0, -1}).value())
                     .value()) {
      lights.viewPosition = {0, 6, 0};
      lights.viewDirection = Dot::Math::Vec3{0, -1, 0};
      ShadowSettings settings;
      settings.resolution = 128;
      settings.cascades = 1;
      settings.distance = 12;
      settings.filterRadius = 0;
      settings.normalBias = 0;
      settings.slopeBias = 0;
      lights.lights = {{.direction = {1, -1, 0}, .intensity = 4, .shadows = settings}};
      REQUIRE(renderer.setLighting(lights));
      Material surface;
      surface.model = SurfaceModel::MetallicRoughness;
      surface.baseColor = {0.5F, 0.5F, 0.5F, 1};
      surface.roughness = 1;
      const auto rotation = Dot::Math::rotationEulerDegrees({-90, 0, 0}).value();
      draws[0] = {&ground, Dot::Math::compose({}, rotation).value(), {1, 1, 1, 1}, surface};
      draws[1] = {&caster, Dot::Math::compose({0, 1, 0}).value(), {1, 1, 1, 1}, surface};
    }

    auto render() -> HdrImage {
      auto result = renderer.renderMeshesInto(target, draws, camera);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return renderer.readHdrTarget(target).value();
    }

    auto red(const HdrImage& image, Dot::Math::Vec3 world) -> float {
      const auto clip = Dot::Math::transformPoint(camera, world).value();
      const auto x = static_cast<std::uint32_t>(
          std::floor((clip.x * 0.5F + 0.5F) * static_cast<float>(image.width)));
      const auto y = static_cast<std::uint32_t>(
          std::floor((-clip.y * 0.5F + 0.5F) * static_cast<float>(image.height)));
      return image.pixels.at((static_cast<std::size_t>(y) * image.width + x) * 4);
    }
  };
}  // namespace

TEST_CASE("shadows.gpu_async_group_publishes_no_maps_and_preserves_real_occlusion") {
  Fixture fixture;
  CHECK_FALSE(fixture.renderer.shadowPipelinesReady().value());
  auto ready = fixture.renderer.prepareShadowPipelines();
  REQUIRE(ready);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

  while (!*ready && std::chrono::steady_clock::now() < deadline) {
    CHECK(fixture.renderer.resourceStats()->shadowBytes == 0);
    CHECK(fixture.target.info()->frames == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ready = fixture.renderer.prepareShadowPipelines();
    REQUIRE_MESSAGE(ready, (ready ? "" : ready.error().message));
  }

  REQUIRE(*ready);
  CHECK(fixture.renderer.shadowPipelinesReady().value());
  CHECK(fixture.renderer.resourceStats()->shadowBytes == 0);
  const auto shadowed = fixture.render();
  const auto lit = fixture.red(shadowed, {-1, 0, 0});
  CHECK(lit > .4F);
  CHECK(fixture.red(shadowed, {1, 0, 0}) < .01F);
  fixture.draws[1].castsShadow = false;
  CHECK(fixture.red(fixture.render(), {1, 0, 0}) == doctest::Approx(lit).epsilon(.01));
  CHECK(fixture.renderer.prepareShadowPipelines().value());
  CHECK(fixture.renderer.resourceStats()->shadowBytes == 128 * 128 * 4);
}

TEST_CASE("shadows.gpu_async_group_moves_threads_and_abandoned_compilation_release_safely") {
  for (int index = 0; index < 3; ++index) {
    auto gpu = Renderer::create().value();
    REQUIRE(gpu.prepareShadowPipelines());
    bool rejected{};
    std::thread worker(
        [&] { rejected = !gpu.prepareShadowPipelines() && !gpu.shadowPipelinesReady(); });
    worker.join();
    CHECK(rejected);
    auto moved = std::move(gpu);
    CHECK_FALSE(gpu.prepareShadowPipelines());
    CHECK_FALSE(gpu.shadowPipelinesReady());

    if (index == 1) {
      auto ready = moved.prepareShadowPipelines();
      REQUIRE(ready);
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

      while (!*ready && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ready = moved.prepareShadowPipelines();
        REQUIRE(ready);
      }

      REQUIRE(*ready);
      CHECK(moved.shadowPipelinesReady().value());
    }
  }
}

TEST_CASE("shadows.gpu_scene_readiness_observes_current_requirements_and_gui_reads_do_no_work") {
  auto gpu = Renderer::create().value();
  auto drawing = SceneRenderer::create(gpu).value();
  auto scene = Dot::World::Scene::create().value();
  CHECK(drawing.readiness(scene).value() == ScenePreparationStage::Ready);
  const auto lamp = scene.createEntity("Lamp").value();
  Dot::World::Light light;
  light.shadows = ShadowSettings{};
  light.enabled = false;
  REQUIRE(scene.setLight(lamp, light));
  CHECK(drawing.readiness(scene).value() == ScenePreparationStage::Ready);
  light.enabled = true;
  REQUIRE(scene.setLight(lamp, light));
  CHECK(drawing.readiness(scene).value() == ScenePreparationStage::Shadows);
  REQUIRE(gpu.enableGui());
  REQUIRE(gpu.renderGui(
      [&]() -> Dot::Core::Result<void> {
        CHECK(drawing.readiness(scene).value() == ScenePreparationStage::Shadows);
        CHECK_FALSE(gpu.shadowPipelinesReady().value());
        CHECK_FALSE(drawing.prepare(scene));
        return {};
      },
      16, 16));
  CHECK_FALSE(gpu.shadowPipelinesReady().value());
  REQUIRE(gpu.closeGui());
  REQUIRE(scene.setLight(lamp, std::nullopt));
  CHECK(drawing.readiness(scene).value() == ScenePreparationStage::Ready);
  REQUIRE(scene.setLight(lamp, light));
  REQUIRE(scene.setEnvironment(Dot::World::Environment{"assets/missing.hdr"}));
  CHECK(drawing.readiness(scene).value() == ScenePreparationStage::Environment);
  REQUIRE(scene.setEnvironment(std::nullopt));
  auto stage = drawing.prepare(scene);
  REQUIRE(stage);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);

  while (*stage != ScenePreparationStage::Ready && std::chrono::steady_clock::now() < deadline) {
    CHECK(gpu.resourceStats()->shadowBytes == 0);
    CHECK(gpu.resourceStats()->environments == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    stage = drawing.prepare(scene);
    REQUIRE_MESSAGE(stage, (stage ? "" : stage.error().message));
  }

  REQUIRE(*stage == ScenePreparationStage::Ready);
  CHECK(gpu.shadowPipelinesReady().value());

  CHECK(drawing.stats()->bytes == 0);
  CHECK_FALSE(gpu.environment().value());
  auto moved = std::move(scene);
  CHECK_FALSE(drawing.readiness(scene));
  CHECK(drawing.readiness(moved).value() == ScenePreparationStage::Ready);
}

TEST_CASE("shadows.gpu_real_occlusion_moves_with_casters_and_cast_receive_flags") {
  Fixture fixture;
  const auto shadowed = fixture.render();
  const auto lit = fixture.red(shadowed, {-1, 0, 0});
  CHECK(lit > 0.4F);
  CHECK(fixture.red(shadowed, {1, 0, 0}) < 0.01F);
  CHECK(fixture.renderer.resourceStats()->shadowBytes == 128 * 128 * 4);
  fixture.draws[1].castsShadow = false;
  const auto noCaster = fixture.render();
  CHECK(fixture.red(noCaster, {1, 0, 0}) == doctest::Approx(lit).epsilon(0.01));
  fixture.draws[1].castsShadow = true;
  fixture.draws[0].receivesShadow = false;
  CHECK(fixture.red(fixture.render(), {1, 0, 0}) == doctest::Approx(lit).epsilon(0.01));
  fixture.draws[0].receivesShadow = true;
  fixture.draws[1].transform = Dot::Math::compose({-2, 1, 0}).value();
  const auto moved = fixture.render();
  CHECK(fixture.red(moved, {1, 0, 0}) > 0.4F);
  CHECK(fixture.red(moved, {-1, 0, 0}) < 0.01F);
  fixture.lights.lights[0].shadows.reset();
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  CHECK(fixture.renderer.resourceStats()->shadowBytes == 0);
  CHECK(fixture.red(fixture.render(), {-1, 0, 0}) > 0.4F);
}

TEST_CASE("shadows.gpu_filtering_creates_measured_partial_visibility_and_cascades_reallocate") {
  Fixture fixture;
  const auto hard = fixture.render();
  fixture.lights.lights[0].shadows->filterRadius = 6;
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  const auto soft = fixture.render();
  const auto reference = fixture.red(hard, {-1, 0, 0});
  std::size_t hardPartial{};
  std::size_t softPartial{};

  for (std::uint32_t x = 0; x < 129; ++x) {
    const auto index = (64 * 129 + x) * 4;
    hardPartial += hard.pixels[index] > reference * 0.05F && hard.pixels[index] < reference * 0.95F;
    softPartial += soft.pixels[index] > reference * 0.05F && soft.pixels[index] < reference * 0.95F;
  }

  CHECK(softPartial > hardPartial + 3);
  fixture.lights.lights[0].shadows->resolution = 256;
  fixture.lights.lights[0].shadows->cascades = 4;
  fixture.lights.lights[0].shadows->filterRadius = 0;

  for (const auto height : {0.6F, 2.0F, 4.0F, 8.0F}) {
    fixture.lights.viewPosition = {0, height, 0};
    fixture.camera = Dot::Math::multiply(Dot::Math::orthographic(-4, 4, -4, 4, 0.1F, 12).value(),
                                         Dot::Math::lookAt({0, height, 0}, {}, {0, 0, -1}).value())
                         .value();
    REQUIRE(fixture.renderer.setLighting(fixture.lights));
    CHECK(fixture.red(fixture.render(), {1, 0, 0}) < reference * 0.1F);
  }

  CHECK(fixture.renderer.resourceStats()->shadowBytes == 256 * 256 * 4 * 4);
  CHECK(fixture.target.info()->frames == 6);
}

TEST_CASE("shadows.gpu_only_requested_direct_light_is_attenuated_ibl_and_emission_survive") {
  Fixture fixture;
  auto sun = fixture.lights.lights[0];
  fixture.lights.lights.insert(fixture.lights.lights.begin(),
                               Light{.direction = {1, -1, 0}, .intensity = 1});
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  const auto mixed = fixture.render();
  CHECK(fixture.red(mixed, {1, 0, 0})
        == doctest::Approx(fixture.red(mixed, {-1, 0, 0}) * 0.2F).epsilon(0.015));
  fixture.draws[0].material.emissive = {2, 0, 0};
  CHECK(fixture.red(fixture.render(), {1, 0, 0}) > 2);
  auto map = fixture.renderer
                 .bakeEnvironment(HdrImage{4, 2, std::vector<float>(32, 1)}, {16, 4, 16, 16, 32})
                 .value();
  REQUIRE(fixture.renderer.setEnvironment(EnvironmentLighting{map, 1, 0, false}));
  CHECK(fixture.red(fixture.render(), {1, 0, 0}) > 2.4F);
  const auto before = fixture.renderer.readHdrTarget(fixture.target)->pixels;
  const auto frames = fixture.target.info()->frames;
  auto invalid = fixture.lights;
  invalid.lights[0].shadows = sun.shadows;
  invalid.lights[0].shadows->resolution = 129;
  CHECK_FALSE(fixture.renderer.setLighting(invalid));
  invalid = fixture.lights;
  invalid.lights[1].kind = LightKind::Spot;
  invalid.lights[1].outerConeDegrees = 90;
  CHECK_FALSE(fixture.renderer.setLighting(invalid));
  invalid = fixture.lights;
  invalid.lights[1].shadows->resolution = 129;
  CHECK_FALSE(fixture.renderer.setLighting(invalid));
  Dot::Math::Mat4 singular;
  singular.data.fill(0);
  CHECK_FALSE(fixture.renderer.renderMeshesInto(fixture.target, fixture.draws, singular));
  auto malformed = fixture.draws;
  malformed[1].transform.data[3] = 1;
  CHECK_FALSE(fixture.renderer.renderMeshesInto(fixture.target, malformed, fixture.camera));
  CHECK(fixture.target.info()->frames == frames);
  CHECK(fixture.renderer.readHdrTarget(fixture.target)->pixels == before);
}

TEST_CASE("visibility.gpu_off_camera_caster_still_shadows_visible_receiver") {
  Fixture fixture;
  fixture.draws[1].transform = Dot::Math::compose({-5, 5, 0}).value();
  const auto shadowed = fixture.render();
  auto report = fixture.renderer.visibilityStats().value();
  REQUIRE(report.mesh);
  CHECK(report.mesh->inputInstances == 2);
  CHECK(report.mesh->visibleInstances == 1);
  CHECK(report.mesh->culledInstances == 1);
  CHECK(report.mesh->shadowInstances == 2);
  const auto dark = fixture.red(shadowed, {0, 0, 0});
  fixture.draws[1].castsShadow = false;
  const auto lit = fixture.render();
  CHECK(fixture.red(lit, {0, 0, 0}) > dark + 0.3F);
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowInstances == 1);
  CHECK(fixture.renderer.visibilityStats()->mesh->visibleInstances == 1);
}

TEST_CASE("instancing.gpu_shadow_cascade_batches_match_independent_resource_reference") {
  Fixture fixture;
  fixture.lights.lights[0].shadows->cascades = 4;
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  std::array<MeshDraw, 3> draws{{fixture.draws[0], fixture.draws[1], fixture.draws[1]}};
  draws[1].transform = Dot::Math::compose({-1, 1, 0}).value();
  draws[2].transform = Dot::Math::compose({1, 1, 0}).value();
  REQUIRE(fixture.renderer.renderMeshesInto(fixture.target, draws, fixture.camera));
  const auto batched = fixture.renderer.readHdrTarget(fixture.target).value();
  const auto report = fixture.renderer.visibilityStats().value();
  REQUIRE(report.mesh);
  CHECK(report.mesh->visibleInstances == 3);
  CHECK(report.mesh->colorDraws == 2);
  CHECK(report.mesh->shadowInstances == 12);
  CHECK(report.mesh->shadowDraws == 8);
  auto copy = fixture.renderer.upload(cube({0.5F, 0.5F, 0.5F}).value()).value();
  draws[2].mesh = &copy;
  REQUIRE(fixture.renderer.renderMeshesInto(fixture.target, draws, fixture.camera));
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowDraws == 12);
  CHECK((fixture.renderer.readHdrTarget(fixture.target)->pixels == batched.pixels));
}

TEST_CASE("shadows.gpu_mask_texture_holes_and_blend_do_not_cast_opaque_shadows") {
  Fixture fixture;
  const auto shadow = fixture.render();
  CHECK(fixture.red(shadow, {1, 0, 0}) < .01F);
  auto transparent = fixture.renderer.uploadTexture(Image{1, 1, {255, 255, 255, 0}}).value();
  fixture.draws[1].material.alphaMode = AlphaMode::Mask;
  fixture.draws[1].material.baseColorTexture = transparent;
  CHECK(fixture.red(fixture.render(), {1, 0, 0}) > .4F);
  fixture.draws[1].material.alphaCutoff = 0;
  CHECK(fixture.red(fixture.render(), {1, 0, 0}) < .01F);
  fixture.draws[1].material.alphaMode = AlphaMode::Blend;
  fixture.draws[1].material.baseColorTexture.reset();
  fixture.draws[1].material.baseColor.w = .5F;
  CHECK(fixture.red(fixture.render(), {1, 0, 0}) > .4F);
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowInstances == 1);
}

TEST_CASE("shadows.gpu_local_occlusion_flags_masks_motion_and_resource_transitions") {
  Fixture fixture;

  for (const auto kind : {LightKind::Point, LightKind::Spot}) {
    auto& light = fixture.lights.lights[0];
    light.kind = kind;
    light.position = {-2, 3, 0};
    light.direction = {2, -3, 0};
    light.intensity = 80;
    light.range = 12;
    light.innerConeDegrees = 35;
    light.outerConeDegrees = 55;
    fixture.draws[1].transform = Dot::Math::compose({0, 1, 0}).value();
    REQUIRE(fixture.renderer.setLighting(fixture.lights));
    const auto shadowed = fixture.render();
    CHECK(fixture.red(shadowed, {1, 0, 0}) < .01F);
    CHECK(fixture.red(shadowed, {-1, 0, 0}) > .3F);
    const auto bytes = 128 * 128 * 4 * (kind == LightKind::Point ? 6 : 1);
    CHECK(fixture.renderer.resourceStats()->shadowBytes == bytes);
    fixture.draws[1].castsShadow = false;
    const auto unshadowed = fixture.render();
    const auto reference = fixture.red(unshadowed, {1, 0, 0});
    CHECK(reference > .3F);
    fixture.draws[1].castsShadow = true;
    fixture.draws[0].receivesShadow = false;
    CHECK(fixture.red(fixture.render(), {1, 0, 0}) == doctest::Approx(reference));
    fixture.draws[0].receivesShadow = true;
    fixture.draws[1].transform = Dot::Math::compose({-2, 1, 0}).value();
    CHECK(fixture.red(fixture.render(), {1, 0, 0}) == doctest::Approx(reference));
    fixture.draws[1].transform = Dot::Math::compose({0, 1, 0}).value();
    auto transparent = fixture.renderer.uploadTexture(Image{1, 1, {255, 255, 255, 0}}).value();
    fixture.draws[1].material.alphaMode = AlphaMode::Mask;
    fixture.draws[1].material.baseColorTexture = transparent;
    CHECK(fixture.red(fixture.render(), {1, 0, 0}) == doctest::Approx(reference));
    fixture.draws[1].material.alphaMode = AlphaMode::Opaque;
    fixture.draws[1].material.baseColorTexture.reset();
    light.shadows->filterRadius = 6;
    REQUIRE(fixture.renderer.setLighting(fixture.lights));
    const auto soft = fixture.render();
    std::size_t partial{};

    for (std::size_t i = 0; i < soft.pixels.size(); i += 4) {
      partial += unshadowed.pixels[i] > .1F && soft.pixels[i] > unshadowed.pixels[i] * .05F
                 && soft.pixels[i] < unshadowed.pixels[i] * .95F;
    }

    CHECK(partial > 10);
    light.shadows->filterRadius = 0;
    light.shadows->distance = 1;
    REQUIRE(fixture.renderer.setLighting(fixture.lights));
    CHECK(fixture.red(fixture.render(), {1, 0, 0}) == doctest::Approx(reference));
    light.shadows->distance = 12;
  }

  fixture.lights.lights[0].shadows.reset();
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  CHECK(fixture.renderer.resourceStats()->shadowBytes == 0);
}

TEST_CASE("shadows.gpu_point_all_faces_and_soft_cube_seams_remain_occluded") {
  Fixture fixture;
  auto& light = fixture.lights.lights[0];
  light.kind = LightKind::Point;
  light.position = {};
  light.intensity = 80;
  light.range = 12;
  light.shadows->normalBias = .01F;
  fixture.draws[1].material.doubleSided = true;
  const std::array<Dot::Math::Vec3, 12> rays{{{1, 0, 0},
                                              {-1, 0, 0},
                                              {0, 1, 0},
                                              {0, -1, 0},
                                              {0, 0, 1},
                                              {0, 0, -1},
                                              {1, -1, 0},
                                              {1, -1, 1},
                                              {1, -.97F, 0},
                                              {1, -1.03F, 0},
                                              {1, -.97F, .98F},
                                              {1, -1.03F, 1.02F}}};
  auto wall = fixture.renderer.upload(cube({8, 8, .1F}).value()).value();
  fixture.draws[0].mesh = &wall;

  for (const auto ray : rays) {
    const auto direction = Dot::Math::normalized(ray).value();
    const auto up
        = std::abs(direction.y) > .99F ? Dot::Math::Vec3{0, 0, 1} : Dot::Math::Vec3{0, 1, 0};
    const auto receiver = direction * 4;
    auto wallView = Dot::Math::lookAt(receiver, {}, up).value();
    fixture.draws[0].transform = Dot::Math::inverse(wallView).value();
    fixture.draws[1].transform = Dot::Math::compose(direction * 2).value();
    fixture.lights.viewPosition = direction * .5F;
    fixture.lights.viewDirection = direction;
    // Clip the occluder before the near plane; it still participates in shadows.
    // 用近平面裁掉投射物；它仍参与阴影投射。
    fixture.camera = Dot::Math::multiply(Dot::Math::orthographic(-1, 1, -1, 1, 2.5F, 12).value(),
                                         Dot::Math::lookAt(direction * .5F, receiver, up).value())
                         .value();

    for (const auto radius : {0.F, 6.F}) {
      light.shadows->filterRadius = radius;
      REQUIRE(fixture.renderer.setLighting(fixture.lights));
      CHECK(fixture.red(fixture.render(), receiver) < .01F);
      fixture.draws[1].castsShadow = false;
      CHECK(fixture.red(fixture.render(), receiver) > .3F);
      fixture.draws[1].castsShadow = true;
    }
  }
}

TEST_CASE("shadows.gpu_local_normal_bias_at_source_keeps_finite_lit_receiver") {
  Fixture fixture;
  auto& light = fixture.lights.lights[0];
  light.position = {0, .2F, 0};
  light.direction = {0, -1, 0};
  light.intensity = .5F;
  light.range = 1;
  light.shadows->normalBias = .2F;

  for (const auto kind : {LightKind::Point, LightKind::Spot}) {
    light.kind = kind;
    REQUIRE(fixture.renderer.setLighting(fixture.lights));
    REQUIRE(fixture.renderer.renderMeshesInto(
        fixture.target, std::span<const MeshDraw>(fixture.draws).first(1), fixture.camera));
    const auto image = fixture.renderer.readHdrTarget(fixture.target).value();
    CHECK(fixture.red(image, {}) > .3F);

    CHECK(std::all_of(image.pixels.begin(), image.pixels.end(),
                      [](float value) { return std::isfinite(value); }));
  }
}

TEST_CASE("shadows.gpu_multiple_mixed_lights_match_independent_frames_and_reorder") {
  Fixture fixture;
  auto sun = fixture.lights.lights[0];
  sun.shadows->filterRadius = 6;
  auto point = sun;
  point.kind = LightKind::Point;
  point.position = {2, 3, 0};
  point.intensity = 50;
  point.color = {0, 1, 0};
  point.shadows->resolution = 256;
  auto spot = point;
  spot.kind = LightKind::Spot;
  spot.position = {-2, 3, 0};
  spot.direction = {2, -3, 0};
  spot.color = {0, 0, 1};
  spot.innerConeDegrees = 35;
  spot.outerConeDegrees = 55;
  spot.shadows->resolution = 128;
  sun.color = {1, 0, 0};
  const std::array lights{sun, point, spot};
  std::vector<HdrImage> references;

  for (const auto& light : lights) {
    fixture.lights.lights = {light};
    REQUIRE(fixture.renderer.setLighting(fixture.lights));
    references.push_back(fixture.render());
  }

  fixture.lights.lights = {sun, point, spot};
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  auto mixed = fixture.render();
  CHECK(fixture.renderer.resourceStats()->shadowBytes == 7 * 256 * 256 * 4);

  for (std::size_t pixel = 0; pixel < mixed.pixels.size(); pixel += 4) {
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const auto expected = references[channel].pixels[pixel + channel];
      CHECK(mixed.pixels[pixel + channel] == doctest::Approx(expected).epsilon(.003).scale(.001));
    }
  }

  std::reverse(fixture.lights.lights.begin(), fixture.lights.lights.end());
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  const auto reordered = fixture.render();
  CHECK(reordered.pixels == mixed.pixels);
  fixture.lights.lights.erase(fixture.lights.lights.begin() + 1);
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  CHECK(fixture.renderer.resourceStats()->shadowBytes == 7 * 256 * 256 * 4);
  const auto removed = fixture.render();
  CHECK(fixture.renderer.resourceStats()->shadowBytes == 2 * 128 * 128 * 4);
  CHECK(fixture.red(removed, {1, 0, 0}) == doctest::Approx(fixture.red(references[0], {1, 0, 0})));
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowInstances < 2 * 7);
}

TEST_CASE("shadows.gpu_budget_failure_preserves_lights_maps_and_completed_target") {
  RendererConfig config;
  config.maxShadowBytes = 128 * 128 * 4;
  auto renderer = Renderer::create(nullptr, config).value();
  auto target = renderer.createTarget(4, 4).value();
  ShadowSettings settings;
  settings.resolution = 128;
  settings.cascades = 1;
  settings.distance = 10;
  Lighting lighting;
  lighting.lights = {{.shadows = settings}};
  REQUIRE(renderer.setLighting(lighting));
  auto camera = Dot::Math::multiply(Dot::Math::perspective(60, 1, .1F, 10).value(),
                                    Dot::Math::lookAt({0, 0, 1}, {}).value())
                    .value();
  REQUIRE(renderer.renderMeshesInto(target, {}, camera));
  auto image = renderer.readHdrTarget(target).value();
  const auto bytes = renderer.resourceStats()->shadowBytes;
  const auto frames = target.info()->frames;
  lighting.lights.push_back(lighting.lights[0]);
  CHECK_FALSE(renderer.setLighting(lighting));
  CHECK(renderer.lighting()->lights.size() == 1);
  CHECK(renderer.resourceStats()->shadowBytes == bytes);
  CHECK(target.info()->frames == frames);
  CHECK(renderer.readHdrTarget(target)->pixels == image.pixels);
  RendererConfig invalid;
  invalid.maxShadowBytes = 0;
  CHECK_FALSE(Renderer::create(nullptr, invalid));
  invalid.maxShadowBytes = 512 * 1024 * 1024 + 1;
  CHECK_FALSE(Renderer::create(nullptr, invalid));
  lighting.lights.clear();
  REQUIRE(renderer.setLighting(lighting));
  CHECK(renderer.resourceStats()->shadowBytes == 0);
}

TEST_CASE("shadows.gpu_all_64_point_sources_pack_and_last_light_keeps_real_occlusion") {
  Fixture fixture;
  auto light = fixture.lights.lights[0];
  light.kind = LightKind::Point;
  light.position = {-2, 3, 0};
  light.intensity = 0;
  light.range = 12;
  fixture.lights.lights.assign(64, light);
  fixture.lights.lights.back().intensity = 80;
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  const auto all = fixture.render();
  CHECK(fixture.renderer.resourceStats()->shadowBytes == 24 * 1024 * 1024);
  CHECK(fixture.red(all, {1, 0, 0}) < .01F);
  CHECK(fixture.red(all, {-1, 0, 0}) > .3F);
  CHECK(fixture.render().pixels == all.pixels);
  const auto reused = fixture.renderer.visibilityStats()->mesh.value();
  CHECK(reused.shadowDraws == 0);
  CHECK(reused.shadowPagesUpdated == 0);
  CHECK(reused.shadowPagesReused > 0);
  fixture.lights.lights[1].intensity = 40;
  fixture.lights.lights[1].color = {0, 1, 0};
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  const auto neighbours = fixture.render();
  CHECK(neighbours.pixels != all.pixels);
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesUpdated == 0);
  // Change one packed source; its neighbours must survive the whole-page clear.
  // 改变一组已打包光源，整页清除后其相邻图块必须保持正确。
  fixture.lights.lights.front().position.x -= 1;
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  CHECK(fixture.render().pixels == neighbours.pixels);
  const auto partial = fixture.renderer.visibilityStats()->mesh.value();
  CHECK(partial.shadowPagesUpdated > 0);
  CHECK(partial.shadowPagesUpdated <= 6);
  CHECK(partial.shadowPagesReused > 0);
  CHECK(partial.shadowPagesUpdated + partial.shadowPagesReused == reused.shadowPagesReused);
  fixture.lights.lights[1].intensity = 0;
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  CHECK(fixture.render().pixels == all.pixels);
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesUpdated == 0);
  light.intensity = 80;
  fixture.lights.lights.assign(1, light);
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  const auto single = fixture.render();
  CHECK(single.pixels == all.pixels);
  CHECK(fixture.renderer.resourceStats()->shadowBytes == 6 * 128 * 128 * 4);
}

TEST_CASE("shadows.gpu_cache_reuses_depth_across_shading_changes_and_invalidates_camera") {
  Fixture fixture;
  const auto first = fixture.render();
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesUpdated == 1);
  CHECK(fixture.render().pixels == first.pixels);
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesUpdated == 0);
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesReused == 1);
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowDraws == 0);
  fixture.lights.lights[0].intensity *= .5F;
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  const auto dimmed = fixture.render();
  CHECK(dimmed.pixels != first.pixels);
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesUpdated == 0);
  fixture.draws[0].material.roughness = .5F;
  fixture.draws[0].receivesShadow = false;
  fixture.render();
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesReused == 1);
  fixture.lights.viewPosition.x += 1;
  fixture.camera
      = Dot::Math::multiply(
            Dot::Math::orthographic(-4, 4, -4, 4, .1F, 12).value(),
            Dot::Math::lookAt(fixture.lights.viewPosition, {1, 0, 0}, {0, 0, -1}).value())
            .value();
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  fixture.render();
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesUpdated == 1);
}

TEST_CASE("shadows.gpu_cache_updates_only_local_pages_affected_by_moving_casters") {
  Fixture fixture;
  auto spot = fixture.lights.lights[0];
  spot.kind = LightKind::Spot;
  spot.position = {0, 6, 0};
  spot.direction = {0, -1, 0};
  spot.range = 10;
  spot.intensity = 80;
  auto remote = spot;
  remote.position.x = 30;
  fixture.lights.lights = {spot, remote};
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  std::array draws{fixture.draws[0], fixture.draws[1], fixture.draws[1]};
  draws[2].transform = Dot::Math::compose({30, 1, 0}).value();
  REQUIRE(fixture.renderer.renderMeshesInto(fixture.target, draws, fixture.camera));
  const auto first = fixture.renderer.readHdrTarget(fixture.target).value();
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesUpdated == 2);
  draws[2].transform = Dot::Math::compose({31, 1, 0}).value();
  REQUIRE(fixture.renderer.renderMeshesInto(fixture.target, draws, fixture.camera));
  CHECK(fixture.renderer.readHdrTarget(fixture.target)->pixels == first.pixels);
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesUpdated == 1);
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesReused == 1);
  draws[2].castsShadow = false;
  REQUIRE(fixture.renderer.renderMeshesInto(fixture.target, draws, fixture.camera));
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesUpdated == 1);
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesReused == 1);
  REQUIRE(fixture.renderer.renderMeshesInto(fixture.target, draws, fixture.camera));
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesUpdated == 0);
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesReused == 2);
}

TEST_CASE("shadows.gpu_cache_does_not_retain_resources_or_confuse_expired_mask_with_white") {
  Fixture fixture;
  auto texture = fixture.renderer.uploadTexture(Image{1, 1, {255, 255, 255, 0}}).value();
  fixture.draws[1].material.alphaMode = AlphaMode::Mask;
  fixture.draws[1].material.baseColorTexture = texture;
  CHECK(fixture.red(fixture.render(), {1, 0, 0}) > .4F);
  fixture.draws[1].material.baseColorTexture.reset();
  texture = {};
  CHECK(fixture.renderer.resourceStats()->textures == 0);
  CHECK(fixture.red(fixture.render(), {1, 0, 0}) < .01F);
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesUpdated == 1);
  fixture.caster = {};
  CHECK(fixture.renderer.resourceStats()->meshes == 1);
  fixture.caster = fixture.renderer.upload(cube({.5F, .5F, .5F}).value()).value();
  fixture.render();
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesUpdated == 1);
}

TEST_CASE("shadows.gpu_cache_abandoned_encoding_preserves_target_and_recovers_with_fresh_depth") {
  Fixture fixture;
  fixture.lights.lights[0].kind = LightKind::Point;
  fixture.lights.lights[0].position = {-2, 3, 0};
  REQUIRE(fixture.renderer.setLighting(fixture.lights));
  const auto first = fixture.render();
  const auto report = fixture.renderer.visibilityStats()->mesh.value();
  const auto frames = fixture.target.info()->frames;
  REQUIRE(fixture.renderer.setOcclusion(OcclusionSettings{}));
  Dot::Math::Mat4 shear;
  shear.data[8] = .5F;
  const auto camera = Dot::Math::multiply(fixture.camera, shear).value();
  const auto invalid = fixture.renderer.renderMeshesInto(fixture.target, fixture.draws, camera);
  REQUIRE_FALSE(invalid);
  CHECK(invalid.error().message.find("occlusion") != std::string::npos);
  CHECK(fixture.renderer.visibilityStats()->mesh.value() == report);
  CHECK(fixture.target.info()->frames == frames);
  CHECK(fixture.renderer.readHdrTarget(fixture.target)->pixels == first.pixels);
  REQUIRE(fixture.renderer.setOcclusion(std::nullopt));
  CHECK(fixture.render().pixels == first.pixels);
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesUpdated == 6);
  CHECK(fixture.render().pixels == first.pixels);
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowPagesReused == 6);
}

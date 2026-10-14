#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <thread>
#include <vector>

import Dot.Graphics.Renderer;
import Dot.Math;

using namespace Dot::Graphics;

namespace {
  auto createRenderer() -> Renderer {
    auto result = Renderer::create();
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return std::move(*result);
  }

  struct Fixture {
    Renderer renderer;
    Mesh ground;
    Mesh object;
    RenderTarget target;
    Dot::Math::Mat4 camera;
    std::array<MeshDraw, 2> draws;

    Fixture()
        : renderer(createRenderer()),
          ground(renderer.upload(quad({8, 8}).value()).value()),
          object(renderer.upload(cube().value()).value()),
          target(renderer.createTarget(129, 129).value()),
          camera(Dot::Math::multiply(Dot::Math::perspective(50, 1, 0.1F, 30).value(),
                                     Dot::Math::lookAt({4, 4, 6}, {}).value())
                     .value()) {
      Lighting lighting;
      lighting.viewPosition = {4, 4, 6};
      REQUIRE(renderer.setLighting(lighting));
      auto map
          = renderer.bakeEnvironment(HdrImage{4, 2, std::vector<float>(32, 1)}, {16, 4, 16, 16, 32})
                .value();
      REQUIRE(renderer.setEnvironment(EnvironmentLighting{map, 1, 0, false}));
      Material material;
      material.model = SurfaceModel::MetallicRoughness;
      material.baseColor = {0.5F, 0.5F, 0.5F, 1};
      material.roughness = 1;
      draws[0]
          = {&ground,
             Dot::Math::compose({}, Dot::Math::rotationEulerDegrees({-90, 0, 0}).value()).value(),
             {1, 1, 1, 1},
             material};
      draws[1] = {&object, Dot::Math::compose({0, 0.5F, 0}).value(), {1, 1, 1, 1}, material};
    }

    auto render(bool includeObject = true) -> HdrImage {
      const auto count = includeObject ? 2U : 1U;
      auto result = renderer.renderMeshesInto(target, std::span(draws).first(count), camera);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return renderer.readHdrTarget(target).value();
    }
  };
}  // namespace

TEST_CASE("occlusion.gpu_plane_background_contact_and_lifted_objects_use_real_guides") {
  Fixture fixture;
  REQUIRE(fixture.renderer.setOcclusion(OcclusionSettings{}));
  CHECK_FALSE(fixture.renderer.readOcclusion());
  fixture.render(false);
  auto plane = fixture.renderer.readOcclusion();
  REQUIRE_MESSAGE(plane, (plane ? "" : plane.error().message));
  CHECK(*std::min_element(plane->pixels.begin(), plane->pixels.end()) >= 254);
  const auto contact = fixture.render();
  const auto visibility = fixture.renderer.readOcclusion().value();
  const auto minimum = *std::min_element(visibility.pixels.begin(), visibility.pixels.end());
  CHECK(minimum < 235);
  std::size_t occluded{};

  for (std::size_t index = 0; index < visibility.pixels.size(); index += 4) {
    occluded += visibility.pixels[index] < 250;
    CHECK(visibility.pixels[index] == visibility.pixels[index + 1]);
    CHECK(visibility.pixels[index] == visibility.pixels[index + 2]);
    CHECK(visibility.pixels[index + 3] == 255);
  }

  CHECK(occluded > 20);
  CHECK(visibility.pixels[0] == 255);
  fixture.draws[1].transform = Dot::Math::compose({0, 2, 0}).value();
  fixture.render();
  const auto lifted = fixture.renderer.readOcclusion().value();
  std::size_t liftedDark{};

  for (std::size_t index = 0; index < lifted.pixels.size(); index += 4) {
    liftedDark += lifted.pixels[index] < 250;
  }

  CHECK(liftedDark < occluded);
  auto settings = OcclusionSettings{};
  settings.strength = 0;
  REQUIRE(fixture.renderer.setOcclusion(settings));
  fixture.render();
  const auto zeroStrength = fixture.renderer.readOcclusion().value();
  CHECK(*std::min_element(zeroStrength.pixels.begin(), zeroStrength.pixels.end()) == 255);
}

TEST_CASE("occlusion.gpu_diffuse_only_composition_preserves_direct_specular_emission_and_unlit") {
  Fixture fixture;
  const auto before = fixture.render();
  REQUIRE(fixture.renderer.setOcclusion(OcclusionSettings{}));
  const auto shaded = fixture.render();
  float maximumDifference{};

  for (std::size_t index = 0; index < before.pixels.size(); index += 4) {
    maximumDifference = std::max(maximumDifference, before.pixels[index] - shaded.pixels[index]);
    CHECK(shaded.pixels[index] <= before.pixels[index] + 0.001F);
  }

  CHECK(maximumDifference > 0.03F);
  fixture.draws[0].material.metallic = 1;
  fixture.draws[1].material.metallic = 1;
  REQUIRE(fixture.renderer.setOcclusion(std::nullopt));
  const auto metal = fixture.render();
  REQUIRE(fixture.renderer.setOcclusion(OcclusionSettings{}));
  CHECK(fixture.render().pixels == metal.pixels);
  REQUIRE(fixture.renderer.setEnvironment(std::nullopt));
  Lighting lighting;
  lighting.viewPosition = {4, 4, 6};
  lighting.lights = {{.direction = {0, -1, 0}, .intensity = 3}};
  REQUIRE(fixture.renderer.setLighting(lighting));
  fixture.draws[0].material.metallic = 0;
  fixture.draws[0].material.emissive = {2, 1, 0};
  REQUIRE(fixture.renderer.setOcclusion(std::nullopt));
  const auto direct = fixture.render();
  REQUIRE(fixture.renderer.setOcclusion(OcclusionSettings{}));
  CHECK(fixture.render().pixels == direct.pixels);
  fixture.draws[0].material.model = SurfaceModel::Unlit;
  const auto unlit = fixture.render();
  REQUIRE(fixture.renderer.setOcclusion(std::nullopt));
  CHECK(fixture.render().pixels == unlit.pixels);
}

TEST_CASE("occlusion.gpu_resize_disable_transaction_thread_and_failure_retention") {
  Fixture fixture;
  CHECK_FALSE(fixture.renderer.occlusion().value());
  CHECK_FALSE(fixture.renderer.readOcclusion());
  REQUIRE(fixture.renderer.setOcclusion(OcclusionSettings{}));
  fixture.render();
  const auto image = fixture.renderer.readOcclusion().value();
  CHECK(fixture.renderer.resourceStats()->occlusionBytes == 129 * 129 * 24);
  auto bad = OcclusionSettings{};
  bad.radius = std::numeric_limits<float>::quiet_NaN();
  const auto revision = fixture.renderer.occlusionRevision().value();
  CHECK_FALSE(fixture.renderer.setOcclusion(bad));
  CHECK(fixture.renderer.occlusionRevision().value() == revision);
  CHECK(fixture.renderer.occlusion()->value() == OcclusionSettings{});
  CHECK(fixture.renderer.readOcclusion()->pixels == image.pixels);
  bool rejected{};
  std::thread worker([&] {
    rejected = !fixture.renderer.occlusion() && !fixture.renderer.setOcclusion(std::nullopt)
               && !fixture.renderer.readOcclusion() && !fixture.renderer.occlusionRevision();
  });
  worker.join();
  CHECK(rejected);
  Dot::Math::Mat4 singular;
  singular.data.fill(0);
  const auto frames = fixture.target.info()->frames;
  CHECK_FALSE(fixture.renderer.renderMeshesInto(fixture.target, fixture.draws, singular));
  CHECK(fixture.target.info()->frames == frames);
  CHECK(fixture.renderer.readOcclusion()->pixels == image.pixels);
  auto target = fixture.renderer.createTarget(65, 97).value();
  REQUIRE(fixture.renderer.renderMeshesInto(target, fixture.draws, fixture.camera));
  CHECK(fixture.renderer.readOcclusion()->width == 65);
  CHECK(fixture.renderer.readOcclusion()->height == 97);
  CHECK(fixture.renderer.resourceStats()->occlusionBytes == 65 * 97 * 24);
  REQUIRE(fixture.renderer.setOcclusion(std::nullopt));
  CHECK(fixture.renderer.resourceStats()->occlusionBytes == 0);
  CHECK_FALSE(fixture.renderer.readOcclusion());
}

TEST_CASE("occlusion.gpu_orthographic_camera_motion_and_normal_maps_keep_flat_surfaces_clear") {
  Fixture fixture;
  Lighting lighting;
  lighting.viewPosition = {4, 4, 6};
  lighting.viewDirection = Dot::Math::normalized(Dot::Math::Vec3{-4, -4, -6}).value();
  REQUIRE(fixture.renderer.setLighting(lighting));
  fixture.camera = Dot::Math::multiply(Dot::Math::orthographic(-4, 4, -4, 4, 0.1F, 30).value(),
                                       Dot::Math::lookAt(lighting.viewPosition, {}).value())
                       .value();
  OcclusionSettings quality;
  quality.directions = 16;
  quality.steps = 8;
  quality.filterRadius = 4;
  REQUIRE(fixture.renderer.setOcclusion(quality));
  fixture.render(false);
  const auto flat = fixture.renderer.readOcclusion().value();
  auto normal = fixture.renderer.uploadTexture(Image{1, 1, {230, 128, 190, 255}},
                                               {.colorSpace = ColorSpace::Linear});
  REQUIRE(normal);
  fixture.draws[0].material.normalTexture = *normal;
  fixture.render(false);
  CHECK(fixture.renderer.readOcclusion()->pixels == flat.pixels);

  CHECK(*std::min_element(flat.pixels.begin(), flat.pixels.end()) >= 254);
  fixture.render();
  const auto contact = fixture.renderer.readOcclusion().value();
  CHECK(*std::min_element(contact.pixels.begin(), contact.pixels.end()) < 235);
  lighting.viewPosition = {-4, 4, 6};
  lighting.viewDirection = Dot::Math::normalized(Dot::Math::Vec3{4, -4, -6}).value();
  REQUIRE(fixture.renderer.setLighting(lighting));
  fixture.camera = Dot::Math::multiply(Dot::Math::orthographic(-4, 4, -4, 4, 0.1F, 30).value(),
                                       Dot::Math::lookAt(lighting.viewPosition, {}).value())
                       .value();
  fixture.render();
  const auto moved = fixture.renderer.readOcclusion().value();
  CHECK(moved.pixels != contact.pixels);
  CHECK(*std::min_element(moved.pixels.begin(), moved.pixels.end()) < 235);
}

TEST_CASE("visibility.gpu_ssao_depth_skips_offscreen_meshes_and_matches_visible_only_image") {
  Fixture fixture;
  REQUIRE(fixture.renderer.setOcclusion(OcclusionSettings{}));
  fixture.draws[1].transform = Dot::Math::compose({100, 0.5F, 0}).value();
  REQUIRE(fixture.renderer.renderMeshesInto(fixture.target, std::span(fixture.draws).first(1),
                                            fixture.camera));
  const auto baseline = fixture.renderer.readTarget(fixture.target).value();
  REQUIRE(fixture.renderer.renderMeshesInto(fixture.target, fixture.draws, fixture.camera));
  CHECK((fixture.renderer.readTarget(fixture.target)->pixels == baseline.pixels));
  const auto report = fixture.renderer.visibilityStats().value();
  REQUIRE(report.mesh);
  CHECK(report.mesh->visibleInstances == 1);
  CHECK(report.mesh->culledInstances == 1);
  CHECK(report.mesh->occlusionInstances == 1);
}

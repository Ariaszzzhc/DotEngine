#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

import Dot.Graphics.Renderer;
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
  CHECK_FALSE(fixture.renderer.setLighting(invalid));
  invalid = fixture.lights;
  invalid.lights[1].kind = LightKind::Spot;
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
  CHECK(report.mesh->inputDraws == 2);
  CHECK(report.mesh->visibleDraws == 1);
  CHECK(report.mesh->culledDraws == 1);
  CHECK(report.mesh->shadowDraws == 2);
  const auto dark = fixture.red(shadowed, {0, 0, 0});
  fixture.draws[1].castsShadow = false;
  const auto lit = fixture.render();
  CHECK(fixture.red(lit, {0, 0, 0}) > dark + 0.3F);
  CHECK(fixture.renderer.visibilityStats()->mesh->shadowDraws == 1);
  CHECK(fixture.renderer.visibilityStats()->mesh->visibleDraws == 1);
}

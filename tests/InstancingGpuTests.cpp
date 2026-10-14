#include <doctest/doctest.h>

#include <array>
#include <vector>

import Dot.Graphics.Renderer;
import Dot.Math;

namespace {
  using namespace Dot::Graphics;

  auto camera() -> Dot::Math::Mat4 {
    return Dot::Math::multiply(Dot::Math::orthographic(-2, 2, -2, 2, 0.1F, 20).value(),
                               Dot::Math::lookAt({0, 0, 5}, {}).value())
        .value();
  }
}  // namespace

TEST_CASE("instancing.gpu_per_instance_pbr_attributes_match_unbatched_mesh_resource_reference") {
  auto renderer = Renderer::create().value();
  const auto geometry = cube({0.3F, 0.3F, 0.3F}).value();
  auto mesh = renderer.upload(geometry).value();
  auto target = renderer.createTarget(97, 97).value();
  Lighting lighting;
  lighting.viewPosition = {0, 0, 5};
  lighting.viewDirection = Dot::Math::Vec3{0, 0, -1};
  lighting.lights = {{.direction = {0, 0, -1}, .intensity = 4}};
  REQUIRE(renderer.setLighting(lighting));
  const auto map
      = renderer.bakeEnvironment(HdrImage{4, 2, std::vector<float>(32, 1)}, {16, 4, 16, 16, 32})
            .value();
  REQUIRE(renderer.setEnvironment(EnvironmentLighting{map, 1, 0, false}));
  REQUIRE(renderer.setOcclusion(OcclusionSettings{}));
  std::vector<MeshDraw> draws;

  for (int i = 0; i < 16; ++i) {
    Material material;
    material.model = i % 3 == 0 ? SurfaceModel::Unlit : SurfaceModel::MetallicRoughness;
    material.baseColor = {0.1F + static_cast<float>(i) * 0.04F, 0.5F, 0.3F, 1};
    material.emissive = {static_cast<float>(i) * 0.01F, 0, 0};
    material.metallic = i % 2 ? 1.F : 0.F;
    material.roughness = 0.1F + static_cast<float>(i) * 0.05F;
    material.doubleSided = true;
    const auto rotation
        = Dot::Math::rotationEulerDegrees({20, static_cast<float>(i) * 5, 0}).value();
    draws.push_back({&mesh,
                     Dot::Math::compose({-1.2F + static_cast<float>(i % 4) * 0.8F,
                                         -1.2F + static_cast<float>(i / 4) * 0.8F, 0},
                                        rotation, {i % 2 ? -1.F : 1.F, 1.2F, 0.7F})
                         .value(),
                     {1, 0.8F, 1, 1},
                     material});
  }

  REQUIRE(renderer.renderMeshesInto(target, draws, camera()));
  const auto batched = renderer.readTarget(target).value();
  CHECK(renderer.visibilityStats()->mesh->visibleInstances == 16);
  CHECK(renderer.visibilityStats()->mesh->colorDraws == 1);
  CHECK(renderer.visibilityStats()->mesh->occlusionDraws == 1);
  std::vector<Mesh> copies;
  copies.reserve(draws.size());

  for (auto& draw : draws) {
    copies.push_back(renderer.upload(geometry).value());
    draw.mesh = &copies.back();
  }

  REQUIRE(renderer.renderMeshesInto(target, draws, camera()));
  CHECK(renderer.visibilityStats()->mesh->colorDraws == 16);
  CHECK(renderer.visibilityStats()->mesh->occlusionDraws == 16);
  CHECK((renderer.readTarget(target)->pixels == batched.pixels));
}

TEST_CASE(
    "instancing.gpu_texture_keys_and_equal_depth_source_order_keep_exact_pixels_and_pick_ids") {
  auto renderer = Renderer::create().value();
  const auto geometry = quad().value();
  auto mesh = renderer.upload(geometry).value();
  auto other = renderer.upload(geometry).value();
  auto red = renderer.uploadTexture(Image{1, 1, {255, 0, 0, 255}}).value();
  auto green = renderer.uploadTexture(Image{1, 1, {0, 255, 0, 255}}).value();
  Material a;
  a.baseColorTexture = red;
  Material b;
  b.baseColorTexture = green;
  std::array<MeshDraw, 4> draws{{{&mesh, {}, {1, 1, 1, 1}, a},
                                 {&mesh, {}, {1, 1, 1, 1}, b},
                                 {&other, {}, {1, 1, 1, 1}, b},
                                 {&mesh, {}, {1, 1, 1, 1}, a}}};
  auto image = renderer.renderMeshes(draws, camera(), 65, 65).value();
  CHECK(renderer.visibilityStats()->mesh->colorDraws == 4);
  CHECK(renderer.pickMeshes(draws, camera(), 65, 65, {0.5F, 0.5F}).value() == 0);
  CHECK(renderer.visibilityStats()->pick->draws == 3);

  for (auto& draw : draws) {
    draw.material = a;
  }

  auto equal = renderer.renderMeshes(draws, camera(), 65, 65).value();
  CHECK(renderer.visibilityStats()->mesh->colorDraws == 3);
  CHECK((equal.pixels == image.pixels));
  CHECK(renderer.pickMeshes(draws, camera(), 65, 65, {0.5F, 0.5F}).value() == 0);
}

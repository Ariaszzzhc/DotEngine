#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

import Dot.Graphics.Renderer;
import Dot.Math;

namespace {
  using namespace Dot::Graphics;

  auto pixel(Renderer& renderer, const std::vector<MeshDraw>& draws) -> std::array<float, 4> {
    auto target = renderer.createTarget(1, 1).value();
    REQUIRE(renderer.renderMeshesInto(target, draws, {}));
    auto image = renderer.readHdrTarget(target).value();
    return {image.pixels[0], image.pixels[1], image.pixels[2], image.pixels[3]};
  }

  auto translated(float z) -> Dot::Math::Mat4 {
    return Dot::Math::compose({0, 0, z}).value();
  }
}  // namespace

TEST_CASE("alpha.gpu_opaque_ignores_alpha_mask_cutoff_and_pick_match_actual_coverage") {
  auto renderer = Renderer::create().value();
  auto geometry = quad({2, 2}).value();
  auto mesh = renderer.upload(geometry).value();
  Material front;
  front.baseColor = {1, 0, 0, .25F};
  Material back;
  back.baseColor = {0, 1, 0, 1};
  std::vector<MeshDraw> draws{{&mesh, translated(.2F), {1, 1, 1, 1}, front},
                              {&mesh, translated(.7F), {1, 1, 1, 1}, back}};
  CHECK(pixel(renderer, draws)[0] == 1);
  CHECK(pixel(renderer, draws)[3] == 1);
  draws[0].material.alphaMode = AlphaMode::Mask;
  CHECK(pixel(renderer, draws)[1] == 1);
  CHECK(renderer.pickMeshes(draws, {}, 1, 1, {.5F, .5F}).value() == 1);
  draws[0].material.alphaCutoff = .25F;
  CHECK(pixel(renderer, draws)[0] == 1);
  CHECK(renderer.pickMeshes(draws, {}, 1, 1, {.5F, .5F}).value() == 0);
  auto map = renderer.uploadTexture(Image{1, 1, {255, 255, 255, 0}}).value();
  draws[0].material.baseColorTexture = map;
  CHECK(pixel(renderer, draws)[1] == 1);
  CHECK(renderer.pickMeshes(draws, {}, 1, 1, {.5F, .5F}).value() == 1);
  draws[0].material.alphaMode = AlphaMode::Blend;
  CHECK(renderer.pickMeshes(draws, {}, 1, 1, {.5F, .5F}).value() == 1);
}

TEST_CASE("alpha.gpu_stable_sorted_hdr_blend_no_depth_write_and_source_pick_identity") {
  auto renderer = Renderer::create().value();
  auto mesh = renderer.upload(quad({2, 2}).value()).value();
  Material red;
  red.alphaMode = AlphaMode::Blend;
  red.baseColor = {1, 0, 0, .5F};
  red.emissive = {2, 0, 0};
  Material blue = red;
  blue.baseColor = {0, 0, 1, .5F};
  blue.emissive = {};
  Material green;
  green.baseColor = {0, 1, 0, 1};
  std::vector<MeshDraw> draws{{&mesh, translated(.2F), {1, 1, 1, 1}, red},
                              {&mesh, translated(.8F), {1, 1, 1, 1}, blue},
                              {&mesh, translated(.9F), {1, 1, 1, 1}, green}};
  const auto output = pixel(renderer, draws);
  CHECK(renderer.visibilityStats()->mesh->blendedInstances == 2);
  CHECK(renderer.visibilityStats()->mesh->solidInstances == 1);
  CHECK(output[0] == doctest::Approx(1.5));
  CHECK(output[1] == doctest::Approx(.25));
  CHECK(output[2] == doctest::Approx(.25));
  CHECK(output[3] == 1);
  CHECK(renderer.pickMeshes(draws, {}, 1, 1, {.5F, .5F}).value() == 0);
  std::swap(draws[0], draws[1]);
  CHECK(pixel(renderer, draws) == output);
  CHECK(renderer.pickMeshes(draws, {}, 1, 1, {.5F, .5F}).value() == 1);
  auto report = renderer.visibilityStats().value();
  draws[0].material.alphaCutoff = -1;
  CHECK_FALSE(renderer.renderMeshes(draws, {}, 1, 1));
  CHECK(renderer.visibilityStats().value() == report);
}

TEST_CASE("alpha.gpu_mask_and_blend_geometry_guides_follow_depth_policy") {
  auto renderer = Renderer::create().value();
  REQUIRE(renderer.setOcclusion(OcclusionSettings{}));
  auto mesh = renderer.upload(quad({2, 2}).value()).value();
  const auto camera = Dot::Math::multiply(Dot::Math::orthographic(-1, 1, -1, 1, .1F, 10).value(),
                                          Dot::Math::lookAt({0, 0, 3}, {}).value())
                          .value();
  Material material;
  material.alphaMode = AlphaMode::Blend;
  material.baseColor.w = .5F;
  auto target = renderer.createTarget(16, 16).value();
  std::array draws{MeshDraw{&mesh, {}, {1, 1, 1, 1}, material}};
  auto rendered = renderer.renderMeshesInto(target, draws, camera);
  REQUIRE_MESSAGE(rendered, (rendered ? "" : rendered.error().message));
  CHECK(renderer.visibilityStats()->mesh->occlusionInstances == 0);
  draws[0].material.alphaMode = AlphaMode::Mask;
  draws[0].material.baseColor.w = 0;
  rendered = renderer.renderMeshesInto(target, draws, camera);
  REQUIRE_MESSAGE(rendered, (rendered ? "" : rendered.error().message));
  auto ao = renderer.readOcclusion().value();
  CHECK(std::ranges::all_of(ao.pixels, [](auto byte) { return byte == 255; }));
}

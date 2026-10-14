#include <doctest/doctest.h>

#include <array>
#include <optional>
#include <thread>
#include <vector>

import Dot.Graphics.Renderer;
import Dot.Math;

using namespace Dot::Graphics;

namespace {
  auto device() -> Renderer {
    auto result = Renderer::create();
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return std::move(*result);
  }
}  // namespace

TEST_CASE("picking.gpu_uses_visible_triangles_depth_and_pixel_centers_without_changing_frames") {
  auto renderer = device();
  auto quadMesh = renderer.upload(quad({2, 2}).value()).value();
  MeshData triangleData;
  triangleData.vertices = {{{-1, -1, 0}}, {{1, -1, 0}}, {{-1, 1, 0}}};
  auto triangle = renderer.upload(triangleData).value();
  const auto camera = Dot::Math::multiply(Dot::Math::orthographic(-2, 2, -2, 2, 0.1F, 20).value(),
                                          Dot::Math::lookAt({0, 0, 5}, {}).value())
                          .value();
  std::array<MeshDraw, 2> draws{{{&triangle, Dot::Math::compose({0, 0, 1}).value()}, {&quadMesh}}};
  auto target = renderer.createTarget(101, 73).value();
  REQUIRE(renderer.renderMeshesInto(target, draws, camera));
  const auto original = renderer.readTarget(target).value();
  const auto frames = target.info()->frames;
  auto result = renderer.pickMeshes(draws, camera, 101, 73, {0.375F, 0.625F});
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(*result == 0);
  CHECK(renderer.pickMeshes(draws, camera, 101, 73, {0.625F, 0.375F}).value() == 1);
  CHECK_FALSE(renderer.pickMeshes(draws, camera, 101, 73, {0.95F, 0.95F}).value());
  CHECK_FALSE(renderer.pickMeshes({}, camera, 101, 73, {0.5F, 0.5F}).value());
  CHECK(target.info()->frames == frames);
  CHECK(renderer.readTarget(target)->pixels == original.pixels);
  CHECK(renderer.resourceStats()->targets == 1);
  CHECK_FALSE(renderer.pickMeshes(draws, camera, 101, 73, {1, 0}));
  CHECK_FALSE(renderer.pickMeshes(draws, camera, 0, 73, {}));
  bool rejected{};
  std::thread worker([&] { rejected = !renderer.pickMeshes(draws, camera, 101, 73, {}); });
  worker.join();
  CHECK(rejected);
}

TEST_CASE("picking.gpu_matches_opaque_backface_and_mirrored_surface_policy") {
  auto renderer = device();
  auto mesh = renderer.upload(quad({2, 2}).value()).value();
  const auto camera = Dot::Math::multiply(Dot::Math::perspective(60, 1, 0.1F, 20).value(),
                                          Dot::Math::lookAt({0, 0, 5}, {}).value())
                          .value();
  Material physical;
  physical.model = SurfaceModel::MetallicRoughness;
  std::array<MeshDraw, 2> draws{
      {{&mesh,
        Dot::Math::compose({0, 0, 1}, Dot::Math::rotationEulerDegrees({0, 180, 0}).value()).value(),
        {1, 1, 1, 1},
        physical},
       {&mesh}}};
  CHECK(renderer.pickMeshes(draws, camera, 129, 129, {0.5F, 0.5F}).value() == 1);
  draws[0].material.doubleSided = true;
  CHECK(renderer.pickMeshes(draws, camera, 129, 129, {0.5F, 0.5F}).value() == 0);
  draws[0].material.doubleSided = false;
  draws[0].transform = Dot::Math::compose({0, 0, 1}, {}, {-1, 1, 1}).value();
  CHECK(renderer.pickMeshes(draws, camera, 129, 129, {0.5F, 0.5F}).value() == 0);
  auto moved = std::move(renderer);
  CHECK_FALSE(renderer.pickMeshes(draws, camera, 129, 129, {0.5F, 0.5F}));
  CHECK(moved.pickMeshes(draws, camera, 129, 129, {0.5F, 0.5F}).value() == 0);
}

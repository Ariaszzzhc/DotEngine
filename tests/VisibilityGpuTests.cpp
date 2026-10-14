#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <thread>
#include <utility>
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

TEST_CASE("visibility.gpu_dense_offscreen_frame_skips_draws_preserves_pixels_and_failure_report") {
  auto renderer = Renderer::create().value();
  CHECK_FALSE(renderer.visibilityStats()->mesh);
  CHECK_FALSE(renderer.visibilityStats()->pick);
  auto mesh = renderer.upload(cube().value()).value();
  auto target = renderer.createTarget(65, 65).value();
  const MeshDraw visible{&mesh};
  REQUIRE(renderer.renderMeshesInto(target, std::span(&visible, 1), camera()));
  const auto baseline = renderer.readTarget(target).value();
  std::vector<MeshDraw> draws(1024, visible);

  for (std::size_t i = 1; i < draws.size(); ++i) {
    draws[i].transform = Dot::Math::compose({100 + static_cast<float>(i), 0, 0}).value();
  }

  REQUIRE(renderer.renderMeshesInto(target, draws, camera()));
  const auto result = renderer.readTarget(target).value();
  CHECK((result.pixels == baseline.pixels));
  const auto report = renderer.visibilityStats().value();
  REQUIRE(report.mesh);
  CHECK(report.mesh->inputInstances == 1024);
  CHECK(report.mesh->visibleInstances == 1);
  CHECK(report.mesh->culledInstances == 1023);
  CHECK(report.mesh->shadowInstances == 0);
  const auto frames = target.info()->frames;
  draws.back().tint.x = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(renderer.renderMeshesInto(target, draws, camera()));
  CHECK(renderer.visibilityStats().value() == report);
  CHECK(target.info()->frames == frames);
  CHECK((renderer.readTarget(target)->pixels == baseline.pixels));
  bool rejected{};
  std::thread worker([&] { rejected = !renderer.visibilityStats(); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(renderer);
  CHECK_FALSE(renderer.visibilityStats());
  CHECK(moved.visibilityStats().value() == report);
}

TEST_CASE("visibility.gpu_pixel_crop_keeps_original_draw_indices_and_independent_reports") {
  auto renderer = Renderer::create().value();
  auto mesh = renderer.upload(quad().value()).value();
  std::array<MeshDraw, 3> draws{{{&mesh, Dot::Math::compose({10, 0, 0}).value()},
                                 {&mesh, Dot::Math::compose({-10, 0, 0}).value()},
                                 {&mesh}}};
  REQUIRE(renderer.renderMeshes(draws, camera(), 65, 65));
  const auto meshReport = renderer.visibilityStats()->mesh;
  auto picked = renderer.pickMeshes(draws, camera(), 65, 65, {0.5F, 0.5F});
  REQUIRE(picked);
  CHECK(*picked == 2);
  auto report = renderer.visibilityStats().value();
  REQUIRE(report.pick);
  CHECK(report.pick->inputInstances == 3);
  CHECK(report.pick->candidateInstances == 1);
  CHECK(report.pick->culledInstances == 2);
  CHECK(report.mesh == meshReport);
  CHECK_FALSE(renderer.pickMeshes(draws, camera(), 0, 65, {0.5F, 0.5F}));
  CHECK(renderer.visibilityStats().value() == report);
  CHECK_FALSE(renderer.pickMeshes(draws, camera(), 65, 65, {0.99F, 0.99F}).value());
  CHECK(renderer.visibilityStats()->pick->candidateInstances == 0);
  CHECK(renderer.visibilityStats()->mesh == meshReport);
}

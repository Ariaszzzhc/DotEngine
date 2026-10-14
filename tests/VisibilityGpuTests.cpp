#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

import Dot.Graphics.Renderer;
import Dot.Graphics.Frustum;
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
  CHECK(report.mesh->spatial.rebuilt);
  CHECK(report.mesh->spatial.indexedInstances == 1024);
  CHECK(report.mesh->spatial.nodeTests + report.mesh->spatial.boundsTests
            + report.mesh->spatial.exactTests
        < 64);
  CHECK(report.mesh->spatial.unindexedTests == 0);
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

TEST_CASE("visibility.gpu_bvh_reuse_motion_deletion_projective_and_extreme_bounds") {
  auto renderer = Renderer::create().value();
  auto mesh = renderer.upload(cube().value()).value();
  std::vector<MeshDraw> draws(64, MeshDraw{&mesh});

  for (std::size_t i = 0; i < draws.size(); ++i) {
    draws[i].transform = Dot::Math::compose({100 + float(i), 0, 0}).value();
  }

  REQUIRE(renderer.renderMeshes(draws, camera(), 33, 33));
  auto stats = renderer.visibilityStats()->mesh->spatial;
  CHECK(stats.rebuilt);
  CHECK(stats.nodeTests == 1);
  auto movedCamera
      = Dot::Math::multiply(camera(), Dot::Math::compose({0.2F, 0, 0}).value()).value();
  REQUIRE(renderer.renderMeshes(draws, movedCamera, 33, 33));
  stats = renderer.visibilityStats()->mesh->spatial;
  CHECK_FALSE(stats.rebuilt);
  CHECK(stats.changedBounds == 0);
  CHECK(stats.refittedNodes == 0);
  draws[37].transform = {};
  REQUIRE(renderer.renderMeshes(draws, camera(), 33, 33));
  stats = renderer.visibilityStats()->mesh->spatial;
  CHECK_FALSE(stats.rebuilt);
  CHECK(stats.changedBounds == 1);
  CHECK(stats.refittedNodes < 8);
  CHECK(renderer.visibilityStats()->mesh->visibleInstances == 1);
  CHECK(renderer.pickMeshes(draws, camera(), 33, 33, {0.5F, 0.5F}).value() == 37);
  draws.erase(draws.begin());
  REQUIRE(renderer.renderMeshes(draws, camera(), 33, 33));
  CHECK(renderer.visibilityStats()->mesh->spatial.rebuilt);
  CHECK(renderer.pickMeshes(draws, camera(), 33, 33, {0.5F, 0.5F}).value() == 36);
  draws.assign(1, MeshDraw{&mesh});
  REQUIRE(renderer.renderMeshes(draws, camera(), 33, 33));
  draws[0].transform.data[15] = 2;
  REQUIRE(renderer.renderMeshes(draws, camera(), 33, 33));
  stats = renderer.visibilityStats()->mesh->spatial;
  CHECK(stats.indexedInstances == 0);
  CHECK(stats.unindexedTests == 1);
  CHECK(stats.exactTests == 1);
  CHECK(renderer.visibilityStats()->mesh->visibleInstances == 1);
  draws[0].transform = {};
  draws[0].transform.data[0] = std::numeric_limits<float>::max();
  draws[0].transform.data[12] = std::numeric_limits<float>::max();
  REQUIRE(renderer.renderMeshes(draws, Dot::Math::Mat4{}, 33, 33));
  CHECK(renderer.visibilityStats()->mesh->spatial.unindexedTests == 1);
  CHECK(renderer.visibilityStats()->mesh->visibleInstances == 0);
  REQUIRE(renderer.renderMeshes({}, camera(), 33, 33));
  CHECK(renderer.visibilityStats()->mesh->inputInstances == 0);
  CHECK(renderer.visibilityStats()->mesh->spatial.nodeTests == 0);
}

TEST_CASE("visibility.gpu_affine_bvh_matches_homogeneous_rotated_sheared_boundary_boxes") {
  auto renderer = Renderer::create().value();
  auto geometry = cube().value();
  auto mesh = renderer.upload(geometry).value();
  const Bounds local{{-0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}};
  std::vector<MeshDraw> draws;

  for (int i = 0; i < 128; ++i) {
    auto model = Dot::Math::compose({float(i % 16) - 8, float(i / 16) - 4, 0},
                                    Dot::Math::rotationEulerDegrees({20, float(i), 35}).value(),
                                    {i % 2 ? -0.8F : 0.8F, 0.7F, 1.2F})
                     .value();
    model.data[4] += 0.35F;
    draws.push_back({&mesh, model});
  }

  draws.push_back({&mesh, Dot::Math::compose({2.5F, 0, 0}).value()});
  draws.push_back({&mesh, Dot::Math::compose({-2.5F, 0, 0}).value()});

  for (int step = 0; step < 3; ++step) {
    const auto view
        = Dot::Math::multiply(camera(), Dot::Math::compose({float(step), 0, 0}).value()).value();
    const auto frustum = Frustum::create(view).value();
    std::size_t count{};

    for (const auto& draw : draws) {
      count += frustum.intersects(local, draw.transform).value();
    }

    REQUIRE(renderer.renderMeshes(draws, view, 33, 33));
    CHECK(renderer.visibilityStats()->mesh->visibleInstances == count);
    CHECK(renderer.visibilityStats()->mesh->spatial.unindexedTests == 0);
  }
}

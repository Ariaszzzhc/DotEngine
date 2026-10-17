#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <iterator>
#include <limits>
#include <span>
#include <thread>
#include <utility>
#include <vector>

import Dot.Graphics.Renderer;
import Dot.Math;

namespace {
  using namespace Dot::Graphics;

  struct Fixture {
    Renderer renderer{Renderer::create().value()};
    Mesh mesh{renderer.upload(cube().value()).value()};
    RenderTarget target{renderer.createTarget(96, 64).value()};
    std::array<MeshDraw, 1> draws{MeshDraw{&mesh}};
    Dot::Math::Mat4 camera
        = Dot::Math::multiply(Dot::Math::orthographic(-2, 2, -2, 2, .1F, 12).value(),
                              Dot::Math::lookAt({0, 0, 5}, {}).value())
              .value();

    auto render() -> void {
      const auto result = renderer.renderMeshesInto(target, draws, camera);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    }

    auto drain() -> MeshProfiling {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
      auto result = renderer.profiling().value();

      while (result.pending && !result.error && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
        result = renderer.profiling().value();
      }

      REQUIRE_FALSE(result.error);
      REQUIRE(result.pending == 0);
      return result;
    }
  };
}  // namespace

TEST_CASE("profiling.gpu_opt_in_preserves_pixels_and_matches_real_passes_and_cached_shadows") {
  Fixture f;
  Lighting lights;
  lights.viewPosition = {0, 0, 5};
  lights.lights = {{.direction = {1, -1, -1},
                    .shadows = ShadowSettings{.resolution = 128, .cascades = 1, .distance = 12}}};
  REQUIRE(f.renderer.setLighting(lights));
  REQUIRE(f.renderer.setOcclusion(OcclusionSettings{}));
  f.render();
  const auto baseline = f.renderer.readTarget(f.target).value();
  CHECK_FALSE(f.renderer.profiling()->enabled);
  CHECK(f.renderer.profiling()->cpu.empty());
  REQUIRE(f.renderer.setProfiling(true));
  f.draws[0].transform = Dot::Math::compose({.25F, 0, 0}).value();
  f.render();
  const auto firstPixels = f.renderer.readTarget(f.target).value();
  bool opaque = true;

  for (std::size_t i = 3; i < firstPixels.pixels.size(); i += 4) {
    opaque &= firstPixels.pixels[i] == 255;
  }

  CHECK(opaque);
  const auto first = f.drain();
  REQUIRE(first.cpu.size() == 1);
  CHECK(first.cpu[0].serial == 1);
  CHECK(first.cpu[0].width == 96);
  CHECK(first.cpu[0].height == 64);
  CHECK(first.cpu[0].work == f.renderer.visibilityStats()->mesh.value());
  CHECK(first.cpu[0].prepareNanoseconds > 0);
  CHECK(first.cpu[0].encodeNanoseconds > 0);
  CHECK(first.cpu[0].submitNanoseconds > 0);

  if (first.gpuSupported) {
    REQUIRE(first.gpu.size() == 1);
    CHECK(first.gpuBufferBytes == 49'792);
    CHECK(first.gpu[0].serial == first.cpu[0].serial);
    CHECK(first.gpu[0].work == first.cpu[0].work);

    for (const auto& pass : first.gpu[0].passNanoseconds) {
      REQUIRE(pass.has_value());
    }

    for (const auto pass : {0U, 2U, 3U}) {
      CHECK(*first.gpu[0].passNanoseconds[pass] <= first.gpu[0].totalNanoseconds);
    }
  } else {
    CHECK(first.gpu.empty());
    CHECK(first.gpuBufferBytes == 0);
  }

  f.render();
  const auto cachedPixels = f.renderer.readTarget(f.target).value();
  const auto firstMismatch = std::mismatch(cachedPixels.pixels.begin(), cachedPixels.pixels.end(),
                                           firstPixels.pixels.begin());
  INFO("First different byte: ", std::distance(cachedPixels.pixels.begin(), firstMismatch.first));
  CHECK((cachedPixels.pixels == firstPixels.pixels));
  const auto cached = f.drain();
  REQUIRE(cached.cpu.size() == 2);
  CHECK(cached.cpu.back().work.shadowDraws == 0);

  if (cached.gpuSupported) {
    REQUIRE(cached.gpu.size() == 2);
    CHECK_FALSE(cached.gpu.back().passNanoseconds[0]);
    CHECK(cached.gpu.back().passNanoseconds[1].has_value());
  }

  REQUIRE(f.renderer.setProfiling(false));
  f.draws[0].transform = {};
  f.render();
  CHECK((f.renderer.readTarget(f.target)->pixels == baseline.pixels));
  CHECK_FALSE(f.renderer.profiling()->enabled);
  CHECK(f.renderer.profiling()->gpuBufferBytes == 0);
}

TEST_CASE("profiling.gpu_history_is_bounded_serialized_and_rejected_frames_do_not_publish") {
  Fixture f;
  REQUIRE(f.renderer.setProfiling(true));

  for (int i = 0; i < 130; ++i) {
    f.render();
    CHECK(f.renderer.profiling()->pending <= 4);
  }

  auto profile = f.drain();
  REQUIRE(profile.cpu.size() == 120);
  CHECK(profile.cpu.front().serial == 11);
  CHECK(profile.cpu.back().serial == 130);
  CHECK(profile.gpu.size() <= 120);
  CHECK(std::ranges::is_sorted(profile.gpu, {}, &MeshGpuTiming::serial));
  const auto serial = profile.cpu.back().serial;
  f.draws[0].tint.x = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(f.renderer.renderMeshesInto(f.target, f.draws, f.camera));
  CHECK(f.renderer.profiling()->cpu.back().serial == serial);
  REQUIRE(f.renderer.readTarget(f.target));
  REQUIRE(f.renderer.pickMeshes({}, f.camera, 96, 64, {.5F, .5F}));
  CHECK(f.renderer.profiling()->cpu.back().serial == serial);
  REQUIRE(f.renderer.setProfiling(true));
  CHECK(f.renderer.profiling()->cpu.size() == 120);
  CHECK(f.renderer.profiling()->session == profile.session);
  REQUIRE(f.renderer.setProfiling(false));
  REQUIRE(f.renderer.setProfiling(true));
  CHECK(f.renderer.profiling()->session > profile.session);
  CHECK(f.renderer.profiling()->cpu.empty());
  CHECK(f.renderer.profiling()->gpu.empty());
}

TEST_CASE("profiling.gpu_pending_shutdown_thread_guards_and_moved_ownership_are_safe") {
  Fixture f;
  const auto resources = f.renderer.resourceStats().value();

  for (int i = 0; i < 3; ++i) {
    REQUIRE(f.renderer.setProfiling(true));
    f.render();
    REQUIRE(f.renderer.setProfiling(false));
    CHECK(f.renderer.profiling()->gpuBufferBytes == 0);
  }

  CHECK(f.renderer.resourceStats()->meshes == resources.meshes);
  CHECK(f.renderer.resourceStats()->textures == resources.textures);
  REQUIRE(f.renderer.setProfiling(true));
  f.render();
  bool readRejected{};
  bool writeRejected{};
  std::thread worker([&] {
    readRejected = !f.renderer.profiling();
    writeRejected = !f.renderer.setProfiling(false);
  });
  worker.join();
  CHECK(readRejected);
  CHECK(writeRejected);
  auto moved = std::move(f.renderer);
  CHECK_FALSE(f.renderer.profiling());
  CHECK_FALSE(f.renderer.setProfiling(true));
  CHECK(moved.profiling()->enabled);
  CHECK(moved.profiling()->cpu.size() == 1);
}

TEST_CASE("profiling.gpu_independent_devices_keep_pixel_readbacks_and_sample_ownership") {
  Fixture red;
  Fixture blue;
  red.draws[0].tint = {1, 0, 0, 1};
  blue.draws[0].tint = {0, 0, 1, 1};
  red.render();
  blue.render();
  const auto expectedRed = red.renderer.readTarget(red.target).value();
  const auto expectedBlue = blue.renderer.readTarget(blue.target).value();
  REQUIRE((expectedRed.pixels != expectedBlue.pixels));
  REQUIRE(red.renderer.setProfiling(true));
  REQUIRE(blue.renderer.setProfiling(true));

  for (int frame = 0; frame < 20; ++frame) {
    red.render();
    blue.render();
    const auto r = red.renderer.readTarget(red.target).value();
    const auto b = blue.renderer.readTarget(blue.target).value();
    bool redPresent{};
    bool bluePresent{};
    bool opaque = true;

    for (std::size_t i = 0; i < r.pixels.size(); i += 4) {
      redPresent |= r.pixels[i] != 0;
      bluePresent |= b.pixels[i + 2] != 0;
      opaque &= r.pixels[i + 3] == 255 && b.pixels[i + 3] == 255;
    }

    CHECK(redPresent);
    CHECK(bluePresent);
    CHECK(opaque);
    CHECK((r.pixels == expectedRed.pixels));
    CHECK((b.pixels == expectedBlue.pixels));
  }

  const auto r = red.drain();
  const auto b = blue.drain();
  CHECK(r.cpu.size() == 20);
  CHECK(b.cpu.size() == 20);
  CHECK(r.cpu.back().serial == 20);
  CHECK(b.cpu.back().serial == 20);
}

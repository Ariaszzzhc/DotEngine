#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <thread>
#include <utility>
#include <vector>

import Dot.Graphics.Renderer;
import Dot.Math;
import Dot.Core.Result;
import Dot.Platform.Window;

using namespace Dot::Graphics;

namespace {
  auto triangle(float depth = 0.5F, Dot::Math::Vec4 color = {1, 0, 0, 1}) -> std::array<Vertex, 3> {
    return {
        {{{-0.8F, -0.8F, depth}, color}, {{0.8F, -0.8F, depth}, color}, {{0, 0.8F, depth}, color}}};
  }

  auto pixel(const Image& image, std::uint32_t x, std::uint32_t y) -> std::array<std::uint8_t, 4> {
    const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {image.pixels.at(offset), image.pixels.at(offset + 1), image.pixels.at(offset + 2),
            image.pixels.at(offset + 3)};
  }
}  // namespace

TEST_CASE("graphics.native_surface_resize_and_repeated_lifetime") {
  for (int iteration = 0; iteration < 4; ++iteration) {
    Dot::Platform::WindowConfig config;
    config.title = "DotEngine Surface Lifetime";
    config.width = 160;
    config.height = 120;
    config.gpuSurface = true;
    auto window = Dot::Platform::Window::create(config);
    REQUIRE_MESSAGE(window, (window ? "" : window.error().message));
    auto renderer = Renderer::create(&*window);
    REQUIRE_MESSAGE(renderer, (renderer ? "" : renderer.error().message));
    int presented = 0;

    for (int frame = 0; frame < 6; ++frame) {
      if (frame == 3) {
        REQUIRE(window->resize(240, 180));
      }

      auto result = renderer->present(triangle(), {});
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));

      if (*result == PresentResult::Presented) {
        ++presented;
      }
    }

    CHECK(presented >= 4);
  }
}

TEST_CASE("graphics.reject_invalid_surface_owner") {
  auto window = Dot::Platform::Window::create();
  REQUIRE(window);
  auto moved = std::move(*window);
  auto renderer = Renderer::create(&*window);
  REQUIRE_FALSE(renderer);
  CHECK(renderer.error().code == Dot::Core::ErrorCode::InvalidState);
  CHECK(moved.size());
}

TEST_CASE("graphics.native_indexed_cube_with_perspective_camera") {
  Dot::Platform::WindowConfig config;
  config.title = "DotEngine Indexed Cube";
  config.width = 320;
  config.height = 240;
  config.gpuSurface = true;
  auto window = Dot::Platform::Window::create(config);
  REQUIRE(window);
  auto renderer = Renderer::create(&*window);
  auto geometry = cube();
  REQUIRE(renderer);
  REQUIRE(geometry);
  auto mesh = renderer->upload(*geometry);
  REQUIRE(mesh);
  auto view = Dot::Math::lookAt({2, 2, 4}, {});
  auto projection = Dot::Math::perspective(60, 4.0F / 3.0F, 0.1F, 100);
  REQUIRE(view);
  REQUIRE(projection);
  auto camera = Dot::Math::multiply(*projection, *view);
  REQUIRE(camera);
  const std::array draws{MeshDraw{&*mesh, {}, {0.2F, 0.7F, 1, 1}}};
  auto image = renderer->renderMeshes(draws, *camera, 64, 64);
  REQUIRE(image);
  auto center = pixel(*image, 32, 32);
  CHECK(center[1] > 100);
  CHECK(center[2] > 200);
  int presented = 0;

  for (int frame = 0; frame < 6; ++frame) {
    auto result = renderer->presentMeshes(draws, *camera);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));

    if (*result == PresentResult::Presented) {
      ++presented;
    }
  }

  CHECK(presented >= 4);
}

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

TEST_CASE("graphics.real_adapter_and_triangle_readback") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE_MESSAGE(renderer, (renderer ? "" : renderer.error().message));
  auto info = renderer->adapterInfo();
  REQUIRE(info);
  CHECK_FALSE(info->name.empty());
  CHECK(info->maxTextureDimension >= 256);
#if defined(__APPLE__)
  CHECK(info->backend == Backend::Metal);
#elif defined(_WIN32)
  CHECK(info->backend == Backend::D3D12);
#else
  CHECK(info->backend == Backend::Vulkan);
#endif
  auto image = renderer->render(triangle(), {}, 64, 64);
  REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
  CHECK(image->pixels.size() == 64 * 64 * 4);
  CHECK(pixel(*image, 32, 32) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
  CHECK(pixel(*image, 0, 0) == std::array<std::uint8_t, 4>{0, 0, 0, 255});
}

TEST_CASE("graphics.readback_padding_and_single_pixel") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE(renderer);

  for (const auto dimensions : {std::array{13U, 7U}, std::array{1U, 1U}, std::array{65U, 3U}}) {
    auto image = renderer->render({}, {}, dimensions[0], dimensions[1], {0.25F, 0.5F, 0.75F, 1});
    REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
    CHECK(image->pixels.size() == dimensions[0] * dimensions[1] * 4);

    for (std::uint32_t y = 0; y < dimensions[1]; ++y) {
      for (std::uint32_t x = 0; x < dimensions[0]; ++x) {
        auto value = pixel(*image, x, y);
        CHECK(value[0] == 64);
        CHECK(value[1] == 128);
        CHECK(value[2] == 191);
        CHECK(value[3] == 255);
      }
    }
  }
}

TEST_CASE("graphics.depth_occlusion_and_clip_range") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE(renderer);
  std::array<Vertex, 6> geometry;
  auto nearTriangle = triangle(0.2F, {0, 1, 0, 1});
  auto farTriangle = triangle(0.8F, {1, 0, 0, 1});
  std::copy(nearTriangle.begin(), nearTriangle.end(), geometry.begin());
  std::copy(farTriangle.begin(), farTriangle.end(), geometry.begin() + 3);
  auto image = renderer->render(geometry, {}, 32, 32);
  REQUIRE(image);
  CHECK(pixel(*image, 16, 16) == std::array<std::uint8_t, 4>{0, 255, 0, 255});

  for (float depth : {-0.1F, 1.1F}) {
    image = renderer->render(triangle(depth), {}, 32, 32);
    REQUIRE(image);
    CHECK(pixel(*image, 16, 16) == std::array<std::uint8_t, 4>{0, 0, 0, 255});
  }
}

TEST_CASE("graphics.transform_uniform_changes_rasterized_position") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE(renderer);
  auto transform = Dot::Math::compose({0.6F, 0, 0}, {}, {0.2F, 0.2F, 1});
  REQUIRE(transform);
  auto image = renderer->render(triangle(), *transform, 100, 100);
  REQUIRE(image);
  CHECK(pixel(*image, 50, 50) == std::array<std::uint8_t, 4>{0, 0, 0, 255});
  CHECK(pixel(*image, 80, 50) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
  image = renderer->render(triangle(), {}, 100, 100);
  REQUIRE(image);
  CHECK(pixel(*image, 50, 50) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
}

TEST_CASE("graphics.reject_invalid_inputs_without_poisoning_device") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE(renderer);
  CHECK_FALSE(renderer->render({}, {}, 0, 32));
  CHECK_FALSE(renderer->render({}, {}, 32, 0));
  CHECK_FALSE(renderer->render({}, {}, 4097, 1));
  CHECK_FALSE(renderer->render({}, {}, 1, std::numeric_limits<std::uint32_t>::max()));
  auto geometry = triangle();
  CHECK_FALSE(renderer->render(std::span{geometry}.first(2), {}, 32, 32));
  geometry[0].position.x = std::numeric_limits<float>::infinity();
  CHECK_FALSE(renderer->render(geometry, {}, 32, 32));
  geometry = triangle();
  geometry[1].color.y = -1;
  CHECK_FALSE(renderer->render(geometry, {}, 32, 32));
  geometry = triangle();
  geometry[0].normal = {0, 0, 2};
  CHECK_FALSE(renderer->render(geometry, {}, 32, 32));
  geometry = triangle();
  geometry[0].normal = {0, 0, std::numeric_limits<float>::denorm_min()};
  CHECK_FALSE(renderer->render(geometry, {}, 32, 32));
  geometry = triangle();
  geometry[0].texCoord.y = std::numeric_limits<float>::infinity();
  CHECK_FALSE(renderer->render(geometry, {}, 32, 32));
  auto mesh = quad().value();
  mesh.vertices[0].normal = {0, 0, 2};
  CHECK_FALSE(renderer->upload(mesh));
  CHECK(renderer->resourceStats()->meshes == 0);
  Dot::Math::Mat4 invalid;
  invalid.data[4] = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(renderer->render({}, invalid, 32, 32));
  CHECK_FALSE(renderer->render({}, {}, 32, 32, {0, 0, 65505, 1}));
  CHECK_FALSE(renderer->present({}, {}));
  auto image = renderer->render(triangle(), {}, 32, 32);
  REQUIRE(image);
  CHECK(pixel(*image, 16, 16) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
}

TEST_CASE("graphics.move_preserves_device_and_rejects_empty_owner") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  auto other
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE(renderer);
  REQUIRE(other);
  auto moved = std::move(*renderer);
  CHECK_FALSE(renderer->adapterInfo());
  CHECK_FALSE(renderer->render({}, {}, 8, 8));
  CHECK_FALSE(renderer->present({}, {}));
  *other = std::move(moved);
  CHECK_FALSE(moved.adapterInfo());
  CHECK(other->render(triangle(), {}, 8, 8));
}

TEST_CASE("graphics.reject_foreign_thread_operations") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE(renderer);
  bool rejected = false;
  std::thread worker([&] {
    rejected
        = !renderer->adapterInfo() && !renderer->render({}, {}, 8, 8) && !renderer->present({}, {});
  });
  worker.join();
  CHECK(rejected);
  CHECK(renderer->render({}, {}, 8, 8));
}

TEST_CASE("graphics.repeated_device_and_resource_lifetime") {
  for (int iteration = 0; iteration < 12; ++iteration) {
    auto renderer
        = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
    REQUIRE_MESSAGE(renderer, (renderer ? "" : renderer.error().message));

    for (int frame = 0; frame < 5; ++frame) {
      auto image = renderer->render(triangle(), {}, 13, 17);
      REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
      CHECK(pixel(*image, 6, 8) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
    }
  }
}

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

TEST_CASE("graphics.indexed_upload_owns_immutable_geometry") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  auto geometry = quad({1.6F, 1.6F});
  REQUIRE(renderer);
  REQUIRE(geometry);
  auto mesh = renderer->upload(*geometry);
  REQUIRE_MESSAGE(mesh, (mesh ? "" : mesh.error().message));
  auto info = mesh->info();
  REQUIRE(info);
  CHECK(info->vertices == 4);
  CHECK(info->indices == 6);
  geometry->vertices.clear();
  geometry->indices.clear();
  auto model = Dot::Math::compose({0, 0, 0.5F});
  REQUIRE(model);
  const std::array draws{MeshDraw{&*mesh, *model, {0, 1, 0, 1}}};
  auto image = renderer->renderMeshes(draws, {}, 32, 32);
  REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
  CHECK(pixel(*image, 16, 16) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
  CHECK(pixel(*image, 0, 0) == std::array<std::uint8_t, 4>{0, 0, 0, 255});
}

TEST_CASE("graphics.shared_mesh_draws_use_independent_uniforms") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  auto geometry = quad();
  REQUIRE(renderer);
  REQUIRE(geometry);
  auto mesh = renderer->upload(*geometry);
  REQUIRE(mesh);
  auto left = Dot::Math::compose({-0.5F, 0, 0.5F}, {}, {0.3F, 0.3F, 1});
  auto right = Dot::Math::compose({0.5F, 0, 0.5F}, {}, {0.3F, 0.3F, 1});
  REQUIRE(left);
  REQUIRE(right);
  const std::array draws{MeshDraw{&*mesh, *left, {1, 0, 0, 1}},
                         MeshDraw{&*mesh, *right, {0, 1, 0, 1}}};
  auto image = renderer->renderMeshes(draws, {}, 100, 100);
  REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
  CHECK(pixel(*image, 25, 50) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
  CHECK(pixel(*image, 75, 50) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
  CHECK(pixel(*image, 50, 50) == std::array<std::uint8_t, 4>{0, 0, 0, 255});
}

TEST_CASE("graphics.nonindexed_upload_and_camera_projection") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE(renderer);
  MeshData geometry;
  auto vertices = triangle();
  geometry.vertices.assign(vertices.begin(), vertices.end());
  auto mesh = renderer->upload(geometry);
  REQUIRE(mesh);
  auto info = mesh->info();
  REQUIRE(info);
  CHECK(info->indices == 0);
  const std::array draws{MeshDraw{&*mesh}};
  auto image = renderer->renderMeshes(draws, {}, 32, 32);
  REQUIRE(image);
  CHECK(pixel(*image, 16, 16) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
  auto camera = Dot::Math::compose({0.5F, 0, 0}, {}, {0.2F, 0.2F, 1});
  REQUIRE(camera);
  image = renderer->renderMeshes(draws, *camera, 100, 100);
  REQUIRE(image);
  CHECK(pixel(*image, 75, 50) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
  CHECK(pixel(*image, 50, 50) == std::array<std::uint8_t, 4>{0, 0, 0, 255});
}

TEST_CASE("graphics.mesh_copies_moves_and_release_accounting") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  auto geometry = cube();
  REQUIRE(renderer);
  REQUIRE(geometry);
  CHECK_FALSE(renderer->upload({}));
  auto stats = renderer->resourceStats();
  REQUIRE(stats);
  CHECK(stats->meshes == 0);

  {
    auto mesh = renderer->upload(*geometry);
    REQUIRE(mesh);
    auto copy = *mesh;
    auto moved = std::move(*mesh);
    CHECK_FALSE(mesh->info());
    CHECK(copy.info());
    CHECK(moved.info());
    stats = renderer->resourceStats();
    REQUIRE(stats);
    CHECK(stats->meshes == 1);
    CHECK(stats->vertexBytes == geometry->vertices.size() * sizeof(Vertex));
    CHECK(stats->indexBytes == geometry->indices.size() * sizeof(std::uint32_t));
    copy = Mesh{};
    stats = renderer->resourceStats();
    REQUIRE(stats);
    CHECK(stats->meshes == 1);
    Mesh workerCopy = moved;
    std::thread worker([owned = std::move(workerCopy)]() mutable { owned = Mesh{}; });
    worker.join();
    CHECK(moved.info());
  }

  stats = renderer->resourceStats();
  REQUIRE(stats);
  CHECK(stats->meshes == 0);
  CHECK(stats->vertexBytes == 0);
  CHECK(stats->indexBytes == 0);
}

TEST_CASE("graphics.mesh_outlives_renderer_and_rejects_foreign_device") {
  Mesh mesh;
  auto geometry = cube();
  REQUIRE(geometry);

  {
    auto renderer
        = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
    REQUIRE(renderer);
    auto uploaded = renderer->upload(*geometry);
    REQUIRE(uploaded);
    mesh = *uploaded;
    auto foreign
        = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
    REQUIRE(foreign);
    const std::array draws{MeshDraw{&mesh}};
    CHECK_FALSE(foreign->renderMeshes(draws, {}, 8, 8));
    CHECK(mesh.info());
    auto moved = std::move(*renderer);
    CHECK(mesh.info());
    CHECK(moved.renderMeshes(draws, {}, 8, 8));
    CHECK_FALSE(renderer->upload(*geometry));
    CHECK_FALSE(renderer->resourceStats());
    CHECK_FALSE(renderer->renderMeshes({}, {}, 8, 8));
    CHECK_FALSE(renderer->presentMeshes({}, {}));
  }

  CHECK_FALSE(mesh.info());
  mesh = Mesh{};
  CHECK_FALSE(mesh.info());
}

TEST_CASE("graphics.invalid_mesh_draws_are_transactional") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  auto geometry = quad();
  REQUIRE(renderer);
  REQUIRE(geometry);
  auto mesh = renderer->upload(*geometry);
  REQUIRE(mesh);
  std::array draws{MeshDraw{&*mesh}};
  draws[0].mesh = nullptr;
  CHECK_FALSE(renderer->renderMeshes(draws, {}, 8, 8));
  Mesh empty;
  draws[0].mesh = &empty;
  CHECK_FALSE(renderer->renderMeshes(draws, {}, 8, 8));
  draws[0].mesh = &*mesh;
  draws[0].tint.y = -1;
  CHECK_FALSE(renderer->renderMeshes(draws, {}, 8, 8));
  draws[0].tint.y = 1;
  draws[0].transform.data[0] = std::numeric_limits<float>::infinity();
  CHECK_FALSE(renderer->renderMeshes(draws, {}, 8, 8));
  draws[0].transform = {};
  std::vector<MeshDraw> tooMany(4097, draws[0]);
  CHECK_FALSE(renderer->renderMeshes(tooMany, {}, 8, 8));
  CHECK_FALSE(renderer->renderMeshes(draws, {}, 0, 8));
  CHECK_FALSE(renderer->presentMeshes(draws, {}));
  auto image = renderer->renderMeshes({}, {}, 8, 8, {0, 0, 1, 1});
  REQUIRE(image);
  CHECK(pixel(*image, 0, 0) == std::array<std::uint8_t, 4>{0, 0, 255, 255});
  CHECK(renderer->renderMeshes(draws, {}, 8, 8));
}

TEST_CASE("graphics.mesh_api_rejects_worker_thread") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  auto geometry = quad();
  REQUIRE(renderer);
  REQUIRE(geometry);
  bool rejected = false;
  std::thread worker([&] {
    rejected = !renderer->upload(*geometry) && !renderer->resourceStats()
               && !renderer->renderMeshes({}, {}, 8, 8) && !renderer->presentMeshes({}, {});
  });
  worker.join();
  CHECK(rejected);
  CHECK(renderer->upload(*geometry));
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

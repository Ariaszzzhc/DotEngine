#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Graphics.SceneRenderer;
import Dot.Graphics.Renderer;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Platform.Window;
import Dot.Math;
import Dot.Physics.SceneSimulation;
import Dot.Physics.Body;

namespace {
  using namespace Dot::Graphics;
  using namespace Dot::World;

  auto makeScene() -> Scene {
    auto scene = Scene::create();
    REQUIRE(scene);
    auto camera = scene->createEntity("Camera");
    REQUIRE(camera);
    Camera component;
    component.primary = true;
    component.projection = Projection::Orthographic;
    component.orthographicHeight = 2;
    REQUIRE(scene->setCamera(*camera, component));
    REQUIRE(scene->setTransform(*camera, {{0, 0, 5}}));
    return std::move(*scene);
  }

  auto addQuad(Scene& scene, float x, Dot::Math::Vec4 tint) -> EntityId {
    auto entity = scene.createEntity("Quad");
    REQUIRE(entity);
    MeshRenderer mesh;
    mesh.kind = MeshKind::Quad;
    mesh.tint = tint;
    REQUIRE(scene.setMeshRenderer(*entity, mesh));
    REQUIRE(scene.setTransform(*entity, {{x, 0, 0}, {}, {0.3F, 0.3F, 1}}));
    return *entity;
  }

  auto pixel(const Image& image, std::uint32_t x, std::uint32_t y) -> std::array<std::uint8_t, 4> {
    const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {image.pixels.at(offset), image.pixels.at(offset + 1), image.pixels.at(offset + 2),
            image.pixels.at(offset + 3)};
  }

  struct Directory {
    std::filesystem::path root;

    Directory() {
      std::random_device random;

      for (int attempt = 0; attempt < 100; ++attempt) {
        root = std::filesystem::temp_directory_path()
               / ("dot-scene-render-" + std::to_string(random()) + "-" + std::to_string(random()));
        std::error_code error;

        if (std::filesystem::create_directory(root, error)) {
          return;
        }

        if (error) {
          throw std::runtime_error(error.message());
        }
      }

      throw std::runtime_error("Cannot create scene-render test directory");
    }

    ~Directory() {
      std::error_code ignored;
      std::filesystem::remove_all(root, ignored);
    }
  };
}  // namespace

TEST_CASE("scene_render.instance_transforms_tints_and_shared_cache") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE(renderer);
  auto service = SceneRenderer::create(*renderer);
  REQUIRE(service);
  auto scene = makeScene();
  const auto left = addQuad(scene, -0.5F, {1, 0, 0, 1});
  const auto right = addQuad(scene, 0.5F, {0, 1, 0, 1});
  auto image = service->render(scene, 100, 100);
  REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
  CHECK(pixel(*image, 25, 50) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
  CHECK(pixel(*image, 75, 50) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
  CHECK(service->stats()->meshes == 1);
  CHECK(renderer->resourceStats()->meshes == 1);
  REQUIRE(scene.destroyEntity(left));
  REQUIRE(scene.destroyEntity(right));
  REQUIRE(service->render(scene, 10, 10));
  CHECK(service->stats()->meshes == 0);
  CHECK(service->stats()->bytes == 0);
  CHECK(renderer->resourceStats()->meshes == 0);
}

TEST_CASE("scene_render.physics_writeback_changes_actual_metal_pixels") {
  auto scene = makeScene();
  auto camera = scene.primaryCamera();
  REQUIRE(camera);
  auto cameraDescription = scene.entity(*camera)->camera;
  cameraDescription->orthographicHeight = 3;
  REQUIRE(scene.setCamera(*camera, cameraDescription));
  REQUIRE(scene.setTransform(*camera, {{0, 0.75F, 5}}));
  auto floor = scene.createEntity("Floor");
  auto body = scene.createEntity("Falling Cube");
  REQUIRE(floor);
  REQUIRE(body);
  REQUIRE(scene.setTransform(*floor, {{0, -0.25F, 0}, {}, {6, 0.5F, 6}}));
  RigidBody floorDescription;
  floorDescription.motion = Dot::Physics::Motion::Static;
  floorDescription.shape = Dot::Physics::Shape::Box;
  REQUIRE(scene.setRigidBody(*floor, floorDescription));
  auto description = floorDescription;
  description.motion = Dot::Physics::Motion::Dynamic;
  REQUIRE(scene.setRigidBody(*body, description));
  REQUIRE(scene.setTransform(*body, {{0, 1.5F, 0}, {}, {0.5F, 0.5F, 0.5F}}));
  MeshRenderer mesh;
  mesh.tint = {0, 1, 0, 1};
  REQUIRE(scene.setMeshRenderer(*body, mesh));
  auto simulation = Dot::Physics::SceneSimulation::create(scene);
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE(simulation);
  REQUIRE(renderer);
  auto service = SceneRenderer::create(*renderer);
  REQUIRE(service);
  auto before = service->render(scene, 100, 100);
  REQUIRE(before);
  CHECK(pixel(*before, 50, 25) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
  CHECK(pixel(*before, 50, 67) == std::array<std::uint8_t, 4>{0, 0, 0, 255});

  for (int frame = 0; frame < 180; ++frame) {
    REQUIRE(simulation->step(scene, 1.0 / 60));
  }

  auto after = service->render(scene, 100, 100);
  REQUIRE(after);
  CHECK(pixel(*after, 50, 25) == std::array<std::uint8_t, 4>{0, 0, 0, 255});
  CHECK(pixel(*after, 50, 67) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
  REQUIRE(scene.destroyEntity(*body));
  REQUIRE(simulation->synchronize(scene));
  REQUIRE(service->render(scene, 10, 10));
  CHECK(simulation->stats()->bodies == 1);
  CHECK(renderer->resourceStats()->meshes == 0);
}

TEST_CASE("scene_render.visibility_and_camera_override") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE(renderer);
  auto service = SceneRenderer::create(*renderer);
  REQUIRE(service);
  auto scene = makeScene();
  auto entity = addQuad(scene, 0, {1, 0, 0, 1});
  auto description = scene.entity(entity);
  REQUIRE(description);
  description->mesh->visible = false;
  REQUIRE(scene.setMeshRenderer(entity, description->mesh));
  auto camera = scene.primaryCamera();
  REQUIRE(camera);
  auto component = scene.entity(*camera)->camera;
  component->primary = false;
  REQUIRE(scene.setCamera(*camera, component));
  CHECK_FALSE(service->render(scene, 10, 10));
  auto image = service->render(scene, 10, 10, {0, 0, 1, 1}, *camera);
  REQUIRE(image);
  CHECK(pixel(*image, 5, 5) == std::array<std::uint8_t, 4>{0, 0, 255, 255});
  CHECK(service->stats()->meshes == 0);
  auto foreign = makeScene();
  CHECK_FALSE(service->render(scene, 10, 10, {}, foreign.primaryCamera().value()));
}

TEST_CASE("scene_render.missing_assets_fail_without_substitutes") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE(renderer);
  auto service = SceneRenderer::create(*renderer);
  REQUIRE(service);
  auto scene = makeScene();
  auto entity = addQuad(scene, 0, {1, 0, 0, 1});
  MeshRenderer component;
  component.kind = MeshKind::Asset;
  component.asset = "missing.obj";
  REQUIRE(scene.setMeshRenderer(entity, component));
  CHECK_FALSE(service->render(scene, 10, 10));
  CHECK(service->stats()->meshes == 0);
  Directory directory;
  auto files = Dot::Core::FileSystem::open(directory.root);
  REQUIRE(files);
  auto rooted = SceneRenderer::create(*renderer, *files);
  REQUIRE(rooted);
  auto result = rooted->render(scene, 10, 10);
  REQUIRE_FALSE(result);
  CHECK(result.error().code == Dot::Core::ErrorCode::NotFound);
  CHECK(result.error().message.find("missing.obj") != std::string::npos);
  CHECK(renderer->resourceStats()->meshes == 0);
}

TEST_CASE("scene_render.file_import_warnings_and_explicit_cache_reload") {
  Directory directory;
  auto files = Dot::Core::FileSystem::open(directory.root);
  REQUIRE(files);
  REQUIRE(files->writeTextAtomic(
      "triangle.obj", "o triangle\nv -1 -1 0\nv 1 -1 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1\n"));
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE(renderer);
  auto service = SceneRenderer::create(*renderer, *files);
  REQUIRE(service);
  auto scene = makeScene();
  auto entity = addQuad(scene, 0, {1, 0, 0, 1});
  MeshRenderer component;
  component.kind = MeshKind::Asset;
  component.asset = "triangle.obj";
  component.tint = {0, 1, 0, 1};
  REQUIRE(scene.setMeshRenderer(entity, component));
  auto image = service->render(scene, 32, 32);
  REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
  CHECK(pixel(*image, 16, 16) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
  auto warnings = service->warnings();
  REQUIRE(warnings);
  REQUIRE(warnings->size() == 1);
  CHECK(warnings->front().find("triangle.obj") != std::string::npos);
  REQUIRE(files->writeTextAtomic("triangle.obj", "broken"));
  CHECK(service->render(scene, 32, 32));
  REQUIRE(service->clearCache());
  CHECK_FALSE(service->render(scene, 32, 32));
  CHECK(service->stats()->meshes == 0);
}

TEST_CASE("scene_render.cache_budget_and_invalid_inputs") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE(renderer);
  CHECK_FALSE(SceneRenderer::create(*renderer, std::nullopt, 0));
  CHECK_FALSE(SceneRenderer::create(*renderer, std::nullopt, 1024ULL * 1024 * 1024 + 1));
  auto service = SceneRenderer::create(*renderer, std::nullopt, 1);
  REQUIRE(service);
  auto scene = makeScene();
  const auto entity = addQuad(scene, 0, {1, 0, 0, 1});
  auto result = service->render(scene, 10, 10);
  REQUIRE_FALSE(result);
  CHECK(result.error().code == Dot::Core::ErrorCode::Unavailable);
  CHECK(service->stats()->meshes == 0);
  CHECK(renderer->resourceStats()->meshes == 0);
  CHECK_FALSE(service->render(scene, 0, 10));
  CHECK_FALSE(service->render(scene, 10, 4097));
  CHECK_FALSE(service->render(scene, 10, 10, {0, -1, 0, 1}));
  CHECK_FALSE(service->present(scene));
  REQUIRE(scene.destroyEntity(entity));
  CHECK(service->render(scene, 10, 10));
}

TEST_CASE("scene_render.moves_worker_rejection_and_release") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE(renderer);
  auto scene = makeScene();
  const auto entity = addQuad(scene, 0, {1, 0, 0, 1});
  static_cast<void>(entity);

  {
    auto service = SceneRenderer::create(*renderer);
    REQUIRE(service);
    auto moved = std::move(*service);
    CHECK_FALSE(service->render(scene, 10, 10));
    CHECK_FALSE(service->present(scene));
    CHECK_FALSE(service->stats());
    CHECK_FALSE(service->warnings());
    CHECK_FALSE(service->clearCache());
    REQUIRE(moved.render(scene, 10, 10));
    bool rejected = false;
    std::thread worker([&] {
      rejected = !moved.render(scene, 10, 10) && !moved.present(scene) && !moved.stats()
                 && !moved.warnings() && !moved.clearCache();
    });
    worker.join();
    CHECK(rejected);
    CHECK(renderer->resourceStats()->meshes == 1);
  }

  CHECK(renderer->resourceStats()->meshes == 0);
}

TEST_CASE("scene_render.native_scene_presentation_and_surface_size") {
  Dot::Platform::WindowConfig config;
  config.title = "DotEngine Scene";
  config.width = 200;
  config.height = 200;
  config.gpuSurface = true;
  auto window = Dot::Platform::Window::create(config);
  REQUIRE(window);
  auto renderer = Renderer::create(&*window);
  REQUIRE(renderer);
  auto service = SceneRenderer::create(*renderer);
  REQUIRE(service);
  auto scene = makeScene();
  const auto entity = addQuad(scene, 0, {0, 1, 0, 1});
  static_cast<void>(entity);
  auto size = renderer->surfaceSize();
  REQUIRE(size);
  CHECK(size->width > 0);
  CHECK(size->height > 0);
  int presented = 0;

  for (int frame = 0; frame < 6; ++frame) {
    if (frame == 3) {
      REQUIRE(window->resize(300, 200));
    }

    auto result = service->present(scene);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));

    if (*result == PresentResult::Presented) {
      ++presented;
    }
  }

  CHECK(presented >= 4);
}

TEST_CASE("scene_render.visible_draw_limit_and_clear_validation_precede_upload") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE(renderer);
  auto service = SceneRenderer::create(*renderer);
  REQUIRE(service);
  auto scene = makeScene();

  for (int index = 0; index < 4097; ++index) {
    auto entity = scene.createEntity();
    REQUIRE(entity);
    REQUIRE(scene.setMeshRenderer(*entity, MeshRenderer{}));
  }

  auto result = service->render(scene, 10, 10);
  REQUIRE_FALSE(result);
  CHECK(result.error().code == Dot::Core::ErrorCode::Unavailable);
  CHECK(service->stats()->meshes == 0);
  CHECK(renderer->resourceStats()->meshes == 0);
  auto minimal = makeScene();
  const auto entity = addQuad(minimal, 0, {1, 0, 0, 1});
  static_cast<void>(entity);
  CHECK_FALSE(service->render(minimal, 10, 10, {2, 0, 0, 1}));
  CHECK(service->stats()->meshes == 0);
  CHECK(renderer->resourceStats()->meshes == 0);
}

TEST_CASE("scene_render.deserialized_scene_preserves_actual_gpu_pixels") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
  REQUIRE(renderer);
  auto service = SceneRenderer::create(*renderer);
  REQUIRE(service);
  auto scene = makeScene();
  const auto entity = addQuad(scene, 0, {0, 1, 0, 1});
  static_cast<void>(entity);
  auto source = encodeScene(scene);
  REQUIRE(source);
  auto decoded = decodeScene(*source);
  REQUIRE(decoded);
  auto image = service->render(*decoded, 32, 32);
  REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
  CHECK(pixel(*image, 16, 16) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
}

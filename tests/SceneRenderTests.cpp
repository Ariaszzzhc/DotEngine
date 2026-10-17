#include <doctest/doctest.h>

#include <array>
#include <chrono>
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
import Dot.Assets.Mesh;
import Dot.Assets.Material;
import Dot.Graphics.Renderer;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneFiles;
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

TEST_CASE("scene_render.full_scene_instance_capacity_and_clear_validation") {
  auto renderer
      = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}})
            .value();
  auto service = SceneRenderer::create(renderer).value();
  auto scene = makeScene();

  for (int index = 0; index < 9999; ++index) {
    auto entity = scene.createEntity().value();
    REQUIRE(scene.setMeshRenderer(entity, MeshRenderer{}));
  }

  auto result = service.render(scene, 10, 10);
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(service.stats()->meshes == 1);
  CHECK(renderer.visibilityStats()->mesh->inputInstances == 9999);
  CHECK(renderer.visibilityStats()->mesh->visibleInstances == 9999);
  CHECK(renderer.visibilityStats()->mesh->colorDraws == 1);
  REQUIRE(service.clearCache());
  CHECK(renderer.resourceStats()->meshes == 0);
  auto minimal = makeScene();
  addQuad(minimal, 0, {1, 0, 0, 1});
  CHECK_FALSE(service.render(minimal, 10, 10, {2, 0, 0, 1}));
  CHECK(service.stats()->meshes == 0);
  CHECK(renderer.resourceStats()->meshes == 0);
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

TEST_CASE(
    "scene_render.independent_views_pick_exact_entities_and_hierarchical_bounds_without_camera_"
    "edits") {
  auto renderer = Renderer::create().value();
  auto drawing = SceneRenderer::create(renderer).value();
  auto scene = Dot::World::Scene::create("Editor view").value();
  auto parent = scene.createEntity("Parent").value();
  auto mesh = scene.createEntityWithId("Picked mesh", 9007199254740993ULL).value();
  REQUIRE(scene.setMeshRenderer(mesh, Dot::World::MeshRenderer{}));
  REQUIRE(scene.setParent(mesh, parent));
  REQUIRE(scene.setTransform(parent, {{2, 0, 0}}));
  REQUIRE(scene.setTransform(mesh, {{1, 0, 0}, {}, {-2, 1, 1}}));
  View view;
  view.position = {3, 0, 5};
  view.orthographic = true;
  view.orthographicHeight = 4;
  const auto before = Dot::World::encodeScene(scene).value();
  auto target = renderer.createTarget(101, 73).value();
  CHECK_FALSE(drawing.renderInto(scene, target));
  REQUIRE(drawing.renderInto(scene, target, {0, 0, 0, 1}, view));
  const auto bounds = drawing.bounds(scene, mesh).value();
  CHECK(bounds.minimum == Dot::Math::Vec3{2, -0.5F, -0.5F});
  CHECK(bounds.maximum == Dot::Math::Vec3{4, 0.5F, 0.5F});
  auto hit = drawing.pick(scene, 101, 73, {0.5F, 0.5F}, view);
  REQUIRE_MESSAGE(hit, (hit ? "" : hit.error().message));
  CHECK(*hit == mesh);
  CHECK_FALSE(drawing.pick(scene, 101, 73, {0.99F, 0.99F}, view).value());
  CHECK(Dot::World::encodeScene(scene).value() == before);
  REQUIRE(scene.setMeshRenderer(mesh, Dot::World::MeshRenderer{.asset = {}, .visible = false}));
  CHECK_FALSE(drawing.pick(scene, 101, 73, {0.5F, 0.5F}, view).value());
  CHECK_FALSE(drawing.pick(scene, 0, 73, {}, view));
  auto point = drawing.bounds(scene, parent).value();
  CHECK(point.minimum == Dot::Math::Vec3{2, 0, 0});
  CHECK(point.maximum == point.minimum);
}

TEST_CASE("scene_render.frustum_reduces_dense_scene_submissions_and_camera_changes_recompute") {
  auto renderer = Renderer::create().value();
  auto drawing = SceneRenderer::create(renderer).value();
  auto scene = makeScene();
  addQuad(scene, 0, {1, 0, 0, 1});

  for (int i = 0; i < 512; ++i) {
    addQuad(scene, 100 + static_cast<float>(i), {0, 1, 0, 1});
  }

  auto image = drawing.render(scene, 65, 65);
  REQUIRE(image);
  const auto report = renderer.visibilityStats().value();
  REQUIRE(report.mesh);
  CHECK(report.mesh->inputInstances == 513);
  CHECK(report.mesh->visibleInstances == 1);
  CHECK(report.mesh->culledInstances == 512);
  CHECK(drawing.stats()->meshes == 1);
  const auto pixel0 = pixel(*image, 32, 32);
  CHECK(pixel0[0] > 200);
  auto view = Dot::Graphics::View{};
  view.position = {100, 0, 5};
  view.orthographic = true;
  view.orthographicHeight = 0.8F;
  auto moved = drawing.render(scene, 65, 65, {0, 0, 0, 1}, view);
  REQUIRE(moved);
  CHECK(renderer.visibilityStats()->mesh->visibleInstances == 1);
  const auto pixel1 = pixel(*moved, 32, 32);
  CHECK(pixel1[1] > 200);
  CHECK(static_cast<int>(pixel1[1]) > static_cast<int>(pixel1[0]) + 100);
  CHECK(pixel1 != pixel0);
}

namespace {
  auto waitGeometry(SceneRenderer& service, const Scene& scene) -> void {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto state = service.prepare(scene);

    while (state && *state != ScenePreparationStage::Ready
           && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      state = service.prepare(scene);
    }

    REQUIRE_MESSAGE(state, (state ? "" : state.error().message));
    REQUIRE(*state == ScenePreparationStage::Ready);
  }

  auto setAsset(Scene& scene, EntityId entity, std::string path) -> void {
    auto mesh = scene.entity(entity)->mesh.value();
    mesh.kind = MeshKind::Asset;
    mesh.asset = std::move(path);
    REQUIRE(scene.setMeshRenderer(entity, mesh));
  }
}  // namespace

TEST_CASE(
    "scene_render.geometry_preparation_uploads_one_shared_resource_per_poll_and_matches_pixels") {
  Directory directory;
  auto files = Dot::Core::FileSystem::open(directory.root).value();
  auto mesh = quad().value();
  REQUIRE(Dot::Assets::saveMesh(files, "a.dotmesh", mesh));
  REQUIRE(Dot::Assets::saveMesh(files, "b.dotmesh", mesh));
  auto scene = makeScene();
  setAsset(scene, addQuad(scene, -.5F, {1, 0, 0, 1}), "a.dotmesh");
  setAsset(scene, addQuad(scene, .5F, {0, 1, 0, 1}), "a.dotmesh");
  setAsset(scene, addQuad(scene, 0, {0, 0, 1, 1}), "b.dotmesh");
  addQuad(scene, 0, {1, 1, 1, 1});
  auto reference = Renderer::create().value();
  auto direct = SceneRenderer::create(reference, files).value();
  const auto expected = direct.render(scene, 32, 32).value();
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, files).value();
  CHECK(service.readiness(scene).value() == ScenePreparationStage::Geometry);
  auto target = renderer.createTarget(32, 32).value();
  REQUIRE(renderer.renderMeshesInto(target, {}, {}, {1, 0, 1, 1}));
  const auto before = renderer.readTarget(target).value();
  auto state = service.prepare(scene);
  REQUIRE(state);
  CHECK(*state == ScenePreparationStage::Geometry);
  CHECK(service.stats()->meshes == 0);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

  while (state && *state != ScenePreparationStage::Ready
         && std::chrono::steady_clock::now() < deadline) {
    const auto count = service.stats()->meshes;
    state = service.prepare(scene);
    CHECK(service.stats()->meshes <= count + 1);
    CHECK(renderer.readTarget(target)->pixels == before.pixels);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  REQUIRE(state);
  REQUIRE(*state == ScenePreparationStage::Ready);
  CHECK(service.stats()->meshes == 3);
  REQUIRE(std::filesystem::remove(directory.root / "a.dotmesh"));
  REQUIRE(std::filesystem::remove(directory.root / "b.dotmesh"));
  REQUIRE(service.renderInto(scene, target));
  CHECK(renderer.readTarget(target)->pixels == expected.pixels);
}

TEST_CASE("scene_render.geometry_readiness_is_pure_and_owner_gui_move_boundaries_preserve_work") {
  auto scene = makeScene();
  addQuad(scene, 0, {1, 0, 0, 1});
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer).value();
  CHECK(service.readiness(scene).value() == ScenePreparationStage::Geometry);
  CHECK(service.readiness(scene).value() == ScenePreparationStage::Geometry);
  CHECK(renderer.resourceStats()->meshes == 0);
  REQUIRE(renderer.enableGui());
  REQUIRE(renderer.renderGui(
      [&]() -> Dot::Core::Result<void> {
        CHECK(service.readiness(scene).value() == ScenePreparationStage::Geometry);
        CHECK_FALSE(service.prepare(scene));
        return {};
      },
      8, 8));
  REQUIRE(renderer.closeGui());
  bool rejected{};
  std::thread worker([&] { rejected = !service.prepare(scene) && !service.readiness(scene); });
  worker.join();
  CHECK(rejected);
  CHECK(renderer.resourceStats()->meshes == 0);
  REQUIRE(service.prepare(scene).value() == ScenePreparationStage::Geometry);
  auto moved = std::move(service);
  CHECK_FALSE(service.prepare(scene));
  waitGeometry(moved, scene);
  CHECK(renderer.resourceStats()->meshes == 1);
  REQUIRE(moved.clearCache());
  CHECK(renderer.resourceStats()->meshes == 0);
}

TEST_CASE("scene_render.geometry_cancellation_discards_late_missing_hidden_and_cleared_work") {
  Directory directory;
  auto files = Dot::Core::FileSystem::open(directory.root).value();
  REQUIRE(Dot::Assets::saveMesh(files, "valid.dotmesh", quad().value()));
  auto scene = makeScene();
  const auto owner = addQuad(scene, 0, {0, 1, 0, 1});
  setAsset(scene, owner, "missing.dotmesh");
  auto renderer = Renderer::create().value();

  {
    auto service = SceneRenderer::create(renderer, files).value();
    REQUIRE(service.prepare(scene).value() == ScenePreparationStage::Geometry);
    auto hidden = scene.entity(owner)->mesh.value();
    hidden.visible = false;
    REQUIRE(scene.setMeshRenderer(owner, hidden));
    CHECK(service.prepare(scene).value() == ScenePreparationStage::Ready);
    CHECK(renderer.resourceStats()->meshes == 0);
    hidden.visible = true;
    hidden.asset = "valid.dotmesh";
    REQUIRE(scene.setMeshRenderer(owner, hidden));
    waitGeometry(service, scene);
    CHECK(service.stats()->meshes == 1);
    REQUIRE(service.clearCache());
    CHECK(renderer.resourceStats()->meshes == 0);
    REQUIRE(service.prepare(scene).value() == ScenePreparationStage::Geometry);
    REQUIRE(service.clearCache());
    CHECK(renderer.resourceStats()->meshes == 0);
    waitGeometry(service, scene);
    REQUIRE(service.clearCache());
    REQUIRE(service.prepare(scene).value() == ScenePreparationStage::Geometry);
  }

  CHECK(renderer.resourceStats()->meshes == 0);
}

TEST_CASE("scene_render.geometry_errors_require_explicit_reload_or_new_source_and_never_draw") {
  for (bool newSource : {false, true}) {
    Directory directory;
    auto files = Dot::Core::FileSystem::open(directory.root).value();
    auto scene = makeScene();
    setAsset(scene, addQuad(scene, 0, {0, 1, 0, 1}), "missing.dotmesh");
    auto renderer = Renderer::create().value();
    auto service = SceneRenderer::create(renderer, files).value();
    auto target = renderer.createTarget(16, 16).value();
    auto state = service.prepare(scene);
    REQUIRE(state);
    CHECK(*state == ScenePreparationStage::Geometry);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while (state && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      state = service.prepare(scene);
    }

    REQUIRE_FALSE(state);
    CHECK(state.error().message.find("missing.dotmesh") != std::string::npos);
    CHECK(target.info()->frames == 0);
    CHECK(renderer.resourceStats()->meshes == 0);
    REQUIRE(Dot::Assets::saveMesh(files, "missing.dotmesh", quad().value()));
    CHECK_FALSE(service.readiness(scene));
    CHECK_FALSE(service.prepare(scene));

    if (newSource) {
      scene = Dot::World::decodeScene(Dot::World::encodeScene(scene).value()).value();
    } else {
      REQUIRE(service.clearCache());
    }

    waitGeometry(service, scene);
    REQUIRE(service.renderInto(scene, target));
    CHECK(target.info()->frames == 1);
  }
}

TEST_CASE("scene_render.geometry_budget_retry_reuses_owned_data_after_unneeded_sources_retire") {
  Directory directory;
  auto files = Dot::Core::FileSystem::open(directory.root).value();
  const auto mesh = quad().value();
  REQUIRE(Dot::Assets::saveMesh(files, "a.dotmesh", mesh));
  REQUIRE(Dot::Assets::saveMesh(files, "b.dotmesh", mesh));
  auto scene = makeScene();
  const auto first = addQuad(scene, 0, {1, 0, 0, 1});
  setAsset(scene, first, "a.dotmesh");
  const auto bytes
      = mesh.vertices.size() * sizeof(Vertex) + mesh.indices.size() * sizeof(std::uint32_t);
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, files, bytes).value();
  waitGeometry(service, scene);
  auto target = renderer.createTarget(16, 16).value();
  REQUIRE(service.renderInto(scene, target));
  const auto before = renderer.readTarget(target).value();
  setAsset(scene, addQuad(scene, 0, {0, 0, 1, 1}), "b.dotmesh");
  auto state = service.prepare(scene);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

  while (state && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    state = service.prepare(scene);
  }

  REQUIRE_FALSE(state);
  CHECK(state.error().code == Dot::Core::ErrorCode::Unavailable);
  CHECK(renderer.readTarget(target)->pixels == before.pixels);
  REQUIRE(std::filesystem::remove(directory.root / "b.dotmesh"));
  auto hidden = scene.entity(first)->mesh.value();
  hidden.visible = false;
  REQUIRE(scene.setMeshRenderer(first, hidden));
  waitGeometry(service, scene);
  CHECK(service.stats()->meshes == 1);
  CHECK(service.stats()->bytes == bytes);
  REQUIRE(service.renderInto(scene, target));
  CHECK(renderer.readTarget(target)->pixels != before.pixels);
}

TEST_CASE("scene_render.geometry_progress_is_independent_of_continuing_material_drafts") {
  Directory directory;
  auto files = Dot::Core::FileSystem::open(directory.root).value();
  REQUIRE(Dot::Assets::saveMesh(files, "mesh.dotmesh", quad().value()));
  Dot::Assets::MaterialDescription description;
  REQUIRE(Dot::Assets::saveMaterial(files, "surface.dotmaterial", description));
  const auto original = files.readText("surface.dotmaterial").value();
  auto scene = makeScene();
  const auto owner = addQuad(scene, 0, {1, 1, 1, 1});
  setAsset(scene, owner, "mesh.dotmesh");
  REQUIRE(scene.setMaterial(owner, MaterialBinding{"surface.dotmaterial"}));
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, files).value();
  REQUIRE(service.prepare(scene).value() == ScenePreparationStage::Geometry);
  auto stage = ScenePreparationStage::Geometry;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  std::size_t changes{};

  while (stage != ScenePreparationStage::Ready && std::chrono::steady_clock::now() < deadline) {
    description.roughness = (changes++ % 2 == 0) ? .4F : .6F;
    REQUIRE(service.previewMaterial(MaterialPreview{"surface.dotmaterial", description}));
    auto next = service.prepare(scene);
    REQUIRE_MESSAGE(next, (next ? "" : next.error().message));
    stage = *next;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  CHECK(changes > 0);
  CHECK(stage == ScenePreparationStage::Ready);
  CHECK(service.stats()->meshes == 1);
  CHECK(files.readText("surface.dotmaterial").value() == original);
  REQUIRE(service.render(scene, 16, 16));
}

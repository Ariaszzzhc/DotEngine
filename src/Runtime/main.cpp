#include <array>
#include <chrono>
#include <exception>
#include <expected>
#include <filesystem>
#include <optional>
#include <print>
#include <string_view>
#include <vector>

import Dot.Core;
import Dot.Core.Application;
import Dot.Platform.Window;
import Dot.Math;
import Dot.Graphics.Renderer;
import Dot.Assets.Mesh;
import Dot.World.Scene;
import Dot.Graphics.SceneRenderer;
import Dot.World.SceneFiles;
import Dot.Physics.World;

namespace Dot::Runtime {
  auto testScenePhysics(bool render) -> int;
  auto testAudioClip(const std::filesystem::path& root) -> int;
  auto testAudioMix() -> int;
  auto testAudioOutput() -> int;
  auto testAudioPlayback() -> int;
  auto testAudioSpatial() -> int;
  auto testSceneAudio(const std::filesystem::path& root) -> int;
  auto testAudioBatch() -> int;
  auto testSceneAudioPlayback(const std::filesystem::path& root) -> int;
  auto testScript(const std::filesystem::path& root) -> int;
  auto testProcess(const std::filesystem::path& executable, const std::filesystem::path& root)
      -> int;
  auto testCompiler(const std::filesystem::path& executable, const std::filesystem::path& root)
      -> int;
  auto testScriptHost(const std::filesystem::path& executable, const std::filesystem::path& sdkRoot,
                      const std::filesystem::path& root, bool render) -> int;
  auto testScriptInstances(const std::filesystem::path& executable,
                           const std::filesystem::path& sdkRoot, const std::filesystem::path& root)
      -> int;
  auto testScriptComponents(const std::filesystem::path& executable,
                            const std::filesystem::path& sdkRoot, const std::filesystem::path& root,
                            bool render) -> int;
  auto testScriptAudio(const std::filesystem::path& executable,
                       const std::filesystem::path& sdkRoot, const std::filesystem::path& root,
                       bool native) -> int;
  auto testScriptPhysics(const std::filesystem::path& executable,
                         const std::filesystem::path& sdkRoot, const std::filesystem::path& root,
                         bool render) -> int;
  auto testScriptInput(const std::filesystem::path& executable,
                       const std::filesystem::path& sdkRoot, const std::filesystem::path& root)
      -> int;
  auto testScriptAuthoring(const std::filesystem::path& executable,
                           const std::filesystem::path& sdkRoot, const std::filesystem::path& root,
                           bool render) -> int;
  auto testScriptReload(const std::filesystem::path& executable,
                        const std::filesystem::path& sdkRoot, const std::filesystem::path& root,
                        bool render) -> int;
}  // namespace Dot::Runtime

namespace {
  auto testPhysics() -> int {
    auto world = Dot::Physics::World::create();

    if (!world) {
      std::println(stderr, "{}", world.error().message);
      return 1;
    }

    Dot::Physics::BodyConfig floor;
    floor.motion = Dot::Physics::Motion::Static;
    floor.shape = Dot::Physics::Shape::Box;
    floor.position = {0, -0.5F, 0};
    floor.halfExtents = {5, 0.5F, 5};
    Dot::Physics::BodyConfig falling;
    falling.position = {0, 3, 0};
    const std::array changes{Dot::Physics::BodyChange{std::nullopt, floor},
                             Dot::Physics::BodyChange{std::nullopt, falling}};
    auto created = world->applyChanges(changes);

    if (!created) {
      std::println(stderr, "Physics acceptance body creation failed");
      return 1;
    }

    const auto sphere = (*created)[1];

    for (int frame = 0; frame < 240; ++frame) {
      auto step = world->step(1.0 / 60);

      if (!step) {
        std::println(stderr, "{}", step.error().message);
        return 1;
      }
    }

    auto state = world->body(*sphere);
    auto ray = world->rayCast({0, 2, 0}, {0, -4, 0});

    if (!state || state->position.y < 0.475F || state->position.y > 0.525F || !ray
        || !ray->has_value() || (*ray)->body != *sphere || !(*ray)->normal) {
      std::println(stderr, "Physics acceptance collision/ray verification failed");
      return 1;
    }

    auto removed = world->destroyBody(*sphere);

    if (!removed || world->body(*sphere) || world->stats()->bodies != 1) {
      std::println(stderr, "Physics acceptance lifetime verification failed");
      return 1;
    }

    std::println("Physics acceptance passed: sphere rests at y={:.3f}; ray hit; stale ID rejected",
                 state->position.y);
    return 0;
  }

  auto testSceneFiles(const std::filesystem::path& root) -> int {
    auto files = Dot::Core::FileSystem::open(root);
    auto scene = Dot::World::Scene::create("Saved Scene");

    if (!files || !scene) {
      std::println(stderr, "Scene file acceptance setup failed");
      return 1;
    }

    auto directory = files->createDirectories("scenes");
    auto entity = scene->createEntityWithId("Stored", 50);

    if (!directory || !entity) {
      std::println(stderr, "Scene file acceptance entity/directory failed");
      return 1;
    }

    auto mesh = scene->setMeshRenderer(*entity, Dot::World::MeshRenderer{});

    if (!mesh) {
      std::println(stderr, "{}", mesh.error().message);
      return 1;
    }

    Dot::World::RigidBody body;
    body.shape = Dot::Physics::Shape::Box;
    body.mass = 2;
    auto physics = scene->setRigidBody(*entity, body);

    if (!physics) {
      std::println(stderr, "{}", physics.error().message);
      return 1;
    }

    auto saved = Dot::World::saveScene(*files, "scenes/main.dotscene", *scene);

    if (!saved) {
      std::println(stderr, "{}", saved.error().message);
      return 1;
    }

    auto loaded = Dot::World::loadScene(*files, "scenes/main.dotscene");

    if (!loaded) {
      std::println(stderr, "{}", loaded.error().message);
      return 1;
    }

    auto restored = loaded->findEntity(50);

    if (!restored || !loaded->entity(*restored)->mesh
        || loaded->entity(*restored)->rigidBody != body || loaded->id() == scene->id()) {
      std::println(stderr, "Scene file acceptance restoration failed");
      return 1;
    }

    std::println("Scene file acceptance passed: scenes/main.dotscene");
    return 0;
  }

  auto testSceneRender() -> int {
    auto scene = Dot::World::Scene::create("Rendered Scene");
    auto renderer = Dot::Graphics::Renderer::create();

    if (!scene) {
      std::println(stderr, "{}", scene.error().message);
      return 1;
    }

    if (!renderer) {
      std::println(stderr, "{}", renderer.error().message);
      return 1;
    }

    auto camera = scene->createEntity("Camera");
    auto cube = scene->createEntity("Cube");

    if (!camera || !cube) {
      std::println(stderr, "Scene rendering entity creation failed");
      return 1;
    }

    Dot::World::Camera cameraComponent;
    cameraComponent.primary = true;
    Dot::World::MeshRenderer mesh;
    mesh.tint = {0.2F, 0.7F, 1, 1};
    auto cameraPose = scene->setTransform(*camera, {{0, 0, 4}});
    auto cameraSet = scene->setCamera(*camera, cameraComponent);
    auto meshSet = scene->setMeshRenderer(*cube, mesh);

    if (!cameraPose || !cameraSet || !meshSet) {
      std::println(stderr, "Scene rendering component setup failed");
      return 1;
    }

    auto service = Dot::Graphics::SceneRenderer::create(*renderer);

    if (!service) {
      std::println(stderr, "{}", service.error().message);
      return 1;
    }

    auto image = service->render(*scene, 64, 64);

    if (!image) {
      std::println(stderr, "{}", image.error().message);
      return 1;
    }

    const auto center = (32 * 64 + 32) * 4;

    if (image->pixels[center + 1] <= 100 || image->pixels[center + 2] <= 200
        || service->stats()->meshes != 1) {
      std::println(stderr, "Scene rendering pixel/cache acceptance failed");
      return 1;
    }

    std::println("Scene rendering acceptance passed: camera, entity mesh, GPU pixels");
    return 0;
  }

  auto testScene() -> int {
    auto scene = Dot::World::Scene::create("Acceptance");

    if (!scene) {
      std::println(stderr, "{}", scene.error().message);
      return 1;
    }

    auto parent = scene->createEntityWithId("Parent", 10);
    auto child = scene->createEntity("Child");

    if (!parent || !child) {
      std::println(stderr, "Scene entity creation failed");
      return 1;
    }

    const std::array transforms{Dot::World::TransformUpdate{*parent, {{2, 3, 4}, {}, {2, 2, 2}}},
                                Dot::World::TransformUpdate{*child, {{1, 0, 0}}}};
    const std::array parents{Dot::World::ParentUpdate{*child, *parent}};
    auto transformed = scene->setTransforms(transforms);
    auto attached = scene->setParents(parents);

    if (!transformed || !attached || parent->value != 10 || child->value != 11) {
      std::println(stderr, "Scene hierarchy setup failed");
      return 1;
    }

    auto matrix = scene->worldTransform(*child);

    if (!matrix) {
      std::println(stderr, "{}", matrix.error().message);
      return 1;
    }

    auto position = Dot::Math::transformPoint(*matrix, {});

    if (!position || *position != Dot::Math::Vec3{4, 3, 4}) {
      std::println(stderr, "Scene world transform failed");
      return 1;
    }

    Dot::World::Camera camera;
    camera.primary = true;
    auto cameraSet = scene->setCamera(*child, camera);
    auto meshSet = scene->setMeshRenderer(*parent, Dot::World::MeshRenderer{});
    auto primary = scene->primaryCamera();
    auto viewProjection = scene->viewProjection(*child, 1);

    if (!cameraSet || !meshSet || !primary || *primary != *child || !viewProjection) {
      std::println(stderr, "Scene component acceptance failed");
      return 1;
    }

    auto destroyed = scene->destroyEntity(*parent);
    auto count = scene->entityCount();

    if (!destroyed || !count || *count != 0 || scene->entity(*child)) {
      std::println(stderr, "Scene subtree lifetime failed");
      return 1;
    }

    std::println("Scene acceptance passed: hierarchy, transforms, components, subtree lifetime");
    return 0;
  }

  auto testAssets(const std::filesystem::path& root) -> int {
    auto files = Dot::Core::FileSystem::open(root);

    if (!files) {
      std::println(stderr, "{}", files.error().message);
      return 1;
    }

    auto directory = files->createDirectories("assets");
    auto geometry = Dot::Graphics::cube();

    if (!directory) {
      std::println(stderr, "{}", directory.error().message);
      return 1;
    }

    if (!geometry) {
      std::println(stderr, "{}", geometry.error().message);
      return 1;
    }

    auto saved = Dot::Assets::saveMesh(*files, "assets/cube.dotmesh", *geometry);

    if (!saved) {
      std::println(stderr, "{}", saved.error().message);
      return 1;
    }

    auto loaded = Dot::Assets::loadMesh(*files, "assets/cube.dotmesh");

    if (!loaded) {
      std::println(stderr, "{}", loaded.error().message);
      return 1;
    }

    if (loaded->data.indices != geometry->indices || loaded->data.vertices.size() != 8) {
      std::println(stderr, "Asset acceptance round trip failed");
      return 1;
    }

    std::println("Asset acceptance passed: assets/cube.dotmesh");
    return 0;
  }

  auto testMesh() -> int {
    auto renderer = Dot::Graphics::Renderer::create();
    auto geometry = Dot::Graphics::cube();
    auto view = Dot::Math::lookAt({2, 2, 4}, {});
    auto projection = Dot::Math::perspective(60, 1, 0.1F, 100);

    if (!renderer) {
      std::println(stderr, "{}", renderer.error().message);
      return 1;
    }

    if (!geometry || !view || !projection) {
      std::println(stderr, "Mesh acceptance initialization failed");
      return 1;
    }

    auto mesh = renderer->upload(*geometry);
    auto camera = Dot::Math::multiply(*projection, *view);

    if (!mesh) {
      std::println(stderr, "{}", mesh.error().message);
      return 1;
    }

    if (!camera) {
      std::println(stderr, "Mesh acceptance upload/camera failed");
      return 1;
    }

    const std::array draws{Dot::Graphics::MeshDraw{&*mesh, {}, {0.2F, 0.7F, 1, 1}}};
    auto image = renderer->renderMeshes(draws, *camera, 64, 64);

    if (!image) {
      std::println(stderr, "{}", image.error().message);
      return 1;
    }

    const auto center = (32 * 64 + 32) * 4;

    if (image->pixels[center + 1] <= 100 || image->pixels[center + 2] <= 200) {
      std::println(stderr, "Mesh acceptance rasterization failed");
      return 1;
    }

    std::println("Mesh acceptance passed: indexed cube with perspective camera");
    return 0;
  }

  auto testGeometry() -> int {
    auto mesh = Dot::Graphics::cube({2, 4, 6});

    if (!mesh) {
      std::println(stderr, "{}", mesh.error().message);
      return 1;
    }

    auto bounds = Dot::Graphics::meshBounds(*mesh);

    if (!bounds || bounds->minimum != Dot::Math::Vec3{-1, -2, -3}
        || bounds->maximum != Dot::Math::Vec3{1, 2, 3}) {
      std::println(stderr, "Geometry acceptance failed");
      return 1;
    }

    std::println("Geometry acceptance passed: {} vertices, {} indices", mesh->vertices.size(),
                 mesh->indices.size());
    return 0;
  }

  auto testGraphics(bool native) -> int {
    std::array<Dot::Graphics::Vertex, 3> vertices{{
        {{-0.8F, -0.8F, 0.5F}, {1, 0, 0, 1}},
        {{0.8F, -0.8F, 0.5F}, {0, 1, 0, 1}},
        {{0, 0.8F, 0.5F}, {0, 0, 1, 1}},
    }};

    if (!native) {
      auto renderer = Dot::Graphics::Renderer::create();

      if (!renderer) {
        std::println(stderr, "{}", renderer.error().message);
        return 1;
      }

      auto image = renderer->render(vertices, {}, 64, 64);

      if (!image) {
        std::println(stderr, "{}", image.error().message);
        return 1;
      }

      const auto center = (32 * 64 + 32) * 4;

      if (image->pixels[center] == 0 || image->pixels[center + 1] == 0
          || image->pixels[center + 2] == 0) {
        std::println(stderr, "GPU triangle readback failed");
        return 1;
      }

      auto info = renderer->adapterInfo();

      if (!info) {
        std::println(stderr, "{}", info.error().message);
        return 1;
      }

      std::println("GPU acceptance passed: {}", info->name);
      return 0;
    }

    Dot::Core::ApplicationConfig config;
    config.window.title = "DotEngine - GPU Test";
    config.window.gpuSurface = true;
    config.frameDelay = std::chrono::milliseconds{10};
    config.pauseOnFocusLoss = false;
    auto application = Dot::Core::Application::create(config);

    if (!application) {
      std::println(stderr, "{}", application.error().message);
      return 1;
    }

    auto renderer = Dot::Graphics::Renderer::create(&application->window());

    if (!renderer) {
      std::println(stderr, "{}", renderer.error().message);
      return 1;
    }

    std::size_t frames = 0;
    std::size_t presented = 0;
    Dot::Core::LoopCallbacks callbacks;
    callbacks.render = [&](const Dot::Core::FrameState&) -> Dot::Core::Result<void> {
      ++frames;

      if (frames == 10) {
        auto resized = application->window().resize(800, 600);

        if (!resized) {
          return resized;
        }
      }

      auto result = renderer->present(vertices, {}, {0.03F, 0.04F, 0.06F, 1});

      if (!result) {
        return std::unexpected(result.error());
      }

      if (*result == Dot::Graphics::PresentResult::Presented) {
        ++presented;
      }

      return {};
    };

    auto result = application->run(callbacks, 30);

    if (!result) {
      std::println(stderr, "{}", result.error().message);
      return 1;
    }

    if (presented < 20) {
      std::println(stderr, "Surface acceptance failed: {} frames presented", presented);
      return 1;
    }

    std::println("Surface acceptance passed: {} frames presented, resize included", presented);
    return 0;
  }

  auto testMath() -> int {
    using namespace Dot::Math;
    auto model = compose({2, 3, 4});
    auto view = lookAt({2, 3, 10}, {2, 3, 4});
    auto projection = perspective(60, 1, 0.1F, 100);

    if (!model || !view || !projection) {
      std::println(stderr, "Math acceptance initialization failed");
      return 1;
    }

    auto viewModel = multiply(*view, *model);
    auto clip = viewModel ? multiply(*projection, *viewModel)
                          : Dot::Core::Result<Mat4>{std::unexpected(viewModel.error())};
    auto point
        = clip ? transformPoint(*clip, {}) : Dot::Core::Result<Vec3>{std::unexpected(clip.error())};

    if (!point || point->x != 0 || point->y != 0 || point->z <= 0 || point->z >= 1) {
      std::println(stderr, "Math acceptance projection failed");
      return 1;
    }

    auto parts = decomposeTrs(*model);

    if (!parts || parts->position != Vec3{2, 3, 4} || parts->scale != Vec3{1, 1, 1}
        || decomposeTrs(*projection)) {
      std::println(stderr, "Math acceptance TRS decomposition failed");
      return 1;
    }

    std::println("Math acceptance passed: camera depth {}; affine TRS verified", point->z);
    return 0;
  }

  auto testWindow() -> int {
    Dot::Core::ApplicationConfig config;
    config.window.title = "DotEngine - Window Test";
    config.frameDelay = std::chrono::milliseconds{10};
    config.pauseOnFocusLoss = false;
    auto application = Dot::Core::Application::create(config);

    if (!application) {
      std::println(stderr, "{}", application.error().message);
      return 1;
    }

    std::size_t frames = 0;
    Dot::Core::LoopCallbacks callbacks;
    callbacks.render = [&](const Dot::Core::FrameState&) -> Dot::Core::Result<void> {
      ++frames;
      return {};
    };

    auto result = application->run(callbacks, 20);

    if (!result || frames != 20) {
      std::println(stderr, "Window acceptance failed");
      return 1;
    }

    std::println("Window acceptance passed: {} frames", frames);
    return 0;
  }

  auto testFiles(const std::filesystem::path& root) -> int {
    auto files = Dot::Core::FileSystem::open(root);

    if (!files) {
      std::println(stderr, "{}", files.error().message);
      return 1;
    }

    auto directories = files->createDirectories("foundation");

    if (!directories) {
      std::println(stderr, "{}", directories.error().message);
      return 1;
    }

    auto saved = files->writeTextAtomic("foundation/state.txt", "DotEngine");

    if (!saved) {
      std::println(stderr, "{}", saved.error().message);
      return 1;
    }

    const auto loaded = files->readText("foundation/state.txt");

    if (!loaded || *loaded != "DotEngine") {
      std::println(stderr, "Filesystem acceptance failed");
      return 1;
    }

    std::println("Filesystem acceptance passed");
    return 0;
  }
}  // namespace

auto main(int argc, char** argv) -> int {
  try {
    if (argc == 3 && std::string_view(argv[1]) == "--scene-file-test") {
      return testSceneFiles(argv[2]);
    }

    if (argc == 2 && std::string_view(argv[1]) == "--scene-render-test") {
      return testSceneRender();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--scene-test") {
      return testScene();
    }

    if (argc == 3 && std::string_view(argv[1]) == "--asset-test") {
      return testAssets(argv[2]);
    }

    if (argc == 2 && std::string_view(argv[1]) == "--mesh-test") {
      return testMesh();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--geometry-test") {
      return testGeometry();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--gpu-test") {
      return testGraphics(false);
    }

    if (argc == 2 && std::string_view(argv[1]) == "--render-smoke") {
      return testGraphics(true);
    }

    if (argc == 2 && std::string_view(argv[1]) == "--math-test") {
      return testMath();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--window-smoke") {
      return testWindow();
    }

    if (argc == 3 && std::string_view(argv[1]) == "--file-test") {
      return testFiles(argv[2]);
    }

    if (argc == 2 && std::string_view(argv[1]) == "--scene-physics-test") {
      return Dot::Runtime::testScenePhysics(false);
    }

    if (argc == 2 && std::string_view(argv[1]) == "--scene-physics-render-test") {
      return Dot::Runtime::testScenePhysics(true);
    }

    if (argc == 3 && std::string_view(argv[1]) == "--audio-clip-test") {
      return Dot::Runtime::testAudioClip(argv[2]);
    }

    if (argc == 2 && std::string_view(argv[1]) == "--audio-mix-test") {
      return Dot::Runtime::testAudioMix();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--audio-output-test") {
      return Dot::Runtime::testAudioOutput();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--audio-playback-test") {
      return Dot::Runtime::testAudioPlayback();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--audio-spatial-test") {
      return Dot::Runtime::testAudioSpatial();
    }

    if (argc == 3 && std::string_view(argv[1]) == "--scene-audio-test") {
      return Dot::Runtime::testSceneAudio(argv[2]);
    }

    if (argc == 2 && std::string_view(argv[1]) == "--audio-batch-test") {
      return Dot::Runtime::testAudioBatch();
    }

    if (argc == 3 && std::string_view(argv[1]) == "--scene-audio-play-test") {
      return Dot::Runtime::testSceneAudioPlayback(argv[2]);
    }

    if (argc == 3 && std::string_view(argv[1]) == "--script-test") {
      return Dot::Runtime::testScript(argv[2]);
    }

    if (argc == 4 && std::string_view(argv[1]) == "--process-test") {
      return Dot::Runtime::testProcess(argv[2], argv[3]);
    }

    if (argc == 4 && std::string_view(argv[1]) == "--typescript-test") {
      return Dot::Runtime::testCompiler(argv[2], argv[3]);
    }

    if (argc == 5
        && (std::string_view(argv[1]) == "--script-host-test"
            || std::string_view(argv[1]) == "--script-host-render-test")) {
      return Dot::Runtime::testScriptHost(argv[2], argv[3], argv[4],
                                          std::string_view(argv[1]) == "--script-host-render-test");
    }

    if (argc == 5 && std::string_view(argv[1]) == "--script-instance-test") {
      return Dot::Runtime::testScriptInstances(argv[2], argv[3], argv[4]);
    }

    if (argc == 5
        && (std::string_view(argv[1]) == "--script-components-test"
            || std::string_view(argv[1]) == "--script-components-render-test")) {
      return Dot::Runtime::testScriptComponents(
          argv[2], argv[3], argv[4],
          std::string_view(argv[1]) == "--script-components-render-test");
    }

    if (argc == 5
        && (std::string_view(argv[1]) == "--script-reload-test"
            || std::string_view(argv[1]) == "--script-reload-render-test")) {
      return Dot::Runtime::testScriptReload(
          argv[2], argv[3], argv[4], std::string_view(argv[1]) == "--script-reload-render-test");
    }

    if (argc == 5
        && (std::string_view(argv[1]) == "--script-authoring-test"
            || std::string_view(argv[1]) == "--script-authoring-render-test")) {
      return Dot::Runtime::testScriptAuthoring(
          argv[2], argv[3], argv[4], std::string_view(argv[1]) == "--script-authoring-render-test");
    }

    if (argc == 5 && std::string_view(argv[1]) == "--script-input-test") {
      return Dot::Runtime::testScriptInput(argv[2], argv[3], argv[4]);
    }

    if (argc == 5
        && (std::string_view(argv[1]) == "--script-physics-test"
            || std::string_view(argv[1]) == "--script-physics-render-test")) {
      return Dot::Runtime::testScriptPhysics(
          argv[2], argv[3], argv[4], std::string_view(argv[1]) == "--script-physics-render-test");
    }

    if (argc == 5
        && (std::string_view(argv[1]) == "--script-audio-test"
            || std::string_view(argv[1]) == "--script-audio-play-test")) {
      return Dot::Runtime::testScriptAudio(argv[2], argv[3], argv[4],
                                           std::string_view(argv[1]) == "--script-audio-play-test");
    }

    if (argc == 2 && std::string_view(argv[1]) == "--physics-test") {
      return testPhysics();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--version") {
      std::println("DotEngine 0.1.0 ({})", Dot::Core::platformName());
      return 0;
    }

    if (argc > 1 && !(argc == 2 && std::string_view(argv[1]) == "--self-test")) {
      std::println(
          stderr,
          "Usage: DotRuntime "
          "[--version|--self-test|--math-test|--geometry-test|--mesh-test|--scene-test|"
          "--scene-render-test|--window-smoke|--gpu-test|--render-smoke|--file-test <directory>|"
          "--asset-test <directory>|--scene-file-test <directory>|--physics-test|"
          "--scene-physics-test|--scene-physics-render-test|--audio-clip-test <directory>|"
          "--audio-mix-test|--audio-output-test|--audio-playback-test|--audio-spatial-test|"
          "--scene-audio-test <directory>|--audio-batch-test|--scene-audio-play-test <directory>|"
          "--script-test <directory>|--process-test <fixture> <directory>|"
          "--typescript-test <compiler> <directory>|--script-host-test <compiler> <sdk> "
          "<directory>|"
          "--script-host-render-test <compiler> <sdk> <directory>|"
          "--script-instance-test <compiler> <sdk> <directory>|"
          "--script-components-test <compiler> <sdk> <directory>|"
          "--script-components-render-test <compiler> <sdk> <directory>|"
          "--script-reload-test <compiler> <sdk> <directory>|"
          "--script-reload-render-test <compiler> <sdk> <directory>|"
          "--script-authoring-test <compiler> <sdk> <directory>|"
          "--script-authoring-render-test <compiler> <sdk> <directory>|"
          "--script-input-test <compiler> <sdk> <directory>|"
          "--script-physics-test <compiler> <sdk> <directory>|"
          "--script-physics-render-test <compiler> <sdk> <directory>|"
          "--script-audio-test <compiler> <sdk> <directory>|"
          "--script-audio-play-test <compiler> <sdk> <directory>]");
      return 2;
    }

    Dot::Core::Logger logger;
    Dot::Core::Clock clock;
    Dot::Core::TrackingResource memory;
    auto* allocation = memory.allocate(256);
    memory.deallocate(allocation, 256);
    Dot::Core::FrameStepper stepper(std::chrono::milliseconds{10}, 8);
    const auto step = stepper.advance(std::chrono::milliseconds{25});
    Dot::Core::Signal<int> events;
    int received = 0;
    auto connection = events.subscribe([&](int event) { received = event; });
    const auto dispatched = events.publish(42);

    const auto textLength = Dot::Core::utf8CodePoints("DotEngine 中文🙂");

    if (step.updates != 2 || memory.stats().liveBytes != 0 || dispatched != 1 || received != 42
        || !textLength || *textLength != 13 || Dot::Core::isValidUtf8("\xC0\x80")) {
      logger.write(Dot::Core::LogLevel::Error, "Foundation self-test failed");
      return 1;
    }

    logger.log(Dot::Core::LogLevel::Info, "DotEngine ready on {}; {} fixed updates; {} ns elapsed",
               Dot::Core::platformName(), step.updates, clock.elapsed().count());
    return logger.failures() == 0 ? 0 : 1;
  } catch (const std::exception& error) {
    std::println(stderr, "DotEngine failed: {}", error.what());
    return 1;
  }
}

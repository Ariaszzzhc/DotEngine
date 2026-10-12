#include <array>
#include <chrono>
#include <exception>
#include <expected>
#include <filesystem>
#include <print>
#include <string_view>
#include <vector>

import Dot.Core;
import Dot.Core.Application;
import Dot.Platform.Window;
import Dot.Math;
import Dot.Graphics.Renderer;
import Dot.Assets.Mesh;

namespace {
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

    std::println("Math acceptance passed: camera depth {}", point->z);
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

    if (argc == 2 && std::string_view(argv[1]) == "--version") {
      std::println("DotEngine 0.1.0 ({})", Dot::Core::platformName());
      return 0;
    }

    if (argc > 1 && !(argc == 2 && std::string_view(argv[1]) == "--self-test")) {
      std::println(stderr,
                   "Usage: DotRuntime "
                   "[--version|--self-test|--math-test|--geometry-test|--mesh-test|"
                   "--window-smoke|--gpu-test|--render-smoke|--file-test <directory>|"
                   "--asset-test <directory>]");
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

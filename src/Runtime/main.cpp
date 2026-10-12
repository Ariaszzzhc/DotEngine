#include <chrono>
#include <exception>
#include <filesystem>
#include <print>
#include <string_view>

import Dot.Core;
import Dot.Core.Application;
import Dot.Platform.Window;

namespace {
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
      std::println(
          stderr,
          "Usage: DotRuntime [--version|--self-test|--window-smoke|--file-test <directory>]");
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
    if (step.updates != 2 || memory.stats().liveBytes != 0 || dispatched != 1 || received != 42) {
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

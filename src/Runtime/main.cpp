#include <chrono>
#include <exception>
#include <print>
#include <string_view>

import Dot.Core;

auto main(int argc, char** argv) -> int {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
      std::println("DotEngine 0.1.0 ({})", Dot::Core::platformName());
      return 0;
    }
    if (argc > 1 && !(argc == 2 && std::string_view(argv[1]) == "--self-test")) {
      std::println(stderr, "Usage: DotRuntime [--version|--self-test]");
      return 2;
    }
    Dot::Core::Logger logger;
    Dot::Core::Clock clock;
    Dot::Core::TrackingResource memory;
    auto* allocation = memory.allocate(256);
    memory.deallocate(allocation, 256);
    Dot::Core::FrameStepper stepper(std::chrono::milliseconds{10}, 8);
    const auto step = stepper.advance(std::chrono::milliseconds{25});
    if (step.updates != 2 || memory.stats().liveBytes != 0) {
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

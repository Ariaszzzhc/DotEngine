#include <chrono>
#include <expected>
#include <filesystem>
#include <print>
#include <string>

import Dot.Core.Process;

namespace Dot::Runtime {
  auto testProcess(const std::filesystem::path& executable, const std::filesystem::path& root)
      -> int {
    Dot::Core::ProcessConfig config;
    config.executable = std::filesystem::absolute(executable);
    config.directory = std::filesystem::absolute(root);
    const auto before = Dot::Core::processResources();
    config.arguments = {"stdin"};
    auto eof = Dot::Core::runProcess(config);
    config.arguments = {"exit", "7"};
    auto nonzero = Dot::Core::runProcess(config);

    if (!eof || eof->standardOutput != "eof" || !nonzero || nonzero->exitCode != 7
        || nonzero->standardOutput != "normal output"
        || nonzero->standardError != "failure detail") {
      std::println(stderr, "Process acceptance EOF/status/capture failed");
      return 1;
    }

    config.arguments = {"emit", "64"};
    auto streams = Dot::Core::runProcess(config);

    if (!streams || streams->standardOutput != std::string(64 * 4096, 'o')
        || streams->standardError != std::string(64 * 4096, 'e')) {
      std::println(stderr, "Process acceptance concurrent pipe capture failed");
      return 1;
    }

    config.arguments = {"sleep"};
    config.timeout = std::chrono::milliseconds{50};
    auto timeout = Dot::Core::runProcess(config);
    const auto after = Dot::Core::processResources();

    if (timeout || after.handles != before.handles || after.children != before.children) {
      std::println(stderr, "Process acceptance timeout/cleanup failed");
      return 1;
    }

    std::println(
        "Process acceptance passed: real child status, raw streams, EOF, timeout, cleanup");
    return 0;
  }
}  // namespace Dot::Runtime

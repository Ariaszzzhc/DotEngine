#include <charconv>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <print>
#include <string_view>

#if defined(_WIN32)
#  include <SDL3/SDL_main.h>
#endif

#include "Player.hpp"

import Dot.Core.Types;

/** Launch native project/package playback.
 * 启动原生项目或游戏包播放。 */
auto main(int argc, char** argv) -> int {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
      std::println("DotEngine 0.1.0 ({})", Dot::Core::platformName());
      return 0;
    }

    const bool game = (argc == 3 || argc == 4) && std::string_view(argv[1]) == "--game";
    const bool project = (argc == 4 || argc == 5) && std::string_view(argv[1]) == "--project";

    if (!game && !project) {
      std::println(stderr,
                   "Usage: DotRuntime --version | --game <package> [frames] | "
                   "--project <root> <compiler> [frames]");
      return 2;
    }

    const int budgetIndex = game ? 3 : 4;
    std::size_t frames = 0;

    if (argc > budgetIndex) {
      const std::string_view value(argv[budgetIndex]);
      const auto parsed = std::from_chars(value.data(), value.data() + value.size(), frames);

      if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        std::println(stderr, "{} frame budget must be an unsigned integer",
                     game ? "Game" : "Project");
        return 2;
      }
    }

    return game ? Dot::Runtime::playGame(argv[2], frames)
                : Dot::Runtime::playProject(argv[2], argv[3], frames);
  } catch (const std::exception& error) {
    std::println(stderr, "DotEngine failed: {}", error.what());
    return 1;
  }
}

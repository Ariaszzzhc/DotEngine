#include <charconv>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>

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

    const bool game = argc >= 3 && std::string_view(argv[1]) == "--game";
    const bool project = argc >= 4 && std::string_view(argv[1]) == "--project";

    if (!game && !project) {
      std::println(stderr,
                   "Usage: DotRuntime --version | --game <package> [frames] [--profile <file>] | "
                   "--project <root> <compiler> [frames] [--profile <file>]");
      return 2;
    }

    int argument = game ? 3 : 4;
    std::size_t frames = 0;
    std::optional<std::filesystem::path> profile;

    if (argument < argc && std::string_view(argv[argument]) != "--profile") {
      const std::string_view value(argv[argument++]);
      const auto parsed = std::from_chars(value.data(), value.data() + value.size(), frames);

      if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        std::println(stderr, "{} frame budget must be an unsigned integer",
                     game ? "Game" : "Project");
        return 2;
      }
    }

    if (argument < argc) {
      if (argc - argument != 2 || std::string_view(argv[argument]) != "--profile"
          || std::string_view(argv[argument + 1]).empty()) {
        std::println(stderr, "Expected --profile <file> after the frame budget");
        return 2;
      }

      const std::string_view destination(argv[argument + 1]);
      profile = std::filesystem::path(std::u8string(destination.begin(), destination.end()));
    }

    return game ? Dot::Runtime::playGame(argv[2], frames, std::move(profile))
                : Dot::Runtime::playProject(argv[2], argv[3], frames, std::move(profile));
  } catch (const std::exception& error) {
    std::println(stderr, "DotEngine failed: {}", error.what());
    return 1;
  }
}

#include <SDL3/SDL.h>

#ifdef _WIN32
#  include <SDL3/SDL_main.h>
#endif

#include <charconv>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>

#include "Player.hpp"

/** Play the adjacent game package independently of working directory/compiler.
 * 播放相邻游戏包，不依赖工作目录或编译器。 */
auto main(int argc, char** argv) -> int {
  try {
    std::size_t frames = 0;
    bool budget = false;
    std::optional<std::filesystem::path> profile;

    for (int argument = 1; argument < argc; argument += 2) {
      if (argument + 1 >= argc) {
        std::println(stderr, "Usage: DotGame [--frames <count>] [--profile <file>]");
        return 2;
      }

      const std::string_view option(argv[argument]);
      const std::string_view value(argv[argument + 1]);

      if (option == "--frames" && !budget) {
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), frames);

        if (value.empty() || parsed.ec != std::errc{}
            || parsed.ptr != value.data() + value.size()) {
          std::println(stderr, "Game frame budget must be an unsigned integer");
          return 2;
        }

        budget = true;
      } else if (option == "--profile" && !profile && !value.empty()) {
        profile = std::filesystem::path(std::u8string(value.begin(), value.end()));
      } else {
        std::println(stderr, "Invalid or duplicate player option");
        return 2;
      }
    }

    const auto* base = SDL_GetBasePath();

    if (!base) {
      std::println(stderr, "Resolve game executable directory: {}", SDL_GetError());
      return 1;
    }

    const std::string_view text(base);
    const auto root = std::filesystem::path(std::u8string(text.begin(), text.end()));
    return Dot::Runtime::playGame(root / "game", frames, std::move(profile));
  } catch (const std::exception& error) {
    std::println(stderr, "Standalone game: {}", error.what());
    return 1;
  }
}

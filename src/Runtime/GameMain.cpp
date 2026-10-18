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
#include <vector>

#include "Player.hpp"

/** Play the adjacent game package independently of working directory/compiler.
 * 播放相邻游戏包，不依赖工作目录或编译器。 */
auto main(int argc, char** argv) -> int {
  try {
    if (argc == 3
        && (std::string_view(argv[1]) == "control-status"
            || std::string_view(argv[1]) == "ack-control"
            || std::string_view(argv[1]) == "clear-control")) {
      return Dot::Runtime::playerReceiptAction(argv[1], argv[2]);
    }

    if (argc == 5 && std::string_view(argv[1]) == "send") {
      return Dot::Runtime::sendPlayerRequest(argv[2], argv[3], argv[4]);
    }

    std::vector<std::string_view> arguments;

    for (int argument = 1; argument < argc; ++argument) {
      arguments.emplace_back(argv[argument]);
    }

    auto options = Dot::Runtime::parsePlayerOptions(arguments, false);

    if (!options) {
      std::println(stderr, "{}", options.error().message);
      return 2;
    }

    const auto* base = SDL_GetBasePath();

    if (!base) {
      std::println(stderr, "Resolve game executable directory: {}", SDL_GetError());
      return 1;
    }

    const std::string_view text(base);
    const auto root = std::filesystem::path(std::u8string(text.begin(), text.end()));
    return Dot::Runtime::playGame(root / "game", *options, Dot::Runtime::PlayerLayout::Standalone);
  } catch (const std::exception& error) {
    std::println(stderr, "Standalone game: {}", error.what());
    return 1;
  }
}

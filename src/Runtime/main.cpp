#include <charconv>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

    if (argc == 5 && std::string_view(argv[1]) == "send") {
      return Dot::Runtime::sendPlayerRequest(argv[2], argv[3], argv[4]);
    }

    const bool game = argc >= 3 && std::string_view(argv[1]) == "--game";
    const bool project = argc >= 4 && std::string_view(argv[1]) == "--project";

    if (!game && !project) {
      std::println(stderr,
                   "Usage: DotRuntime --version | send <control> <id> <request-file> | "
                   "(--game <package> | --project <root> <compiler>) "
                   "[frames] [--profile <file>] [--control <parent>] [--data-directory <root>]");
      return 2;
    }

    std::vector<std::string_view> arguments;

    for (int argument = game ? 3 : 4; argument < argc; ++argument) {
      arguments.emplace_back(argv[argument]);
    }

    auto options = Dot::Runtime::parsePlayerOptions(arguments, true);

    if (!options) {
      std::println(stderr, "{}", options.error().message);
      return 2;
    }

    const auto path = [](std::string_view value) {
      return std::filesystem::path{std::u8string(value.begin(), value.end())};
    };

    return game ? Dot::Runtime::playGame(path(argv[2]), *options)
                : Dot::Runtime::playProject(path(argv[2]), path(argv[3]), *options);
  } catch (const std::exception& error) {
    std::println(stderr, "DotEngine failed: {}", error.what());
    return 1;
  }
}

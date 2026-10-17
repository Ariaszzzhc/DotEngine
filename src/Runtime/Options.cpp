#include <charconv>
#include <exception>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <print>
#include <span>
#include <string>
#include <string_view>

#include "Player.hpp"

import Dot.Core.Control;
import Dot.Core.Files;

namespace Dot::Runtime {
  namespace {
    auto path(std::string_view text) -> std::filesystem::path {
      return std::filesystem::path{std::u8string(text.begin(), text.end())};
    }
  }  // namespace

  auto parsePlayerOptions(std::span<const std::string_view> arguments, bool positionalBudget)
      -> Core::Result<PlayerOptions> {
    PlayerOptions options;
    bool budget{};

    for (std::size_t index = 0; index < arguments.size(); ++index) {
      auto option = arguments[index];
      std::string_view value;

      if (index == 0 && positionalBudget && !option.starts_with("--")) {
        value = option;
        option = "--frames";
      } else {
        if (index + 1 >= arguments.size()) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Player option requires a value");
        }

        value = arguments[++index];
      }

      if (option == "--frames" && !budget) {
        const auto parsed
            = std::from_chars(value.data(), value.data() + value.size(), options.frames);

        if (value.empty() || parsed.ec != std::errc{}
            || parsed.ptr != value.data() + value.size()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Player frame budget must be an unsigned integer");
        }

        budget = true;
      } else if (option == "--profile" && !options.profile && !value.empty()) {
        options.profile = path(value);
      } else if (option == "--control" && !options.control && !value.empty()) {
        options.control = path(value);
      } else if (option == "--data-directory" && !options.dataDirectory && !value.empty()) {
        options.dataDirectory = path(value);
      } else {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid or duplicate player option");
      }
    }

    return options;
  }

  auto sendPlayerRequest(std::string_view control, std::string_view id, std::string_view request)
      -> int {
    try {
      const auto absolute = std::filesystem::absolute(path(request));
      auto files = Core::FileSystem::open(absolute.parent_path());
      auto text = files ? files->readText(absolute.filename(), 1024 * 1024)
                        : Core::Result<std::string>{std::unexpected(files.error())};
      auto response = text ? Core::submitControlRequest(path(control), id, *text)
                           : Core::Result<std::filesystem::path>{std::unexpected(text.error())};

      if (!response) {
        std::println(stderr, "{}", response.error().message);
        return 1;
      }

      const auto utf = response->generic_u8string();
      std::println("{}", nlohmann::json{{"ok", true},
                                        {"queued", true},
                                        {"response", std::string(utf.begin(), utf.end())}}
                             .dump());
      return 0;
    } catch (const std::exception& error) {
      std::println(stderr, "Submit player request: {}", error.what());
      return 1;
    }
  }
}  // namespace Dot::Runtime

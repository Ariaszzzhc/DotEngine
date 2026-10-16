module;

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <exception>
#include <filesystem>
#include <memory>
#include <string>

module Dot.Platform.UserData;

import Dot.Core.Paths;

namespace Dot::Platform {
  auto userDataDirectory(std::string_view applicationId) -> Core::Result<Core::FileSystem> {
    if (!Core::isApplicationId(applicationId)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid application identity");
    }

    try {
      const std::string identity{applicationId};
      std::unique_ptr<char, decltype(&SDL_free)> path{
          SDL_GetPrefPath("DotEngine", identity.c_str()), &SDL_free};

      if (!path) {
        return Core::fail(Core::ErrorCode::Io,
                          "Resolve user data directory: " + std::string(SDL_GetError()));
      }

      const std::string text{path.get()};
      return Core::FileSystem::open(std::filesystem::path{std::u8string(text.begin(), text.end())});
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "User data directory: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Platform

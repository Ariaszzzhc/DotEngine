module;

#include <SDL3/SDL.h>

#include <exception>
#include <expected>
#include <filesystem>
#include <string>

module Dot.Platform.ExternalFiles;

import Dot.Core.Text;
import Dot.Platform.System;

namespace Dot::Platform {
  auto fileUri(const std::filesystem::path& path) -> Core::Result<std::string> {
    try {
      const auto bytes = path.generic_u8string();
      const std::string value(bytes.begin(), bytes.end());

      if (!path.is_absolute() || value.size() > 4096 || value.find('\0') != std::string::npos
          || !Core::isValidUtf8(value)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "External file requires a bounded absolute UTF-8 path without NUL");
      }

#ifdef _WIN32
      std::string uri = value.starts_with("//") ? "file:" : "file:///";
#else
      std::string uri = "file://";
#endif
      constexpr auto digits = "0123456789ABCDEF";

      for (std::size_t index = 0; index < value.size(); ++index) {
        const auto byte = static_cast<unsigned char>(value[index]);
        bool literal = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
                       || (byte >= '0' && byte <= '9') || byte == '/' || byte == '-' || byte == '.'
                       || byte == '_' || byte == '~';
#ifdef _WIN32
        literal = literal || (index == 1 && byte == ':' && !value.starts_with("//"));
#endif

        if (literal) {
          uri += static_cast<char>(byte);
        } else {
          uri += '%';
          uri += digits[byte >> 4];
          uri += digits[byte & 15];
        }
      }

      return uri;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Encode external file: " + std::string(error.what()));
    }
  }

  auto openExternalFile(const std::filesystem::path& path) -> Core::Result<void> {
    if (!isMainThread()) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "External application launch requires main thread");
    }

    auto uri = fileUri(path);

    if (!uri) {
      return std::unexpected(uri.error());
    }

    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);

    if (error || !std::filesystem::is_regular_file(status)) {
      return Core::fail(Core::ErrorCode::Io,
                        "External file is missing, inaccessible or not regular: " + *uri);
    }

    if (!SDL_OpenURL(uri->c_str())) {
      return Core::fail(Core::ErrorCode::Unavailable,
                        "Open external file: " + std::string(SDL_GetError()));
    }

    return {};
  }
}  // namespace Dot::Platform

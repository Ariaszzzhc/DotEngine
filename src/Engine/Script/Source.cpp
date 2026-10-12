module;

#include <algorithm>
#include <exception>
#include <expected>
#include <string>
#include <string_view>

#include "../Serialization/Json.hpp"

module Dot.Script.Source;

import Dot.Core.Result;
import Dot.Core.Text;

namespace Dot::Script {
  namespace {
    auto sourceValid(std::string_view source) -> bool {
      if (source.empty() || source.size() > 4096 || source.front() == '/' || source.back() == '/'
          || source.find_first_of("\\:<>\"|?*") != std::string_view::npos
          || !Core::isValidUtf8(source) || source.find('\0') != std::string_view::npos
          || std::ranges::any_of(source, [](unsigned char value) { return value < 32; })
          || (!source.ends_with(".ts") && !source.ends_with(".mts")) || source.ends_with(".d.ts")
          || source.ends_with(".d.mts")) {
        return false;
      }

      std::size_t begin = 0;

      while (begin < source.size()) {
        const auto end = source.find('/', begin);
        const auto part = source.substr(begin, end == std::string_view::npos ? end : end - begin);

        if (part.empty() || part == "." || part == ".." || part.back() == '.'
            || part.back() == ' ') {
          return false;
        }

        if (end == std::string_view::npos) {
          break;
        }

        begin = end + 1;
      }

      return true;
    }

    auto classValid(std::string_view name) -> bool {
      if (name.empty() || name.size() > 128) {
        return false;
      }

      for (std::size_t index = 0; index < name.size(); ++index) {
        const auto c = name[index];
        const bool letter
            = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';

        if (!letter && (index == 0 || c < '0' || c > '9')) {
          return false;
        }
      }

      return true;
    }
  }  // namespace

  auto normalizeComponentProperties(const ComponentProperties& properties)
      -> Core::Result<ComponentProperties> {
    if (!sourceValid(properties.source) || !classValid(properties.className)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid script source path/class export");
    }

    if (properties.properties.empty() || properties.properties.size() > 64 * 1024
        || properties.properties.find('\0') != std::string::npos
        || !Core::isValidUtf8(properties.properties)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid script property JSON bytes");
    }

    try {
      const auto parsed = Serialization::Detail::parseJson(properties.properties, 32);

      if (!parsed.is_object()) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Script properties must be a JSON object");
      }

      auto result = properties;
      result.properties = parsed.dump();

      if (result.properties.size() > 64 * 1024) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Canonical script properties exceed 64 KiB");
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Script properties: " + std::string(error.what()));
    }
  }

  auto emittedModule(const ComponentProperties& properties) -> Core::Result<std::string> {
    auto checked = normalizeComponentProperties(properties);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto path = properties.source;
    path.replace(path.size() - (path.ends_with(".mts") ? 4U : 3U), std::string::npos,
                 path.ends_with(".mts") ? ".mjs" : ".js");
    return path;
  }
}  // namespace Dot::Script

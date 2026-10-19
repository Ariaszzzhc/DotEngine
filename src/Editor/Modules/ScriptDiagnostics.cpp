module;

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

module Dot.Editor.ScriptDiagnostics;

import Dot.Core.Paths;
import Dot.Core.Text;

namespace Dot::Editor {
  auto scriptDiagnostics(std::string_view text, std::string_view scriptsDirectory)
      -> Core::Result<std::vector<ScriptDiagnostic>> {
    if (text.size() > 2 * 1024 * 1024 || text.find('\0') != std::string_view::npos
        || !Core::isValidUtf8(text) || !Core::isPortableRelativePath(scriptsDirectory)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid script diagnostic input");
    }

    try {
      std::vector<ScriptDiagnostic> result;

      while (!text.empty()) {
        const auto end = text.find('\n');
        const auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        const auto close = line.find("): ");
        const auto open = line.rfind('(', close);
        const auto comma = line.find(',', open);

        if (close == std::string_view::npos || open == std::string_view::npos
            || comma == std::string_view::npos || comma >= close) {
          continue;
        }

        std::string source(line.substr(0, open));
        std::ranges::replace(source, '\\', '/');

        if (!source.starts_with("src/") || !Core::isPortableRelativePath(source)
            || (!source.ends_with(".ts") && !source.ends_with(".mts"))) {
          continue;
        }

        const auto position = [&](std::string_view value, std::uint32_t& output) {
          const auto parsed = std::from_chars(value.data(), value.data() + value.size(), output);
          return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && output;
        };
        std::uint32_t row{};
        std::uint32_t column{};

        if (!position(line.substr(open + 1, comma - open - 1), row)
            || !position(line.substr(comma + 1, close - comma - 1), column)) {
          continue;
        }

        const auto path = std::string(scriptsDirectory) + '/' + source.substr(4);

        if (!Core::isPortableRelativePath(path)) {
          continue;
        }

        if (result.size() == 4096) {
          return Core::fail(Core::ErrorCode::Unavailable, "Script diagnostic capacity exceeded");
        }

        result.push_back({path, row, column, std::string(line.substr(close + 3))});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Read script diagnostics: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Editor

module;

#include <algorithm>
#include <array>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <span>
#include <vector>

#include "BuiltinModules.hpp"

module Dot.Script.Modules;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Core.Paths;
import Dot.Core.Text;

namespace Dot::Script {
  auto engineModules() noexcept -> std::span<const EngineModule> {
    static const std::array<EngineModule, 2> modules{
        {{"dot:engine", Detail::engineTypes, Detail::engineSource},
         {"dot:host", Detail::hostTypes, {}}}};
    return modules;
  }

  auto engineModule(std::string_view name) -> Core::Result<EngineModule> {
    for (const auto& module : engineModules()) {
      if (module.name == name) {
        return module;
      }
    }

    return Core::fail(Core::ErrorCode::NotFound, "Unknown engine module: " + std::string(name));
  }

  auto resolveImport(const Core::FileSystem& files, std::string_view base, std::string_view request)
      -> Core::Result<std::string> {
    try {
      if (request.starts_with("dot:")) {
        auto module = engineModule(request);
        return module ? Core::Result<std::string>{std::string(module->name)}
                      : std::unexpected(module.error());
      }

      if (request.empty() || request.size() > 4096 || !Core::isValidUtf8(request)
          || request.find_first_of("\\:<>\"|?*") != std::string_view::npos
          || request.find('\0') != std::string_view::npos
          || (!request.starts_with("./") && !request.starts_with("../"))
          || request.ends_with('/')) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Imports require extensionless relative or dot: names");
      }

      const auto name = request.substr(request.rfind('/') + 1);

      if (std::ranges::any_of(
              std::array{".js", ".mjs", ".ts", ".mts", ".tsx", ".jsx", ".cts", ".cjs", ".json"},
              [&](std::string_view extension) { return name.ends_with(extension); })) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Project imports must omit filename extensions");
      }

      if (!Core::isPortableRelativePath(base)
          || (!base.ends_with(".js") && !base.ends_with(".mjs"))) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Relative import needs a project module base");
      }

      auto combined = std::string(
          base.substr(0, base.rfind('/') == std::string_view::npos ? 0 : base.rfind('/') + 1));
      combined += request;
      std::vector<std::string_view> parts;
      std::size_t begin{};

      while (begin < combined.size()) {
        auto end = combined.find('/', begin);
        const auto part = std::string_view(combined).substr(
            begin, end == std::string::npos ? end : end - begin);

        if (part.empty()) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Import path has an empty component");
        }

        if (part == "..") {
          if (parts.empty()) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Import escapes the project root");
          }

          parts.pop_back();
        } else if (part != ".") {
          parts.push_back(part);
        }

        if (end == std::string::npos) {
          break;
        }

        begin = end + 1;
      }

      std::string local;

      for (auto part : parts) {
        if (!local.empty()) {
          local += '/';
        }

        local += part;
      }

      if (!Core::isPortableRelativePath(local)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Import has a nonportable path");
      }

      std::string found;

      for (const auto* extension : {".js", ".mjs"}) {
        const auto candidate = local + extension;
        const std::filesystem::path relative(std::u8string(candidate.begin(), candidate.end()));
        auto current = files.root();
        bool exists = true;

        for (const auto& part : relative) {
          std::error_code error;
          current /= part;
          auto status = std::filesystem::symlink_status(current, error);

          if (status.type() == std::filesystem::file_type::not_found) {
            exists = false;
            break;
          }

          if (error || std::filesystem::is_symlink(status)
              || (current == files.root() / relative ? !std::filesystem::is_regular_file(status)
                                                     : !std::filesystem::is_directory(status))) {
            return Core::fail(Core::ErrorCode::Io,
                              "Import has an inaccessible/symlink/special component: " + candidate);
          }

          bool exact = false;
          std::filesystem::directory_iterator entries(current.parent_path(), error);

          if (error) {
            return Core::fail(Core::ErrorCode::Io, "Import parent enumeration: " + error.message());
          }

          for (const auto& entry : entries) {
            if (entry.path().filename() == part) {
              exact = true;
              break;
            }
          }

          if (!exact) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Import path casing differs: " + candidate);
          }
        }

        if (exists) {
          if (!found.empty()) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Ambiguous project import: " + local);
          }

          found = candidate;
        }
      }

      if (found.empty()) {
        return Core::fail(Core::ErrorCode::NotFound, "Project import is missing: " + local);
      }

      return found;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Resolve script import: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Script

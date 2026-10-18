module;

#include <algorithm>
#include <array>
#include <exception>
#include <expected>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include "TextJson.hpp"

module Dot.UI.Files;

import Dot.Core.Paths;
import Dot.Core.Text;

namespace Dot::UI {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    constexpr std::size_t maxBytes = 1024 * 1024;
    constexpr std::array kinds{"panel", "label", "button"};

    auto checkedPath(const Core::FileSystem& files, const std::filesystem::path& path, bool saving)
        -> Core::Result<void> {
      const auto utf = path.generic_u8string();
      const std::string text(utf.begin(), utf.end());

      if (text.size() > 1024 || !Core::isPortableRelativePath(text) || !text.starts_with("assets/")
          || !text.ends_with(".dotui")) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "UI requires a portable assets/*.dotui path");
      }

      auto current = files.root();

      for (const auto& part : path) {
        current /= part;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(current, error);
        const bool final = current == files.root() / path;

        if (status.type() == std::filesystem::file_type::not_found && final && saving) {
          return {};
        }

        if (error || std::filesystem::is_symlink(status)
            || (final ? !std::filesystem::is_regular_file(status)
                      : !std::filesystem::is_directory(status))) {
          return Core::fail(
              Core::ErrorCode::Io,
              "UI path '" + text + "' is missing, inaccessible, a symlink or special file");
        }
      }

      return {};
    }
  }  // namespace

  auto decodeLayoutForm(std::string_view source) -> Core::Result<Description> {
    if (source.empty() || source.size() > maxBytes || source.find('\0') != std::string_view::npos
        || !Core::isValidUtf8(source)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid UI JSON bytes");
    }

    try {
      const auto root = Codec::parseJson(source, 12);
      Codec::requireFields(root, {"name", "nodes"});

      if (!root["nodes"].is_array() || root["nodes"].size() > 32) {
        throw std::invalid_argument("UI requires an array of at most 32 nodes");
      }

      Description description{root["name"].get<std::string>(), {}};

      for (const auto& value : root["nodes"]) {
        Codec::requireFields(value, {"key", "parent", "kind", "box", "enabled", "blocksPointer"});
        const auto kind = value["kind"].get<std::string>();
        const auto found = std::find(kinds.begin(), kinds.end(), kind);

        if (found == kinds.end()) {
          throw std::invalid_argument("Unknown UI node kind");
        }

        Node node{value["key"].get<std::string>(),
                  {},
                  static_cast<Kind>(found - kinds.begin()),
                  Detail::textBox<Graphics::TextBox>(value["box"]),
                  value["enabled"].get<bool>(),
                  value["blocksPointer"].get<bool>()};

        if (!value["parent"].is_null()) {
          node.parent = value["parent"].get<std::string>();
        }

        description.nodes.push_back(std::move(node));
      }

      return description;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Decode UI: " + std::string(error.what()));
    }
  }

  auto decodeLayout(std::string_view source) -> Core::Result<Layout> {
    auto description = decodeLayoutForm(source);
    return description ? Layout::create(std::move(*description))
                       : std::unexpected(description.error());
  }

  auto encodeLayout(const Layout& layout) -> Core::Result<std::string> {
    auto description = layout.description();

    if (!description) {
      return std::unexpected(description.error());
    }

    try {
      auto nodes = Json::array();

      for (const auto& node : (*description)->nodes) {
        nodes.push_back({{"key", node.key},
                         {"parent", node.parent ? Json(*node.parent) : Json(nullptr)},
                         {"kind", kinds.at(static_cast<std::size_t>(node.kind))},
                         {"box", Detail::textJson(node.box)},
                         {"enabled", node.enabled},
                         {"blocksPointer", node.blocksPointer}});
      }

      auto text = Json{{"name", (*description)->name}, {"nodes", std::move(nodes)}}.dump(2) + '\n';

      if (text.size() > maxBytes) {
        return Core::fail(Core::ErrorCode::Unavailable, "Encoded UI exceeds 1 MiB");
      }

      return text;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Encode UI: " + std::string(error.what()));
    }
  }

  auto loadLayout(const Core::FileSystem& files, const std::filesystem::path& path)
      -> Core::Result<Layout> {
    auto checked = checkedPath(files, path, false);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto text = files.readText(path, maxBytes);
    auto decoded = text ? decodeLayout(*text) : Core::Result<Layout>{std::unexpected(text.error())};

    if (!decoded) {
      const auto utf = path.generic_u8string();
      return Core::fail(decoded.error().code, "Load UI '" + std::string(utf.begin(), utf.end())
                                                  + "': " + decoded.error().message);
    }

    return decoded;
  }

  auto saveLayout(const Core::FileSystem& files, const std::filesystem::path& path,
                  const Layout& layout) -> Core::Result<void> {
    auto text = encodeLayout(layout);

    if (!text) {
      return std::unexpected(text.error());
    }

    auto checked = checkedPath(files, path, true);
    return checked ? files.writeTextAtomic(path, *text) : std::unexpected(checked.error());
  }
}  // namespace Dot::UI

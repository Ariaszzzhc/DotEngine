module;

#include <exception>
#include <expected>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include "../Serialization/Json.hpp"

module Dot.Animation.Files;

import Dot.Math;
import Dot.Core.Paths;
import Dot.Core.Text;

namespace Dot::Animation {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    constexpr std::size_t maxBytes = 64 * 1024 * 1024;

    auto vector(const Json& value) -> Math::Vec4 {
      const auto v = Codec::numbers<4>(value);
      return {v[0], v[1], v[2], v[3]};
    }

    auto vectorJson(Math::Vec4 value) -> Json {
      return {value.x, value.y, value.z, value.w};
    }

    auto pathChecked(const Core::FileSystem& files, const std::filesystem::path& path, bool saving)
        -> Core::Result<void> {
      const auto bytes = path.generic_u8string();
      const std::string text(bytes.begin(), bytes.end());

      if (!Core::isPortableRelativePath(text) || !text.ends_with(".dotanimation")) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Animation requires a portable relative .dotanimation path");
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
              "Animation path '" + text + "' is missing, inaccessible, a symlink or special file");
        }
      }

      return {};
    }
  }  // namespace

  auto decodeClip(std::string_view source) -> Core::Result<Clip> {
    if (source.empty() || source.size() > maxBytes || source.find('\0') != std::string_view::npos
        || !Core::isValidUtf8(source)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation JSON requires UTF-8 without NUL, at most 64 MiB");
    }

    try {
      auto root = Codec::parseJson(source);
      Codec::requireFields(root, {"name", "tracks"});
      const auto& tracks = root["tracks"];

      if (!tracks.is_array() || tracks.empty() || tracks.size() > 30000) {
        throw std::invalid_argument("Animation requires 1..30000 tracks");
      }

      ClipDescription description{root["name"].get<std::string>(), {}};
      std::size_t count{};

      for (const auto& track : tracks) {
        Codec::requireFields(track, {"node", "path", "interpolation", "keys"});
        const auto node = track["node"];
        const auto path = track["path"].get<std::string>();
        const auto mode = track["interpolation"].get<std::string>();
        const auto& keys = track["keys"];

        if (!node.is_number_integer() || node < 0 || node >= 10000
            || (path != "translation" && path != "rotation" && path != "scale")
            || (mode != "step" && mode != "linear" && mode != "cubic") || !keys.is_array()
            || keys.empty() || keys.size() > 1000000 - count) {
          throw std::invalid_argument("Invalid animation node/path/interpolation/keys");
        }

        count += keys.size();
        Track prepared{node.get<std::uint32_t>(),
                       path == "translation" ? Path::Translation
                       : path == "rotation"  ? Path::Rotation
                                             : Path::Scale,
                       mode == "step"     ? Interpolation::Step
                       : mode == "linear" ? Interpolation::Linear
                                          : Interpolation::CubicSpline,
                       {}};
        prepared.keys.reserve(keys.size());

        for (const auto& key : keys) {
          if (prepared.interpolation == Interpolation::CubicSpline) {
            Codec::requireFields(key, {"time", "value", "inTangent", "outTangent"});
          } else {
            Codec::requireFields(key, {"time", "value"});
          }

          Key value{Codec::number(key["time"]), vector(key["value"])};

          if (prepared.interpolation == Interpolation::CubicSpline) {
            value.inTangent = vector(key["inTangent"]);
            value.outTangent = vector(key["outTangent"]);
          }

          prepared.keys.push_back(value);
        }

        description.tracks.push_back(std::move(prepared));
      }

      return Clip::create(std::move(description));
    } catch (const Json::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation JSON: " + std::string(error.what()));
    } catch (const std::invalid_argument& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation structure: " + std::string(error.what()));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Decode animation: " + std::string(error.what()));
    }
  }

  auto encodeClip(const Clip& clip) -> Core::Result<std::string> {
    auto description = clip.description();

    if (!description) {
      return std::unexpected(description.error());
    }

    try {
      Json tracks = Json::array();

      for (const auto& track : (*description)->tracks) {
        Json keys = Json::array();

        for (const auto& key : track.keys) {
          Json value{{"time", key.time}, {"value", vectorJson(key.value)}};

          if (track.interpolation == Interpolation::CubicSpline) {
            value["inTangent"] = vectorJson(key.inTangent);
            value["outTangent"] = vectorJson(key.outTangent);
          }

          keys.push_back(std::move(value));
        }

        tracks.push_back(
            {{"node", track.node},
             {"path", track.path == Path::Translation ? "translation"
                      : track.path == Path::Rotation  ? "rotation"
                                                      : "scale"},
             {"interpolation", track.interpolation == Interpolation::Step     ? "step"
                               : track.interpolation == Interpolation::Linear ? "linear"
                                                                              : "cubic"},
             {"keys", std::move(keys)}});
      }

      auto source
          = Json{{"name", (*description)->name}, {"tracks", std::move(tracks)}}.dump() + "\n";

      if (source.size() > maxBytes) {
        return Core::fail(Core::ErrorCode::Unavailable, "Encoded animation exceeds 64 MiB");
      }

      return source;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Encode animation: " + std::string(error.what()));
    }
  }

  auto loadClip(const Core::FileSystem& files, const std::filesystem::path& path)
      -> Core::Result<Clip> {
    auto checked = pathChecked(files, path, false);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto source = files.readText(path, maxBytes);
    auto result
        = source ? decodeClip(*source) : Core::Result<Clip>{std::unexpected(source.error())};

    if (!result) {
      return Core::fail(result.error().code,
                        "Animation '" + path.string() + "': " + result.error().message);
    }

    return result;
  }

  auto saveClip(const Core::FileSystem& files, const std::filesystem::path& path, const Clip& clip)
      -> Core::Result<void> {
    auto checked = pathChecked(files, path, true);

    if (!checked) {
      return checked;
    }

    auto source = encodeClip(clip);
    return source ? files.writeTextAtomic(path, *source) : std::unexpected(source.error());
  }
}  // namespace Dot::Animation

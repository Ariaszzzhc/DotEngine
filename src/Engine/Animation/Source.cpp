module;

#include <cmath>
#include <exception>
#include <expected>
#include <limits>
#include <set>
#include <string>

#include "../Serialization/Json.hpp"

module Dot.Animation.Source;

import Dot.Core.Paths;
import Dot.Core.Text;

namespace Dot::Animation {
  auto decodePlayerProperties(std::string_view source) -> Core::Result<PlayerProperties> {
    namespace Codec = Serialization::Detail;

    if (source.size() > 4 * 1024 * 1024 || source.find('\0') != std::string_view::npos
        || !Core::isValidUtf8(source)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid animation player JSON bytes");
    }

    try {
      const auto root = Codec::parseJson(source);
      Codec::requireFields(
          root, {"clips", "targets", "clip", "speed", "looping", "playOnStart", "enabled"});

      if (!root["clips"].is_array() || root["clips"].size() > 1024 || !root["targets"].is_array()
          || root["targets"].size() > 10000 || !root["clip"].is_number_unsigned()
          || root["clip"] > 1023 || !root["speed"].is_number()) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid animation player JSON types/capacities");
      }

      PlayerProperties result{root["clips"].get<std::vector<std::string>>(),
                              {},
                              root["clip"].get<std::uint32_t>(),
                              root["speed"].get<double>(),
                              root["looping"].get<bool>(),
                              root["playOnStart"].get<bool>(),
                              root["enabled"].get<bool>()};

      for (const auto& target : root["targets"]) {
        Codec::requireFields(target, {"node", "entity"});

        if (!target["node"].is_number_unsigned() || target["node"] > 9999) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid animation node index");
        }

        result.targets.push_back(
            {target["node"].get<std::uint32_t>(), Codec::id(target["entity"])});
      }

      auto valid = validatePlayerProperties(result);
      return valid ? Core::Result<PlayerProperties>{std::move(result)}
                   : std::unexpected(valid.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Decode animation player: " + std::string(error.what()));
    }
  }

  auto encodePlayerProperties(const PlayerProperties& properties) -> Core::Result<std::string> {
    auto valid = validatePlayerProperties(properties);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    try {
      using Json = Serialization::Detail::Json;
      Json targets = Json::array();

      for (const auto& target : properties.targets) {
        targets.push_back({{"node", target.node}, {"entity", std::to_string(target.entity)}});
      }

      return Json{{"clips", properties.clips},     {"targets", std::move(targets)},
                  {"clip", properties.clip},       {"speed", properties.speed},
                  {"looping", properties.looping}, {"playOnStart", properties.playOnStart},
                  {"enabled", properties.enabled}}
          .dump();
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Encode animation player: " + std::string(error.what()));
    }
  }

  auto validatePlayerProperties(const PlayerProperties& properties) -> Core::Result<void> {
    if (properties.clips.empty() || properties.clips.size() > 1024 || properties.targets.empty()
        || properties.targets.size() > 10000 || properties.clip >= properties.clips.size()
        || !std::isfinite(properties.speed) || std::abs(properties.speed) > 16) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid animation player capacities/clock");
    }

    std::set<std::string> paths;

    for (const auto& path : properties.clips) {
      if (path.size() > 1024 || !Core::isPortableRelativePath(path) || !path.starts_with("assets/")
          || !path.ends_with(".dotanimation") || !paths.insert(path).second) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid/duplicate animation clip asset path");
      }
    }

    std::set<std::uint32_t> nodes;
    std::set<std::uint64_t> entities;

    for (const auto& target : properties.targets) {
      if (target.node >= 10000 || target.entity == 0
          || target.entity == std::numeric_limits<std::uint64_t>::max()
          || !nodes.insert(target.node).second || !entities.insert(target.entity).second) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid/duplicate animation node binding");
      }
    }

    return {};
  }
}  // namespace Dot::Animation

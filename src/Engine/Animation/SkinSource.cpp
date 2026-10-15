module;

#include <exception>
#include <expected>
#include <limits>
#include <set>
#include <string>

#include "../Serialization/Json.hpp"

module Dot.Animation.SkinSource;

import Dot.Core.Paths;
import Dot.Core.Text;

namespace Dot::Animation {
  auto validateSkinProperties(const SkinProperties& properties) -> Core::Result<void> {
    if (properties.asset.size() > 1024 || !Core::isPortableRelativePath(properties.asset)
        || !properties.asset.starts_with("assets/") || !properties.asset.ends_with(".dotskin")
        || properties.joints.empty() || properties.joints.size() > 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid skin asset/joint capacity");
    }

    std::set<std::uint64_t> unique;

    for (const auto id : properties.joints) {
      if (id == 0 || id == std::numeric_limits<std::uint64_t>::max() || !unique.insert(id).second) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid/duplicate skin joint ID");
      }
    }

    return {};
  }

  auto decodeSkinProperties(std::string_view source) -> Core::Result<SkinProperties> {
    if (source.size() > 128 * 1024 || !Core::isValidUtf8(source)
        || source.find('\0') != std::string_view::npos) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid skin description JSON bytes");
    }

    try {
      namespace Codec = Serialization::Detail;
      const auto json = Codec::parseJson(source);
      Codec::requireFields(json, {"asset", "joints", "enabled"});

      if (!json["joints"].is_array() || json["joints"].size() > 1024) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid skin joint array");
      }

      SkinProperties result{json["asset"].get<std::string>(), {}, json["enabled"].get<bool>()};

      for (const auto& id : json["joints"]) {
        result.joints.push_back(Codec::id(id));
      }

      auto valid = validateSkinProperties(result);
      return valid ? Core::Result<SkinProperties>{std::move(result)}
                   : std::unexpected(valid.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Skin description: " + std::string(error.what()));
    }
  }

  auto encodeSkinProperties(const SkinProperties& properties) -> Core::Result<std::string> {
    auto valid = validateSkinProperties(properties);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    try {
      using Json = Serialization::Detail::Json;
      Json joints = Json::array();

      for (const auto id : properties.joints) {
        joints.push_back(std::to_string(id));
      }

      return Json{{"asset", properties.asset},
                  {"joints", std::move(joints)},
                  {"enabled", properties.enabled}}
          .dump();
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Encode skin description: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Animation

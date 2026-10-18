module;

#include <algorithm>
#include <array>
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
      Codec::requireFields(root, {"clips", "targets", "clip", "speed", "looping", "playOnStart",
                                  "enabled", "layers", "controller"});

      if (!root["clips"].is_array() || root["clips"].size() > 1024 || !root["targets"].is_array()
          || root["targets"].size() > 10000 || !root["clip"].is_number_unsigned()
          || root["clip"] > 1023 || !root["speed"].is_number() || !root["layers"].is_array()
          || root["layers"].size() > 8) {
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

      if (!root["controller"].is_null()) {
        result.controller = root["controller"].get<std::string>();
      }

      for (const auto& target : root["targets"]) {
        Codec::requireFields(target, {"node", "entity"});

        if (!target["node"].is_number_unsigned() || target["node"] > 9999) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid animation node index");
        }

        result.targets.push_back(
            {target["node"].get<std::uint32_t>(), Codec::id(target["entity"])});
      }

      constexpr std::array paths{"translation", "rotation", "scale"};
      std::size_t masks{};

      for (const auto& layer : root["layers"]) {
        Codec::requireFields(layer, {"name", "clip", "mask", "weight", "speed", "looping",
                                     "playOnStart", "mode", "referenceTime"});

        if (!layer["clip"].is_number_unsigned() || layer["clip"] > 1023 || !layer["mask"].is_array()
            || layer["mask"].size() > 30000 - masks || !layer["weight"].is_number()
            || !layer["speed"].is_number() || !layer["referenceTime"].is_number()) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid animation layer JSON");
        }

        const auto mode = layer["mode"].get<std::string>();

        if (mode != "override" && mode != "additive") {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown animation layer mode");
        }

        LayerProperties value{layer["name"].get<std::string>(),
                              layer["clip"].get<std::uint32_t>(),
                              {},
                              layer["weight"].get<double>(),
                              layer["speed"].get<double>(),
                              layer["looping"].get<bool>(),
                              layer["playOnStart"].get<bool>(),
                              mode == "additive" ? LayerMode::Additive : LayerMode::Override,
                              layer["referenceTime"].get<double>()};

        for (const auto& item : layer["mask"]) {
          Codec::requireFields(item, {"node", "path", "weight"});
          const auto path = item["path"].get<std::string>();
          const auto at = std::ranges::find(paths, path);

          if (!item["node"].is_number_unsigned() || item["node"] > 9999 || at == paths.end()
              || !item["weight"].is_number()) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Invalid animation layer mask JSON");
          }

          value.mask.push_back({item["node"].get<std::uint32_t>(),
                                static_cast<Path>(at - paths.begin()),
                                item["weight"].get<double>()});
        }

        masks += value.mask.size();
        result.layers.push_back(std::move(value));
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
      Json layers = Json::array();
      constexpr std::array paths{"translation", "rotation", "scale"};

      for (const auto& target : properties.targets) {
        targets.push_back({{"node", target.node}, {"entity", std::to_string(target.entity)}});
      }

      for (const auto& layer : properties.layers) {
        Json mask = Json::array();

        for (const auto& channel : layer.mask) {
          mask.push_back({{"node", channel.node},
                          {"path", paths.at(static_cast<std::size_t>(channel.path))},
                          {"weight", channel.weight}});
        }

        layers.push_back({{"name", layer.name},
                          {"clip", layer.clip},
                          {"mask", std::move(mask)},
                          {"weight", layer.weight},
                          {"speed", layer.speed},
                          {"looping", layer.looping},
                          {"playOnStart", layer.playOnStart},
                          {"mode", layerModeName(layer.mode)},
                          {"referenceTime", layer.referenceTime}});
      }

      return Json{
          {"clips", properties.clips},
          {"targets", std::move(targets)},
          {"clip", properties.clip},
          {"speed", properties.speed},
          {"looping", properties.looping},
          {"playOnStart", properties.playOnStart},
          {"enabled", properties.enabled},
          {"layers", std::move(layers)},
          {"controller", properties.controller ? Json(*properties.controller) : Json(nullptr)}}
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

    if (properties.controller
        && (properties.controller->size() > 1024
            || !Core::isPortableRelativePath(*properties.controller)
            || !Core::isValidUtf8(*properties.controller)
            || !properties.controller->starts_with("assets/")
            || !properties.controller->ends_with(".dotcontroller"))) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid animation controller asset path");
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

    if (properties.layers.size() > 8) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation supports at most eight layers");
    }

    std::set<std::string> names;
    std::size_t masks{};

    for (const auto& layer : properties.layers) {
      if (!validLayerName(layer.name) || !names.insert(layer.name).second
          || layer.clip >= properties.clips.size() || layer.mask.empty()
          || layer.mask.size() > 30000 - masks || !std::isfinite(layer.weight) || layer.weight < 0
          || layer.weight > 1 || !std::isfinite(layer.speed) || std::abs(layer.speed) > 16
          || (layer.mode != LayerMode::Override && layer.mode != LayerMode::Additive)
          || !std::isfinite(layer.referenceTime) || layer.referenceTime < 0
          || (layer.mode == LayerMode::Override && layer.referenceTime != 0)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid animation layer properties");
      }

      std::set<std::pair<std::uint32_t, Path>> unique;

      for (const auto& channel : layer.mask) {
        if (!nodes.contains(channel.node)
            || (channel.path != Path::Translation && channel.path != Path::Rotation
                && channel.path != Path::Scale)
            || !std::isfinite(channel.weight) || channel.weight < 0 || channel.weight > 1
            || !unique.emplace(channel.node, channel.path).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid animation layer channel mask");
        }
      }

      masks += layer.mask.size();
    }

    return {};
  }
}  // namespace Dot::Animation

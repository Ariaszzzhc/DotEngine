module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

module Dot.Animation.KeyGroup;

import Dot.Core.Result;

namespace Dot::Animation {
  struct KeyGroup::Data {
    KeyGroupDescription value;
  };

  KeyGroup::KeyGroup(std::shared_ptr<const Data> data) : m_data(std::move(data)) {
  }

  auto KeyGroup::create(const Clip& source, std::span<const KeyReference> keys)
      -> Core::Result<KeyGroup> {
    auto original = source.description();

    if (!original) {
      return std::unexpected(original.error());
    }

    if (keys.empty() || keys.size() > MaxSelectedKeys) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Key group requires 1..16384 source keys");
    }

    try {
      const auto& tracks = (*original)->tracks;
      std::map<std::pair<std::uint32_t, Path>, std::size_t> channels;

      for (std::size_t i = 0; i < tracks.size(); ++i) {
        channels.emplace(std::pair{tracks[i].node, tracks[i].path}, i);
      }

      KeyGroupDescription value{{}, 86400, 0, 0};
      value.keys.reserve(keys.size());
      std::set<std::pair<std::size_t, float>> unique;
      std::set<std::size_t> selectedChannels;

      for (const auto& item : keys) {
        if (!std::isfinite(item.time) || item.time < 0 || item.time > 86400) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid key group source time");
        }

        const auto channel = channels.find({item.node, item.path});

        if (channel == channels.end()) {
          return Core::fail(Core::ErrorCode::NotFound, "Key group source channel is missing");
        }

        const auto& track = tracks[channel->second];
        const auto key = std::ranges::lower_bound(track.keys, item.time, {}, &Key::time);

        if (key == track.keys.end() || key->time != item.time) {
          return Core::fail(Core::ErrorCode::NotFound, "Key group source key is missing");
        }

        if (!unique.emplace(channel->second, key->time).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Duplicate key group source");
        }

        selectedChannels.insert(channel->second);
        value.start = std::min(value.start, key->time);
        value.end = std::max(value.end, key->time);
        value.keys.push_back({track.node, track.path, track.interpolation, *key});
      }

      value.channels = selectedChannels.size();
      return KeyGroup(std::make_shared<Data>(Data{std::move(value)}));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Copy animation keys: " + std::string(error.what()));
    }
  }

  auto KeyGroup::description() const -> Core::Result<const KeyGroupDescription*> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Key group is empty or moved");
    }

    return &m_data->value;
  }

  auto KeyGroup::at(float time) const -> Core::Result<std::vector<GroupKey>> {
    auto original = description();

    if (!original) {
      return std::unexpected(original.error());
    }

    if (!std::isfinite(time) || time < 0 || time > 86400) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Key group placement requires 0..86400s");
    }

    try {
      const auto& value = **original;
      const double offset = double(time) - value.start;
      auto placed = value.keys;
      std::set<std::tuple<std::uint32_t, Path, float>> unique;

      for (auto& item : placed) {
        const double target = double(item.key.time) + offset;

        if (target < 0 || target > 86400) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Pasted key exceeds 0..86400s");
        }

        if (offset != 0) {
          item.key.time = static_cast<float>(target);
        }

        if (!unique.emplace(item.node, item.path, item.key.time).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Pasted keys collapse to the same time");
        }
      }

      return placed;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Place animation keys: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Animation

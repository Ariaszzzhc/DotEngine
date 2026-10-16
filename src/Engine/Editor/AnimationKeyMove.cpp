module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <map>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Dot.Editor.AnimationKeyMove;

import Dot.Core.Result;

namespace Dot::Editor {
  namespace {
    auto validTime(double value) -> bool {
      return std::isfinite(value) && value >= 0 && value <= 86400;
    }
  }  // namespace

  AnimationKeyMove::AnimationKeyMove(Animation::Clip source, std::vector<SourceKey> keys)
      : m_source(std::move(source)), m_keys(std::move(keys)), m_target(m_keys.back().key.time) {
  }

  AnimationKeyMove::AnimationKeyMove(AnimationKeyMove&&) noexcept = default;

  auto AnimationKeyMove::operator=(AnimationKeyMove&& other) noexcept -> AnimationKeyMove& {
    if (this != &other) {
      m_source = std::move(other.m_source);
      m_keys = std::move(other.m_keys);
      m_target = other.m_target;
    }

    return *this;
  }

  AnimationKeyMove::~AnimationKeyMove() = default;

  auto AnimationKeyMove::begin(Animation::Clip source,
                               std::span<const Animation::KeyReference> keys)
      -> Core::Result<AnimationKeyMove> {
    auto description = source.description();

    if (!description) {
      return std::unexpected(description.error());
    }

    if (keys.empty() || keys.size() > Animation::MaxRetimeKeys) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Key move requires 1..16384 source keys");
    }

    try {
      const auto& tracks = (*description)->tracks;
      std::map<std::pair<std::uint32_t, Animation::Path>, std::size_t> channels;

      for (std::size_t i = 0; i < tracks.size(); ++i) {
        channels.emplace(std::pair{tracks[i].node, tracks[i].path}, i);
      }

      std::set<std::pair<std::size_t, float>> unique;
      std::vector<SourceKey> sources;
      sources.reserve(keys.size());

      for (const auto& item : keys) {
        if (!validTime(item.time)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid key move source time");
        }

        const auto track = channels.find({item.node, item.path});

        if (track == channels.end()) {
          return Core::fail(Core::ErrorCode::NotFound, "Key move track does not exist");
        }

        const auto& values = tracks[track->second].keys;
        const auto key = std::ranges::lower_bound(values, item.time, {}, &Animation::Key::time);

        if (key == values.end() || key->time != item.time) {
          return Core::fail(Core::ErrorCode::NotFound, "Key move source does not exist");
        }

        if (!unique.emplace(track->second, key->time).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Duplicate key move source");
        }

        sources.push_back({track->second, *key});
      }

      return AnimationKeyMove(std::move(source), std::move(sources));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Begin key move: " + std::string(error.what()));
    }
  }

  auto AnimationKeyMove::update(float time) -> Core::Result<void> {
    if (!m_source.description()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Key move is empty or moved");
    }

    if (!validTime(time)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Key move time requires 0..86400s");
    }

    const double offset = double(time) - m_keys.back().key.time;

    for (const auto& item : m_keys) {
      if (!validTime(double(item.key.time) + offset)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Translated key exceeds 0..86400s");
      }
    }

    m_target = time == m_keys.back().key.time ? m_keys.back().key.time : time;
    return {};
  }

  auto AnimationKeyMove::snapshot() const -> Core::Result<KeyMoveSnapshot> {
    auto description = m_source.description();

    if (!description) {
      return std::unexpected(description.error());
    }

    try {
      const auto& tracks = (*description)->tracks;
      const double offset = double(m_target) - m_keys.back().key.time;
      KeyMoveSnapshot result{{}, m_target, offset != 0, false};
      result.keys.reserve(m_keys.size());
      std::set<std::pair<std::size_t, float>> sources;
      std::map<std::pair<std::size_t, float>, std::size_t> destinations;

      for (const auto& item : m_keys) {
        sources.emplace(item.track, item.key.time);
      }

      for (const auto& item : m_keys) {
        const auto& track = tracks[item.track];
        auto key = item.key;

        if (offset != 0) {
          key.time = static_cast<float>(double(key.time) + offset);
        }

        const auto at = std::ranges::lower_bound(track.keys, key.time, {}, &Animation::Key::time);
        bool occupied = at != track.keys.end() && at->time == key.time
                        && !sources.contains({item.track, at->time});
        const auto [destination, inserted]
            = destinations.emplace(std::pair{item.track, key.time}, result.keys.size());

        if (!inserted) {
          occupied = true;
          result.keys[destination->second].occupied = true;
        }

        result.occupied |= occupied;
        result.keys.push_back({track.node, track.path, item.key.time, key, occupied});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Inspect key move: " + std::string(error.what()));
    }
  }

  auto AnimationKeyMove::edit() const -> Core::Result<Animation::RetimeKeys> {
    auto value = snapshot();

    if (!value) {
      return std::unexpected(value.error());
    }

    if (value->occupied) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation destination key time is occupied");
    }

    try {
      Animation::RetimeKeys result;
      result.keys.reserve(value->keys.size());

      for (const auto& item : value->keys) {
        result.keys.push_back({item.node, item.path, item.sourceTime, item.key.time});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare key move: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Editor

module;

#include <algorithm>
#include <cmath>
#include <expected>
#include <utility>

module Dot.Editor.AnimationKeyMove;

import Dot.Core.Result;

namespace Dot::Editor {
  namespace {
    auto validTime(float value) -> bool {
      return std::isfinite(value) && value >= 0 && value <= 86400;
    }
  }  // namespace

  AnimationKeyMove::AnimationKeyMove(Animation::Clip source, std::size_t track, Animation::Key key)
      : m_source(std::move(source)), m_track(track), m_key(key), m_target(key.time) {
  }

  AnimationKeyMove::AnimationKeyMove(AnimationKeyMove&&) noexcept = default;

  auto AnimationKeyMove::operator=(AnimationKeyMove&&) noexcept -> AnimationKeyMove& = default;

  AnimationKeyMove::~AnimationKeyMove() = default;

  auto AnimationKeyMove::begin(Animation::Clip source, std::uint32_t node, Animation::Path path,
                               float time) -> Core::Result<AnimationKeyMove> {
    auto description = source.description();

    if (!description) {
      return std::unexpected(description.error());
    }

    if (!validTime(time) || node >= 10000
        || (path != Animation::Path::Translation && path != Animation::Path::Rotation
            && path != Animation::Path::Scale)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid key move source/time");
    }

    const auto& tracks = (*description)->tracks;
    const auto track = std::ranges::find_if(
        tracks, [&](const auto& value) { return value.node == node && value.path == path; });

    if (track == tracks.end()) {
      return Core::fail(Core::ErrorCode::NotFound, "Key move track does not exist");
    }

    const auto key = std::ranges::lower_bound(track->keys, time, {}, &Animation::Key::time);

    if (key == track->keys.end() || key->time != time) {
      return Core::fail(Core::ErrorCode::NotFound, "Key move source does not exist");
    }

    const auto index = static_cast<std::size_t>(track - tracks.begin());
    return AnimationKeyMove(std::move(source), index, *key);
  }

  auto AnimationKeyMove::update(float time) -> Core::Result<void> {
    if (!m_source.description()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Key move is empty or moved");
    }

    if (!validTime(time)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Key move time requires 0..86400s");
    }

    m_target = time == m_key.time ? m_key.time : time;
    return {};
  }

  auto AnimationKeyMove::snapshot() const -> Core::Result<KeyMoveSnapshot> {
    auto description = m_source.description();

    if (!description) {
      return std::unexpected(description.error());
    }

    const auto& track = (*description)->tracks[m_track];
    const auto key = std::ranges::lower_bound(track.keys, m_target, {}, &Animation::Key::time);
    auto moved = m_key;
    moved.time = m_target;
    const bool changed = m_target != m_key.time;
    return KeyMoveSnapshot{track.node, track.path,
                           m_key.time, moved,
                           changed,    changed && key != track.keys.end() && key->time == m_target};
  }

  auto AnimationKeyMove::edit() const -> Core::Result<Animation::UpdateKey> {
    auto value = snapshot();

    if (!value) {
      return std::unexpected(value.error());
    }

    if (value->occupied) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Animation destination key time is occupied");
    }

    return Animation::UpdateKey{value->node, value->path, value->sourceTime, value->key};
  }
}  // namespace Dot::Editor

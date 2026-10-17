module;

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <utility>

module Dot.Editor.AnimationTangent;

import Dot.Math;

namespace Dot::Editor {
  namespace {
    auto component(Math::Vec4 value, std::size_t index) -> float {
      return std::array{value.x, value.y, value.z, value.w}.at(index);
    }
  }  // namespace

  AnimationTangent::AnimationTangent(Animation::Clip source, TangentSnapshot value)
      : m_source(std::move(source)), m_value(value) {
  }

  AnimationTangent::AnimationTangent(AnimationTangent&&) noexcept = default;

  auto AnimationTangent::operator=(AnimationTangent&&) noexcept -> AnimationTangent& = default;

  AnimationTangent::~AnimationTangent() = default;

  auto AnimationTangent::begin(Animation::Clip source, Animation::KeyReference reference,
                               std::size_t index, bool incoming) -> Core::Result<AnimationTangent> {
    auto description = source.description();

    if (!description) {
      return std::unexpected(description.error());
    }

    if (!std::isfinite(reference.time) || reference.time < 0 || reference.time > 86400
        || index >= (reference.path == Animation::Path::Rotation ? 4U : 3U)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid tangent key/component");
    }

    const auto& tracks = (*description)->tracks;
    const auto track = std::ranges::find_if(tracks, [&](const auto& item) {
      return item.node == reference.node && item.path == reference.path;
    });

    if (track == tracks.end() || track->interpolation != Animation::Interpolation::CubicSpline) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Tangent requires an existing cubic track");
    }

    const auto key
        = std::ranges::lower_bound(track->keys, reference.time, {}, &Animation::Key::time);

    if (key == track->keys.end() || key->time != reference.time) {
      return Core::fail(Core::ErrorCode::NotFound, "Tangent source key does not exist");
    }

    if ((incoming && key == track->keys.begin()) || (!incoming && key + 1 == track->keys.end())) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Tangent side has no adjacent segment");
    }

    const double offset = (double((incoming ? key - 1 : key + 1)->time) - key->time) / 3;
    const float slope = component(incoming ? key->inTangent : key->outTangent, index);
    return AnimationTangent(
        std::move(source),
        {reference, *key, index, incoming, slope, slope, offset, double(key->time) + offset,
         double(component(key->value, index)) + offset * slope, false});
  }

  auto AnimationTangent::update(float slope) -> Core::Result<void> {
    if (!m_source.description()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Tangent is empty or moved");
    }

    if (!std::isfinite(slope)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Tangent slope must be finite");
    }

    m_value.slope = slope == m_value.original ? m_value.original : slope;
    auto& tangent = m_value.incoming ? m_value.key.inTangent : m_value.key.outTangent;
    const std::array components{&tangent.x, &tangent.y, &tangent.z, &tangent.w};
    *components[m_value.component] = m_value.slope;
    m_value.changed = m_value.slope != m_value.original;
    m_value.handleValue
        = double(component(m_value.key.value, m_value.component)) + m_value.offset * m_value.slope;
    return {};
  }

  auto AnimationTangent::snapshot() const -> Core::Result<TangentSnapshot> {
    if (!m_source.description()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Tangent is empty or moved");
    }

    return m_value;
  }

  auto AnimationTangent::value() const -> Core::Result<Animation::Clip> {
    auto state = snapshot();

    if (!state) {
      return std::unexpected(state.error());
    }

    if (!state->changed) {
      return m_source;
    }

    const std::array<Animation::ClipEdit, 1> edits{Animation::UpdateKey{
        state->source.node, state->source.path, state->source.time, state->key}};
    return Animation::editClip(m_source, edits);
  }
}  // namespace Dot::Editor

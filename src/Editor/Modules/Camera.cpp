module;

#include <algorithm>
#include <cmath>
#include <expected>
#include <numbers>

module Dot.Editor.Camera;

namespace Dot::Editor {
  namespace {
    auto basis(const CameraState& state) -> Core::Result<Math::Mat4> {
      auto rotation = Math::rotationEulerDegrees({state.pitch, state.yaw, 0});
      return rotation ? Math::compose({}, *rotation) : std::unexpected(rotation.error());
    }
  }  // namespace

  auto Camera::state() const noexcept -> CameraState {
    return m_state;
  }

  auto Camera::setState(const CameraState& state) -> Core::Result<void> {
    if (!Math::isFinite(state.position) || !Math::isFinite(state.pivot) || !std::isfinite(state.yaw)
        || std::abs(state.yaw) > 180 || !std::isfinite(state.pitch) || std::abs(state.pitch) > 89
        || !std::isfinite(state.speed) || state.speed < 0.01F || state.speed > 1000
        || !std::isfinite(state.orthographicHeight) || state.orthographicHeight < 0.01F
        || state.orthographicHeight > 1e5) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid editor camera state");
    }

    for (const auto point : {state.position, state.pivot}) {
      if (std::abs(point.x) > 1e6 || std::abs(point.y) > 1e6 || std::abs(point.z) > 1e6) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Editor camera exceeds world range");
      }
    }

    auto rotation = Math::rotationEulerDegrees({state.pitch, state.yaw, 0});
    auto lens
        = Graphics::validateView(Graphics::View{.position = state.position,
                                                .rotation = *rotation,
                                                .nearPlane = state.nearPlane,
                                                .farPlane = state.farPlane,
                                                .orthographic = state.orthographic,
                                                .orthographicHeight = state.orthographicHeight});

    if (!lens) {
      return lens;
    }

    m_state = state;
    return {};
  }

  auto Camera::view() const -> Core::Result<Graphics::View> {
    auto rotation = Math::rotationEulerDegrees({m_state.pitch, m_state.yaw, 0});

    if (!rotation) {
      return std::unexpected(rotation.error());
    }

    return Graphics::View{.position = m_state.position,
                          .rotation = *rotation,
                          .nearPlane = m_state.nearPlane,
                          .farPlane = m_state.farPlane,
                          .orthographic = m_state.orthographic,
                          .orthographicHeight = m_state.orthographicHeight};
  }

  auto Camera::turn(Math::Vec2 degrees, bool orbit) -> Core::Result<void> {
    if (!std::isfinite(degrees.x) || !std::isfinite(degrees.y) || std::abs(degrees.x) > 360
        || std::abs(degrees.y) > 360) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid editor camera turn");
    }

    auto next = m_state;
    next.yaw = std::remainder(next.yaw + degrees.x, 360.F);
    next.pitch = std::clamp(next.pitch + degrees.y, -89.F, 89.F);

    if (orbit) {
      const float distance
          = static_cast<float>(std::max(0.05, Math::length(next.position - next.pivot)));
      auto axes = basis(next);

      if (!axes) {
        return std::unexpected(axes.error());
      }

      next.position = next.pivot + Math::transformVector(*axes, {0, 0, distance}).value();
    }

    return setState(next);
  }

  auto Camera::move(Math::Vec3 direction, double seconds, bool fast) -> Core::Result<void> {
    if (!Math::isFinite(direction) || std::abs(direction.x) > 1 || std::abs(direction.y) > 1
        || std::abs(direction.z) > 1 || !std::isfinite(seconds) || seconds < 0 || seconds > 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid editor camera motion");
    }

    const auto magnitude = Math::length(direction);

    if (magnitude == 0 || seconds == 0) {
      return {};
    }

    auto axes = basis(m_state);

    if (!axes) {
      return std::unexpected(axes.error());
    }

    const float scale
        = static_cast<float>(seconds * m_state.speed * (fast ? 4 : 1) / std::max(1.0, magnitude));
    auto delta = Math::transformVector(
        *axes, {direction.x * scale, direction.y * scale, -direction.z * scale});

    if (!delta) {
      return std::unexpected(delta.error());
    }

    auto next = m_state;
    next.position = next.position + *delta;
    next.pivot = next.pivot + *delta;
    return setState(next);
  }

  auto Camera::pan(Math::Vec2 viewportDelta, float aspect) -> Core::Result<void> {
    if (!std::isfinite(viewportDelta.x) || !std::isfinite(viewportDelta.y)
        || std::abs(viewportDelta.x) > 2 || std::abs(viewportDelta.y) > 2 || !std::isfinite(aspect)
        || aspect < 0.01F || aspect > 100) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid editor viewport pan");
    }

    auto axes = basis(m_state);

    if (!axes) {
      return std::unexpected(axes.error());
    }

    const float height
        = m_state.orthographic
              ? m_state.orthographicHeight
              : static_cast<float>(2
                                   * std::max(0.05, Math::length(m_state.position - m_state.pivot))
                                   * std::tan(std::numbers::pi / 6));
    auto delta = Math::transformVector(
        *axes, {-viewportDelta.x * height * aspect, viewportDelta.y * height, 0});

    if (!delta) {
      return std::unexpected(delta.error());
    }

    auto next = m_state;
    next.position = next.position + *delta;
    next.pivot = next.pivot + *delta;
    return setState(next);
  }

  auto Camera::dolly(float steps) -> Core::Result<void> {
    if (!std::isfinite(steps) || std::abs(steps) > 20) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid editor camera dolly");
    }

    auto next = m_state;
    const float factor = std::pow(0.85F, steps);

    if (next.orthographic) {
      next.orthographicHeight = std::clamp(next.orthographicHeight * factor, 0.01F, 1e5F);
    } else {
      const auto delta = next.position - next.pivot;
      auto direction = Math::normalized(delta);

      if (!direction) {
        return std::unexpected(direction.error());
      }

      const float distance
          = std::clamp(static_cast<float>(Math::length(delta)) * factor, 0.05F, 1e5F);
      next.position = next.pivot + *direction * distance;
    }

    return setState(next);
  }

  auto Camera::focus(const Graphics::Bounds& bounds, float aspect) -> Core::Result<void> {
    auto checked = Graphics::transformedBounds(bounds, {});

    if (!checked || !std::isfinite(aspect) || aspect < 0.01F || aspect > 100) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid editor focus bounds/aspect");
    }

    const auto center = (bounds.minimum + bounds.maximum) * 0.5F;
    const double radius = std::max(0.25, Math::length(bounds.maximum - center));
    const double angle
        = std::min(std::numbers::pi / 6, std::atan(std::tan(std::numbers::pi / 6) * aspect));
    const float distance = static_cast<float>(radius * 1.25 / std::sin(angle));
    auto axes = basis(m_state);

    if (!axes) {
      return std::unexpected(axes.error());
    }

    auto next = m_state;
    next.pivot = center;
    next.position = center + Math::transformVector(*axes, {0, 0, distance}).value();
    next.orthographicHeight = static_cast<float>(radius * 2.5 / std::min(1.F, aspect));
    next.farPlane = static_cast<float>(std::max(10000.0, distance + radius * 2));
    next.nearPlane = std::max(0.05F, next.farPlane / 500000.F);
    return setState(next);
  }
}  // namespace Dot::Editor

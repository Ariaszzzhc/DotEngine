module;

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <optional>

module Dot.Graphics.Overlay;

import Dot.Core.Result;
import Dot.Math;

namespace Dot::Graphics {
  namespace {
    using Clip = std::array<double, 4>;
    constexpr double minW = 1e-12;

    auto clip(const Math::Mat4& matrix, Math::Vec3 point) -> Clip {
      Clip result{};

      for (std::size_t row = 0; row < 4; ++row) {
        result[row] = static_cast<double>(matrix.data[row]) * point.x
                      + static_cast<double>(matrix.data[row + 4]) * point.y
                      + static_cast<double>(matrix.data[row + 8]) * point.z + matrix.data[row + 12];
      }

      return result;
    }

    auto planes(const Clip& value) -> std::array<double, 7> {
      return {value[3] + value[0],
              value[3] - value[0],
              value[3] + value[1],
              value[3] - value[1],
              value[2],
              value[3] - value[2],
              value[3] - minW};
    }

    auto uv(const Clip& value) -> Math::Vec2 {
      return {static_cast<float>((std::clamp(value[0] / value[3], -1.0, 1.0) + 1) * .5),
              static_cast<float>((1 - std::clamp(value[1] / value[3], -1.0, 1.0)) * .5)};
    }

    auto valid(Math::Vec2 value) -> bool {
      return std::isfinite(value.x) && std::isfinite(value.y) && value.x >= 0 && value.x <= 1
             && value.y >= 0 && value.y <= 1;
    }

    auto color(Math::Vec4 value) -> bool {
      return Math::isFinite(value) && value.x >= 0 && value.x <= 1 && value.y >= 0 && value.y <= 1
             && value.z >= 0 && value.z <= 1 && value.w >= 0 && value.w <= 1;
    }

    auto size(float value) -> bool {
      return std::isfinite(value) && value >= .5F && value <= 16;
    }
  }  // namespace

  auto validateOverlay(const ViewportOverlay& overlay) -> Core::Result<void> {
    if (overlay.lines.size() > 4096 || overlay.points.size() > 4096) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Viewport overlay exceeds geometry limits");
    }

    for (const auto& line : overlay.lines) {
      if (!valid(line.segment.from) || !valid(line.segment.to) || !color(line.color)
          || !size(line.thickness)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid viewport overlay line");
      }
    }

    for (const auto& point : overlay.points) {
      if (!valid(point.position) || !color(point.color) || !size(point.radius)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid viewport overlay point");
      }
    }

    return {};
  }

  OverlayProjector::OverlayProjector(Math::Mat4 viewProjection) noexcept
      : m_viewProjection(viewProjection) {
  }

  auto OverlayProjector::create(const Math::Mat4& viewProjection)
      -> Core::Result<OverlayProjector> {
    auto inverse = Math::inverse(viewProjection);
    return inverse ? Core::Result<OverlayProjector>{OverlayProjector(viewProjection)}
                   : std::unexpected(inverse.error());
  }

  auto OverlayProjector::point(Math::Vec3 world) const -> Core::Result<std::optional<Math::Vec2>> {
    if (!Math::isFinite(world)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Overlay point must be finite");
    }

    const auto value = clip(m_viewProjection, world);

    for (auto distance : planes(value)) {
      if (distance < 0) {
        return std::optional<Math::Vec2>{};
      }
    }

    return std::optional{uv(value)};
  }

  auto OverlayProjector::segment(Math::Vec3 from, Math::Vec3 to) const
      -> Core::Result<std::optional<OverlaySegment>> {
    if (!Math::isFinite(from) || !Math::isFinite(to)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Overlay segment must be finite");
    }

    const auto first = clip(m_viewProjection, from);
    const auto last = clip(m_viewProjection, to);
    const auto a = planes(first);
    const auto b = planes(last);
    double enter = 0;
    double leave = 1;

    for (std::size_t i = 0; i < a.size(); ++i) {
      if (a[i] < 0 && b[i] < 0) {
        return std::optional<OverlaySegment>{};
      }

      if ((a[i] < 0) != (b[i] < 0)) {
        const auto t = a[i] / (a[i] - b[i]);

        if (a[i] < 0) {
          enter = std::max(enter, t);
        } else {
          leave = std::min(leave, t);
        }
      }
    }

    if (enter > leave) {
      return std::optional<OverlaySegment>{};
    }

    Clip start{}, end{};

    for (std::size_t i = 0; i < 4; ++i) {
      start[i] = first[i] + (last[i] - first[i]) * enter;
      end[i] = first[i] + (last[i] - first[i]) * leave;
    }

    if (start[3] <= 0 || end[3] <= 0) {
      return std::optional<OverlaySegment>{};
    }

    return std::optional{OverlaySegment{uv(start), uv(end)}};
  }
}  // namespace Dot::Graphics

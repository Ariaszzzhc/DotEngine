module;

#include <algorithm>
#include <cmath>
#include <expected>

module Dot.Graphics.View;

namespace Dot::Graphics {
  auto validateView(const View& view, float aspect) -> Core::Result<void> {
    if (!Math::isFinite(view.position) || std::abs(view.position.x) > 1e6
        || std::abs(view.position.y) > 1e6 || std::abs(view.position.z) > 1e6
        || !Math::normalized(view.rotation) || !std::isfinite(aspect) || aspect < 0.01F
        || aspect > 100 || !std::isfinite(view.nearPlane) || view.nearPlane < 0.001F
        || !std::isfinite(view.farPlane) || view.farPlane > 1e6 || view.nearPlane >= view.farPlane
        || static_cast<double>(view.farPlane) / view.nearPlane > 1e6
        || !std::isfinite(view.verticalFovDegrees) || view.verticalFovDegrees < 1
        || view.verticalFovDegrees > 170 || !std::isfinite(view.orthographicHeight)
        || view.orthographicHeight < 0.01F || view.orthographicHeight > 1e5) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid independent view pose/lens/aspect");
    }

    return {};
  }

  auto viewProjection(const View& view, float aspect) -> Core::Result<Math::Mat4> {
    auto checked = validateView(view, aspect);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    const float half = view.orthographicHeight * 0.5F;
    auto projection = view.orthographic ? Math::orthographic(-half * aspect, half * aspect, -half,
                                                             half, view.nearPlane, view.farPlane)
                                        : Math::perspective(view.verticalFovDegrees, aspect,
                                                            view.nearPlane, view.farPlane);
    auto world = Math::compose(view.position, view.rotation);

    if (!projection || !world) {
      return std::unexpected(!projection ? projection.error() : world.error());
    }

    auto inverse = Math::inverse(*world);
    return inverse ? Math::multiply(*projection, *inverse) : std::unexpected(inverse.error());
  }

  auto pixelProjection(const Math::Mat4& viewProjection, std::uint32_t width, std::uint32_t height,
                       Math::Vec2 uv) -> Core::Result<Math::Mat4> {
    if (!Math::isFinite(viewProjection) || !width || !height || width > 4096 || height > 4096
        || !std::isfinite(uv.x) || !std::isfinite(uv.y) || uv.x < 0 || uv.x >= 1 || uv.y < 0
        || uv.y >= 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid framebuffer pixel projection");
    }

    auto inverse = Math::inverse(viewProjection);

    if (!inverse) {
      return std::unexpected(inverse.error());
    }

    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float x = (std::floor(uv.x * w) + 0.5F) * 2 - w;
    const float y = h - (std::floor(uv.y * h) + 0.5F) * 2;
    Math::Mat4 crop;
    crop.data[0] = w;
    crop.data[5] = h;
    crop.data[12] = -x;
    crop.data[13] = -y;
    return Math::multiply(crop, viewProjection);
  }

  auto transformedBounds(const Bounds& bounds, const Math::Mat4& transform)
      -> Core::Result<Bounds> {
    const auto& m = transform.data;

    if (!Math::isFinite(bounds.minimum) || !Math::isFinite(bounds.maximum)
        || bounds.minimum.x > bounds.maximum.x || bounds.minimum.y > bounds.maximum.y
        || bounds.minimum.z > bounds.maximum.z || !Math::isFinite(transform) || m[3] != 0
        || m[7] != 0 || m[11] != 0 || m[15] != 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid affine bounds transform");
    }

    Bounds result;

    for (int index = 0; index < 8; ++index) {
      auto point
          = Math::transformPoint(transform, {index & 1 ? bounds.maximum.x : bounds.minimum.x,
                                             index & 2 ? bounds.maximum.y : bounds.minimum.y,
                                             index & 4 ? bounds.maximum.z : bounds.minimum.z});

      if (!point || std::abs(point->x) > 1e6 || std::abs(point->y) > 1e6
          || std::abs(point->z) > 1e6) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Transformed bounds exceed world range");
      }

      if (index == 0) {
        result = {*point, *point};
      } else {
        result.minimum
            = {std::min(result.minimum.x, point->x), std::min(result.minimum.y, point->y),
               std::min(result.minimum.z, point->z)};
        result.maximum
            = {std::max(result.maximum.x, point->x), std::max(result.maximum.y, point->y),
               std::max(result.maximum.z, point->z)};
      }
    }

    return result;
  }
}  // namespace Dot::Graphics

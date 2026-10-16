module;

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <expected>
#include <limits>
#include <optional>
#include <span>

module Dot.Graphics.Shadows;

namespace Dot::Graphics {
  namespace {
    auto bounded(Math::Vec3 value) -> bool {
      return Math::isFinite(value) && std::abs(value.x) <= 1e6 && std::abs(value.y) <= 1e6
             && std::abs(value.z) <= 1e6;
    }

    auto corner(const Bounds& bounds, std::uint32_t index) -> Math::Vec3 {
      return {index & 1 ? bounds.maximum.x : bounds.minimum.x,
              index & 2 ? bounds.maximum.y : bounds.minimum.y,
              index & 4 ? bounds.maximum.z : bounds.minimum.z};
    }

    auto localProjection(Math::Vec3 position, std::span<const Math::Vec3> directions, float fov,
                         float range, const ShadowSettings& settings)
        -> Core::Result<ShadowProjection> {
      auto checked = validateShadowSettings(settings);

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (!bounded(position) || !std::isfinite(range) || range < .01F || range > 1e6) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid local shadow position/range");
      }

      const auto far = std::min(range, settings.distance);
      const auto near = std::min(.05F, far * .01F);
      auto projection = Math::perspective(fov, 1, near, far);

      if (!projection) {
        return std::unexpected(projection.error());
      }

      ShadowProjection result{};
      result.origin = position;
      result.nearDepth = near;
      result.splits[0] = far;
      result.count = static_cast<std::uint32_t>(directions.size());
      result.forward = directions[0];

      for (std::size_t index = 0; index < directions.size(); ++index) {
        const auto direction = directions[index];
        auto rotation = Math::lookAt(
            {}, direction,
            std::abs(direction.y) > .99F ? Math::Vec3{0, 0, 1} : Math::Vec3{0, 1, 0});

        if (!rotation) {
          return std::unexpected(rotation.error());
        }

        Math::Mat4 translation;
        translation.data[12] = -position.x;
        translation.data[13] = -position.y;
        translation.data[14] = -position.z;
        auto view = Math::multiply(*rotation, translation);

        if (!view) {
          return std::unexpected(view.error());
        }

        auto matrix = Math::multiply(*projection, *view);

        if (!matrix) {
          return std::unexpected(matrix.error());
        }

        result.matrices[index] = *matrix;
      }

      return result;
    }
  }  // namespace

  auto validateShadowSettings(const ShadowSettings& s) -> Core::Result<void> {
    const auto range = [](float value, float low, float high) {
      return std::isfinite(value) && value >= low && value <= high;
    };

    if (!std::has_single_bit(s.resolution) || s.resolution < 128 || s.resolution > 2048
        || s.cascades < 1 || s.cascades > 4 || !range(s.distance, 0.1F, 10000)
        || !range(s.splitLambda, 0, 1) || !range(s.depthBias, 0, 0.02F)
        || !range(s.slopeBias, 0, 0.02F) || !range(s.normalBias, 0, 0.2F)
        || !range(s.filterRadius, 0, 8)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid shadow settings");
    }

    return {};
  }

  auto fitPointShadows(Math::Vec3 position, float range, const ShadowSettings& settings)
      -> Core::Result<ShadowProjection> {
    const std::array<Math::Vec3, 6> directions{
        {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    auto result = localProjection(position, directions, 90, range, settings);

    if (result) {
      result->kind = ShadowProjectionKind::Point;
    }

    return result;
  }

  auto fitSpotShadow(Math::Vec3 position, Math::Vec3 direction, float outerConeDegrees, float range,
                     const ShadowSettings& settings) -> Core::Result<ShadowProjection> {
    auto ray = Math::normalized(direction);

    if (!ray || !std::isfinite(outerConeDegrees) || outerConeDegrees <= 0
        || outerConeDegrees >= 90) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid shadow spot direction/cone");
    }

    const std::array directions{*ray};
    auto result = localProjection(position, directions, outerConeDegrees * 2, range, settings);

    if (result) {
      result->kind = ShadowProjectionKind::Spot;
    }

    return result;
  }

  auto fitDirectionalShadows(const Math::Mat4& viewProjection, Math::Vec3 camera,
                             Math::Vec3 lightDirection, const ShadowSettings& settings,
                             std::span<const Bounds> casters) -> Core::Result<ShadowProjection> {
    auto checked = validateShadowSettings(settings);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!bounded(camera) || casters.size() > 10000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid shadow camera/caster count");
    }

    for (const auto& bounds : casters) {
      if (!bounded(bounds.minimum) || !bounded(bounds.maximum)
          || bounds.minimum.x > bounds.maximum.x || bounds.minimum.y > bounds.maximum.y
          || bounds.minimum.z > bounds.maximum.z) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid shadow caster bounds");
      }
    }

    auto inverse = Math::inverse(viewProjection);
    auto direction = Math::normalized(lightDirection);

    if (!inverse || !direction) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid shadow camera/light matrix");
    }

    std::array<Math::Vec3, 8> frustum;
    Math::Vec3 nearCenter;
    Math::Vec3 farCenter;
    std::optional<bool> negativeWeight;

    for (std::uint32_t index = 0; index < 8; ++index) {
      const auto& m = inverse->data;
      const auto weight = static_cast<double>(m[3]) * (index & 1 ? 1 : -1)
                          + static_cast<double>(m[7]) * (index & 2 ? 1 : -1)
                          + static_cast<double>(m[11]) * (index & 4 ? 1 : 0) + m[15];

      if (negativeWeight && *negativeWeight != std::signbit(weight)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Shadow frustum crosses a homogeneous horizon");
      }

      negativeWeight = std::signbit(weight);
      auto point = Math::transformPoint(
          *inverse, {index & 1 ? 1.0F : -1.0F, index & 2 ? 1.0F : -1.0F, index & 4 ? 1.0F : 0.0F});

      if (!point || !bounded(*point)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid finite shadow camera frustum");
      }

      frustum[index] = *point;

      if (index < 4) {
        nearCenter = nearCenter + *point * 0.25F;
      } else {
        farCenter = farCenter + *point * 0.25F;
      }
    }

    auto forward = Math::normalized(farCenter - nearCenter);

    if (!forward) {
      return std::unexpected(forward.error());
    }

    const auto near = static_cast<float>(Math::dot(nearCenter - camera, *forward));
    const auto far = static_cast<float>(Math::dot(farCenter - camera, *forward));
    const auto end = std::min(far, settings.distance);

    if (!std::isfinite(near) || !std::isfinite(far) || near <= 0 || far <= near || end <= near) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Shadow camera depth interval is invalid");
    }

    const auto up = std::abs(direction->y) > 0.99F ? Math::Vec3{1, 0, 0} : Math::Vec3{0, 1, 0};
    const auto rotation = Math::lookAt({}, *direction, up).value();
    ShadowProjection result{{}, {}, {}, camera, *forward, settings.cascades, near};

    for (std::uint32_t cascade = 0; cascade < settings.cascades; ++cascade) {
      const auto fraction = static_cast<float>(cascade + 1) / static_cast<float>(settings.cascades);
      const auto logarithmic = near * std::pow(end / near, fraction);
      const auto linear = near + (end - near) * fraction;
      const auto split = settings.splitLambda * logarithmic + (1 - settings.splitLambda) * linear;
      result.splits[cascade] = cascade + 1 == settings.cascades ? end : split;
      const auto previous = cascade == 0 ? near : result.splits[cascade - 1];
      const auto beforePrevious = cascade < 2 ? near : result.splits[cascade - 2];
      const auto begin = previous - (cascade == 0 ? 0 : (previous - beforePrevious) * 0.1F);
      std::array<Math::Vec3, 8> corners;
      Math::Vec3 center;

      for (std::uint32_t index = 0; index < 8; ++index) {
        const auto depth = index < 4 ? begin : result.splits[cascade];
        const auto factor = (depth - near) / (far - near);
        corners[index]
            = frustum[index % 4] + (frustum[index % 4 + 4] - frustum[index % 4]) * factor;
        center = center + corners[index] * 0.125F;
      }

      double radius{};

      for (auto point : corners) {
        radius = std::max(radius, Math::length(point - center));
      }

      const auto extent = static_cast<float>(std::ceil(radius * 16) / 16)
                          * static_cast<float>(settings.resolution)
                          / static_cast<float>(settings.resolution - 2);

      if (extent <= 0 || !std::isfinite(extent)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Shadow cascade extent is invalid");
      }

      const auto texel = 2 * extent / static_cast<float>(settings.resolution);
      result.texelSize[cascade] = texel;
      auto lightCenter = Math::transformPoint(rotation, center).value();
      lightCenter.x = std::round(lightCenter.x / texel) * texel;
      lightCenter.y = std::round(lightCenter.y / texel) * texel;
      auto minimumZ = std::numeric_limits<float>::max();
      auto maximumZ = std::numeric_limits<float>::lowest();

      for (const auto& point : corners) {
        const auto z = Math::transformPoint(rotation, point).value().z;
        minimumZ = std::min(minimumZ, z);
        maximumZ = std::max(maximumZ, z);
      }

      for (const auto& bounds : casters) {
        auto low = Math::Vec3{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                              std::numeric_limits<float>::max()};
        auto high = low * -1;

        for (std::uint32_t index = 0; index < 8; ++index) {
          const auto point = Math::transformPoint(rotation, corner(bounds, index)).value();
          low = {std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
          high = {std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z)};
        }

        if (high.x >= lightCenter.x - extent && low.x <= lightCenter.x + extent
            && high.y >= lightCenter.y - extent && low.y <= lightCenter.y + extent) {
          minimumZ = std::min(minimumZ, low.z);
          maximumZ = std::max(maximumZ, high.z);
        }
      }

      const auto padding = std::max(0.1F, (maximumZ - minimumZ) * 0.01F);
      auto view = rotation;
      view.data[12] = -lightCenter.x;
      view.data[13] = -lightCenter.y;
      view.data[14] = -(maximumZ + padding);
      auto projection = Math::orthographic(-extent, extent, -extent, extent, 0,
                                           maximumZ - minimumZ + padding * 2);

      if (!projection) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Shadow projection exceeds precision");
      }

      auto matrix = Math::multiply(*projection, view);

      if (!matrix) {
        return std::unexpected(matrix.error());
      }

      result.matrices[cascade] = *matrix;
    }

    return result;
  }
}  // namespace Dot::Graphics

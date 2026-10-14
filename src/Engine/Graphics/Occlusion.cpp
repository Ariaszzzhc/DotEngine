module;

#include <algorithm>
#include <bit>
#include <cmath>
#include <expected>
#include <optional>

module Dot.Graphics.Occlusion;

namespace Dot::Graphics {
  auto validateOcclusion(const OcclusionSettings& settings) -> Core::Result<void> {
    if (!std::isfinite(settings.radius) || settings.radius < 0.05F || settings.radius > 5
        || !std::isfinite(settings.bias) || settings.bias < 0 || settings.bias > 0.2F
        || !std::isfinite(settings.strength) || settings.strength < 0 || settings.strength > 4
        || !std::has_single_bit(settings.directions) || settings.directions < 4
        || settings.directions > 16 || settings.steps < 2 || settings.steps > 8
        || settings.filterRadius > 4) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid ambient occlusion settings");
    }

    return {};
  }

  auto reconstructPosition(const OcclusionCamera& camera, Math::Vec2 uv, float depth)
      -> Core::Result<Math::Vec3> {
    if (!std::isfinite(uv.x) || !std::isfinite(uv.y) || uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1
        || !std::isfinite(depth) || depth < 0 || depth > 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid occlusion UV/depth");
    }

    return Math::transformPoint(camera.inverse, {uv.x * 2 - 1, 1 - uv.y * 2, depth});
  }

  auto occlusionCamera(const Math::Mat4& viewProjection) -> Core::Result<OcclusionCamera> {
    auto inverse = Math::inverse(viewProjection);

    if (!inverse) {
      return std::unexpected(inverse.error());
    }

    std::optional<bool> negativeWeight;

    for (int index = 0; index < 8; ++index) {
      const auto x = index & 1 ? 1.0F : -1.0F;
      const auto y = index & 2 ? 1.0F : -1.0F;
      const auto z = index & 4 ? 1.0F : 0.0F;
      const auto& m = inverse->data;
      const auto weight = static_cast<double>(m[3]) * x + static_cast<double>(m[7]) * y
                          + static_cast<double>(m[11]) * z + m[15];
      auto point = Math::transformPoint(*inverse, {x, y, z});

      if (!point || !Math::isFinite(*point) || std::abs(point->x) > 1e6 || std::abs(point->y) > 1e6
          || std::abs(point->z) > 1e6
          || (negativeWeight && *negativeWeight != std::signbit(weight))) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid finite occlusion frustum");
      }

      negativeWeight = std::signbit(weight);
    }

    const auto center = Math::transformPoint(*inverse, {0, 0, 0.5F}).value();
    auto right = Math::normalized(Math::transformPoint(*inverse, {0.5F, 0, 0.5F}).value() - center);
    auto up = Math::normalized(Math::transformPoint(*inverse, {0, 0.5F, 0.5F}).value() - center);

    if (!right || !up || std::abs(Math::dot(*right, *up)) > 0.001) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid orthogonal occlusion image axes");
    }

    return OcclusionCamera{viewProjection, *inverse, *right, *up};
  }

  auto occlusionBytes(std::uint32_t width, std::uint32_t height) -> Core::Result<std::uint64_t> {
    const auto bytes = static_cast<std::uint64_t>(width) * height * 24;

    if (!width || !height || width > 4096 || height > 4096 || bytes > 256 * 1024 * 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Occlusion dimensions exceed 4096/256 MiB working budget");
    }

    return bytes;
  }
}  // namespace Dot::Graphics

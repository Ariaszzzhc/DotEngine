module;

#include <algorithm>
#include <cmath>
#include <expected>
#include <limits>
#include <string>

module Dot.Graphics.Frustum;

namespace Dot::Graphics {
  auto Frustum::create(const Math::Mat4& viewProjection) -> Core::Result<Frustum> {
    auto inverse = Math::inverse(viewProjection);

    if (!inverse) {
      return Core::fail(inverse.error().code, "Invalid frustum camera: " + inverse.error().message);
    }

    Frustum result;
    const auto& m = viewProjection.data;

    for (std::size_t column = 0; column < 4; ++column) {
      const double w = m[column * 4 + 3];
      const double x = m[column * 4];
      const double y = m[column * 4 + 1];
      const double z = m[column * 4 + 2];
      result.m_planes[0][column] = w + x;
      result.m_planes[1][column] = w - x;
      result.m_planes[2][column] = w + y;
      result.m_planes[3][column] = w - y;
      result.m_planes[4][column] = z;
      result.m_planes[5][column] = w - z;
    }

    for (auto& plane : result.m_planes) {
      const double maximum = std::max({std::abs(plane[0]), std::abs(plane[1]), std::abs(plane[2])});

      if (maximum == 0) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Frustum has a degenerate clip plane");
      }

      for (auto& component : plane) {
        component /= maximum;
      }
    }

    return result;
  }

  auto Frustum::intersects(const Bounds& local, const Math::Mat4& world) const
      -> Core::Result<bool> {
    if (!Math::isFinite(local.minimum) || !Math::isFinite(local.maximum) || !Math::isFinite(world)
        || local.minimum.x > local.maximum.x || local.minimum.y > local.maximum.y
        || local.minimum.z > local.maximum.z) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid frustum bounds/model");
    }

    std::array<std::array<double, 4>, 8> corners{};

    for (std::size_t index = 0; index < corners.size(); ++index) {
      const double p[4]{index & 1 ? local.maximum.x : local.minimum.x,
                        index & 2 ? local.maximum.y : local.minimum.y,
                        index & 4 ? local.maximum.z : local.minimum.z, 1};

      for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
          corners[index][row] += static_cast<double>(world.data[column * 4 + row]) * p[column];
        }
      }
    }

    constexpr double roundoff = 8 * std::numeric_limits<float>::epsilon();

    for (const auto& plane : m_planes) {
      bool outside = true;

      for (const auto& corner : corners) {
        double distance{};
        double magnitude = 1;

        for (std::size_t column = 0; column < 4; ++column) {
          const double term = plane[column] * corner[column];
          distance += term;
          magnitude += std::abs(term);
        }

        if (distance >= -roundoff * magnitude) {
          outside = false;
          break;
        }
      }

      if (outside) {
        return false;
      }
    }

    return true;
  }
}  // namespace Dot::Graphics

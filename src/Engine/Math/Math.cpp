module;

#include <algorithm>
#include <cmath>
#include <expected>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>

module Dot.Math;

import Dot.Core.Result;

namespace Dot::Math {
  namespace {
    auto native(Vec3 value) -> glm::dvec3 {
      return {value.x, value.y, value.z};
    }

    auto native(Quat value) -> glm::dquat {
      return {value.w, value.x, value.y, value.z};
    }

    auto native(const Mat4& value) -> glm::dmat4 {
      glm::dmat4 matrix(0);

      for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
          matrix[column][row] = value.data[static_cast<std::size_t>(column * 4 + row)];
        }
      }

      return matrix;
    }

    auto representable(double value) -> bool {
      return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max();
    }

    auto converted(glm::dvec3 value) -> Core::Result<Vec3> {
      if (!representable(value.x) || !representable(value.y) || !representable(value.z)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Vector result exceeds the supported float range");
      }

      return Vec3{static_cast<float>(value.x), static_cast<float>(value.y),
                  static_cast<float>(value.z)};
    }

    auto converted(const glm::dmat4& value) -> Core::Result<Mat4> {
      Mat4 result;

      for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
          if (!representable(value[column][row])) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Matrix result exceeds the supported float range");
          }

          result.data[static_cast<std::size_t>(column * 4 + row)]
              = static_cast<float>(value[column][row]);
        }
      }

      return result;
    }
  }  // namespace

  auto isFinite(Vec3 value) noexcept -> bool {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
  }

  auto isFinite(Vec4 value) noexcept -> bool {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)
           && std::isfinite(value.w);
  }

  auto isFinite(Quat value) noexcept -> bool {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)
           && std::isfinite(value.w);
  }

  auto isFinite(const Mat4& value) noexcept -> bool {
    return std::all_of(value.data.begin(), value.data.end(),
                       [](float element) { return std::isfinite(element); });
  }

  auto operator+(Vec3 left, Vec3 right) noexcept -> Vec3 {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
  }

  auto operator-(Vec3 left, Vec3 right) noexcept -> Vec3 {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
  }

  auto operator*(Vec3 value, float scale) noexcept -> Vec3 {
    return {value.x * scale, value.y * scale, value.z * scale};
  }

  auto dot(Vec3 left, Vec3 right) noexcept -> double {
    return glm::dot(native(left), native(right));
  }

  auto length(Vec3 value) noexcept -> double {
    return glm::length(native(value));
  }

  auto normalized(Vec3 value) -> Core::Result<Vec3> {
    if (!isFinite(value) || length(value) == 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Cannot normalize a zero or non-finite vector");
    }

    return converted(glm::normalize(native(value)));
  }

  auto normalized(Quat value) -> Core::Result<Quat> {
    if (!isFinite(value) || glm::length(native(value)) == 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Cannot normalize a zero or non-finite quaternion");
    }

    const auto quaternion = glm::normalize(native(value));
    return Quat{static_cast<float>(quaternion.x), static_cast<float>(quaternion.y),
                static_cast<float>(quaternion.z), static_cast<float>(quaternion.w)};
  }

  auto rotationEulerDegrees(Vec3 degrees) -> Core::Result<Quat> {
    if (!isFinite(degrees)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Rotation angles must be finite");
    }

    const glm::dvec3 reduced{std::remainder(degrees.x, 360.0), std::remainder(degrees.y, 360.0),
                             std::remainder(degrees.z, 360.0)};
    const glm::dquat rotation(glm::radians(reduced));
    return normalized(Quat{static_cast<float>(rotation.x), static_cast<float>(rotation.y),
                           static_cast<float>(rotation.z), static_cast<float>(rotation.w)});
  }

  auto eulerDegrees(Quat rotation) -> Core::Result<Vec3> {
    auto unit = normalized(rotation);

    if (!unit) {
      return std::unexpected(unit.error());
    }

    return converted(glm::degrees(glm::eulerAngles(native(*unit))));
  }

  auto compose(Vec3 translation, Quat rotation, Vec3 scale) -> Core::Result<Mat4> {
    if (!isFinite(translation) || !isFinite(scale) || scale.x == 0 || scale.y == 0
        || scale.z == 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Transforms require finite translation and non-zero finite scale");
    }

    auto unit = normalized(rotation);

    if (!unit) {
      return std::unexpected(unit.error());
    }

    return converted(glm::translate(glm::dmat4(1), native(translation))
                     * glm::mat4_cast(native(*unit)) * glm::scale(glm::dmat4(1), native(scale)));
  }

  auto decomposeTrs(const Mat4& matrix) -> Core::Result<TransformParts> {
    if (!isFinite(matrix) || matrix.data[3] != 0 || matrix.data[7] != 0 || matrix.data[11] != 0
        || matrix.data[15] != 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "TRS decomposition requires a finite affine matrix");
    }

    const auto source = native(matrix);
    glm::dvec3 scale{glm::length(glm::dvec3{source[0]}), glm::length(glm::dvec3{source[1]}),
                     glm::length(glm::dvec3{source[2]})};

    if (scale.x == 0 || scale.y == 0 || scale.z == 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "TRS decomposition rejects zero-length basis columns");
    }

    glm::dmat3 basis{glm::dvec3{source[0]} / scale.x, glm::dvec3{source[1]} / scale.y,
                     glm::dvec3{source[2]} / scale.z};
    constexpr double tolerance = 1e-5;

    if (std::abs(glm::dot(basis[0], basis[1])) > tolerance
        || std::abs(glm::dot(basis[0], basis[2])) > tolerance
        || std::abs(glm::dot(basis[1], basis[2])) > tolerance) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "TRS decomposition cannot represent matrix shear");
    }

    if (glm::determinant(basis) < 0) {
      basis[0] = -basis[0];
      scale.x = -scale.x;
    }

    auto supportedScale = converted(scale);

    if (!supportedScale) {
      return std::unexpected(supportedScale.error());
    }

    const auto orientation = glm::normalize(glm::quat_cast(basis));
    auto rotation
        = normalized(Quat{static_cast<float>(orientation.x), static_cast<float>(orientation.y),
                          static_cast<float>(orientation.z), static_cast<float>(orientation.w)});

    if (!rotation) {
      return std::unexpected(rotation.error());
    }

    if (rotation->w < 0) {
      *rotation = {-rotation->x, -rotation->y, -rotation->z, -rotation->w};
    }

    TransformParts result{
        {matrix.data[12], matrix.data[13], matrix.data[14]}, *rotation, *supportedScale};
    auto rebuilt = compose(result.position, result.rotation, result.scale);

    if (!rebuilt) {
      return std::unexpected(rebuilt.error());
    }

    for (int column = 0; column < 3; ++column) {
      const double columnScale = std::abs(scale[column]);

      for (int row = 0; row < 3; ++row) {
        const auto index = static_cast<std::size_t>(column * 4 + row);
        const double difference = static_cast<double>(rebuilt->data[index]) - matrix.data[index];

        if (std::abs(difference) > tolerance * columnScale) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "TRS decomposition reconstruction exceeds relative tolerance");
        }
      }
    }

    return result;
  }

  auto multiply(const Mat4& left, const Mat4& right) -> Core::Result<Mat4> {
    if (!isFinite(left) || !isFinite(right)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Matrix inputs must be finite");
    }

    return converted(native(left) * native(right));
  }

  auto inverse(const Mat4& matrix) -> Core::Result<Mat4> {
    if (!isFinite(matrix) || glm::determinant(native(matrix)) == 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Cannot invert a singular or non-finite matrix");
    }

    return converted(glm::inverse(native(matrix)));
  }

  auto transformPoint(const Mat4& matrix, Vec3 point) -> Core::Result<Vec3> {
    if (!isFinite(matrix) || !isFinite(point)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Point transform inputs must be finite");
    }

    const auto transformed = native(matrix) * glm::dvec4(native(point), 1);

    if (transformed.w == 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Point projects to zero homogeneous weight");
    }

    return converted(glm::dvec3(transformed) / transformed.w);
  }

  auto transformVector(const Mat4& matrix, Vec3 vector) -> Core::Result<Vec3> {
    if (!isFinite(matrix) || !isFinite(vector)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Vector transform inputs must be finite");
    }

    return converted(glm::dvec3(native(matrix) * glm::dvec4(native(vector), 0)));
  }

  auto lookAt(Vec3 position, Vec3 target, Vec3 up) -> Core::Result<Mat4> {
    if (!isFinite(position) || !isFinite(target) || !isFinite(up)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Camera vectors must be finite");
    }

    const auto direction = native(target) - native(position);

    if (glm::length(direction) == 0 || glm::length(glm::cross(direction, native(up))) == 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Camera direction and up vector must form a valid basis");
    }

    return converted(glm::lookAtRH(native(position), native(target), native(up)));
  }

  auto perspective(float verticalFovDegrees, float aspect, float nearPlane, float farPlane)
      -> Core::Result<Mat4> {
    if (!std::isfinite(verticalFovDegrees) || !std::isfinite(aspect) || !std::isfinite(nearPlane)
        || !std::isfinite(farPlane) || verticalFovDegrees <= 0 || verticalFovDegrees >= 180
        || aspect <= 0 || nearPlane <= 0 || farPlane <= nearPlane) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid perspective camera parameters");
    }

    return converted(glm::perspectiveRH_ZO(
        glm::radians(static_cast<double>(verticalFovDegrees)), static_cast<double>(aspect),
        static_cast<double>(nearPlane), static_cast<double>(farPlane)));
  }

  auto orthographic(float left, float right, float bottom, float top, float nearPlane,
                    float farPlane) -> Core::Result<Mat4> {
    if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(bottom)
        || !std::isfinite(top) || !std::isfinite(nearPlane) || !std::isfinite(farPlane)
        || left >= right || bottom >= top || nearPlane < 0 || farPlane <= nearPlane) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid orthographic camera parameters");
    }

    return converted(glm::orthoRH_ZO(
        static_cast<double>(left), static_cast<double>(right), static_cast<double>(bottom),
        static_cast<double>(top), static_cast<double>(nearPlane), static_cast<double>(farPlane)));
  }
}  // namespace Dot::Math

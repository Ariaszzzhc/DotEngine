module;

#include <array>

export module Dot.Math;

import Dot.Core.Result;

export namespace Dot::Math {
  struct Vec2 {
    float x{};
    float y{};
    auto operator==(const Vec2&) const -> bool = default;
  };
  struct Vec3 {
    float x{};
    float y{};
    float z{};
    auto operator==(const Vec3&) const -> bool = default;
  };
  struct Vec4 {
    float x{};
    float y{};
    float z{};
    float w{};
    auto operator==(const Vec4&) const -> bool = default;
  };
  struct Quat {
    float x{};
    float y{};
    float z{};
    float w{1};
    auto operator==(const Quat&) const -> bool = default;
  };

  /** Column-major, right-handed matrices; projection depth is [0, 1].
   * 列主序、右手坐标系，投影深度范围为 [0, 1]。 */
  struct Mat4 {
    std::array<float, 16> data{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    auto operator==(const Mat4&) const -> bool = default;
  };

  [[nodiscard]] auto isFinite(Vec3 value) noexcept -> bool;
  [[nodiscard]] auto isFinite(Vec4 value) noexcept -> bool;
  [[nodiscard]] auto isFinite(Quat value) noexcept -> bool;
  [[nodiscard]] auto isFinite(const Mat4& value) noexcept -> bool;
  auto operator+(Vec3 left, Vec3 right) noexcept -> Vec3;
  auto operator-(Vec3 left, Vec3 right) noexcept -> Vec3;
  auto operator*(Vec3 value, float scale) noexcept -> Vec3;
  [[nodiscard]] auto dot(Vec3 left, Vec3 right) noexcept -> double;
  [[nodiscard]] auto length(Vec3 value) noexcept -> double;
  /** Reject zero/non-finite inputs; finite extremes use double intermediates.
   * 拒绝零值及非有限输入；有限极值使用双精度中间结果。 */
  [[nodiscard]] auto normalized(Vec3 value) -> Core::Result<Vec3>;
  [[nodiscard]] auto normalized(Quat value) -> Core::Result<Quat>;
  /** Euler angles use degrees and the quaternion convention Rz * Ry * Rx.
   * 欧拉角单位为度，四元数旋转约定为 Rz * Ry * Rx。 */
  [[nodiscard]] auto rotationEulerDegrees(Vec3 degrees) -> Core::Result<Quat>;
  [[nodiscard]] auto eulerDegrees(Quat rotation) -> Core::Result<Vec3>;
  /** Compose T * R * S with non-zero scale; normalize the supplied rotation.
   * 按 T * R * S 组合变换，缩放必须非零，输入旋转会归一化。 */
  [[nodiscard]] auto compose(Vec3 translation, Quat rotation = {}, Vec3 scale = {1, 1, 1})
      -> Core::Result<Mat4>;
  [[nodiscard]] auto multiply(const Mat4& left, const Mat4& right) -> Core::Result<Mat4>;
  [[nodiscard]] auto inverse(const Mat4& matrix) -> Core::Result<Mat4>;
  /** Transform a position and divide by homogeneous weight; reject zero weight.
   * 变换位置并除以齐次权重，拒绝零权重。 */
  [[nodiscard]] auto transformPoint(const Mat4& matrix, Vec3 point) -> Core::Result<Vec3>;
  [[nodiscard]] auto transformVector(const Mat4& matrix, Vec3 vector) -> Core::Result<Vec3>;
  /** Camera forward is -Z; degenerate direction/up bases are errors.
   * 相机前向为 -Z，退化的方向与向上向量基会返回错误。 */
  [[nodiscard]] auto lookAt(Vec3 position, Vec3 target, Vec3 up = {0, 1, 0}) -> Core::Result<Mat4>;
  [[nodiscard]] auto perspective(float verticalFovDegrees, float aspect, float nearPlane,
                                 float farPlane) -> Core::Result<Mat4>;
  [[nodiscard]] auto orthographic(float left, float right, float bottom, float top, float nearPlane,
                                  float farPlane) -> Core::Result<Mat4>;
}  // namespace Dot::Math

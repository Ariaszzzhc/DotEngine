module;

#include <cstdint>
#include <memory>

export module Dot.Editor.TransformTool;

import Dot.Core.Result;
import Dot.Math;
import Dot.World.Scene;
import Dot.Editor.Document;

export namespace Dot::Editor {
  /** Closest point between a world axis and a viewport ray; top-left UVs use
   * [0,1] and the engine's right-handed [0,1] depth convention. Near-parallel
   * axes fail rather than jumping. Delta is absolute from the first sample.
   * 世界轴与视口射线最近点，左上 UV 使用 [0,1] 及引擎右手 [0,1] 深度约定。
   * 近乎平行时返回错误避免跳跃，位移始终相对首个采样。 */
  class AxisDrag {
  public:
    [[nodiscard]] static auto begin(const Math::Mat4& viewProjection, Math::Vec3 origin,
                                    Math::Vec3 axis, Math::Vec2 uv) -> Core::Result<AxisDrag>;
    [[nodiscard]] auto delta(Math::Vec2 uv, float snap = 0) const -> Core::Result<Math::Vec3>;

  private:
    AxisDrag() = default;
    Math::Mat4 m_inverse;
    Math::Vec3 m_origin;
    Math::Vec3 m_axis;
    double m_start{};
  };

  /** Rotation rays intersect a canonical axis plane in an affine operation frame.
   * Samples unwrap continuously; snapping applies to the total angle in degrees.
   * Frame may include parent stretch/reflection, but never entity scale.
   * 旋转射线在仿射操作框架中相交于规范轴平面，连续展开采样并按总角度吸附；
   * 框架可含父级伸缩/反射，不含实体自身缩放。 */
  class RotationDrag {
  public:
    [[nodiscard]] static auto begin(const Math::Mat4& viewProjection, const Math::Mat4& frame,
                                    int axis, Math::Vec2 uv) -> Core::Result<RotationDrag>;
    [[nodiscard]] auto degrees(Math::Vec2 uv, float snap = 0) -> Core::Result<float>;

  private:
    RotationDrag() = default;
    Math::Mat4 m_clipToFrame;
    int m_axis{};
    double m_previous{};
    double m_total{};
  };

  /** Local frame = parent-world * local T*R, deliberately excluding entity scale;
   * world frame = translation only. This makes reflected/nonuniform parents explicit.
   * 局部框架为父世界变换乘局部 T*R，明确排除实体缩放；世界框架只保留平移，
   * 因而父级反射/非均匀伸缩不会被假正交基替代。 */
  [[nodiscard]] auto transformFrame(const World::Scene& scene, World::EntityId entity, bool local)
      -> Core::Result<Math::Mat4>;

  /** Owns a disposable preview scene. Updates validate world-to-parent-local TRS
   * without touching the document. Commit is one revision-checked undo entry;
   * shear/singular parents and stale or foreign documents fail without data loss.
   * Keep the document at the same address through commit. Preview operations use
   * the creating thread. Move preserves preview borrows; destruction releases it.
   * 拥有可丢弃预览场景，更新校验世界到父局部 TRS 而不修改文档；提交为单次版本校验
   * 撤销记录。错切、奇异父变换、过期及异文档均无损失败；提交前保持文档地址稳定，
   * 预览操作限创建线程；移动保留借用，析构释放草稿。 */
  class TransformGesture {
  public:
    [[nodiscard]] static auto begin(const Document& document, std::uint64_t revision,
                                    std::uint64_t entity) -> Core::Result<TransformGesture>;
    TransformGesture(const TransformGesture&) = delete;
    auto operator=(const TransformGesture&) -> TransformGesture& = delete;
    TransformGesture(TransformGesture&&) noexcept;
    auto operator=(TransformGesture&&) noexcept -> TransformGesture&;
    ~TransformGesture();
    [[nodiscard]] auto update(const Math::Mat4& world) -> Core::Result<void>;
    /** Absolute operation relative to the gesture start; local edits preserve
     * original position/scale or rotation. World edits use strict parent-local TRS.
     * 相对手势开始的绝对操作；局部编辑保留原位置/缩放或旋转，世界编辑严格校验父局部 TRS。 */
    [[nodiscard]] auto rotate(int axis, float degrees, bool local) -> Core::Result<void>;
    [[nodiscard]] auto scale(int axis, float factor, bool local) -> Core::Result<void>;
    [[nodiscard]] auto world() const -> Core::Result<Math::Mat4>;
    [[nodiscard]] auto scene() const -> Core::Result<const World::Scene*>;
    [[nodiscard]] auto commit(Document& document) -> Core::Result<EditReport>;
    [[nodiscard]] auto entity() const noexcept -> std::uint64_t;
    [[nodiscard]] auto revision() const noexcept -> std::uint64_t;

  private:
    struct Impl;
    explicit TransformGesture(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Editor

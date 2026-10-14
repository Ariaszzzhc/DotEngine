module;

export module Dot.Editor.Camera;

import Dot.Core.Result;
import Dot.Math;
export import Dot.Graphics.View;

export namespace Dot::Editor {
  /** Session-owned camera; navigation never edits scene entities or history.
   * Degrees, meters and normalized viewport pan units are explicit at the boundary.
   * 会话拥有的相机；导航不编辑场景实体或历史，边界明确角度、米及归一化视口平移单位。 */
  struct CameraState {
    Math::Vec3 position{6, 4, 8};
    Math::Vec3 pivot{};
    float yaw{36.8699F};
    float pitch{-21.8014F};
    float speed{5};
    bool orthographic{};
    float orthographicHeight{10};
    float nearPlane{0.05F};
    float farPlane{10000};
    auto operator==(const CameraState&) const -> bool = default;
  };

  class Camera {
  public:
    [[nodiscard]] auto state() const noexcept -> CameraState;
    [[nodiscard]] auto setState(const CameraState& state) -> Core::Result<void>;
    [[nodiscard]] auto view() const -> Core::Result<Graphics::View>;
    [[nodiscard]] auto turn(Math::Vec2 degrees, bool orbit) -> Core::Result<void>;
    /** Local X right, Y up, Z forward; diagonal speed is bounded to the speed scalar.
     * 局部 X 向右、Y 向上、Z 向前，对角运动速度不超过速度标量。 */
    [[nodiscard]] auto move(Math::Vec3 direction, double seconds, bool fast = false)
        -> Core::Result<void>;
    [[nodiscard]] auto pan(Math::Vec2 viewportDelta, float aspect) -> Core::Result<void>;
    [[nodiscard]] auto dolly(float steps) -> Core::Result<void>;
    [[nodiscard]] auto focus(const Graphics::Bounds& bounds, float aspect) -> Core::Result<void>;

  private:
    CameraState m_state;
  };
}  // namespace Dot::Editor

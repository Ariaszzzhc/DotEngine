module;

#include <cstddef>

export module Dot.Editor.AnimationTangent;

export import Dot.Animation.Edit;
import Dot.Core.Result;

export namespace Dot::Editor {
  struct TangentSnapshot {
    Animation::KeyReference source;
    Animation::Key key;
    std::size_t component;
    bool incoming;
    float original;
    float slope;
    double offset;
    double handleTime;
    double handleValue;
    bool changed;
  };

  /** Own one existing cubic key/component/side. Slopes are native derivatives per
   * second; quaternion components precede normalization, never Euler angles.
   * A handle uses one third of the adjacent interval; unused endpoint sides fail.
   * Updates change only the ghost scalar; invalid updates retain it. Returning to
   * the original slope restores its float bits. value() builds a native candidate,
   * without touching scene/file/device/history. The caller preflights pose/skin and
   * publishes once. Move retires the source; destruction discards the ghost.
   * 拥有现有三次键的单个分量/侧。斜率为每秒原生导数，四元数在归一化前编辑，不是欧拉角。
   * 手柄取相邻间隔的三分之一；端点未使用侧拒绝。更新仅改变候选标量，失败保留原候选；
   * 返回原斜率恢复原 float 位。value 构造原生候选，不触及场景/文件/设备/历史；调用方
   * 预检姿态/蒙皮后提交一次。移动使源失效，销毁丢弃候选。 */
  class AnimationTangent {
  public:
    [[nodiscard]] static auto begin(Animation::Clip source, Animation::KeyReference key,
                                    std::size_t component, bool incoming)
        -> Core::Result<AnimationTangent>;
    AnimationTangent(const AnimationTangent&) = delete;
    auto operator=(const AnimationTangent&) -> AnimationTangent& = delete;
    AnimationTangent(AnimationTangent&&) noexcept;
    auto operator=(AnimationTangent&&) noexcept -> AnimationTangent&;
    ~AnimationTangent();
    [[nodiscard]] auto update(float slope) -> Core::Result<void>;
    [[nodiscard]] auto snapshot() const -> Core::Result<TangentSnapshot>;
    [[nodiscard]] auto value() const -> Core::Result<Animation::Clip>;

  private:
    AnimationTangent(Animation::Clip source, TangentSnapshot value);
    Animation::Clip m_source;
    TangentSnapshot m_value;
  };
}  // namespace Dot::Editor

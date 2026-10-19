module;

#include <array>
#include <cstdint>

export module Dot.Editor.AnimationPosing;

export import Dot.Animation.Edit;
export import Dot.Animation.TwoBone;
import Dot.Core.Result;
import Dot.Math;
import Dot.World.Scene;

export namespace Dot::Editor {
  struct TwoBoneKeys {
    Animation::Clip value;
    Animation::TwoBonePose pose;
    std::array<Animation::KeyReference, 3> selection;
    Math::Vec3 target;
  };

  struct PoseKeySample {
    Math::Vec3 reached;
    double residual;
  };

  /** Build three rotation keys at an explicit native float time from a current posed scene.
   * Resolve the logical end through this owner's bindings; both direct ancestors must also
   * be uniquely bound within its subtree. Caller associates the scene with the current clip
   * and frozen audition generation. Preserve other channels/keys and interpolation modes.
   * Existing cubic tangents survive replacement; new cubic keys have zero derivatives.
   * Choose quaternion sign in the existing key/preceding-key hemisphere. Missing rotation
   * tracks become linear. No scene/history/files/devices change; the result owns its clip.
   * Validate a real resampled candidate with validateTwoBoneKeys before publication because
   * active layers, transitions or sub-float playhead precision can prevent reproducing it.
   * 在当前姿态场景显式 float 时刻创建三个旋转键；按所属播放器逻辑末端寻找直接祖先，
   * 三关节须唯一绑定且在其子树内。调用方关联当前片段及冻结预览代次。保留其他键/通道及
   * 插值；替换三次键保留切线，新三次键导数为零，符号沿用现键或前键半球，缺轨使用线性。
   * 不改场景/历史/文件/设备，结果拥有片段。发布前须校验实际重采样候选，防止混合层、
   * 过渡或低于 float 精度的游标时间不能复现所求姿态。 */
  [[nodiscard]] auto prepareTwoBoneKeys(const World::Scene& posed, World::EntityId owner,
                                        const Animation::Clip& clip, float time, std::uint32_t end,
                                        Math::Vec3 target, Math::Vec3 pole)
      -> Core::Result<TwoBoneKeys>;

  /** Validate actual candidate local TRS and end world position, remapping only scene scope.
   * Relative rotation matrix tolerance 1e-5; translations/scales stay exact. End position uses
   * 1e-5 * max(1, total world segment length), matching native solver float validation.
   * Return the actual candidate end position/residual with double distance intermediates.
   * Does not simulate, publish or mutate either
   * input; requires the scene's owner thread.
   * 仅重映射场景作用域，校验实际候选局部 TRS 及末端世界位置；旋转矩阵容差 1e-5，
   * 平移/缩放须精确相同，末端位置沿用原生浮点校验容差。不运行模拟或发布/修改输入，
   * 返回实际候选末端位置，遵守场景拥有线程约束。 */
  [[nodiscard]] auto validateTwoBoneKeys(const World::Scene& candidate, const TwoBoneKeys& keys)
      -> Core::Result<PoseKeySample>;
}  // namespace Dot::Editor

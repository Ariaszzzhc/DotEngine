module;

#include <array>

export module Dot.Animation.TwoBone;

import Dot.Core.Result;
import Dot.Math;
import Dot.World.Scene;

export namespace Dot::Animation {
  struct TwoBoneRequest {
    World::EntityId root;
    World::EntityId middle;
    World::EntityId end;
    Math::Vec3 target;
    Math::Vec3 pole;
  };

  struct TwoBonePose {
    std::array<World::TransformUpdate, 3> updates;
    Math::Vec3 reached;
    double residual;
    bool clamped;
  };

  /** Solve a direct root->middle->end chain in scene world coordinates without scene writes,
   * IO or devices. Rotate only; keep all local positions/scales and end world orientation.
   * Positive uniform local scales are required throughout the ancestor chain. Segment lengths
   * and root-target distance must be >=1e-5 world units; pole projection must exceed 1e-5
   * times its distance from root. Unreachable targets clamp to the closed reach interval and
   * report the actual residual. Antiparallel swings use a deterministic orthogonal axis.
   * Result owns three current-scope updates; apply atomically before mutating/replacing the
   * source scene. Scene owner-thread rules apply. This is pose construction, not a persistent
   * runtime constraint, joint-limit solver, foot planting or physics root motion.
   * 在场景世界坐标求解直接 root->middle->end 链，不写场景、不读文件/初始化设备；仅旋转，
   * 保留局部位置/缩放及末端世界朝向。整条祖先链局部缩放须正且均匀；骨长/目标距离至少
   * 1e-5 世界单位，pole 垂直投影须大于其根距的 1e-5。不可达目标夹入闭合可达区间并报告
   * 实际残差；反向旋转采用确定正交轴。结果拥有三个当前作用域更新，须在替换/修改源前
   * 原子应用，遵守场景线程规则；不实现持久运行约束、关节限位、脚部锁定或物理根运动。 */
  [[nodiscard]] auto solveTwoBone(const World::Scene& scene, const TwoBoneRequest& request)
      -> Core::Result<TwoBonePose>;
}  // namespace Dot::Animation

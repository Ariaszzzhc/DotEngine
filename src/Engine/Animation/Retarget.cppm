module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

export module Dot.Animation.Retarget;

export import Dot.Animation.Clip;
import Dot.Core.Result;
import Dot.Math;
import Dot.World.Scene;

export namespace Dot::Animation {
  struct RetargetNode {
    std::string name;
    std::optional<std::uint32_t> parent;
    Math::TransformParts reference;
    std::optional<std::uint32_t> binding;
  };

  struct RetargetRig {
    std::vector<RetargetNode> nodes;
    std::uint32_t anchor{};
  };

  struct RetargetMapping {
    std::uint32_t source;
    std::uint32_t target;
    std::string sourceName;
    std::string targetName;
  };

  struct RetargetRate {
    std::uint32_t numerator{30};
    std::uint32_t denominator{1};
  };

  struct RetargetTranslation {
    std::uint32_t target;
    double scale{1};
  };

  struct RetargetSettings {
    std::string name;
    std::vector<RetargetMapping> mappings;
    RetargetRate rate{};
    std::optional<RetargetTranslation> translation{};
  };

  struct RetargetResult {
    Clip clip;
    std::size_t frames;
    std::size_t emittedNodes;
    std::vector<std::uint32_t> unmappedSource;
    std::vector<std::uint32_t> unmappedTarget;
  };

  /** Validate an owned rig graph and finite invertible reference transforms.
   * 校验拥有式骨架图和有限可逆参考变换。 */
  [[nodiscard]] auto validateRetargetRig(const RetargetRig& rig) -> Core::Result<void>;

  /** Capture an animation owner's bound subtree and intervening helpers as owned reference data.
   * The anchor's external parent is omitted so instance placement stays outside rig space.
   * Bindings must be descendants of the owner; no files, simulation or devices. Scene-thread rules
   * apply. 捕获动画所有者的绑定子树及中间辅助节点为独立参考数据；锚点外部父级不进入骨架空间，
   * 保留实例放置独立性。绑定须在所有者子树内；不读文件、不运行模拟/设备，遵守场景线程规则。 */
  [[nodiscard]] auto captureRetargetRig(const World::Scene& scene, World::EntityId owner)
      -> Core::Result<RetargetRig>;

  /** Bake explicit one-to-one, name-checked mappings using reference-relative world rotations
   * in each anchor's fixed orientation frame. Mapped ancestry must agree through unmapped helpers.
   * Reference local scales must be positive/uniform; animated scale changes reject, not disappear.
   * Mapping the anchor is forbidden. Emit local linear TRS for complete bound subtrees of mapped
   * roots, holding unmapped descendants at target reference; nodes outside those subtrees stay
   * untouched. Optional displacement transfer addresses one mapped root in world units with an
   * explicit scale; it is bone motion, not physics/root-motion extraction or IK. Fixed rate 1..240
   * Hz, <=8192 samples,
   * <=1M keys and <=4M evaluated rig nodes. Always include the exact endpoint, coalescing a grid
   * point rounded onto it; reject interior float time collisions/overflow. Input rigs/clips remain
   * unchanged; cancellation yields no partial clip.
   * 使用显式一对一及名称校验映射，在固定锚点朝向空间传递相对参考的世界旋转差；越过未映射
   * 辅助节点后，映射祖先须一致。参考局部缩放须正且均匀，动态缩放明确拒绝。不可映射锚点。
   * 输出映射根的完整绑定子树局部线性 TRS，未映射后代保持目标参考，外部节点不写入。
   * 可显式按世界单位比例传递一个映射根的位移；不是物理根运动提取或 IK。固定 1..240 Hz，
   * 至多 8192
   * 样本/百万键/四百万求值节点；含精确末帧，拒绝时间碰撞/溢出。输入不变，取消不返回半片段。 */
  [[nodiscard]] auto retargetClip(const RetargetRig& source, const RetargetRig& target,
                                  const Clip& clip, const RetargetSettings& settings,
                                  std::stop_token stop = {}) -> Core::Result<RetargetResult>;
}  // namespace Dot::Animation

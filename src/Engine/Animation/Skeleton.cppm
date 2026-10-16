module;

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module Dot.Animation.Skeleton;

import Dot.Animation.Skin;
import Dot.Core.Result;
import Dot.Math;
import Dot.World.Scene;

export namespace Dot::Animation {
  struct SkeletonJoint {
    World::EntityId entity;
    std::string name;
    Math::Vec3 position;
    /** Nearest bound ancestor index and real hierarchy-link distance; no mesh-root
     * or index-order edges are invented.
     * 最近的已绑定祖先索引与实际层级边数，不虚构网格根/序号连线。 */
    std::optional<std::uint32_t> parent;
    std::uint32_t parentLinks{};
  };

  struct SkeletonPose {
    World::EntityId owner;
    std::string asset;
    std::vector<SkeletonJoint> joints;
  };

  /** Owned inspection of the actual enabled skin and current world-space joints.
   * Reuses native SceneSkin count/palette validation; preserves binding order and
   * multiple roots, collapsing only intervening unbound hierarchy nodes. No asset
   * IO, scene writes, simulation, device or GUI initialization. Scene owner-thread
   * rules apply; retired-source snapshots remain owned and independent.
   * 检查启用蒙皮及当前世界关节，复用原生 SceneSkin 的数量/调色板校验；保留绑定顺序和
   * 多根，仅折叠未绑定的中间层级节点。不读资产、不修改场景、不初始化模拟/设备/GUI；
   * 遵守场景线程约束，源销毁后快照仍独立拥有数据。 */
  [[nodiscard]] auto sampleSkeleton(const World::Scene& scene, World::EntityId owner, Skin skin)
      -> Core::Result<SkeletonPose>;
}  // namespace Dot::Animation

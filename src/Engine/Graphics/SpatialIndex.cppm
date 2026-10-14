module;

#include <cstdint>
#include <span>
#include <vector>

export module Dot.Graphics.SpatialIndex;

import Dot.Core.Result;
export import Dot.Graphics.Frustum;

export namespace Dot::Graphics {
  struct SpatialUpdate {
    bool rebuilt{};
    std::uint32_t changedBounds{};
    std::uint32_t refittedNodes{};
  };

  struct SpatialHit {
    std::uint32_t index;
    bool inside;
    auto operator==(const SpatialHit&) const -> bool = default;
  };

  struct SpatialQuery {
    std::vector<SpatialHit> hits;
    std::uint32_t nodeTests{};
    std::uint32_t boundsTests{};
  };

  /** Owns up to 10000 finite world AABBs in a balanced BVH. Same-count updates
   * refit affected branches, count changes rebuild. Validation/allocation precede
   * publication; failures preserve the old tree. Query restores input order and
   * marks strictly inside subtrees, avoiding per-object tests. No resource/device
   * ownership; callers synchronize concurrent mutations and retain no node borrows.
   * 拥有最多 10000 个世界 AABB 的平衡 BVH；同数量更新重拟合影响分支，数量变化重建。
   * 校验/分配先于发布，失败保留旧树；结果恢复输入顺序，内部子树避免逐对象测试。
   * 不拥有设备资源，调用者同步并发修改，不暴露节点借用。 */
  class SpatialIndex {
  public:
    [[nodiscard]] auto update(std::span<const Bounds> bounds) -> Core::Result<SpatialUpdate>;
    [[nodiscard]] auto query(const Frustum& frustum) const -> Core::Result<SpatialQuery>;

  private:
    struct Node {
      Bounds bounds;
      std::uint32_t first{};
      std::uint32_t count{};
      std::uint32_t left{};
      std::uint32_t right{};
      std::uint32_t parent{};
    };

    std::vector<Bounds> m_bounds;
    std::vector<Node> m_nodes;
    std::vector<std::uint32_t> m_order;
    std::vector<std::uint32_t> m_leaves;
  };
}  // namespace Dot::Graphics

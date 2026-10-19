module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Editor.HierarchyTree;

import Dot.Core.Result;

export namespace Dot::Editor {
  struct HierarchyItem {
    std::uint64_t id;
    std::optional<std::uint64_t> parent;
    std::string name;
  };

  struct HierarchyNode {
    HierarchyItem item;
    std::string key;
    std::vector<std::size_t> children;
  };

  /** Immutable ordered forest for a stopped authoring snapshot, not a second scene.
   * At most 10000 unique nonzero IDs and 256 levels; reject missing parents/cycles.
   * Roots/siblings retain input order, including forward parent references. Names
   * use the scene's 1..256 UTF-8 byte contract. Copies share storage; spans borrow
   * that storage. A default/moved tree is empty. Read-only use may cross threads.
   * 停止状态创作快照的不可变有序森林，不另造场景；最多 10000 个唯一非零 ID、256 层，
   * 拒绝缺失父级及循环。根/同级保持输入顺序，允许父级后置；名称遵守场景的 1..256
   * UTF-8 字节约定。副本共用存储，span 借用该存储；默认/移走的树为空，可跨线程只读。 */
  class HierarchyTree {
  public:
    HierarchyTree() noexcept;
    [[nodiscard]] static auto create(std::vector<HierarchyItem> items)
        -> Core::Result<HierarchyTree>;
    HierarchyTree(const HierarchyTree&) noexcept;
    auto operator=(const HierarchyTree&) noexcept -> HierarchyTree&;
    HierarchyTree(HierarchyTree&&) noexcept;
    auto operator=(HierarchyTree&&) noexcept -> HierarchyTree&;
    ~HierarchyTree();
    [[nodiscard]] auto nodes() const noexcept -> std::span<const HierarchyNode>;
    [[nodiscard]] auto roots() const noexcept -> std::span<const std::size_t>;
    /** One linear pass plus ancestor propagation; match self or descendants, not
     * every descendant of a matching parent. ASCII case folds, other UTF-8 bytes
     * match exactly. A query has at most 128 UTF-8 bytes without NUL. Empty matches
     * all nodes. Returned flags index nodes(); no expansion/selection is changed.
     * 单次线性扫描及祖先传播；匹配自身或后代，不包含仅因父级匹配而加入的所有后代。
     * ASCII 忽略大小写，其余 UTF-8 字节精确匹配；查询最多 128 UTF-8 字节且无 NUL。
     * 空查询匹配全部节点。结果按 nodes() 索引，不改变展开或选择。 */
    [[nodiscard]] auto filter(std::string_view query) const -> Core::Result<std::vector<bool>>;

  private:
    struct Impl;
    explicit HierarchyTree(std::shared_ptr<const Impl> impl) noexcept;
    std::shared_ptr<const Impl> m_impl;
  };
}  // namespace Dot::Editor

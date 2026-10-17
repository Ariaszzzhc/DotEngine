module;

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Dot.World.SceneLinks;

import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneContent;
import Dot.World.SceneSources;

export namespace Dot::World {
  struct SceneLinkUpdate {
    Scene scene;
    EntityId root;
    std::vector<std::uint64_t> created;
  };

  enum class SceneOverrideKind { Property, DeletedNode, AddedNode };

  struct SceneOverrideKey {
    SceneOverrideKind kind{};
    std::uint64_t entity{};
    std::string path;
    auto operator==(const SceneOverrideKey&) const -> bool = default;
  };

  /** Values own serialized JSON; absent differs from the JSON literal null.
   * Property paths escape object keys as JSON Pointers; arrays are atomic.
   * Deleted-node values refer to reserved, currently absent target identities.
   * 值拥有序列化 JSON，缺失不同于 JSON null；属性路径使用 JSON Pointer 转义，数组为整体。
   * 已删除节点仍使用已预留、当前缺失的目标身份。 */
  struct SceneOverride {
    SceneOverrideKey key;
    std::optional<std::uint64_t> source;
    std::optional<std::string> baseline;
    std::optional<std::string> local;
    std::optional<SceneSourceKey> sourceKey;
  };

  struct SceneOverridePage {
    EntityId root;
    std::string asset;
    std::optional<std::uint64_t> node;
    std::size_t offset{};
    std::size_t limit{};
    std::size_t total{};
    std::vector<SceneOverride> changes;
    /** Complete top-level property counts for this filter, independent of paging.
     * 当前过滤范围的完整顶层属性计数，不受分页影响。 */
    std::map<std::string, std::size_t> fields;
  };

  /** Direct ownership wins, otherwise search ancestors; no owning link is a
   * successful empty result. Requires a live current-scene entity. 优先直接归属，
   * 否则查找祖先；无所属链接成功返回空值，输入须为当前场景存活实体。 */
  [[nodiscard]] auto findSceneLinkRoot(const Scene& target, EntityId entity)
      -> Core::Result<std::optional<EntityId>>;

  /** Inspect applied changes against the last refreshed baseline, without I/O.
   * Optional node filters mapped (including deleted) or added values; placement
   * roots have no inherited properties. Stable source/object-key traversal, then added-value order.
   * Limit is 1..256; offset may equal but not exceed total. No selection/history effects.
   * 无 I/O 查看相对上次刷新基线的已应用修改，可过滤映射值（含已删除）或新增值；放置根
   * 没有继承属性。按源值/对象键遍历及新增值稳定排序；页长 1..256，偏移不超过总数，不改选择/历史。
   */
  [[nodiscard]] auto inspectSceneOverrides(const Scene& target, EntityId root,
                                           std::optional<std::uint64_t> node = {},
                                           std::size_t offset = 0, std::size_t limit = 32)
      -> Core::Result<SceneOverridePage>;

  /** Revert 1..256 unique current override keys atomically to stored baseline.
   * Added-node reset removes its subtree, rejecting inherited descendants or
   * dangling references. Restore dependent deleted nodes in one batch. No I/O or
   * new identity allocation; failures leave the source unchanged.
   * 原子还原 1..256 个唯一的当前覆盖到存储基线；新增节点还原删除其子树，拒绝继承后代或
   * 悬空引用；依赖的已删除节点须同批恢复。不读盘或分配新身份，失败不改变源。 */
  [[nodiscard]] auto prepareSceneOverrideRevert(const Scene& target, EntityId root,
                                                std::span<const SceneOverrideKey> keys)
      -> Core::Result<SceneLinkUpdate>;

  /** Revert all inherited properties/deletions and remove local additions;
   * retain placement and unrelated entities. Uses stored baseline even if the
   * source file is unavailable. Final native/file limits still apply.
   * 还原全部继承属性/删除并移除本地新增，保留放置及无关实体；即使源文件不可用仍使用
   * 存储基线，最终场景及文件限制依旧适用。 */
  [[nodiscard]] auto prepareSceneBaselineRevert(const Scene& target, EntityId root)
      -> Core::Result<SceneLinkUpdate>;

  /** Restore one complete top-level field of a live inherited node from the
   * stored remapped baseline. Placement roots, additions, id and unknown fields
   * reject. No source I/O or new entity IDs; final native references must remain valid.
   * 从已重映射的存储基线恢复存活继承节点的完整顶层字段；拒绝放置根、新增节点、id 及
   * 未知字段。不读源文件或分配实体身份，最终原生引用仍须有效。 */
  [[nodiscard]] auto prepareSceneFieldRevert(const Scene& target, EntityId root, EntityId entity,
                                             std::string_view field)
      -> Core::Result<SceneLinkUpdate>;

  /** Link a materialized copy to a project source, with an owned baseline and
   * stable node mapping. Source links are flattened into their saved entities;
   * globals and imported primary flags follow ordinary scene insertion rules.
   * 将展开副本关联项目源，拥有基线与稳定节点映射；源的链接按已保存实体展平，
   * 全局设置及导入主标记遵守普通场景插入规则。 */
  [[nodiscard]] auto prepareLinkedScene(const Scene& target, const Scene& source,
                                        std::string_view asset, std::uint64_t firstValue,
                                        const Transform& placement = {},
                                        std::optional<EntityId> parent = {})
      -> Core::Result<SceneContentResult>;

  /** Explicit provenance keeps intermediate materialization IDs separate from
   * persistent source keys. 显式来源将中间展开 ID 与持久源键分离。 */
  [[nodiscard]] auto prepareLinkedScene(const Scene& target, const SceneSourceSnapshot& source,
                                        std::uint64_t firstValue, const Transform& placement = {},
                                        std::optional<EntityId> parent = {})
      -> Core::Result<SceneContentResult>;

  /** Three-way source refresh preserves local property/deletion overrides and
   * local additions. Arrays are atomic; objects merge by property. Removing an
   * overridden source node fails unless revert is explicit. Revert restores all
   * source-owned nodes, retaining placement and unrelated/local entities. Broken
   * references or invalid final components reject the entire candidate. No I/O.
   * 三方刷新保留本地属性/删除覆盖及新增实体；数组为整体，对象按属性合并。删除已覆盖
   * 源节点须显式 revert，否则失败；revert 恢复源拥有节点，保留放置根及本地/无关实体。
   * 悬空引用或最终组件非法使整个候选失败，不读盘。 */
  [[nodiscard]] auto prepareSceneLinkUpdate(const Scene& target, const Scene& source, EntityId root,
                                            std::uint64_t firstValue, bool revert = false)
      -> Core::Result<SceneLinkUpdate>;

  [[nodiscard]] auto prepareSceneLinkUpdate(const Scene& target, const SceneSourceSnapshot& source,
                                            EntityId root, std::uint64_t firstValue,
                                            bool revert = false) -> Core::Result<SceneLinkUpdate>;
}  // namespace Dot::World

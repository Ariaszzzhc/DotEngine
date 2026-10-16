module;

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

export module Dot.World.SceneLinks;

import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneContent;

export namespace Dot::World {
  struct SceneLinkUpdate {
    Scene scene;
    EntityId root;
    std::vector<std::uint64_t> created;
  };

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
}  // namespace Dot::World

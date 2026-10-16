module;

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

export module Dot.World.SceneContent;

import Dot.Core.Result;
import Dot.World.Scene;

export namespace Dot::World {
  struct ContentEntityMapping {
    EntityId source;
    EntityId target;
  };

  /** An independently validated candidate with an owned ID map and container.
   * Source scenes are never mutated; target handles use the candidate's fresh
   * scope. Callers explicitly publish the candidate, not individual entities.
   * 独立校验的候选场景，拥有 ID 映射及容器；不修改源场景，目标句柄使用候选的新范围。
   * 调用者须显式发布整个候选，不逐个发布实体。 */
  struct SceneContentResult {
    Scene scene;
    EntityId root;
    std::vector<ContentEntityMapping> entities;
  };

  /** Extract the descendant union of 1..10000 unique selected handles. A new root
   * at local identity represents the last selected entity's world origin. Values
   * are compact; native skin/animation references must stay within the union.
   * External parents are flattened with strict world TRS decomposition; shear
   * fails. Environment/AO are excluded, primary flags and opaque script data are
   * retained. The new root needs one of the scene's 10000 slots. No I/O/devices.
   * 提取 1..10000 个唯一选择句柄的子树并集，新的单位根表示末项实体的世界原点。
   * ID 紧凑分配，蒙皮/动画引用必须在并集内；外部父级按严格世界 TRS 展平，剪切失败。
   * 不复制环境/AO，保留主标记及不透明脚本数据；新根占用 10000 个槽之一，不读盘或创建设备。 */
  [[nodiscard]] auto extractSceneContent(const Scene& source, std::span<const EntityId> selected,
                                         std::string_view name) -> Core::Result<SceneContentResult>;

  /** Prepare insertion under a new container with parent-local placement. Source
   * environment/AO are ignored; target globals and existing entity values remain.
   * Imported primary cameras/listeners are demoted and native references remap.
   * firstValue must exceed every live target value; authoring callers supply their
   * history high-water mark. Failure leaves both sources/allocator ownership intact.
   * This returns a fresh-scope candidate, not in-place runtime instantiation or a link.
   * 在新容器下准备插入，放置变换相对父级；忽略内容的环境/AO，保留目标全局数据及现有实体值。
   * 导入的主相机/听者取消主标记，原生引用重映射；firstValue 必须超过全部存活目标值，
   * 编辑调用者须提供历史高水位。失败不修改任一源及调用者分配器；返回新范围候选，
   * 不是运行时原地实例化或链接。 */
  [[nodiscard]] auto prepareSceneInstance(const Scene& target, const Scene& content,
                                          std::uint64_t firstValue, const Transform& placement = {},
                                          std::optional<EntityId> parent = {})
      -> Core::Result<SceneContentResult>;
}  // namespace Dot::World

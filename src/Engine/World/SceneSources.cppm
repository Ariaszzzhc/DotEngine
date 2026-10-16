module;

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Dot.World.SceneSources;

import Dot.Core.Result;
import Dot.World.Scene;

export namespace Dot::World {
  /** Closed materialized source plus persistent logical provenance. Native entity
   * values may be temporary; keys/floors belong to the original source namespaces.
   * 封闭的已展开源及持久逻辑来源；原生实体值可为临时值，键及上界属于原始源命名空间。 */
  struct SceneSourceSnapshot {
    Scene scene;
    std::string asset;
    std::vector<SceneSourceOrigin> origins;
    std::vector<SceneSourceNamespace> namespaces;
  };

  [[nodiscard]] auto validateSceneSourceSnapshot(const SceneSourceSnapshot& source)
      -> Core::Result<void>;

  /** Capture an independent saved/native scene. Local keys use its native values;
   * linked nodes inherit keys/namespaces under their persistent instance roots.
   * No source reads or input mutation. 捕获独立的已保存/原生场景，本地键使用其原生值，
   * 关联节点在持久实例根下继承来源键/命名空间；不读源文件或修改输入。 */
  [[nodiscard]] auto captureSceneSource(const Scene& scene, std::string_view asset)
      -> Core::Result<SceneSourceSnapshot>;

  /** Consume a working materialization with explicit original local identities
   * and original physical namespace. Local origins cover its live, unmapped nodes
   * with singleton keys. Original namespace values also retain saved inherited
   * allocations, permitting explicit unlink without confusing temporary IDs.
   * 接收工作展开场景，显式提供原始本地身份及物理命名空间；本地来源须以单元素键覆盖
   * 存活未映射节点。原命名空间还记录已保存的继承分配，允许解除关联而不混淆临时 ID。 */
  [[nodiscard]] auto prepareSceneSourceSnapshot(Scene scene, std::string_view asset,
                                                std::span<const SceneSourceOrigin> localOrigins,
                                                SceneSourceNamespace localNamespace)
      -> Core::Result<SceneSourceSnapshot>;
}  // namespace Dot::World

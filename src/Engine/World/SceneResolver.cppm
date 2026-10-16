module;

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

export module Dot.World.SceneResolver;

import Dot.Core.Result;
import Dot.World.SceneSources;

export namespace Dot::World {
  struct SceneResolveLimits {
    std::size_t depth{32};
    std::size_t sources{256};
    std::size_t links{4096};
    std::size_t inputBytes{64 * 1024 * 1024};
    std::size_t resolvedBytes{128 * 1024 * 1024};
    std::size_t entities{100000};
  };

  struct SceneResolveStats {
    std::size_t sources{};
    std::size_t links{};
    std::size_t inputBytes{};
    std::size_t resolvedBytes{};
    /** Live entities decoded and produced after each linked refresh, including
     * repeated placements. 解码及每次关联刷新产生的存活实体总数，包含重复放置。 */
    std::size_t entities{};
  };

  struct ResolvedSceneSource {
    SceneSourceSnapshot source;
    SceneResolveStats stats;
  };

  /** Synchronous, owned source text, bounded by the supplied remaining byte quota.
   * A portable project-relative asset is read at most once per resolution. The
   * callback must not modify source assets. Resolver also checks returned size.
   * 同步返回拥有的源文本，遵守给定剩余字节额度；每次解析至多读取一次可移植项目相对路径。
   * 回调不得修改来源资产，解析器也会校验返回大小。 */
  using SceneSourceLoader = std::function<Core::Result<std::string>(std::string_view, std::size_t)>;

  /** Resolve saved linked sources bottom-up in root-value order, preserving each
   * intermediate scene's overrides and original local identities. Cached shared
   * dependencies retain path identity and obey depth limits on every placement.
   * Cycles, ownerAsset references, missing/invalid inputs, merge conflicts and
   * graph-wide budgets reject the whole owned result. Limits may only lower the
   * defaults; no filesystem writes or devices. ownerAsset may be empty.
   * 按根值顺序自底向上解析已保存关联来源，保留每个中间场景的覆盖及原始本地身份；
   * 共享依赖按路径缓存，每次放置仍校验深度。循环、ownerAsset 引用、缺失/非法输入、
   * 合并冲突及图总预算使整个拥有结果失败。额度仅可低于默认值；不写文件或创建设备，
   * ownerAsset 可为空。 */
  [[nodiscard]] auto resolveSceneSource(std::string_view asset, const SceneSourceLoader& loader,
                                        const SceneResolveLimits& limits = {},
                                        std::string_view ownerAsset = {})
      -> Core::Result<ResolvedSceneSource>;
}  // namespace Dot::World

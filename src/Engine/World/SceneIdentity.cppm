module;

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Dot.World.SceneIdentity;

import Dot.Core.Result;

export namespace Dot::World {
  using SceneSourceKey = std::vector<std::uint64_t>;

  /** Namespace prefixes contain persistent source instance-root values; a node
   * key appends its source-local value. Native materialization IDs are separate.
   * 命名空间前缀由持久源实例根值组成，节点键追加源内节点值，与展开时的原生 ID 分离。 */
  struct SceneSourceOrigin {
    std::uint64_t entity{};
    SceneSourceKey key;
    auto operator==(const SceneSourceOrigin&) const -> bool = default;
  };

  struct SceneSourceNamespace {
    SceneSourceKey prefix;
    std::string asset;
    std::optional<std::uint64_t> nextEntity{1};
    /** Live physical source allocations, including saved inherited values and
     * nodes hidden by outer overrides. 存活物理源分配，包含已保存的继承值及被外层覆盖隐藏的节点。
     */
    std::vector<std::uint64_t> entities;
    auto operator==(const SceneSourceNamespace&) const -> bool = default;
  };

  /** Validate complete, bounded provenance: one empty-prefix root matching asset,
   * ancestor-closed namespaces, unique native IDs/keys and reserved source values.
   * At most 10000 origins/namespaces, keys 1..32 values, portable scene paths.
   * 校验完整有界来源：唯一空前缀根须匹配资产，命名空间祖先闭合，原生 ID/键唯一且源值已预留。
   * 最多 10000 个来源/命名空间，键长 1..32，资产须为可移植场景路径。 */
  [[nodiscard]] auto validateSceneSourceIdentity(std::string_view asset,
                                                 std::span<const SceneSourceOrigin> origins,
                                                 std::span<const SceneSourceNamespace> namespaces)
      -> Core::Result<void>;

  /** Materialization IDs may change; logical source keys cannot reuse retired
   * source values. Existing namespace assets/floors must stay stable/monotonic.
   * New nested instances require a live prior root or a fresh parent-space value.
   * 展开 ID 可以变化，逻辑来源键不得复用退役源值；现有命名空间资产/上界须保持一致/单调。
   * 新嵌套实例须使用此前存活的根或父命名空间中的新值。 */
  [[nodiscard]] auto validateSceneSourceUpdate(std::string_view asset,
                                               std::span<const SceneSourceOrigin> previous,
                                               std::span<const SceneSourceNamespace> oldNamespaces,
                                               std::span<const SceneSourceOrigin> incoming,
                                               std::span<const SceneSourceNamespace> newNamespaces)
      -> Core::Result<void>;
}  // namespace Dot::World

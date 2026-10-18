module;

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Script.NavigationSource;

export import Dot.Script.Host;
import Dot.Core.Result;
import Dot.World.Scene;
import Dot.Navigation.Surface;
import Dot.Physics.Mesh;
import Dot.Script.PhysicsSource;

export namespace Dot::Script {
  /** Native navigation for an original scene/owner thread. Immutable prepared
   * surfaces and collision assets are captured; no query file fallback. Route starts at
   * the actual character feet, validates the agent profile and rechecks static geometry.
   * Scoped movement follows paths and samples reciprocal avoidance from measured native bodies.
   * Physics remains position authority; no off-mesh/jump planning.
   * 原场景/拥有线程的原生导航，捕获不可变已准备表面与碰撞资源，查询不回退读文件。
   * 路线从角色实际脚底开始，校验体型并复核静态几何；范围内移动基于实际角色快照跟随
   * 路径及互惠避让，位置仍由物理决定，不规划跳跃连接。 */
  class NavigationSource {
  public:
    [[nodiscard]] static auto create(std::shared_ptr<World::Scene> scene,
                                     const PhysicsSource& physics,
                                     std::optional<Navigation::Surface> surface = {},
                                     std::vector<Physics::MeshAsset> meshes = {})
        -> Core::Result<NavigationSource>;
    [[nodiscard]] auto bindings() const -> Core::Result<HostBindings>;
    /** Shared strict move/stop/state/agents protocol, with scoped entity identities.
     * 共享严格 move/stop/state/agents 协议，使用范围内实体身份。 */
    [[nodiscard]] auto perform(std::string_view request) -> Core::Result<std::string>;
    /** Owner scheduling after physics synchronization and fixed script intent, before physics.
     * Never called by scripts. 物理同步及固定脚本意图后、物理步进前由拥有者调度，不向脚本暴露。 */
    [[nodiscard]] auto advance(double seconds) -> Core::Result<void>;
    auto retire() noexcept -> void;

  private:
    struct Impl;
    explicit NavigationSource(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

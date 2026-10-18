module;

#include <memory>
#include <optional>
#include <vector>

export module Dot.Script.NavigationSource;

export import Dot.Script.Host;
import Dot.Core.Result;
import Dot.World.Scene;
import Dot.Navigation.Surface;
import Dot.Physics.Mesh;
import Dot.Script.PhysicsSource;

export namespace Dot::Script {
  /** Read-only native navigation for an original scene/owner thread. Immutable prepared
   * surfaces and collision assets are captured; no query file fallback. Route starts at
   * the actual character feet, validates the agent profile and rechecks static geometry.
   * Dynamic obstacles remain native physics authority, without crowd/off-mesh planning.
   * 原场景/拥有线程的只读原生导航，捕获不可变已准备表面与碰撞资源，查询不回退读文件。
   * 路线从角色实际脚底开始，校验体型并复核静态几何；动态障碍仍由物理处理，不提供群体
   * 避让或跳跃连接规划。 */
  class NavigationSource {
  public:
    [[nodiscard]] static auto create(std::shared_ptr<World::Scene> scene,
                                     const PhysicsSource& physics,
                                     std::optional<Navigation::Surface> surface = {},
                                     std::vector<Physics::MeshAsset> meshes = {})
        -> Core::Result<NavigationSource>;
    [[nodiscard]] auto bindings() const -> Core::Result<HostBindings>;

  private:
    struct Impl;
    explicit NavigationSource(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

module;

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

export module Dot.Script.PhysicsSource;

export import Dot.Script.Host;
import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Physics.Mesh;
import Dot.World.Scene;
import Dot.Physics.World;

export namespace Dot::Script {
  struct PhysicsSourceConfig {
    Physics::WorldConfig simulation;
    std::size_t contactJsonBytes{1024 * 1024};
  };

  /** Owns a real scene simulation, original shared scene and last contact batch.
   * Operations require the creating thread; quiescent destruction may occur
   * elsewhere. Moves preserve bindings; callbacks retain ownership. Moving or
   * replacing the captured Scene invalidates this service. 拥有真实场景仿真、原始共享
   * 场景和最近接触批次；操作限创建线程，静止销毁可在异线程进行。移动保留绑定，回调
   * 保留所有权；移动/替换所捕获的 Scene 会使服务失效。 */
  class PhysicsSource {
  public:
    [[nodiscard]] static auto create(std::shared_ptr<World::Scene> scene,
                                     const PhysicsSourceConfig& config = {},
                                     std::optional<Core::FileSystem> files = {},
                                     std::optional<std::vector<Physics::MeshAsset>> prepared = {})
        -> Core::Result<PhysicsSource>;
    PhysicsSource(const PhysicsSource&) = delete;
    auto operator=(const PhysicsSource&) -> PhysicsSource& = delete;
    PhysicsSource(PhysicsSource&&) noexcept;
    auto operator=(PhysicsSource&&) noexcept -> PhysicsSource&;
    ~PhysicsSource();

    /** Native scheduling only; scripts cannot advance/reset their own world.
     * Synchronization/step publishes owned, non-consuming contact events for all
     * script readers. Post-mutation capture/serialization failures fault this
     * source and require reset; stale contact batches are then rejected. JSON
     * capacity is 1 byte..16 MiB and must fit the consuming Runtime's transport.
     * 仅供原生调度，脚本不能推进/重置世界；同步/步进发布所有脚本可重复读的拥有接触数据。
     * 修改后的捕获/序列化失败使源故障，须重置后继续，并拒绝旧接触批次。JSON 容量为
     * 1 字节..16 MiB，须适配消费 Runtime 的传输预算。 */
    [[nodiscard]] auto synchronize() -> Core::Result<void>;
    /** One native synchronization consumes uncached geometry without disk fallback.
     * 单次原生同步消费未缓存几何，不回退读文件。 */
    [[nodiscard]] auto synchronizePrepared(std::span<const Physics::MeshAsset> prepared)
        -> Core::Result<void>;
    [[nodiscard]] auto step(double seconds) -> Core::Result<void>;
    /** Advance 1 microsecond..1 second in equal slices <=250 ms, publishing one
     * batch containing every slice's contacts in order. Invalid duration has no
     * effects; failures after an earlier slice require reset, without rollback.
     * 以不超过 250 毫秒的等分步推进 1 微秒..1 秒，按序发布所有分步接触的单个批次。
     * 无效时长无副作用；已完成分步后的失败须重置，不回滚。 */
    [[nodiscard]] auto advance(double seconds) -> Core::Result<void>;
    /** Reset from authoring without replacing callbacks; increments generation
     * and publishes an empty batch. Failed preparation retains the old world.
     * 从编辑数据重置且不替换回调，递增仿真代并发布空批次；准备失败保留旧世界。 */
    [[nodiscard]] auto reset() -> Core::Result<void>;
    /** Queries describe last synchronization. Entity calls reject missing/foreign
     * owners immediately; ray hits on removed owners require native synchronization.
     * Reads are allowed during script preparation; controls are ReadWrite.
     * 查询描述最近同步状态；实体调用立即拒绝缺失/跨场景所有者，射线命中已删所有者时
     * 须原生同步。脚本准备期间允许读，控制方法标记 ReadWrite。 */
    [[nodiscard]] auto bindings() const -> Core::Result<HostBindings>;

  private:
    [[nodiscard]] auto synchronizeSource(
        std::optional<std::span<const Physics::MeshAsset>> prepared) -> Core::Result<void>;
    struct Impl;
    explicit PhysicsSource(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

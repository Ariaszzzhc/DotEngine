module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Dot.World.Scene;

import Dot.Core.Result;
import Dot.Math;

export namespace Dot::World {
  /** Scene-scoped handle. Values are never reused within a scene; moving its
   * owner preserves handles, destroying an entity invalidates its handle.
   * 场景范围句柄；场景内值不复用，移动所有者保留句柄，销毁实体使其句柄失效。
   */
  struct EntityId {
    std::uint64_t scene{};
    std::uint64_t value{};
    auto operator==(const EntityId&) const -> bool = default;
  };

  struct Transform {
    Math::Vec3 position{};
    Math::Quat rotation{};
    Math::Vec3 scale{1, 1, 1};
  };

  /** Owned snapshot; editing it does not mutate the scene.
   * 拥有数据的快照，修改快照不会更改场景。 */
  struct EntityInfo {
    EntityId id;
    std::string name;
    Transform transform;
    std::optional<EntityId> parent;
  };

  /** EnTT-backed scene. Operations require the creating thread; destruction may
   * occur after quiescence on any thread. Names are 1..256 UTF-8 bytes without NUL.
   * The scene holds at most 10000 entities with hierarchy depth at most 256.
   * 基于 EnTT 的场景；操作须在创建线程进行，静止后可在任意线程销毁。
   * 名称为 1..256 字节的 UTF-8 且不能含 NUL；最多 10000 个实体，层级深度最多 256。
   */
  class Scene {
  public:
    [[nodiscard]] static auto create(std::string_view name = "Untitled") -> Core::Result<Scene>;
    Scene(const Scene&) = delete;
    auto operator=(const Scene&) -> Scene& = delete;
    Scene(Scene&&) noexcept;
    auto operator=(Scene&&) noexcept -> Scene&;
    ~Scene();

    [[nodiscard]] auto id() const noexcept -> std::uint64_t;
    [[nodiscard]] auto name() const -> Core::Result<std::string>;
    [[nodiscard]] auto rename(std::string_view name) -> Core::Result<void>;
    [[nodiscard]] auto entityCount() const -> Core::Result<std::size_t>;
    [[nodiscard]] auto createEntity(std::string_view name = "Entity") -> Core::Result<EntityId>;
    /** Destroy the entity and its entire subtree. 删除实体及其整个子树。 */
    [[nodiscard]] auto destroyEntity(EntityId entity) -> Core::Result<void>;
    [[nodiscard]] auto findEntity(std::uint64_t value) const -> Core::Result<EntityId>;
    [[nodiscard]] auto entity(EntityId entity) const -> Core::Result<EntityInfo>;
    /** Snapshots sorted by creation ID for deterministic consumers.
     * 按创建 ID 排序的快照，供消费者确定性使用。 */
    [[nodiscard]] auto entities() const -> Core::Result<std::vector<EntityInfo>>;
    [[nodiscard]] auto renameEntity(EntityId entity, std::string_view name) -> Core::Result<void>;
    /** Normalize rotation; reject singular/non-finite transforms or any resulting
     * world-matrix overflow. Failures leave the previous scene state intact.
     * 归一化旋转，拒绝奇异/非有限变换及世界矩阵溢出；失败保留此前状态。
     */
    [[nodiscard]] auto setTransform(EntityId entity, const Transform& transform)
        -> Core::Result<void>;
    /** Keep local transform when reparenting. Reject cycles, foreign/stale handles,
     * excessive depth, and world overflow; failure is transactional.
     * 重新指定父实体时保留局部变换；拒绝环、跨场景/失效句柄、过深层级及世界溢出。
     * 失败不改变状态。
     */
    [[nodiscard]] auto setParent(EntityId entity, std::optional<EntityId> parent)
        -> Core::Result<void>;
    [[nodiscard]] auto worldTransform(EntityId entity) const -> Core::Result<Math::Mat4>;

  private:
    struct Impl;
    explicit Scene(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::World

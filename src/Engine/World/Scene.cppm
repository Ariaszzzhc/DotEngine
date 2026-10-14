module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Dot.World.Scene;

import Dot.Core.Result;
import Dot.Math;
import Dot.Physics.Body;
import Dot.Graphics.Lighting;
export import Dot.Graphics.Environment;
export import Dot.Audio.Source;
export import Dot.Script.Source;

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

  enum class Projection { Perspective, Orthographic };

  struct Camera {
    Projection projection{Projection::Perspective};
    float verticalFovDegrees{60};
    float orthographicHeight{10};
    float nearPlane{0.1F};
    float farPlane{1000};
    bool primary{};
  };

  enum class MeshKind { Cube, Quad, Asset };

  /** Asset paths are project-relative UTF-8 with forward slashes, no parent
   * traversal, and .dotmesh/.obj suffixes. Built-in kinds require an empty path.
   * 资产路径须为项目相对 UTF-8 路径，使用正斜杠、不能回溯父目录，且以 .dotmesh/.obj 结尾。
   * 内置类型要求路径为空。
   */
  struct MeshRenderer {
    MeshKind kind{MeshKind::Cube};
    std::string asset;
    Math::Vec4 tint{1, 1, 1, 1};
    bool visible{true};
  };

  /** Project-root material asset binding; absent binding deliberately uses the
   * mesh's unlit tint. Presence requires a portable .dotmaterial asset path.
   * 项目根材质资源绑定；未绑定时明确使用网格的无光照染色，绑定须为可移植 .dotmaterial 路径。 */
  struct MaterialBinding {
    std::string asset;
  };

  /** Scene-owned project-relative opaque .hdr panorama and GPU processing quality.
   * Linear intensity [0,64], yaw degrees [-360,360]; absent means no environment.
   * 场景拥有项目相对路径的不透明 .hdr 全景及 GPU 处理质量；线性强度 [0,64]，
   * Y 轴角度 [-360,360]；缺省明确表示无环境。 */
  struct Environment {
    std::string asset;
    float intensity{1};
    float rotationDegrees{};
    bool background{true};
    Graphics::EnvironmentConfig processing{};
    auto operator==(const Environment&) const -> bool = default;
  };

  using LightKind = Graphics::LightKind;

  /** Direct-light authoring without a device. Entity world position and local -Z
   * ray direction provide the light pose; range is world meters, unaffected by scale.
   * Directional intensity is lux; point/spot intensity is candela. Enabled scene
   * lights are bounded by the renderer's 64-light limit; excess is an error.
   * 不含设备的直接光编辑描述；实体世界位置及局部 -Z 光线方向决定光源位姿，范围按世界米计，
   * 不受缩放影响；方向光为 lux，点光/聚光为 candela。启用光源遵守渲染器 64 光源限制，超限报错。 */
  struct Light {
    LightKind kind{LightKind::Directional};
    Math::Vec3 color{1, 1, 1};
    float intensity{1};
    float range{10};
    float innerConeDegrees{20};
    float outerConeDegrees{30};
    bool enabled{true};
  };

  struct TransformUpdate {
    EntityId entity;
    Transform transform;
  };

  struct ParentUpdate {
    EntityId entity;
    std::optional<EntityId> parent;
  };

  /** Stored body properties, with pose taken from the entity transform. Setting
   * this description does not initialize a physics backend or allocate a body.
   * Initial velocity is world-space. Hierarchy/scale suitability is a separate
   * simulation-binding responsibility.
   * 保存的刚体属性，位姿来自实体变换；设置描述不初始化后端或分配仿真刚体。
   * 初始速度使用世界坐标，层级与缩放是否适用由独立的仿真绑定阶段负责。 */
  using RigidBody = Physics::BodyProperties;

  /** Stored audio authoring only; world pose comes from the entity. No file or
   * playback backend is initialized by editing. 仅保存音频编辑描述，世界位姿来自实体；
   * 编辑不读取文件，也不初始化播放后端。 */
  using AudioSource = Audio::SourceProperties;

  struct AudioListener {
    bool primary{};
    auto operator==(const AudioListener&) const -> bool = default;
  };

  using ScriptComponent = Script::ComponentProperties;

  /** Owned snapshot; editing it does not mutate the scene.
   * 拥有数据的快照，修改快照不会更改场景。 */
  struct EntityInfo {
    EntityId id;
    std::string name;
    Transform transform;
    std::optional<EntityId> parent;
    std::optional<Camera> camera{};
    std::optional<MeshRenderer> mesh{};
    std::optional<RigidBody> rigidBody{};
    std::optional<AudioSource> audioSource{};
    std::optional<AudioListener> audioListener{};
    std::optional<ScriptComponent> script{};
    std::optional<MaterialBinding> material{};
    std::optional<Light> light{};
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
    /** CPU-only transactional validation; resource existence is checked by consumers.
     * 仅 CPU 的事务式校验；消费者另行检查资源是否存在。 */
    [[nodiscard]] auto setEnvironment(std::optional<Environment> environment) -> Core::Result<void>;
    [[nodiscard]] auto environment() const -> Core::Result<std::optional<Environment>>;
    [[nodiscard]] auto entityCount() const -> Core::Result<std::size_t>;
    [[nodiscard]] auto createEntity(std::string_view name = "Entity") -> Core::Result<EntityId>;
    /** Import a monotonic ID: it must be >= the next available value and below
     * uint64 max. Skipped/consumed values cannot later be reused.
     * 导入单调 ID：须不小于下个可用值且小于 uint64 最大值，跳过或消耗的值不再复用。 */
    [[nodiscard]] auto createEntityWithId(std::string_view name, std::uint64_t value)
        -> Core::Result<EntityId>;
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
    /** Batch updates reject duplicate handles and validate the final scene once.
     * All changes roll back together on failure; intermediate states are private.
     * 批量更新拒绝重复句柄，并一次校验最终场景；失败时全部回滚，中间状态不对外暴露。 */
    [[nodiscard]] auto setTransforms(std::span<const TransformUpdate> updates)
        -> Core::Result<void>;
    [[nodiscard]] auto setParents(std::span<const ParentUpdate> updates) -> Core::Result<void>;
    [[nodiscard]] auto worldTransform(EntityId entity) const -> Core::Result<Math::Mat4>;
    /** At most one camera may be primary. Views use the full world inverse,
     * which must remain representable. 最多一个主相机；视图使用可表示的完整世界逆矩阵。 */
    [[nodiscard]] auto setCamera(EntityId entity, std::optional<Camera> camera)
        -> Core::Result<void>;
    [[nodiscard]] auto primaryCamera() const -> Core::Result<EntityId>;
    [[nodiscard]] auto viewProjection(EntityId camera, float aspect) const
        -> Core::Result<Math::Mat4>;
    /** Store a description; the render service checks asset existence/loading.
     * 保存描述，由渲染服务检查资产是否存在并负责加载。 */
    [[nodiscard]] auto setMeshRenderer(EntityId entity, std::optional<MeshRenderer> mesh)
        -> Core::Result<void>;
    /** Store validated authoring, without loading assets or creating GPU resources.
     * Failures retain existing components; removal is idempotent.
     * 保存已校验编辑描述，不加载资源或创建 GPU 资源；失败保留组件，移除幂等。 */
    [[nodiscard]] auto setMaterial(EntityId entity, std::optional<MaterialBinding> material)
        -> Core::Result<void>;
    [[nodiscard]] auto setLight(EntityId entity, std::optional<Light> light) -> Core::Result<void>;
    /** Validate every body property before replacement; removal is idempotent.
     * 替换前校验所有刚体属性，移除具有幂等性。 */
    [[nodiscard]] auto setRigidBody(EntityId entity, std::optional<RigidBody> body)
        -> Core::Result<void>;
    /** Validate the complete description before replacement; missing assets are
     * resolved by the playback binding, and removal is idempotent.
     * 替换前校验完整描述，缺失资产由播放绑定处理，移除具有幂等性。 */
    [[nodiscard]] auto setAudioSource(EntityId entity, std::optional<AudioSource> source)
        -> Core::Result<void>;
    /** At most one primary listener, independent of the primary camera. A second
     * primary is rejected before mutation; removal is idempotent.
     * 最多一个主监听器，与主相机独立；第二个主监听器在修改前拒绝，移除具有幂等性。 */
    [[nodiscard]] auto setAudioListener(EntityId entity, std::optional<AudioListener> listener)
        -> Core::Result<void>;
    [[nodiscard]] auto primaryAudioListener() const -> Core::Result<EntityId>;
    /** Validate and canonicalize authoring only; removal is idempotent. No code
     * is loaded and invalid input leaves the previous description unchanged.
     * 仅校验并规范编辑描述，移除幂等；不加载代码，无效输入保留此前描述。 */
    [[nodiscard]] auto setScript(EntityId entity, std::optional<ScriptComponent> script)
        -> Core::Result<void>;

  private:
    struct Impl;
    explicit Scene(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::World

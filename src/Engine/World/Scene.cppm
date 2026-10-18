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
export import Dot.World.SceneIdentity;
import Dot.Math;
import Dot.Physics.Body;
import Dot.Physics.Character;
import Dot.Graphics.Lighting;
export import Dot.Graphics.Environment;
export import Dot.Graphics.Occlusion;
export import Dot.Audio.Source;
export import Dot.Script.Source;
export import Dot.Animation.Source;
export import Dot.Animation.SkinSource;

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

  /** Exact runtime ownership, including the placement container. Reparenting does
   * not transfer ownership; values remain reserved after removal.
   * 精确运行时所有权，包含放置容器；改父级不转移所有权，移除后仍预留实体值。 */
  struct SceneAttachment {
    EntityId root;
    std::vector<EntityId> entities;
  };

  /** source indexes the flattened baseline; key carries persistent logical
   * provenance, independent of intermediate native IDs. source 索引展平基线，
   * key 保存不依赖中间原生 ID 的持久逻辑来源。 */
  struct SceneLinkMapping {
    std::uint64_t source{};
    std::uint64_t target{};
    SceneSourceKey key;
    auto operator==(const SceneLinkMapping&) const -> bool = default;
  };

  /** Persisted authoring provenance. Baseline is an owned native source scene
   * without links; mappings also retain locally deleted, reserved target values.
   * Runtime consumes materialized entities without reading source files.
   * 持久化编辑来源；基线拥有不含链接的原生源场景文本，映射保留本地已删除的预留目标值。
   * 运行时直接使用已展开实体，不读取源文件。 */
  struct SceneLink {
    std::uint64_t root{};
    std::string asset;
    std::string baseline;
    std::vector<SceneLinkMapping> mapping;
    std::vector<SceneSourceNamespace> namespaces;
    auto operator==(const SceneLink&) const -> bool = default;
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
    bool castsShadow{true};
    bool receivesShadow{true};
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
    std::optional<Graphics::ShadowSettings> shadows{};
  };

  struct TransformUpdate {
    EntityId entity;
    Transform transform;
  };

  struct ParentUpdate {
    EntityId entity;
    std::optional<EntityId> parent;
  };

  enum class ParentSpace { Local, World };

  /** Stored body properties, with pose taken from the entity transform. Setting
   * this description does not initialize a physics backend or allocate a body.
   * Initial velocity is world-space. Hierarchy/scale suitability is a separate
   * simulation-binding responsibility.
   * 保存的刚体属性，位姿来自实体变换；设置描述不初始化后端或分配仿真刚体。
   * 初始速度使用世界坐标，层级与缩放是否适用由独立的仿真绑定阶段负责。 */
  using RigidBody = Physics::BodyProperties;

  /** CPU-only feet-origin capsule description, mutually exclusive with a rigid
   * body on the same entity. World pose/scale are checked by SceneSimulation.
   * 纯 CPU 脚底原点胶囊描述，与同实体刚体互斥；世界位姿/缩放由仿真绑定检查。 */
  using CharacterController = Physics::CharacterProperties;

  /** Stored audio authoring only; world pose comes from the entity. No file or
   * playback backend is initialized by editing. 仅保存音频编辑描述，世界位姿来自实体；
   * 编辑不读取文件，也不初始化播放后端。 */
  using AudioSource = Audio::SourceProperties;

  struct AudioListener {
    bool primary{};
    auto operator==(const AudioListener&) const -> bool = default;
  };

  using ScriptComponent = Script::ComponentProperties;
  using AnimationPlayer = Animation::PlayerProperties;
  using SkinBinding = Animation::SkinProperties;

  struct AnimationPlayerInfo {
    EntityId entity;
    AnimationPlayer properties;
    auto operator==(const AnimationPlayerInfo&) const -> bool = default;
  };

  struct TransformInfo {
    EntityId id;
    Transform transform;
    std::optional<EntityId> parent;
  };

  struct ColliderInfo {
    EntityId id;
    std::optional<RigidBody> rigidBody;
    std::optional<CharacterController> character;
  };

  struct AudioSourceInfo {
    EntityId id;
    AudioSource properties;
  };

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
    std::optional<AnimationPlayer> animation{};
    std::optional<SkinBinding> skin{};
    std::optional<CharacterController> character{};
  };

  class DetachedScene;

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

    /** Exclusively detach CPU descriptions after all borrows are quiescent. Success empties
     * this Scene; the opaque move-only envelope has no scene/entity access and can cross a
     * synchronized thread boundary. adopt consumes it only on success, assigns a fresh scope
     * and binds operations to the receiving thread, invalidating every old scoped handle.
     * No devices, backend instances or runtime services are contained/transferred.
     * 全部借用静止后独占分离 CPU 描述；成功使本 Scene 为空。不可复制的封套不提供场景/实体
     * 访问，可经同步在线程间交接。adopt 仅成功时消费封套，分配新范围并绑定接收线程，使全部
     * 旧范围句柄失效；封套不包含或转移设备、后端实例及运行服务。 */
    [[nodiscard]] auto detach() -> Core::Result<DetachedScene>;
    [[nodiscard]] static auto adopt(DetachedScene& detached) -> Core::Result<Scene>;
    [[nodiscard]] auto id() const noexcept -> std::uint64_t;
    [[nodiscard]] auto name() const -> Core::Result<std::string>;
    [[nodiscard]] auto rename(std::string_view name) -> Core::Result<void>;
    /** CPU-only transactional validation; resource existence is checked by consumers.
     * 仅 CPU 的事务式校验；消费者另行检查资源是否存在。 */
    [[nodiscard]] auto setEnvironment(std::optional<Environment> environment) -> Core::Result<void>;
    [[nodiscard]] auto environment() const -> Core::Result<std::optional<Environment>>;
    /** Saved optional geometric AO; CPU-only validation, null disables it.
     * 保存可选几何 AO；仅 CPU 校验，空值关闭效果。 */
    [[nodiscard]] auto setOcclusion(std::optional<Graphics::OcclusionSettings> settings)
        -> Core::Result<void>;
    [[nodiscard]] auto occlusion() const
        -> Core::Result<std::optional<Graphics::OcclusionSettings>>;
    /** Optional native UI resource; validates a portable assets/ .dotui path only.
     * Consumers validate resource bytes before publishing a scene.
     * 可选原生 UI 资源，仅验证 assets/ 下可移植 .dotui 路径；消费者发布场景前验证资源。 */
    [[nodiscard]] auto setUi(std::optional<std::string> asset) -> Core::Result<void>;
    [[nodiscard]] auto ui() const -> Core::Result<std::optional<std::string>>;
    [[nodiscard]] auto entityCount() const -> Core::Result<std::size_t>;
    [[nodiscard]] auto sceneLinks() const -> Core::Result<std::vector<SceneLink>>;
    /** Atomically replace validated authoring links. Roots must be live; mapping
     * ownership is disjoint and all target values must already be reserved.
     * Baselines use the native codec and cannot recursively contain links.
     * 原子替换经校验的编辑链接，根须存活，映射所有权不重叠且目标值已预留；
     * 基线使用原生编解码，不能递归包含链接。 */
    [[nodiscard]] auto setSceneLinks(std::span<const SceneLink> links) -> Core::Result<void>;
    /** Next unused value, or null when allocation is permanently exhausted.
     * Values survive serialization independently of live entities; querying does
     * not reserve one. 下一个未使用值，分配永久耗尽时为空；与存活实体分别持久化，查询不预留。 */
    [[nodiscard]] auto nextEntityValue() const -> Core::Result<std::optional<std::uint64_t>>;
    /** Reserve all lower values without creating entities. The floor can only
     * advance; null seals allocation, and zero/uint64 max are not valid finite
     * floors. Failure leaves the allocator intact. 预留所有更低值而不创建实体，上界只能
     * 前进；空值封闭分配，零/uint64 最大值不是有效有限上界；失败不改变分配器。 */
    [[nodiscard]] auto reserveEntityValuesBefore(std::optional<std::uint64_t> next)
        -> Core::Result<void>;
    [[nodiscard]] auto createEntity(std::string_view name = "Entity") -> Core::Result<EntityId>;
    /** Import a monotonic ID: it must be >= the next available value and below
     * uint64 max. Skipped/consumed values cannot later be reused.
     * 导入单调 ID：须不小于下个可用值且小于 uint64 最大值，跳过或消耗的值不再复用。 */
    [[nodiscard]] auto createEntityWithId(std::string_view name, std::uint64_t value)
        -> Core::Result<EntityId>;
    /** Destroy the entity and its entire subtree. 删除实体及其整个子树。 */
    [[nodiscard]] auto destroyEntity(EntityId entity) -> Core::Result<void>;
    /** Atomically append materialized content, preparing only incoming descriptions.
     * Preserve existing descriptions, scope and handles; validate grafted world
     * transforms before publication. Source globals/links are excluded, primary flags demoted,
     * native references remapped. No files, devices or script callbacks are touched.
     * 原子追加已展开内容，仅准备输入描述；保留旧描述、范围及句柄，发布前校验挂接世界变换。
     * 排除来源全局数据/链接，取消主标记，
     * 重映射原生引用；不读文件、不接触设备或执行脚本回调。 */
    [[nodiscard]] auto appendContent(const Scene& content, const Transform& placement = {},
                                     std::optional<EntityId> parent = {})
        -> Core::Result<SceneAttachment>;
    /** Remove exactly these owned values, tolerating already destroyed members.
     * Foreign/duplicate handles or surviving hierarchy/skin/animation references
     * fail before mutation; unrelated descendants are never implicitly deleted.
     * 精确移除拥有实体，容许已删除成员；外来/重复句柄或存活父级/蒙皮/动画引用在修改前失败，
     * 不隐式删除无关后代。 */
    [[nodiscard]] auto removeContent(std::span<const EntityId> entities) -> Core::Result<void>;
    [[nodiscard]] auto findEntity(std::uint64_t value) const -> Core::Result<EntityId>;
    /** Exact valid UTF-8 name lookup, sorted by creation ID. Return every duplicate
     * and an empty owned list when absent, without copying unrelated components.
     * 按有效 UTF-8 名称精确查找，按创建 ID 排序；保留全部同名实体，无匹配时返回独立空列表，
     * 不复制无关组件。 */
    [[nodiscard]] auto findEntities(std::string_view name) const
        -> Core::Result<std::vector<EntityId>>;
    [[nodiscard]] auto entity(EntityId entity) const -> Core::Result<EntityInfo>;
    /** Owned, ID-sorted animation descriptions without unrelated component copies.
     * 按 ID 排序的动画描述副本，不复制无关组件。 */
    [[nodiscard]] auto animationPlayers() const -> Core::Result<std::vector<AnimationPlayerInfo>>;
    /** Sorted controller owners without copying unrelated component data.
     * 返回按 ID 排序的角色控制器所有者，不复制无关组件。 */
    [[nodiscard]] auto characterControllers() const -> Core::Result<std::vector<EntityId>>;
    /** Validate local live targets without IO. At most 128 players, aggregate
     * 10000 bindings/1024 clip paths. External-target deletion is rejected until
     * its player is removed/rebound. Playback checks asset/physics authority.
     * 不读文件，校验场景内有效目标；至多 128 播放器，合计万绑定/1024 片段路径。
     * 删除其他播放器引用的目标前须移除/改绑播放器，播放阶段校验资源及物理写入权。 */
    [[nodiscard]] auto setAnimationPlayer(EntityId entity, std::optional<AnimationPlayer> player)
        -> Core::Result<void>;
    /** Store at most 128 skin bindings. Require a mesh and unique live local joints.
     * Files/count matching are consumer responsibilities. Referenced joint deletion
     * requires removing/rebinding external owners; remove skin before its mesh.
     * 至多保存 128 蒙皮绑定，要求网格及唯一有效的场景内关节；文件/数量由消费方校验。
     * 删除被引用关节须先移除/改绑外部所有者；须先移除蒙皮再移除其网格。 */
    [[nodiscard]] auto setSkin(EntityId entity, std::optional<SkinBinding> skin)
        -> Core::Result<void>;
    /** Snapshots sorted by creation ID for deterministic consumers.
     * 按创建 ID 排序的快照，供消费者确定性使用。 */
    [[nodiscard]] auto entities() const -> Core::Result<std::vector<EntityInfo>>;
    /** Owned ID-sorted subsystem snapshots; never copy unrelated components.
     * Every live entity has a transform; colliders contain exactly one physical
     * description; audioSources includes only source owners. Results retain values
     * after mutation/destruction. Same owner/scope checks as entities().
     * 子系统快照按 ID 排序并拥有数据，不复制无关组件；所有有效实体均有变换，碰撞体恰有
     * 一种物理描述，音源列表仅含音源所有者。修改/销毁后快照保留原值，所有者/范围校验同 entities()。
     */
    [[nodiscard]] auto transforms() const -> Core::Result<std::vector<TransformInfo>>;
    [[nodiscard]] auto colliders() const -> Core::Result<std::vector<ColliderInfo>>;
    [[nodiscard]] auto audioSources() const -> Core::Result<std::vector<AudioSourceInfo>>;
    [[nodiscard]] auto renameEntity(EntityId entity, std::string_view name) -> Core::Result<void>;
    /** Owned local pose only, without copying names/scripts/other components.
     * Same owner/scope/liveness checks as entity().
     * 仅复制局部姿态，不复制名称/脚本等组件；与 entity() 共用所有者/范围/有效性校验。 */
    [[nodiscard]] auto transform(EntityId entity) const -> Core::Result<Transform>;
    /** Preserve float-unit rotation components exactly; normalize other rotations.
     * This keeps scene round trips/history stable despite float rounding.
     * Reject singular/non-finite transforms or any resulting
     * world-matrix overflow. Failures leave the previous scene state intact.
     * 精确保留浮点单位旋转分量，其余旋转归一化，避免场景往返及历史状态漂移。
     * 拒绝奇异/非有限变换及世界矩阵溢出；失败保留此前状态。
     */
    [[nodiscard]] auto setTransform(EntityId entity, const Transform& transform)
        -> Core::Result<void>;
    /** Reparent in local space by default; World preserves the old world matrix
     * and rejects unrepresentable shear/singularity. Reject cycles, foreign/stale handles,
     * excessive depth, and world overflow; failure is transactional.
     * 默认保留局部变换；World 保持原世界矩阵并拒绝无法表示的剪切/奇异变换。
     * 拒绝环、跨场景/失效句柄、过深层级及世界溢出。
     * 失败不改变状态。
     */
    [[nodiscard]] auto setParent(EntityId entity, std::optional<EntityId> parent,
                                 ParentSpace space = ParentSpace::Local) -> Core::Result<void>;
    /** Batch updates reject duplicate handles and validate the final scene once.
     * All changes roll back together on failure; intermediate states are private.
     * 批量更新拒绝重复句柄，并一次校验最终场景；失败时全部回滚，中间状态不对外暴露。 */
    [[nodiscard]] auto setTransforms(std::span<const TransformUpdate> updates)
        -> Core::Result<void>;
    [[nodiscard]] auto setParents(std::span<const ParentUpdate> updates,
                                  ParentSpace space = ParentSpace::Local) -> Core::Result<void>;
    /** Group minimal selected roots without changing world poses or internal
     * references. Shared immediate parent is retained, otherwise use scene root;
     * pivot is the last selected entity's world origin. Ancestor/descendant
     * overlap is processed once. An authoring allocator may supply a monotonic ID.
     * Failures publish neither hierarchy nor identity consumption.
     * 将选择的最小子树根成组，保持世界姿态和内部引用；相同直接父级保留，否则置于场景根。
     * 枢轴为末项实体的世界原点，祖孙重叠只处理一次；编辑分配器可提供单调 ID。
     * 失败不发布层级变更或消耗标识。 */
    [[nodiscard]] auto groupEntities(std::span<const EntityId> entities, std::string_view name,
                                     std::optional<std::uint64_t> value = std::nullopt)
        -> Core::Result<EntityId>;
    /** Remove component-free containers while preserving every surviving world
     * pose, including nested selections. Referenced skin joints/animation targets
     * cannot be removed. Returned affected children are owned and ID-sorted;
     * existing handles retain their scope, removed values are never reused.
     * 拆除无功能组件的容器，保持所有存活实体的世界姿态，支持嵌套选择；被蒙皮/动画
     * 引用的节点不可删除。返回拥有且按 ID 排序的受影响子项，存活句柄范围不变，删除值不复用。 */
    [[nodiscard]] auto ungroupEntities(std::span<const EntityId> entities)
        -> Core::Result<std::vector<EntityId>>;
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
    /** Validate without creating a physics world. Failure preserves components;
     * remove a conflicting rigid body explicitly before attaching a controller.
     * 校验而不创建物理世界，失败保留组件；添加前须明确移除冲突刚体。 */
    [[nodiscard]] auto setCharacterController(EntityId entity,
                                              std::optional<CharacterController> character)
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
    friend class DetachedScene;
    struct Impl;
    explicit Scene(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };

  /** Quiescent CPU ownership, consumable once. Moving an ordinary Scene still preserves
   * its creating-thread restriction; only explicit detach/adopt changes owner and scope.
   * Destruction is safe on any thread after exclusive synchronized handoff.
   * 静止的 CPU 独占所有权，仅能消费一次；普通 Scene 移动仍保留创建线程限制，只有显式
   * detach/adopt 改变拥有线程及范围。独占同步交接后可在任意线程销毁。 */
  class DetachedScene {
  public:
    DetachedScene(const DetachedScene&) = delete;
    auto operator=(const DetachedScene&) -> DetachedScene& = delete;
    DetachedScene(DetachedScene&&) noexcept;
    auto operator=(DetachedScene&&) noexcept -> DetachedScene&;
    ~DetachedScene();

  private:
    friend class Scene;
    explicit DetachedScene(std::unique_ptr<Scene::Impl> scene) noexcept;
    std::unique_ptr<Scene::Impl> m_scene;
  };
}  // namespace Dot::World

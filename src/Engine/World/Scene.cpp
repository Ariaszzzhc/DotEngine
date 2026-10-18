module;

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <entt/entity/registry.hpp>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <span>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.World.Scene;

import Dot.Core.Result;
import Dot.Core.Text;
import Dot.Core.Paths;
import Dot.Graphics.Lighting;
import Dot.Math;
import Dot.Physics.Body;
import Dot.Physics.Character;
import Dot.Audio.Source;
import Dot.Script.Source;
import Dot.World.SceneFiles;
import Dot.World.SceneIdentity;
import Dot.World.SceneContent;

namespace Dot::World {
  namespace {
    std::atomic<std::uint64_t> nextScene{1};

    auto sceneIdentity() -> Core::Result<std::uint64_t> {
      auto value = nextScene.load();

      while (value != std::numeric_limits<std::uint64_t>::max()) {
        if (nextScene.compare_exchange_weak(value, value + 1)) {
          return value;
        }
      }

      return Core::fail(Core::ErrorCode::InvalidState, "Scene identity space exhausted");
    }

    auto validateName(std::string_view name) -> Core::Result<void> {
      if (name.empty() || name.size() > 256 || name.find('\0') != std::string_view::npos
          || !Core::isValidUtf8(name)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Names require 1..256 UTF-8 bytes without NUL");
      }

      return {};
    }

    /** Retain float-unit components: repeated normalization can move a small component
     * by one ULP on every load while the large components remain unchanged.
     * Two float epsilons bound squared-length roundoff; matrices still normalize.
     * 保留浮点单位旋转，避免每次载入时小分量漂移一 ULP、其他分量不变。
     * 两倍 float epsilon 覆盖长度平方的舍入误差；矩阵计算仍执行归一化。 */
    auto storedRotation(Math::Quat value) -> Core::Result<Math::Quat> {
      auto normalized = Math::normalized(value);

      if (!normalized) {
        return std::unexpected(normalized.error());
      }

      const double x = value.x;
      const double y = value.y;
      const double z = value.z;
      const double w = value.w;
      const double squaredLength = x * x + y * y + z * z + w * w;

      if (std::abs(squaredLength - 1) <= 2 * std::numeric_limits<float>::epsilon()) {
        return value;
      }

      return *normalized;
    }

    /** Compare scalar storage, not padded object bytes; signed zeros are intentional data.
     * 比较标量存储而非含填充的对象字节，保留有意设置的零符号。 */
    auto sameTransform(const Transform& a, const Transform& b) -> bool {
      const auto same = [](float left, float right) {
        return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
      };
      return same(a.position.x, b.position.x) && same(a.position.y, b.position.y)
             && same(a.position.z, b.position.z) && same(a.rotation.x, b.rotation.x)
             && same(a.rotation.y, b.rotation.y) && same(a.rotation.z, b.rotation.z)
             && same(a.rotation.w, b.rotation.w) && same(a.scale.x, b.scale.x)
             && same(a.scale.y, b.scale.y) && same(a.scale.z, b.scale.z);
    }

    struct Identity {
      std::uint64_t value;
      std::string name;
    };

    struct Hierarchy {
      std::optional<std::uint64_t> parent;
    };

    auto cameraProjection(const Camera& camera, float aspect) -> Core::Result<Math::Mat4> {
      if (!std::isfinite(camera.verticalFovDegrees) || camera.verticalFovDegrees <= 0
          || camera.verticalFovDegrees >= 180 || !std::isfinite(camera.orthographicHeight)
          || camera.orthographicHeight <= 0 || !std::isfinite(aspect) || aspect <= 0) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid camera field of view, height, or aspect");
      }

      switch (camera.projection) {
        case Projection::Perspective:
          return Math::perspective(camera.verticalFovDegrees, aspect, camera.nearPlane,
                                   camera.farPlane);
        case Projection::Orthographic: {
          const float halfHeight = camera.orthographicHeight * 0.5F;
          const float halfWidth = halfHeight * aspect;
          return Math::orthographic(-halfWidth, halfWidth, -halfHeight, halfHeight,
                                    camera.nearPlane, camera.farPlane);
        }
        default:
          return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown camera projection");
      }
    }

    auto validateMeshRenderer(const MeshRenderer& mesh) -> Core::Result<void> {
      const auto tint = mesh.tint;

      if (!Math::isFinite(tint) || tint.x < 0 || tint.x > 1 || tint.y < 0 || tint.y > 1
          || tint.z < 0 || tint.z > 1 || tint.w < 0 || tint.w > 1) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Mesh tint requires finite values in [0,1]");
      }

      if (mesh.kind == MeshKind::Cube || mesh.kind == MeshKind::Quad) {
        if (!mesh.asset.empty()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Built-in mesh kinds require an empty asset path");
        }

        return {};
      }

      if (mesh.kind != MeshKind::Asset || mesh.asset.empty() || mesh.asset.size() > 4096
          || mesh.asset.find('\\') != std::string::npos || mesh.asset.find(':') != std::string::npos
          || mesh.asset.find('\0') != std::string::npos || !Core::isValidUtf8(mesh.asset)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid mesh kind or portable asset path");
      }

      try {
        const std::filesystem::path path(std::u8string(mesh.asset.begin(), mesh.asset.end()));

        if (path.has_root_path()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Mesh asset path must be project-relative");
        }

        for (const auto& component : path) {
          if (component == "..") {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Mesh asset path cannot traverse a parent directory");
          }
        }

        auto extension = path.extension().generic_u8string();

        for (auto& character : extension) {
          if (character >= u8'A' && character <= u8'Z') {
            character = static_cast<char8_t>(character + (u8'a' - u8'A'));
          }
        }

        if (extension != u8".dotmesh" && extension != u8".obj") {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Mesh asset extension must be .dotmesh or .obj");
        }
      } catch (const std::filesystem::filesystem_error& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          std::format("Decode mesh asset path: {}", error.what()));
      }

      return {};
    }
  }  // namespace

  struct Scene::Impl {
    std::thread::id thread{std::this_thread::get_id()};
    std::uint64_t identity{};
    std::uint64_t nextEntity{1};
    std::string name;
    entt::registry registry;
    std::optional<Environment> environment;
    std::optional<std::string> ui;
    std::optional<std::string> m_navigation;
    std::optional<Graphics::OcclusionSettings> occlusion;
    std::vector<SceneLink> links;
    std::unordered_map<std::uint64_t, entt::entity> lookup;

    /** Count nodes including the root so a cached prefix cannot hide excessive depth.
     * 深度计入根节点，避免缓存前缀掩盖超深层级。 */
    struct WorldPose {
      Math::Mat4 matrix;
      std::size_t depth;
    };

    mutable std::unordered_map<std::uint64_t, WorldPose> worldCache;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> m_children;
    bool m_childrenReady{};
    std::uint64_t m_animationBindingRevision{1};

    auto invalidateAnimationBindings() noexcept -> void {
      if (m_animationBindingRevision != std::numeric_limits<std::uint64_t>::max()) {
        ++m_animationBindingRevision;
      }
    }

    auto invalidateWorld() -> void {
      worldCache.clear();
      m_children.clear();
      m_childrenReady = false;
    }

    /** Cache only committed hierarchy edges; structural publication invalidates this index.
     * Build before local transform writes, so allocation/validation failure cannot mutate poses.
     * 仅缓存已提交层级边，结构发布时失效；在局部变换写入前构建，分配/校验失败不改姿态。 */
    auto affectedTransforms(std::span<const std::uint64_t> roots)
        -> Core::Result<std::vector<std::uint64_t>> {
      if (!m_childrenReady) {
        std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> children;

        for (const auto& [value, native] : lookup) {
          const auto parent = registry.get<Hierarchy>(native).parent;

          if (parent) {
            if (!lookup.contains(*parent)) {
              return Core::fail(Core::ErrorCode::InvalidState,
                                "Hierarchy index refers to a missing entity");
            }

            children[*parent].push_back(value);
          }
        }

        m_children.swap(children);
        m_childrenReady = true;
      }

      std::vector<std::uint64_t> affected;
      std::unordered_set<std::uint64_t> seen;
      affected.reserve(roots.size());
      seen.reserve(roots.size());

      for (const auto root : roots) {
        if (seen.insert(root).second) {
          affected.push_back(root);
        }
      }

      for (std::size_t index = 0; index < affected.size(); ++index) {
        const auto children = m_children.find(affected[index]);

        if (children != m_children.end()) {
          for (const auto child : children->second) {
            if (seen.insert(child).second) {
              affected.push_back(child);
            }
          }
        }

        if (affected.size() > lookup.size()) {
          return Core::fail(Core::ErrorCode::InvalidState, "Invalid affected hierarchy closure");
        }
      }

      return affected;
    }

    struct HierarchyChange {
      entt::entity native;
      std::optional<std::uint64_t> oldParent;
      std::optional<std::uint64_t> newParent;
      Transform oldTransform;
      Transform newTransform;
    };

    auto applyHierarchy(std::span<const HierarchyChange> changes) -> Core::Result<void> {
      for (const auto& change : changes) {
        registry.get<Hierarchy>(change.native).parent = change.newParent;
        registry.get<Transform>(change.native) = change.newTransform;
      }

      auto verified = verifyMutation([&] {
        for (const auto& change : changes) {
          registry.get<Hierarchy>(change.native).parent = change.oldParent;
          registry.get<Transform>(change.native) = change.oldTransform;
        }
      });

      if (verified && std::ranges::any_of(changes, [](const auto& change) {
            return change.oldParent != change.newParent;
          })) {
        invalidateAnimationBindings();
      }

      return verified;
    }

    auto add(std::string_view label, std::optional<std::uint64_t> requested)
        -> Core::Result<EntityId> {
      auto checked = check();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      checked = validateName(label);

      if (!checked) {
        return std::unexpected(checked.error());
      }

      auto& scene = *this;

      if (scene.lookup.size() >= 10000
          || scene.nextEntity == std::numeric_limits<std::uint64_t>::max()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Scene entity capacity exhausted");
      }

      const auto value = requested.value_or(scene.nextEntity);

      if (value < scene.nextEntity || value == std::numeric_limits<std::uint64_t>::max()) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Imported entity ID must be monotonic and below uint64 max");
      }

      const auto native = scene.registry.create();

      try {
        scene.registry.emplace<Identity>(native, value, std::string(label));
        scene.registry.emplace<Transform>(native);
        scene.registry.emplace<Hierarchy>(native);
        scene.lookup.emplace(value, native);
      } catch (...) {
        scene.registry.destroy(native);
        throw;
      }

      scene.nextEntity = value + 1;
      scene.m_childrenReady = false;
      return EntityId{scene.identity, value};
    }

    auto check() const -> Core::Result<void> {
      if (thread != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Scene operations require its creating thread");
      }

      return {};
    }

    auto resolve(EntityId entity) const -> Core::Result<entt::entity> {
      auto checked = check();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (entity.scene != identity) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Entity belongs to another scene");
      }

      auto found = lookup.find(entity.value);

      if (found == lookup.end()) {
        return Core::fail(Core::ErrorCode::NotFound, "Entity no longer exists");
      }

      if (!registry.valid(found->second)) {
        return Core::fail(Core::ErrorCode::InvalidState, "Entity registry and handle map disagree");
      }

      return found->second;
    }

    /** Sort compact identities before materializing owned descriptions; moving
     * complete entity DTOs during sort repeatedly moves large optional members.
     * 先排序紧凑身份，再构造拥有描述，避免排序完整实体时移动无关的大型可选载荷。 */
    auto ordered() const -> std::vector<std::pair<std::uint64_t, entt::entity>> {
      std::vector<std::pair<std::uint64_t, entt::entity>> result;
      result.reserve(lookup.size());

      for (const auto& entry : lookup) {
        result.push_back(entry);
      }

      std::ranges::sort(result, {}, &decltype(result)::value_type::first);
      return result;
    }

    auto snapshot(entt::entity native) const -> EntityInfo {
      const auto& value = registry.get<Identity>(native);
      const auto parent = registry.get<Hierarchy>(native).parent;
      const auto* camera = registry.try_get<Camera>(native);
      const auto* mesh = registry.try_get<MeshRenderer>(native);
      const auto* body = registry.try_get<RigidBody>(native);
      const auto* source = registry.try_get<AudioSource>(native);
      const auto* listener = registry.try_get<AudioListener>(native);
      const auto* script = registry.try_get<ScriptComponent>(native);
      const auto* material = registry.try_get<MaterialBinding>(native);
      const auto* light = registry.try_get<Light>(native);
      const auto* animation = registry.try_get<AnimationPlayer>(native);
      const auto* skin = registry.try_get<SkinBinding>(native);
      const auto* character = registry.try_get<CharacterController>(native);
      return {{identity, value.value},
              value.name,
              registry.get<Transform>(native),
              parent ? std::optional{EntityId{identity, *parent}} : std::nullopt,
              camera ? std::optional{*camera} : std::nullopt,
              mesh ? std::optional{*mesh} : std::nullopt,
              body ? std::optional{*body} : std::nullopt,
              source ? std::optional{*source} : std::nullopt,
              listener ? std::optional{*listener} : std::nullopt,
              script ? std::optional{*script} : std::nullopt,
              material ? std::optional{*material} : std::nullopt,
              light ? std::optional{*light} : std::nullopt,
              animation ? std::optional{*animation} : std::nullopt,
              skin ? std::optional{*skin} : std::nullopt,
              character ? std::optional{*character} : std::nullopt};
    }

    auto world(std::uint64_t value) const -> Core::Result<Math::Mat4> {
      std::vector<std::uint64_t> chain;
      std::optional<std::uint64_t> current = value;
      Math::Mat4 matrix;
      std::size_t depth{};

      while (current) {
        if (chain.size() == 256) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Hierarchy contains a cycle or exceeds depth 256");
        }

        auto found = lookup.find(*current);

        if (found == lookup.end()) {
          return Core::fail(Core::ErrorCode::InvalidState, "Hierarchy refers to a missing entity");
        }

        const auto cached = worldCache.find(*current);

        if (cached != worldCache.end()) {
          matrix = cached->second.matrix;
          depth = cached->second.depth;
          break;
        }

        chain.push_back(*current);
        current = registry.get<Hierarchy>(found->second).parent;
      }

      if (depth > 256 || chain.size() > 256 - depth) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Hierarchy contains a cycle or exceeds depth 256");
      }

      for (auto iterator = chain.rbegin(); iterator != chain.rend(); ++iterator) {
        const auto& transform = registry.get<Transform>(lookup.at(*iterator));
        auto local = Math::compose(transform.position, transform.rotation, transform.scale);

        if (!local) {
          return std::unexpected(local.error());
        }

        auto combined = Math::multiply(matrix, *local);

        if (!combined) {
          return std::unexpected(combined.error());
        }

        matrix = *combined;
        worldCache.emplace(*iterator, WorldPose{matrix, ++depth});
      }

      return matrix;
    }

    /** Empty closure is structural: clear and validate all worlds. A transform-only closure
     * contains every changed descendant, so unrelated committed worlds remain authoritative.
     * 空闭包代表结构修改，清缓存并校验全部世界变换；仅变换闭包含全部变化后代，
     * 无关已提交分支的世界变换仍有效。 */
    template <typename F>
    auto verifyMutation(F&& rollback, std::span<const std::uint64_t> affected = {})
        -> Core::Result<void> {
      if (affected.empty()) {
        invalidateWorld();
      } else {
        for (const auto value : affected) {
          worldCache.erase(value);
        }
      }

      try {
        const auto verify = [&](std::uint64_t value) -> Core::Result<void> {
          auto matrix = world(value);

          if (!matrix) {
            return std::unexpected(matrix.error());
          }

          if (registry.all_of<Camera>(lookup.at(value))) {
            auto view = Math::inverse(*matrix);

            if (!view) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Camera world transform cannot be inverted");
            }
          }

          return {};
        };

        const auto checked = [&](std::uint64_t value) -> Core::Result<void> {
          auto result = verify(value);

          if (!result) {
            rollback();
            invalidateWorld();
          }

          return result;
        };

        if (affected.empty()) {
          for (const auto& entry : lookup) {
            auto result = checked(entry.first);

            if (!result) {
              return result;
            }
          }
        } else {
          for (const auto value : affected) {
            auto result = checked(value);

            if (!result) {
              return result;
            }
          }
        }
      } catch (...) {
        rollback();
        invalidateWorld();
        throw;
      }

      return {};
    }
  };

  Scene::Scene(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Scene::Scene(Scene&&) noexcept = default;

  auto Scene::operator=(Scene&&) noexcept -> Scene& = default;

  Scene::~Scene() = default;

  auto Scene::create(std::string_view name) -> Core::Result<Scene> {
    auto checked = validateName(name);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto identity = sceneIdentity();

    if (!identity) {
      return std::unexpected(identity.error());
    }

    auto impl = std::make_unique<Impl>();
    impl->identity = *identity;
    impl->name = name;
    return Scene(std::move(impl));
  }

  DetachedScene::DetachedScene(std::unique_ptr<Scene::Impl> scene) noexcept
      : m_scene(std::move(scene)) {
  }

  DetachedScene::DetachedScene(DetachedScene&&) noexcept = default;

  auto DetachedScene::operator=(DetachedScene&&) noexcept -> DetachedScene& = default;

  DetachedScene::~DetachedScene() = default;

  auto Scene::detach() -> Core::Result<DetachedScene> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene was moved or detached");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return DetachedScene(std::move(m_impl));
  }

  auto Scene::adopt(DetachedScene& detached) -> Core::Result<Scene> {
    if (!detached.m_scene) {
      return Core::fail(Core::ErrorCode::InvalidState, "Detached scene was moved or consumed");
    }

    auto identity = sceneIdentity();

    if (!identity) {
      return std::unexpected(identity.error());
    }

    detached.m_scene->identity = *identity;
    detached.m_scene->thread = std::this_thread::get_id();
    return Scene(std::move(detached.m_scene));
  }

  auto Scene::id() const noexcept -> std::uint64_t {
    return m_impl ? m_impl->identity : 0;
  }

  auto Scene::name() const -> Core::Result<std::string> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->name;
  }

  auto Scene::rename(std::string_view name) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    checked = validateName(name);

    if (!checked) {
      return checked;
    }

    m_impl->name = name;
    return {};
  }

  auto Scene::setEnvironment(std::optional<Environment> environment) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (environment) {
      if (!Core::isPortableRelativePath(environment->asset) || !environment->asset.ends_with(".hdr")
          || !std::isfinite(environment->intensity) || environment->intensity < 0
          || environment->intensity > 64 || !std::isfinite(environment->rotationDegrees)
          || std::abs(environment->rotationDegrees) > 360) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid scene environment path/settings");
      }

      checked = Graphics::validateEnvironmentConfig(environment->processing);

      if (!checked) {
        return checked;
      }
    }

    m_impl->environment = std::move(environment);
    return {};
  }

  auto Scene::environment() const -> Core::Result<std::optional<Environment>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->environment;
  }

  auto Scene::setOcclusion(std::optional<Graphics::OcclusionSettings> settings)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (settings) {
      checked = Graphics::validateOcclusion(*settings);

      if (!checked) {
        return checked;
      }
    }

    m_impl->occlusion = settings;
    return {};
  }

  auto Scene::occlusion() const -> Core::Result<std::optional<Graphics::OcclusionSettings>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();
    return checked ? Core::Result<std::optional<Graphics::OcclusionSettings>>(m_impl->occlusion)
                   : std::unexpected(checked.error());
  }

  auto Scene::setUi(std::optional<std::string> asset) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (asset
        && (asset->size() > 1024 || !Core::isPortableRelativePath(*asset)
            || !asset->starts_with("assets/") || !asset->ends_with(".dotui"))) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Scene UI requires a portable assets/*.dotui path");
    }

    m_impl->ui = std::move(asset);
    return {};
  }

  auto Scene::ui() const -> Core::Result<std::optional<std::string>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();
    return checked ? Core::Result<std::optional<std::string>>(m_impl->ui)
                   : std::unexpected(checked.error());
  }

  auto Scene::setNavigation(std::optional<std::string> asset) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (asset
        && (asset->size() > 1024 || !Core::isPortableRelativePath(*asset)
            || !asset->starts_with("assets/") || !asset->ends_with(".dotnav"))) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Scene navigation requires a portable assets/*.dotnav path");
    }

    m_impl->m_navigation = std::move(asset);
    return {};
  }

  auto Scene::navigation() const -> Core::Result<std::optional<std::string>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();
    return checked ? Core::Result<std::optional<std::string>>(m_impl->m_navigation)
                   : std::unexpected(checked.error());
  }

  auto Scene::entityCount() const -> Core::Result<std::size_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->lookup.size();
  }

  auto Scene::nextEntityValue() const -> Core::Result<std::optional<std::uint64_t>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->nextEntity == std::numeric_limits<std::uint64_t>::max()
               ? std::optional<std::uint64_t>{}
               : std::optional{m_impl->nextEntity};
  }

  auto Scene::sceneLinks() const -> Core::Result<std::vector<SceneLink>> {
    auto ready = entityCount();

    if (!ready) {
      return std::unexpected(ready.error());
    }

    try {
      return m_impl->links;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Read scene links: " + std::string(error.what()));
    }
  }

  auto Scene::setSceneLinks(std::span<const SceneLink> links) -> Core::Result<void> {
    auto ready = entityCount();

    if (!ready) {
      return std::unexpected(ready.error());
    }

    try {
      if (links.size() > 10000) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Too many scene links");
      }

      std::unordered_set<std::uint64_t> owned;
      std::size_t bytes{}, count{}, spaceCount{}, observedCount{};
      std::vector<SceneLink> prepared(links.begin(), links.end());

      for (const auto& link : links) {
        if (!m_impl->lookup.contains(link.root) || !owned.insert(link.root).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Scene link root is missing or shared");
        }
      }

      for (auto& link : prepared) {
        bytes += link.baseline.size();
        count += link.mapping.size();
        spaceCount += link.namespaces.size();

        for (const auto& space : link.namespaces) {
          observedCount += space.entities.size();
        }

        if (!Core::isPortableRelativePath(link.asset) || !link.asset.ends_with(".dotscene")
            || (!link.asset.starts_with("assets/") && !link.asset.starts_with("scenes/"))
            || link.asset.find("/.dot-stage-") != std::string::npos || link.baseline.empty()
            || bytes > 64 * 1024 * 1024 || count > 10000 || spaceCount > 10000
            || observedCount > 100000) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid scene link source or capacity");
        }

        /** The file embeds baselines three JSON levels below the scene root.
         * 文件将基线嵌入场景根之下三层，预留相应解析深度。 */
        const auto data = Serialization::Detail::parseJson(link.baseline, 61);

        if (data.contains("links")) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Scene link baselines cannot nest links");
        }

        auto baseline = decodeScene(link.baseline);

        if (!baseline) {
          return Core::fail(baseline.error().code,
                            "Scene link baseline: " + baseline.error().message);
        }

        const auto nodes = baseline->entities().value();

        if (nodes.size() != link.mapping.size()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Scene link mapping must cover its baseline");
        }

        std::unordered_set<std::uint64_t> sources;
        std::vector<SceneSourceOrigin> origins;

        for (const auto& mapping : link.mapping) {
          if (!baseline->findEntity(mapping.source) || !sources.insert(mapping.source).second
              || mapping.target == 0 || mapping.target >= m_impl->nextEntity
              || !owned.insert(mapping.target).second) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Invalid or overlapping scene link mapping");
          }

          origins.push_back({mapping.source, mapping.key});
        }

        auto identity = validateSceneSourceIdentity(link.asset, origins, link.namespaces);

        if (!identity) {
          return identity;
        }

        for (auto& space : link.namespaces) {
          std::ranges::sort(space.entities);
        }

        std::ranges::sort(link.namespaces, {}, &SceneSourceNamespace::prefix);
        std::ranges::sort(link.mapping, {}, &SceneLinkMapping::source);

        auto canonical = encodeScene(*baseline);

        if (!canonical) {
          return std::unexpected(canonical.error());
        }

        bytes -= link.baseline.size();
        link.baseline = std::move(*canonical);
        bytes += link.baseline.size();

        if (bytes > 64 * 1024 * 1024) {
          return Core::fail(Core::ErrorCode::Unavailable, "Scene link baselines exceed 64 MiB");
        }
      }

      m_impl->links.swap(prepared);
      return {};
    } catch (const std::invalid_argument& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Set scene links: " + std::string(error.what()));
    } catch (const Serialization::Detail::Json::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Set scene links: " + std::string(error.what()));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Set scene links: " + std::string(error.what()));
    }
  }

  auto Scene::reserveEntityValuesBefore(std::optional<std::uint64_t> next) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto value = next.value_or(maximum);

    if ((next && (*next == 0 || *next == maximum)) || value < m_impl->nextEntity) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Entity allocation floor must be monotonic, positive or exhausted");
    }

    m_impl->nextEntity = value;
    return {};
  }

  auto Scene::createEntity(std::string_view name) -> Core::Result<EntityId> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    return m_impl->add(name, std::nullopt);
  }

  auto Scene::createEntityWithId(std::string_view name, std::uint64_t value)
      -> Core::Result<EntityId> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    return m_impl->add(name, value);
  }

  auto Scene::findEntity(std::uint64_t value) const -> Core::Result<EntityId> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    EntityId id{m_impl->identity, value};
    auto native = m_impl->resolve(id);

    if (!native) {
      return std::unexpected(native.error());
    }

    return id;
  }

  auto Scene::entity(EntityId entity) const -> Core::Result<EntityInfo> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    return m_impl->snapshot(*native);
  }

  auto Scene::transform(EntityId entity) const -> Core::Result<Transform> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);
    return native ? Core::Result<Transform>{m_impl->registry.get<Transform>(*native)}
                  : std::unexpected(native.error());
  }

  auto Scene::setSkin(EntityId entity, std::optional<SkinBinding> skin) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene was moved");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    if (!skin) {
      m_impl->registry.remove<SkinBinding>(*native);
      return {};
    }

    const auto* body = m_impl->registry.try_get<RigidBody>(*native);

    if (body && body->shape == Physics::Shape::Mesh) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "A deforming skin cannot own a static mesh collider");
    }

    auto valid = Animation::validateSkinProperties(*skin);

    if (!valid) {
      return valid;
    }

    if (!m_impl->registry.any_of<MeshRenderer>(*native)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Skin binding requires a mesh renderer");
    }

    std::size_t count{1};

    for (const auto entry : m_impl->registry.view<SkinBinding>()) {
      count += entry != *native ? 1U : 0U;
    }

    if (count > 128) {
      return Core::fail(Core::ErrorCode::Unavailable, "Scene exceeds 128 skin bindings");
    }

    for (const auto joint : skin->joints) {
      if (!m_impl->lookup.contains(joint)) {
        return Core::fail(Core::ErrorCode::NotFound,
                          "Skin joint does not exist: " + std::to_string(joint));
      }
    }

    m_impl->registry.emplace_or_replace<SkinBinding>(*native, std::move(*skin));
    return {};
  }

  auto Scene::characterControllers() const -> Core::Result<std::vector<EntityId>> {
    auto ready = entityCount();

    if (!ready) {
      return std::unexpected(ready.error());
    }

    try {
      std::vector<EntityId> result;
      const auto view = m_impl->registry.view<const CharacterController, const Identity>();

      for (const auto native : view) {
        result.push_back({m_impl->identity, view.get<const Identity>(native).value});
      }

      std::ranges::sort(result, {}, &EntityId::value);
      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Enumerate character controllers: " + std::string(error.what()));
    }
  }

  auto Scene::animationPlayers() const -> Core::Result<std::vector<AnimationPlayerInfo>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    std::vector<AnimationPlayerInfo> result;

    for (const auto native : m_impl->registry.view<AnimationPlayer>()) {
      result.push_back({{m_impl->identity, m_impl->registry.get<Identity>(native).value},
                        m_impl->registry.get<AnimationPlayer>(native)});
    }

    std::ranges::sort(result, {}, [](const auto& player) { return player.entity.value; });
    return result;
  }

  auto Scene::animationBindingRevision() const -> Core::Result<std::uint64_t> {
    auto ready = entityCount();

    if (!ready) {
      return std::unexpected(ready.error());
    }

    return m_impl->m_animationBindingRevision;
  }

  auto Scene::setAnimationPlayer(EntityId entity, std::optional<AnimationPlayer> player)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    if (!player) {
      if (m_impl->registry.remove<AnimationPlayer>(*native)) {
        m_impl->invalidateAnimationBindings();
      }

      return {};
    }

    auto checked = Animation::validatePlayerProperties(*player);

    if (!checked) {
      return checked;
    }

    std::size_t count{1}, targets{player->targets.size()}, clips{player->clips.size()};
    std::size_t masks{};

    for (const auto& layer : player->layers) {
      masks += layer.mask.size();
    }

    for (const auto entry : m_impl->registry.view<AnimationPlayer>()) {
      if (entry != *native) {
        const auto& existing = m_impl->registry.get<AnimationPlayer>(entry);
        ++count;
        targets += existing.targets.size();
        clips += existing.clips.size();

        for (const auto& layer : existing.layers) {
          masks += layer.mask.size();
        }
      }
    }

    if (count > 128 || targets > 10000 || clips > 1024 || masks > 30000) {
      return Core::fail(Core::ErrorCode::Unavailable,
                        "Scene animation authoring capacity exceeded");
    }

    for (const auto& target : player->targets) {
      if (!m_impl->lookup.contains(target.entity)) {
        return Core::fail(Core::ErrorCode::NotFound, "Animation target does not exist in scene");
      }
    }

    const auto* previous = m_impl->registry.try_get<AnimationPlayer>(*native);
    const bool changed = !previous || *previous != *player;
    m_impl->registry.emplace_or_replace<AnimationPlayer>(*native, std::move(*player));

    if (changed) {
      m_impl->invalidateAnimationBindings();
    }

    return {};
  }

  auto Scene::children(EntityId parent) const -> Core::Result<std::vector<EntityId>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(parent);

    if (!native) {
      return std::unexpected(native.error());
    }

    try {
      std::vector<EntityId> result;
      const auto view = m_impl->registry.view<const Hierarchy, const Identity>();

      for (const auto child : view) {
        if (view.get<const Hierarchy>(child).parent == parent.value) {
          result.push_back({m_impl->identity, view.get<const Identity>(child).value});
        }
      }

      std::ranges::sort(result, {}, &EntityId::value);
      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Read scene children: " + std::string(error.what()));
    }
  }

  auto Scene::findEntities(std::string_view name) const -> Core::Result<std::vector<EntityId>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    checked = validateName(name);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      std::vector<EntityId> matches;

      for (const auto native : m_impl->registry.view<Identity>()) {
        const auto& identity = m_impl->registry.get<Identity>(native);

        if (identity.name == name) {
          matches.push_back({m_impl->identity, identity.value});
        }
      }

      std::ranges::sort(matches, {}, &EntityId::value);
      return matches;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Find named scene entities: " + std::string(error.what()));
    }
  }

  auto Scene::entities() const -> Core::Result<std::vector<EntityInfo>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      const auto order = m_impl->ordered();
      std::vector<EntityInfo> entities;
      entities.reserve(order.size());

      for (const auto& [id, native] : order) {
        (void)id;
        entities.push_back(m_impl->snapshot(native));
      }

      return entities;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Snapshot scene entities: " + std::string(error.what()));
    }
  }

  auto Scene::transforms() const -> Core::Result<std::vector<TransformInfo>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      const auto order = m_impl->ordered();
      std::vector<TransformInfo> result;
      result.reserve(order.size());

      for (const auto& [id, native] : order) {
        const auto parent = m_impl->registry.get<Hierarchy>(native).parent;
        result.push_back(
            {{m_impl->identity, id},
             m_impl->registry.get<Transform>(native),
             parent ? std::optional{EntityId{m_impl->identity, *parent}} : std::nullopt});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Snapshot scene transforms: " + std::string(error.what()));
    }
  }

  auto Scene::colliders() const -> Core::Result<std::vector<ColliderInfo>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      const auto& registry = m_impl->registry;
      std::vector<std::pair<std::uint64_t, entt::entity>> order;

      for (const auto native : registry.view<const RigidBody>()) {
        order.push_back({registry.get<Identity>(native).value, native});
      }

      for (const auto native : registry.view<const CharacterController>()) {
        order.push_back({registry.get<Identity>(native).value, native});
      }

      std::ranges::sort(order, {}, &decltype(order)::value_type::first);
      std::vector<ColliderInfo> result;
      result.reserve(order.size());

      for (const auto& [id, native] : order) {
        const auto* body = registry.try_get<RigidBody>(native);
        const auto* character = registry.try_get<CharacterController>(native);
        result.push_back({{m_impl->identity, id},
                          body ? std::optional{*body} : std::nullopt,
                          character ? std::optional{*character} : std::nullopt});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Snapshot scene colliders: " + std::string(error.what()));
    }
  }

  auto Scene::audioSources() const -> Core::Result<std::vector<AudioSourceInfo>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      const auto& registry = m_impl->registry;
      std::vector<std::pair<std::uint64_t, entt::entity>> order;

      for (const auto native : registry.view<const AudioSource>()) {
        order.push_back({registry.get<Identity>(native).value, native});
      }

      std::ranges::sort(order, {}, &decltype(order)::value_type::first);
      std::vector<AudioSourceInfo> result;
      result.reserve(order.size());

      for (const auto& [id, native] : order) {
        result.push_back({{m_impl->identity, id}, registry.get<AudioSource>(native)});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Snapshot scene audio sources: " + std::string(error.what()));
    }
  }

  auto Scene::renameEntity(EntityId entity, std::string_view name) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    auto checked = validateName(name);

    if (!checked) {
      return checked;
    }

    m_impl->registry.get<Identity>(*native).name = name;
    return {};
  }

  auto Scene::setTransform(EntityId entity, const Transform& transform) -> Core::Result<void> {
    const TransformUpdate update{entity, transform};
    return setTransforms(std::span{&update, 1});
  }

  auto Scene::setParent(EntityId entity, std::optional<EntityId> parent, ParentSpace space)
      -> Core::Result<void> {
    const ParentUpdate update{entity, parent};
    return setParents(std::span{&update, 1}, space);
  }

  auto Scene::setTransforms(std::span<const TransformUpdate> updates) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (updates.size() > 10000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Transform batch exceeds scene capacity");
    }

    if (updates.empty()) {
      return {};
    }

    struct Prepared {
      entt::entity native;
      Transform previous;
      Transform next;
    };

    try {
      std::vector<Prepared> prepared;
      prepared.reserve(updates.size());
      std::vector<std::uint64_t> changed;
      changed.reserve(updates.size());
      std::unordered_set<std::uint64_t> seen;
      seen.reserve(updates.size());

      for (const auto& update : updates) {
        auto native = m_impl->resolve(update.entity);

        if (!native) {
          return std::unexpected(native.error());
        }

        if (!seen.insert(update.entity.value).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Transform batch contains a duplicate entity");
        }

        const auto previous = m_impl->registry.get<Transform>(*native);

        if (sameTransform(update.transform, previous)) {
          continue;
        }

        auto rotation = storedRotation(update.transform.rotation);
        auto local = Math::compose(update.transform.position, update.transform.rotation,
                                   update.transform.scale);

        if (!rotation) {
          return std::unexpected(rotation.error());
        }

        if (!local) {
          return std::unexpected(local.error());
        }

        const Transform next{update.transform.position, *rotation, update.transform.scale};

        if (!sameTransform(previous, next)) {
          prepared.push_back({*native, previous, next});
          changed.push_back(update.entity.value);
        }
      }

      if (prepared.empty()) {
        return {};
      }

      auto affected = m_impl->affectedTransforms(changed);

      if (!affected) {
        return std::unexpected(affected.error());
      }

      for (const auto& entry : prepared) {
        m_impl->registry.get<Transform>(entry.native) = entry.next;
      }

      return m_impl->verifyMutation(
          [&] {
            for (const auto& entry : prepared) {
              m_impl->registry.get<Transform>(entry.native) = entry.previous;
            }
          },
          *affected);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Edit scene transforms: " + std::string(error.what()));
    }
  }

  auto Scene::setParents(std::span<const ParentUpdate> updates, ParentSpace space)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (updates.size() > 10000 || (space != ParentSpace::Local && space != ParentSpace::World)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid parent batch size/space");
    }

    try {
      std::vector<Impl::HierarchyChange> changes;
      changes.reserve(updates.size());
      std::unordered_set<std::uint64_t> seen;

      for (const auto& update : updates) {
        auto native = m_impl->resolve(update.entity);

        if (!native) {
          return std::unexpected(native.error());
        }

        if (!seen.insert(update.entity.value).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Parent batch contains a duplicate entity");
        }

        if (update.parent) {
          auto parent = m_impl->resolve(*update.parent);

          if (!parent) {
            return std::unexpected(parent.error());
          }
        }

        const auto oldParent = m_impl->registry.get<Hierarchy>(*native).parent;
        const auto newParent = update.parent ? std::optional{update.parent->value} : std::nullopt;
        const auto previous = m_impl->registry.get<Transform>(*native);
        auto next = previous;

        if (space == ParentSpace::World && oldParent != newParent) {
          auto matrix = m_impl->world(update.entity.value);

          if (!matrix) {
            return std::unexpected(matrix.error());
          }

          if (newParent) {
            auto parent = m_impl->world(*newParent);
            auto inverse = parent ? Math::inverse(*parent)
                                  : Core::Result<Math::Mat4>{std::unexpected(parent.error())};

            if (!inverse) {
              return std::unexpected(inverse.error());
            }

            matrix = Math::multiply(*inverse, *matrix);

            if (!matrix) {
              return std::unexpected(matrix.error());
            }
          }

          auto pose = Math::decomposeTrs(*matrix);

          if (!pose) {
            return Core::fail(pose.error().code, "Cannot preserve world pose while reparenting: "
                                                     + pose.error().message);
          }

          next = {pose->position, pose->rotation, pose->scale};
        }

        changes.push_back({*native, oldParent, newParent, previous, next});
      }

      return changes.empty() ? Core::Result<void>{} : m_impl->applyHierarchy(changes);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Reparent scene entities: " + std::string(error.what()));
    }
  }

  auto Scene::groupEntities(std::span<const EntityId> entities, std::string_view name,
                            std::optional<std::uint64_t> value) -> Core::Result<EntityId> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    checked = validateName(name);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (entities.empty() || entities.size() > 10000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Group requires 1..10000 entities");
    }

    if (m_impl->lookup.size() >= 10000) {
      return Core::fail(Core::ErrorCode::Unavailable, "Group exceeds scene capacity");
    }

    try {
      std::unordered_set<std::uint64_t> selected;
      std::vector<std::uint64_t> roots;

      for (const auto entity : entities) {
        auto native = m_impl->resolve(entity);

        if (!native) {
          return std::unexpected(native.error());
        }

        if (!selected.insert(entity.value).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Group contains duplicate entities");
        }
      }

      for (const auto entity : entities) {
        auto parent = m_impl->registry.get<Hierarchy>(m_impl->lookup.at(entity.value)).parent;
        bool covered{};

        while (parent) {
          if (selected.contains(*parent)) {
            covered = true;
            break;
          }

          parent = m_impl->registry.get<Hierarchy>(m_impl->lookup.at(*parent)).parent;
        }

        if (!covered) {
          roots.push_back(entity.value);
        }
      }

      auto common = m_impl->registry.get<Hierarchy>(m_impl->lookup.at(roots.front())).parent;
      bool sameParent = true;

      for (const auto root : roots) {
        if (m_impl->registry.get<Hierarchy>(m_impl->lookup.at(root)).parent != common) {
          common.reset();
          sameParent = false;
          break;
        }
      }

      auto active = m_impl->world(entities.back().value);

      if (!active) {
        return std::unexpected(active.error());
      }

      Math::Vec3 pivot{active->data[12], active->data[13], active->data[14]};

      if (common) {
        auto parent = m_impl->world(*common);
        auto inverse = parent ? Math::inverse(*parent)
                              : Core::Result<Math::Mat4>{std::unexpected(parent.error())};
        auto local = inverse ? Math::transformPoint(*inverse, pivot)
                             : Core::Result<Math::Vec3>{std::unexpected(inverse.error())};

        if (!local) {
          return std::unexpected(local.error());
        }

        pivot = *local;
      }

      std::vector<Impl::HierarchyChange> changes;
      changes.reserve(roots.size());

      for (const auto root : roots) {
        const auto native = m_impl->lookup.at(root);
        const auto oldParent = m_impl->registry.get<Hierarchy>(native).parent;
        const auto previous = m_impl->registry.get<Transform>(native);
        auto next = previous;

        if (sameParent) {
          next.position = next.position - pivot;
        } else {
          auto matrix = m_impl->world(root);

          if (!matrix) {
            return std::unexpected(matrix.error());
          }

          matrix->data[12] -= pivot.x;
          matrix->data[13] -= pivot.y;
          matrix->data[14] -= pivot.z;
          auto pose = Math::decomposeTrs(*matrix);

          if (!pose) {
            return Core::fail(pose.error().code,
                              "Cannot preserve grouped world pose: " + pose.error().message);
          }

          next = {pose->position, pose->rotation, pose->scale};
        }

        changes.push_back({native, oldParent, {}, previous, next});
      }

      const auto previousNext = m_impl->nextEntity;
      auto group = m_impl->add(name, value);

      if (!group) {
        return std::unexpected(group.error());
      }

      /** Only an unpublished container may return its reserved ID on rollback.
       * 仅未发布容器可在回滚时归还预留 ID。 */
      struct PendingGroup {
        Impl& m_scene;
        EntityId m_group;
        std::uint64_t m_previousNext;
        bool m_committed{};

        ~PendingGroup() {
          if (!m_committed) {
            m_scene.registry.destroy(m_scene.lookup.at(m_group.value));
            m_scene.lookup.erase(m_group.value);
            m_scene.nextEntity = m_previousNext;
            m_scene.invalidateWorld();
          }
        }
      } pending{*m_impl, *group, previousNext};

      const auto native = m_impl->lookup.at(group->value);
      m_impl->registry.get<Hierarchy>(native).parent = common;
      m_impl->registry.get<Transform>(native).position = pivot;

      for (auto& change : changes) {
        change.newParent = group->value;
      }

      auto applied = m_impl->applyHierarchy(changes);

      if (!applied) {
        return std::unexpected(applied.error());
      }

      pending.m_committed = true;
      return *group;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Group scene entities: " + std::string(error.what()));
    }
  }

  auto Scene::ungroupEntities(std::span<const EntityId> entities)
      -> Core::Result<std::vector<EntityId>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (entities.empty() || entities.size() > 10000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Ungroup requires 1..10000 containers");
    }

    try {
      std::unordered_set<std::uint64_t> removed;

      for (const auto entity : entities) {
        auto native = m_impl->resolve(entity);

        if (!native) {
          return std::unexpected(native.error());
        }

        if (std::ranges::any_of(m_impl->links,
                                [&](const auto& link) { return link.root == entity.value; })) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Unlink a scene instance before ungrouping it");
        }

        if (!removed.insert(entity.value).second
            || m_impl->registry.any_of<Camera, MeshRenderer, MaterialBinding, Light, RigidBody,
                                       CharacterController, AudioSource, AudioListener,
                                       ScriptComponent, AnimationPlayer, SkinBinding>(*native)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Ungroup requires unique component-free containers");
        }
      }

      for (const auto native : m_impl->registry.view<AnimationPlayer>()) {
        for (const auto& target : m_impl->registry.get<AnimationPlayer>(native).targets) {
          if (removed.contains(target.entity)) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Cannot ungroup a referenced animation target");
          }
        }
      }

      for (const auto native : m_impl->registry.view<SkinBinding>()) {
        for (const auto joint : m_impl->registry.get<SkinBinding>(native).joints) {
          if (removed.contains(joint)) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Cannot ungroup a referenced skin joint");
          }
        }
      }

      std::vector<Impl::HierarchyChange> changes;
      std::vector<EntityId> affected;

      for (const auto& [id, native] : m_impl->lookup) {
        const auto oldParent = m_impl->registry.get<Hierarchy>(native).parent;

        if (removed.contains(id) || !oldParent || !removed.contains(*oldParent)) {
          continue;
        }

        const auto previous = m_impl->registry.get<Transform>(native);
        auto matrix = Math::compose(previous.position, previous.rotation, previous.scale);
        auto parent = oldParent;

        while (parent && removed.contains(*parent)) {
          const auto owner = m_impl->lookup.at(*parent);
          const auto& transform = m_impl->registry.get<Transform>(owner);
          auto local = Math::compose(transform.position, transform.rotation, transform.scale);

          if (!local) {
            return std::unexpected(local.error());
          }

          if (!matrix) {
            return std::unexpected(matrix.error());
          }

          matrix = Math::multiply(*local, *matrix);
          parent = m_impl->registry.get<Hierarchy>(owner).parent;
        }

        if (!matrix) {
          return std::unexpected(matrix.error());
        }

        auto pose = Math::decomposeTrs(*matrix);

        if (!pose) {
          return Core::fail(pose.error().code,
                            "Cannot preserve ungrouped world pose: " + pose.error().message);
        }

        changes.push_back(
            {native, oldParent, parent, previous, {pose->position, pose->rotation, pose->scale}});
        affected.push_back({m_impl->identity, id});
      }

      std::ranges::sort(affected, {}, &EntityId::value);
      auto applied = changes.empty() ? Core::Result<void>{} : m_impl->applyHierarchy(changes);

      if (!applied) {
        return std::unexpected(applied.error());
      }

      for (const auto id : removed) {
        m_impl->registry.destroy(m_impl->lookup.at(id));
        m_impl->lookup.erase(id);
      }

      m_impl->invalidateWorld();
      m_impl->invalidateAnimationBindings();
      return affected;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Ungroup scene entities: " + std::string(error.what()));
    }
  }

  auto Scene::worldTransform(EntityId entity) const -> Core::Result<Math::Mat4> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    return m_impl->world(entity.value);
  }

  auto Scene::appendContent(const Scene& content, const Transform& placement,
                            std::optional<EntityId> parent) -> Core::Result<SceneAttachment> {
    auto next = nextEntityValue();

    if (!next) {
      return std::unexpected(next.error());
    }

    if (!*next) {
      return Core::fail(Core::ErrorCode::Unavailable, "Scene entity values exhausted");
    }

    try {
      auto count = content.entityCount();

      if (!count) {
        return std::unexpected(count.error());
      }

      if (*count == 0) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Runtime content requires nonempty input");
      }

      if (*count + 1 > 10000 - m_impl->lookup.size()) {
        return Core::fail(Core::ErrorCode::Unavailable, "Runtime content exceeds scene capacity");
      }

      if (parent) {
        auto resolved = m_impl->resolve(*parent);

        if (!resolved) {
          return std::unexpected(resolved.error());
        }
      }

      auto empty = Scene::create("Content insertion");

      if (!empty) {
        return std::unexpected(empty.error());
      }

      auto prepared = prepareSceneInstance(*empty, content, **next, placement);

      if (!prepared) {
        return std::unexpected(prepared.error());
      }

      SceneAttachment result{{m_impl->identity, prepared->root.value}, {}};
      result.entities.reserve(prepared->entities.size() + 1);
      result.entities.push_back(result.root);

      for (const auto& mapping : prepared->entities) {
        result.entities.push_back({m_impl->identity, mapping.target.value});
      }

      auto& fragment = *prepared->scene.m_impl;
      fragment.registry.get<Hierarchy>(fragment.lookup.at(result.root.value)).parent
          = parent ? std::optional{parent->value} : std::nullopt;
      std::vector<entt::entity> staged;
      staged.reserve(result.entities.size());

      /** The private registry cannot be observed during this owner-only operation.
       * Keep old descriptions/cache entries; reclaim only unpublished members on
       * any allocation or grafted-world validation failure, before consuming IDs.
       * 此拥有线程操作期间私有注册表不可观察；保留旧描述/缓存，分配或挂接世界变换校验
       * 失败时仅回收未发布成员，且尚未消耗公共实体 ID。 */
      struct PendingContent {
        Impl& m_scene;
        const std::vector<EntityId>& m_members;
        const std::vector<entt::entity>& m_staged;
        bool m_committed{};

        ~PendingContent() {
          if (!m_committed) {
            for (std::size_t index = 0; index < m_staged.size(); ++index) {
              const auto value = m_members[index].value;
              m_scene.worldCache.erase(value);
              m_scene.lookup.erase(value);
              m_scene.registry.destroy(m_staged[index]);
            }
          }
        }
      } pending{*m_impl, result.entities, staged};

      for (const auto member : result.entities) {
        const auto source = fragment.lookup.at(member.value);
        const auto native = m_impl->registry.create();
        staged.push_back(native);
        const auto transfer = [&]<typename T>() {
          if (auto* component = fragment.registry.try_get<T>(source)) {
            m_impl->registry.emplace<T>(native, std::move(*component));
          }
        };

        /** All native descriptions mirrored by Impl::snapshot; no live services.
         * 与 Impl::snapshot 对应的全部原生描述，不含活动子系统。 */
        transfer.operator()<Identity>();
        transfer.operator()<Transform>();
        transfer.operator()<Hierarchy>();
        transfer.operator()<Camera>();
        transfer.operator()<MeshRenderer>();
        transfer.operator()<RigidBody>();
        transfer.operator()<CharacterController>();
        transfer.operator()<AudioSource>();
        transfer.operator()<AudioListener>();
        transfer.operator()<ScriptComponent>();
        transfer.operator()<MaterialBinding>();
        transfer.operator()<Light>();
        transfer.operator()<AnimationPlayer>();
        transfer.operator()<SkinBinding>();
        m_impl->lookup.emplace(member.value, native);
      }

      for (const auto member : result.entities) {
        auto world = m_impl->world(member.value);

        if (!world) {
          return std::unexpected(world.error());
        }

        if (m_impl->registry.all_of<Camera>(m_impl->lookup.at(member.value))) {
          auto inverse = Math::inverse(*world);

          if (!inverse) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Content camera world transform cannot be inverted");
          }
        }
      }

      m_impl->nextEntity = fragment.nextEntity;
      m_impl->m_childrenReady = false;
      m_impl->invalidateAnimationBindings();
      pending.m_committed = true;
      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Append scene content: " + std::string(error.what()));
    }
  }

  auto Scene::removeContent(std::span<const EntityId> entities) -> Core::Result<void> {
    auto count = entityCount();

    if (!count) {
      return std::unexpected(count.error());
    }

    if (entities.empty() || entities.size() > 10000) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Content ownership requires 1..10000 handles");
    }

    try {
      std::unordered_set<std::uint64_t> removed;
      removed.reserve(entities.size());

      for (const auto entity : entities) {
        if (entity.scene != m_impl->identity || entity.value == 0
            || entity.value >= m_impl->nextEntity || !removed.insert(entity.value).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid content ownership handle");
        }
      }

      for (const auto& [value, native] : m_impl->lookup) {
        if (removed.contains(value)) {
          continue;
        }

        const auto parent = m_impl->registry.get<Hierarchy>(native).parent;

        if (parent && removed.contains(*parent)) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Content has a surviving child " + std::to_string(value));
        }

        if (const auto* skin = m_impl->registry.try_get<SkinBinding>(native)) {
          for (const auto joint : skin->joints) {
            if (removed.contains(joint)) {
              return Core::fail(Core::ErrorCode::InvalidState,
                                "Content is referenced by skin owner " + std::to_string(value));
            }
          }
        }

        if (const auto* player = m_impl->registry.try_get<AnimationPlayer>(native)) {
          for (const auto& target : player->targets) {
            if (removed.contains(target.entity)) {
              return Core::fail(
                  Core::ErrorCode::InvalidState,
                  "Content is referenced by animation player " + std::to_string(value));
            }
          }
        }
      }

      for (const auto value : removed) {
        const auto found = m_impl->lookup.find(value);

        if (found != m_impl->lookup.end()) {
          m_impl->registry.destroy(found->second);
          m_impl->lookup.erase(found);
        }
      }

      std::erase_if(m_impl->links, [&](const auto& link) { return removed.contains(link.root); });
      m_impl->invalidateWorld();
      m_impl->invalidateAnimationBindings();
      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Remove scene content: " + std::string(error.what()));
    }
  }

  auto Scene::destroyEntity(EntityId entity) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    auto& scene = *m_impl;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> children;

    for (const auto& entry : scene.lookup) {
      auto parent = scene.registry.get<Hierarchy>(entry.second).parent;

      if (parent) {
        children[*parent].push_back(entry.first);
      }
    }

    std::vector<std::uint64_t> removed{entity.value};

    for (std::size_t index = 0; index < removed.size(); ++index) {
      const auto found = children.find(removed[index]);

      if (found != children.end()) {
        removed.insert(removed.end(), found->second.begin(), found->second.end());
      }
    }

    const std::unordered_set<std::uint64_t> removedIds(removed.begin(), removed.end());

    for (const auto entry : scene.registry.view<AnimationPlayer>()) {
      const auto owner = scene.registry.get<Identity>(entry).value;

      if (!removedIds.contains(owner)) {
        for (const auto& target : scene.registry.get<AnimationPlayer>(entry).targets) {
          if (removedIds.contains(target.entity)) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Entity is referenced by animation player " + std::to_string(owner));
          }
        }
      }
    }

    for (const auto entry : scene.registry.view<SkinBinding>()) {
      const auto owner = scene.registry.get<Identity>(entry).value;

      if (!removedIds.contains(owner)) {
        for (const auto joint : scene.registry.get<SkinBinding>(entry).joints) {
          if (removedIds.contains(joint)) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Joint is referenced by skin owner " + std::to_string(owner));
          }
        }
      }
    }

    for (auto iterator = removed.rbegin(); iterator != removed.rend(); ++iterator) {
      scene.registry.destroy(scene.lookup.at(*iterator));
      scene.lookup.erase(*iterator);
    }

    std::erase_if(scene.links, [&](const auto& link) { return removedIds.contains(link.root); });
    scene.invalidateWorld();
    scene.invalidateAnimationBindings();
    return {};
  }

  auto Scene::setCamera(EntityId entity, std::optional<Camera> camera) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    if (!camera) {
      m_impl->registry.remove<Camera>(*native);
      return {};
    }

    auto projection = cameraProjection(*camera, 1);

    if (!projection) {
      return std::unexpected(projection.error());
    }

    auto world = m_impl->world(entity.value);

    if (!world) {
      return std::unexpected(world.error());
    }

    auto view = Math::inverse(*world);

    if (!view) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Camera world transform cannot be inverted");
    }

    if (camera->primary) {
      for (const auto& entry : m_impl->lookup) {
        const auto* existing = m_impl->registry.try_get<Camera>(entry.second);

        if (entry.second != *native && existing && existing->primary) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Scene already has a primary camera");
        }
      }
    }

    m_impl->registry.emplace_or_replace<Camera>(*native, *camera);
    return {};
  }

  auto Scene::primaryCamera() const -> Core::Result<EntityId> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    for (const auto& entry : m_impl->lookup) {
      const auto* camera = m_impl->registry.try_get<Camera>(entry.second);

      if (camera && camera->primary) {
        return EntityId{m_impl->identity, entry.first};
      }
    }

    return Core::fail(Core::ErrorCode::Unavailable, "Scene has no primary camera");
  }

  auto Scene::viewProjection(EntityId camera, float aspect) const -> Core::Result<Math::Mat4> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(camera);

    if (!native) {
      return std::unexpected(native.error());
    }

    const auto* component = m_impl->registry.try_get<Camera>(*native);

    if (!component) {
      return Core::fail(Core::ErrorCode::InvalidState, "Entity has no camera component");
    }

    auto projection = cameraProjection(*component, aspect);

    if (!projection) {
      return std::unexpected(projection.error());
    }

    auto world = m_impl->world(camera.value);

    if (!world) {
      return std::unexpected(world.error());
    }

    auto view = Math::inverse(*world);

    if (!view) {
      return std::unexpected(view.error());
    }

    return Math::multiply(*projection, *view);
  }

  auto Scene::setMeshRenderer(EntityId entity, std::optional<MeshRenderer> mesh)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    if (!mesh) {
      if (m_impl->registry.any_of<SkinBinding>(*native)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Remove skin binding before its mesh");
      }

      m_impl->registry.remove<MeshRenderer>(*native);
      return {};
    }

    auto checked = validateMeshRenderer(*mesh);

    if (!checked) {
      return checked;
    }

    m_impl->registry.emplace_or_replace<MeshRenderer>(*native, std::move(*mesh));
    return {};
  }

  auto Scene::setMaterial(EntityId entity, std::optional<MaterialBinding> material)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    if (!material) {
      m_impl->registry.remove<MaterialBinding>(*native);
      return {};
    }

    if (!Core::isPortableRelativePath(material->asset)
        || !material->asset.ends_with(".dotmaterial")) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Material binding requires a portable .dotmaterial asset path");
    }

    m_impl->registry.emplace_or_replace<MaterialBinding>(*native, std::move(*material));
    return {};
  }

  auto Scene::setLight(EntityId entity, std::optional<Light> light) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    if (!light) {
      m_impl->registry.remove<Light>(*native);
      return {};
    }

    Graphics::Lighting source;
    source.lights = {{.kind = light->kind,
                      .color = light->color,
                      .intensity = light->intensity,
                      .range = light->range,
                      .innerConeDegrees = light->innerConeDegrees,
                      .outerConeDegrees = light->outerConeDegrees,
                      .shadows = light->shadows}};
    auto checked = Graphics::validatedLighting(source);

    if (!checked) {
      return Core::fail(checked.error().code, "Scene light: " + checked.error().message);
    }

    m_impl->registry.emplace_or_replace<Light>(*native, *light);
    return {};
  }

  auto Scene::setRigidBody(EntityId entity, std::optional<RigidBody> body) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    if (!body) {
      if (m_impl->registry.remove<RigidBody>(*native)) {
        m_impl->invalidateAnimationBindings();
      }

      return {};
    }

    if (body->shape == Physics::Shape::Mesh && m_impl->registry.any_of<SkinBinding>(*native)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "A static mesh collider cannot own a deforming skin");
    }

    if (body->shape == Physics::Shape::Mesh && !body->meshAsset) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Scene mesh collider requires a native mesh resource");
    }

    auto checked = Physics::validateBodyProperties(*body);

    if (!checked) {
      return checked;
    }

    if (m_impl->registry.any_of<CharacterController>(*native)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "An entity cannot own both a rigid body and character controller");
    }

    const bool existed = m_impl->registry.all_of<RigidBody>(*native);
    m_impl->registry.emplace_or_replace<RigidBody>(*native, *body);

    if (!existed) {
      m_impl->invalidateAnimationBindings();
    }

    return {};
  }

  auto Scene::setCharacterController(EntityId entity, std::optional<CharacterController> character)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    if (!character) {
      if (m_impl->registry.remove<CharacterController>(*native)) {
        m_impl->invalidateAnimationBindings();
      }

      return {};
    }

    auto checked = Physics::validateCharacterProperties(*character);

    if (!checked) {
      return checked;
    }

    if (m_impl->registry.any_of<RigidBody>(*native)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "An entity cannot own both a rigid body and character controller");
    }

    const bool existed = m_impl->registry.all_of<CharacterController>(*native);
    m_impl->registry.emplace_or_replace<CharacterController>(*native, *character);

    if (!existed) {
      m_impl->invalidateAnimationBindings();
    }

    return {};
  }

  auto Scene::setAudioSource(EntityId entity, std::optional<AudioSource> source)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    if (!source) {
      m_impl->registry.remove<AudioSource>(*native);
      return {};
    }

    auto checked = Audio::validateSourceProperties(*source);

    if (!checked) {
      return checked;
    }

    m_impl->registry.emplace_or_replace<AudioSource>(*native, std::move(*source));
    return {};
  }

  auto Scene::setAudioListener(EntityId entity, std::optional<AudioListener> listener)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    if (!listener) {
      m_impl->registry.remove<AudioListener>(*native);
      return {};
    }

    if (listener->primary) {
      for (const auto& entry : m_impl->lookup) {
        const auto* existing = m_impl->registry.try_get<AudioListener>(entry.second);

        if (entry.second != *native && existing && existing->primary) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Scene already has a primary audio listener");
        }
      }
    }

    m_impl->registry.emplace_or_replace<AudioListener>(*native, *listener);
    return {};
  }

  auto Scene::setScript(EntityId entity, std::optional<ScriptComponent> script)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    if (!script) {
      m_impl->registry.remove<ScriptComponent>(*native);
      return {};
    }

    auto checked = Script::normalizeComponentProperties(*script);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    m_impl->registry.emplace_or_replace<ScriptComponent>(*native, std::move(*checked));
    return {};
  }

  auto Scene::primaryAudioListener() const -> Core::Result<EntityId> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    const auto& registry = m_impl->registry;

    for (const auto native : registry.view<const AudioListener>()) {
      if (registry.get<AudioListener>(native).primary) {
        return EntityId{m_impl->identity, registry.get<Identity>(native).value};
      }
    }

    return Core::fail(Core::ErrorCode::NotFound, "Scene has no primary audio listener");
  }
}  // namespace Dot::World

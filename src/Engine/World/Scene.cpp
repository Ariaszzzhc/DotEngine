module;

#include <algorithm>
#include <atomic>
#include <cmath>
#include <entt/entity/registry.hpp>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <span>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

module Dot.World.Scene;

import Dot.Core.Result;
import Dot.Core.Text;
import Dot.Math;
import Dot.Physics.Body;
import Dot.Audio.Source;

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
    std::unordered_map<std::uint64_t, entt::entity> lookup;
    mutable std::unordered_map<std::uint64_t, Math::Mat4> worldCache;

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

    auto snapshot(entt::entity native) const -> EntityInfo {
      const auto& value = registry.get<Identity>(native);
      const auto parent = registry.get<Hierarchy>(native).parent;
      const auto* camera = registry.try_get<Camera>(native);
      const auto* mesh = registry.try_get<MeshRenderer>(native);
      const auto* body = registry.try_get<RigidBody>(native);
      const auto* source = registry.try_get<AudioSource>(native);
      const auto* listener = registry.try_get<AudioListener>(native);
      return {{identity, value.value},
              value.name,
              registry.get<Transform>(native),
              parent ? std::optional{EntityId{identity, *parent}} : std::nullopt,
              camera ? std::optional{*camera} : std::nullopt,
              mesh ? std::optional{*mesh} : std::nullopt,
              body ? std::optional{*body} : std::nullopt,
              source ? std::optional{*source} : std::nullopt,
              listener ? std::optional{*listener} : std::nullopt};
    }

    auto world(std::uint64_t value) const -> Core::Result<Math::Mat4> {
      std::vector<std::uint64_t> chain;
      std::optional<std::uint64_t> current = value;

      while (current) {
        if (chain.size() == 256) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Hierarchy contains a cycle or exceeds depth 256");
        }

        auto found = lookup.find(*current);

        if (found == lookup.end()) {
          return Core::fail(Core::ErrorCode::InvalidState, "Hierarchy refers to a missing entity");
        }

        chain.push_back(*current);
        current = registry.get<Hierarchy>(found->second).parent;
      }

      Math::Mat4 matrix;

      for (auto iterator = chain.rbegin(); iterator != chain.rend(); ++iterator) {
        auto cached = worldCache.find(*iterator);

        if (cached != worldCache.end()) {
          matrix = cached->second;
          continue;
        }

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
        worldCache.emplace(*iterator, matrix);
      }

      return matrix;
    }

    template <typename F>
    auto verifyMutation(F&& rollback) -> Core::Result<void> {
      worldCache.clear();

      try {
        for (const auto& entry : lookup) {
          auto matrix = world(entry.first);

          if (!matrix) {
            rollback();
            worldCache.clear();
            return std::unexpected(matrix.error());
          }

          if (registry.all_of<Camera>(entry.second)) {
            auto view = Math::inverse(*matrix);

            if (!view) {
              rollback();
              worldCache.clear();
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Camera world transform cannot be inverted");
            }
          }
        }
      } catch (...) {
        rollback();
        worldCache.clear();
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

  auto Scene::entities() const -> Core::Result<std::vector<EntityInfo>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    std::vector<EntityInfo> entities;
    entities.reserve(m_impl->lookup.size());

    for (const auto& entry : m_impl->lookup) {
      entities.push_back(m_impl->snapshot(entry.second));
    }

    std::ranges::sort(entities, {}, [](const EntityInfo& entity) { return entity.id.value; });
    return entities;
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

  auto Scene::setParent(EntityId entity, std::optional<EntityId> parent) -> Core::Result<void> {
    const ParentUpdate update{entity, parent};
    return setParents(std::span{&update, 1});
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

    std::vector<Prepared> prepared;
    prepared.reserve(updates.size());
    std::unordered_set<std::uint64_t> seen;

    for (const auto& update : updates) {
      auto native = m_impl->resolve(update.entity);

      if (!native) {
        return std::unexpected(native.error());
      }

      if (!seen.insert(update.entity.value).second) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Transform batch contains a duplicate entity");
      }

      auto rotation = Math::normalized(update.transform.rotation);
      auto local = Math::compose(update.transform.position, update.transform.rotation,
                                 update.transform.scale);

      if (!rotation) {
        return std::unexpected(rotation.error());
      }

      if (!local) {
        return std::unexpected(local.error());
      }

      prepared.push_back({*native,
                          m_impl->registry.get<Transform>(*native),
                          {update.transform.position, *rotation, update.transform.scale}});
    }

    for (const auto& entry : prepared) {
      m_impl->registry.get<Transform>(entry.native) = entry.next;
    }

    return m_impl->verifyMutation([&] {
      for (const auto& entry : prepared) {
        m_impl->registry.get<Transform>(entry.native) = entry.previous;
      }
    });
  }

  auto Scene::setParents(std::span<const ParentUpdate> updates) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (updates.size() > 10000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Parent batch exceeds scene capacity");
    }

    if (updates.empty()) {
      return {};
    }

    struct Prepared {
      entt::entity native;
      std::optional<std::uint64_t> previous;
      std::optional<std::uint64_t> next;
    };

    std::vector<Prepared> prepared;
    prepared.reserve(updates.size());
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

      prepared.push_back({*native, m_impl->registry.get<Hierarchy>(*native).parent,
                          update.parent ? std::optional{update.parent->value} : std::nullopt});
    }

    for (const auto& entry : prepared) {
      m_impl->registry.get<Hierarchy>(entry.native).parent = entry.next;
    }

    return m_impl->verifyMutation([&] {
      for (const auto& entry : prepared) {
        m_impl->registry.get<Hierarchy>(entry.native).parent = entry.previous;
      }
    });
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

    for (auto iterator = removed.rbegin(); iterator != removed.rend(); ++iterator) {
      scene.registry.destroy(scene.lookup.at(*iterator));
      scene.lookup.erase(*iterator);
    }

    scene.worldCache.clear();
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

  auto Scene::setRigidBody(EntityId entity, std::optional<RigidBody> body) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    if (!body) {
      m_impl->registry.remove<RigidBody>(*native);
      return {};
    }

    auto checked = Physics::validateBodyProperties(*body);

    if (!checked) {
      return checked;
    }

    m_impl->registry.emplace_or_replace<RigidBody>(*native, *body);
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

  auto Scene::primaryAudioListener() const -> Core::Result<EntityId> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    for (const auto& entry : m_impl->lookup) {
      const auto* listener = m_impl->registry.try_get<AudioListener>(entry.second);

      if (listener && listener->primary) {
        return EntityId{m_impl->identity, entry.first};
      }
    }

    return Core::fail(Core::ErrorCode::NotFound, "Scene has no primary audio listener");
  }
}  // namespace Dot::World

module;

#include <algorithm>
#include <atomic>
#include <entt/entity/registry.hpp>
#include <expected>
#include <limits>
#include <thread>
#include <unordered_map>
#include <utility>

module Dot.World.Scene;

import Dot.Core.Result;
import Dot.Core.Text;
import Dot.Math;

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
  }  // namespace

  struct Scene::Impl {
    std::thread::id thread{std::this_thread::get_id()};
    std::uint64_t identity{};
    std::uint64_t nextEntity{1};
    std::string name;
    entt::registry registry;
    std::unordered_map<std::uint64_t, entt::entity> lookup;
    mutable std::unordered_map<std::uint64_t, Math::Mat4> worldCache;

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
      return {{identity, value.value},
              value.name,
              registry.get<Transform>(native),
              parent ? std::optional{EntityId{identity, *parent}} : std::nullopt};
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

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    checked = validateName(name);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto& scene = *m_impl;

    if (scene.lookup.size() >= 10000
        || scene.nextEntity == std::numeric_limits<std::uint64_t>::max()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene entity capacity exhausted");
    }

    const auto value = scene.nextEntity;
    const auto native = scene.registry.create();

    try {
      scene.registry.emplace<Identity>(native, value, std::string(name));
      scene.registry.emplace<Transform>(native);
      scene.registry.emplace<Hierarchy>(native);
      scene.lookup.emplace(value, native);
    } catch (...) {
      scene.registry.destroy(native);
      throw;
    }

    ++scene.nextEntity;
    return EntityId{scene.identity, value};
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
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    auto rotation = Math::normalized(transform.rotation);
    auto local = Math::compose(transform.position, transform.rotation, transform.scale);

    if (!rotation) {
      return std::unexpected(rotation.error());
    }

    if (!local) {
      return std::unexpected(local.error());
    }

    auto& current = m_impl->registry.get<Transform>(*native);
    const auto previous = current;
    current = {transform.position, *rotation, transform.scale};
    return m_impl->verifyMutation([&] { current = previous; });
  }

  auto Scene::setParent(EntityId entity, std::optional<EntityId> parent) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene has been moved from");
    }

    auto native = m_impl->resolve(entity);

    if (!native) {
      return std::unexpected(native.error());
    }

    if (parent) {
      auto parentNative = m_impl->resolve(*parent);

      if (!parentNative) {
        return std::unexpected(parentNative.error());
      }
    }

    auto& current = m_impl->registry.get<Hierarchy>(*native).parent;
    const auto previous = current;
    current = parent ? std::optional{parent->value} : std::nullopt;
    return m_impl->verifyMutation([&] { current = previous; });
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
}  // namespace Dot::World

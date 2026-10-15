module;

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <format>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

module Dot.Physics.SceneSimulation;

import Dot.Core.Result;
import Dot.Math;
import Dot.World.Scene;
import Dot.Physics.Body;
import Dot.Physics.Character;
import Dot.Physics.World;

namespace Dot::Physics {
  namespace {
    using Scene = Dot::World::Scene;
    using EntityId = Dot::World::EntityId;
    using Transform = Dot::World::Transform;

    auto context(EntityId entity, const Core::Error& error) -> std::unexpected<Core::Error> {
      auto result = error;
      result.message = std::format("Physics entity {}: {}", entity.value, error.message);
      return std::unexpected(std::move(result));
    }

    auto configuration(const BodyProperties& description, const Math::TransformParts& pose)
        -> Core::Result<BodyConfig> {
      BodyConfig result;
      static_cast<BodyProperties&>(result) = description;
      result.position = pose.position;
      result.rotation = pose.rotation;
      const Math::Vec3 scale{std::abs(pose.scale.x), std::abs(pose.scale.y),
                             std::abs(pose.scale.z)};

      if (description.shape == Shape::Box) {
        result.halfExtents
            = {description.halfExtents.x * scale.x, description.halfExtents.y * scale.y,
               description.halfExtents.z * scale.z};
      } else {
        const float maximum = std::max({scale.x, scale.y, scale.z});

        if (std::abs(scale.x - scale.y) > 1e-5F * maximum
            || std::abs(scale.x - scale.z) > 1e-5F * maximum) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Sphere/capsule physics requires uniform world scale");
        }

        result.radius *= scale.x;

        if (description.shape == Shape::Capsule) {
          result.halfHeight *= scale.x;
        }
      }

      return validatedBodyConfig(result);
    }

    auto configuration(const CharacterProperties& description, const Math::TransformParts& pose)
        -> Core::Result<CharacterConfig> {
      const auto scale = pose.scale;
      const auto maximum = std::max({scale.x, scale.y, scale.z});

      if (scale.x <= 0 || scale.y <= 0 || scale.z <= 0
          || std::abs(scale.x - scale.y) > maximum * 1e-5F
          || std::abs(scale.x - scale.z) > maximum * 1e-5F) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Character controllers require positive uniform world scale");
      }

      auto rotation = Math::compose({}, pose.rotation);

      if (!rotation) {
        return std::unexpected(rotation.error());
      }

      const auto& matrix = rotation->data;

      if (std::abs(matrix[4]) > 1e-5F || std::abs(matrix[5] - 1) > 1e-5F
          || std::abs(matrix[6]) > 1e-5F) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Character controllers require a Y-up world orientation");
      }

      CharacterConfig result;
      static_cast<CharacterProperties&>(result) = description;
      result.position = pose.position;
      result.radius *= scale.x;
      result.halfHeight *= scale.x;
      result.yawDegrees = std::atan2(matrix[8], matrix[10]) * 180 / std::numbers::pi_v<float>;
      return validatedCharacterConfig(result);
    }

    auto localTransform(const Math::Mat4& world, const Math::Mat4& parent, Math::Vec3 authoredScale)
        -> Core::Result<Transform> {
      auto inverse = Math::inverse(parent);

      if (!inverse) {
        return std::unexpected(inverse.error());
      }

      auto local = Math::multiply(*inverse, world);

      if (!local) {
        return std::unexpected(local.error());
      }

      auto parts = Math::decomposeTrs(*local);

      if (!parts) {
        return std::unexpected(parts.error());
      }

      const std::array authored{authoredScale.x, authoredScale.y, authoredScale.z};
      const std::array canonical{parts->scale.x, parts->scale.y, parts->scale.z};
      std::array<float, 3> scales{};
      Math::Mat4 basis;

      for (int column = 0; column < 3; ++column) {
        const auto axis = static_cast<std::size_t>(column);
        const float magnitude = std::abs(canonical[axis]);
        scales[axis] = std::copysign(magnitude, authored[axis]);

        if (std::abs(magnitude - std::abs(authored[axis])) <= 1e-5F * magnitude) {
          scales[axis] = authored[axis];
        }

        for (int row = 0; row < 3; ++row) {
          const auto index = static_cast<std::size_t>(column * 4 + row);
          basis.data[index] = local->data[index] / scales[axis];
        }
      }

      auto orientation = Math::decomposeTrs(basis);

      if (!orientation) {
        return std::unexpected(orientation.error());
      }

      return Transform{parts->position, orientation->rotation, {scales[0], scales[1], scales[2]}};
    }
  }  // namespace

  struct SceneSimulation::Impl {
    struct Entry {
      BodyId body;
      BodyProperties description;
      Math::Mat4 source;
      Math::Vec3 scale;
    };

    struct CharacterEntry {
      CharacterId character;
      CharacterProperties description;
      Math::Mat4 source;
      Math::Vec3 scale;
    };

    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_scene;
    WorldConfig m_config;
    World m_world;
    std::unordered_map<std::uint64_t, Entry> m_entries;
    std::unordered_map<std::uint64_t, CharacterEntry> m_characters;
    bool m_faulted{};

    Impl(std::uint64_t scene, WorldConfig config, World world)
        : m_scene(scene), m_config(config), m_world(std::move(world)) {
    }

    auto ready(bool mutate = false) const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Scene simulation requires its creating thread");
      }

      if (mutate && m_faulted) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Scene simulation faulted; reset from authoring data to resume");
      }

      return {};
    }

    auto resolve(EntityId entity, bool mutate = false) const -> Core::Result<BodyId> {
      auto checked = ready(mutate);

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (entity.scene != m_scene) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Physics entity belongs to another scene");
      }

      const auto found = m_entries.find(entity.value);

      if (found == m_entries.end()) {
        return Core::fail(Core::ErrorCode::NotFound, "Entity has no synchronized physics body");
      }

      return found->second.body;
    }

    auto resolveCharacter(EntityId entity, bool mutate = false) const -> Core::Result<CharacterId> {
      auto checked = ready(mutate);

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (entity.scene != m_scene || entity.value == 0) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid character entity or foreign scene");
      }

      const auto found = m_characters.find(entity.value);

      if (found == m_characters.end()) {
        return Core::fail(Core::ErrorCode::NotFound,
                          "Entity has no synchronized character controller");
      }

      return found->second.character;
    }
  };

  SceneSimulation::SceneSimulation(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  SceneSimulation::SceneSimulation(SceneSimulation&&) noexcept = default;

  auto SceneSimulation::operator=(SceneSimulation&&) noexcept -> SceneSimulation& = default;

  SceneSimulation::~SceneSimulation() = default;

  auto SceneSimulation::create(const Scene& scene, const WorldConfig& config)
      -> Core::Result<SceneSimulation> {
    auto checked = scene.entityCount();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto world = World::create(config);

    if (!world) {
      return std::unexpected(world.error());
    }

    try {
      SceneSimulation result{std::make_unique<Impl>(scene.id(), config, std::move(*world))};
      auto synchronized = result.synchronize(scene);

      if (!synchronized) {
        return std::unexpected(synchronized.error());
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        std::format("Initialize scene simulation: {}", error.what()));
    }
  }

  auto SceneSimulation::synchronize(const Scene& scene) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene simulation has been moved");
    }

    auto ready = m_impl->ready(true);

    if (!ready) {
      return ready;
    }

    if (scene.id() != m_impl->m_scene) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Scene simulation binding mismatch");
    }

    bool committed = false;

    try {
      auto entities = scene.entities();

      if (!entities) {
        return std::unexpected(entities.error());
      }

      std::unordered_map<std::uint64_t, Impl::Entry> desired;
      std::vector<BodyChange> changes;
      std::vector<std::uint64_t> keys;
      std::unordered_map<std::uint64_t, Impl::CharacterEntry> desiredCharacters;
      std::vector<CharacterChange> characterChanges;
      std::vector<std::uint64_t> characterKeys;

      for (const auto& entity : *entities) {
        if (!entity.rigidBody) {
          continue;
        }

        auto matrix = scene.worldTransform(entity.id);

        if (!matrix) {
          return context(entity.id, matrix.error());
        }

        auto parts = Math::decomposeTrs(*matrix);

        if (!parts) {
          return context(entity.id, parts.error());
        }

        auto config = configuration(*entity.rigidBody, *parts);

        if (!config) {
          return context(entity.id, config.error());
        }

        config->userTag = entity.id.value;

        const auto existing = m_impl->m_entries.find(entity.id.value);

        if (existing != m_impl->m_entries.end() && existing->second.description == *entity.rigidBody
            && existing->second.source == *matrix) {
          desired.emplace(entity.id.value, existing->second);
          continue;
        }

        std::optional<BodyId> id;
        bool preserveVelocity = false;
        bool preserveAngularVelocity = false;

        if (existing != m_impl->m_entries.end()) {
          id = existing->second.body;

          if (config->motion != Motion::Static) {
            preserveVelocity = entity.rigidBody->velocity == existing->second.description.velocity;
            preserveAngularVelocity = true;
          }
        }

        desired.emplace(entity.id.value, Impl::Entry{id.value_or(BodyId{}), *entity.rigidBody,
                                                     *matrix, parts->scale});
        changes.push_back({id, *config, preserveVelocity, preserveAngularVelocity});
        keys.push_back(entity.id.value);
      }

      if (desired.size() > m_impl->m_config.maxBodies) {
        return Core::fail(Core::ErrorCode::Unavailable, "Scene physics exceeds body capacity");
      }

      for (const auto& [key, entry] : m_impl->m_entries) {
        if (!desired.contains(key)) {
          changes.push_back({entry.body, std::nullopt});
          keys.push_back(key);
        }
      }

      for (const auto& entity : *entities) {
        if (!entity.character) {
          continue;
        }

        auto matrix = scene.worldTransform(entity.id);

        if (!matrix) {
          return context(entity.id, matrix.error());
        }

        auto parts = Math::decomposeTrs(*matrix);

        if (!parts) {
          return context(entity.id, parts.error());
        }

        auto config = configuration(*entity.character, *parts);

        if (!config) {
          return context(entity.id, config.error());
        }

        const auto existing = m_impl->m_characters.find(entity.id.value);

        if (existing != m_impl->m_characters.end()
            && existing->second.description == *entity.character
            && existing->second.source == *matrix) {
          desiredCharacters.emplace(entity.id.value, existing->second);
          continue;
        }

        std::optional<CharacterId> id;
        bool preserveMotion{};

        if (existing != m_impl->m_characters.end()) {
          id = existing->second.character;
          preserveMotion = existing->second.source == *matrix;

          if (preserveMotion) {
            auto state = m_impl->m_world.character(*id);

            if (!state) {
              return context(entity.id, state.error());
            }

            config = configuration(*entity.character,
                                   {state->position, state->rotation, parts->scale});

            if (!config) {
              return context(entity.id, config.error());
            }
          }
        }

        desiredCharacters.emplace(
            entity.id.value, Impl::CharacterEntry{id.value_or(CharacterId{}), *entity.character,
                                                  *matrix, parts->scale});
        characterChanges.push_back({id, *config, preserveMotion});
        characterKeys.push_back(entity.id.value);
      }

      for (const auto& [key, entry] : m_impl->m_characters) {
        if (!desiredCharacters.contains(key)) {
          characterChanges.push_back({entry.character, std::nullopt});
          characterKeys.push_back(key);
        }
      }

      auto applied = m_impl->m_world.applyChanges(changes, characterChanges);

      if (!applied) {
        auto stats = m_impl->m_world.stats();
        m_impl->m_faulted = !stats || stats->faulted;
        return std::unexpected(applied.error());
      }

      committed = true;

      for (std::size_t i = 0; i < keys.size(); ++i) {
        if (applied->bodies[i]) {
          desired.at(keys[i]).body = *applied->bodies[i];
        }
      }

      for (std::size_t i = 0; i < characterKeys.size(); ++i) {
        if (applied->characters[i]) {
          desiredCharacters.at(characterKeys[i]).character = *applied->characters[i];
        }
      }

      m_impl->m_entries.swap(desired);
      m_impl->m_characters.swap(desiredCharacters);
      return {};
    } catch (const std::exception& error) {
      m_impl->m_faulted = m_impl->m_faulted || committed;
      return Core::fail(Core::ErrorCode::External,
                        std::format("Synchronize scene physics: {}", error.what()));
    }
  }

  auto SceneSimulation::step(Scene& scene, double seconds) -> Core::Result<void> {
    if (!std::isfinite(seconds) || seconds < 0.000001 || seconds > 0.25) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid scene physics step duration");
    }

    auto synchronized = synchronize(scene);

    if (!synchronized) {
      return synchronized;
    }

    auto advanced = m_impl->m_world.step(seconds);

    if (!advanced) {
      m_impl->m_faulted = true;
      return advanced;
    }

    try {
      auto entities = scene.entities();

      if (!entities) {
        m_impl->m_faulted = true;
        return std::unexpected(entities.error());
      }

      std::unordered_map<std::uint64_t, std::size_t> indices;
      std::vector<std::pair<std::size_t, std::size_t>> order;
      std::vector<Math::Mat4> matrices(entities->size());
      std::vector<Dot::World::TransformUpdate> updates;

      for (std::size_t i = 0; i < entities->size(); ++i) {
        indices.emplace((*entities)[i].id.value, i);
      }

      for (std::size_t i = 0; i < entities->size(); ++i) {
        std::size_t depth = 0;
        auto parent = (*entities)[i].parent;

        while (parent) {
          ++depth;
          parent = (*entities)[indices.at(parent->value)].parent;
        }

        order.emplace_back(depth, i);
      }

      std::ranges::sort(order);

      for (const auto& [depth, index] : order) {
        (void)depth;
        const auto& entity = (*entities)[index];
        const Math::Mat4 parent
            = entity.parent ? matrices[indices.at(entity.parent->value)] : Math::Mat4{};
        auto transform = entity.transform;
        const auto binding = m_impl->m_entries.find(entity.id.value);
        const auto characterBinding = m_impl->m_characters.find(entity.id.value);

        if (binding != m_impl->m_entries.end()) {
          auto state = m_impl->m_world.body(binding->second.body);

          if (!state) {
            m_impl->m_faulted = true;
            return context(entity.id, state.error());
          }

          bool write = state->motion != Motion::Static;

          if (!write && entity.parent) {
            auto previousParent = scene.worldTransform(*entity.parent);

            if (!previousParent) {
              m_impl->m_faulted = true;
              return context(entity.id, previousParent.error());
            }

            write = *previousParent != parent;
          }

          if (write) {
            auto world = Math::compose(state->position, state->rotation, binding->second.scale);
            auto local = world ? localTransform(*world, parent, entity.transform.scale)
                               : Core::Result<Transform>{std::unexpected(world.error())};

            if (!local) {
              m_impl->m_faulted = true;
              return context(entity.id, local.error());
            }

            updates.push_back({entity.id, *local});
            /** Predict the normalized value stored by the scene batch, so the
             * cached source matches its subsequent world-matrix calculation.
             * 预测场景批处理实际存储的归一化值，使缓存与随后计算的世界矩阵一致。 */
            auto rotation = Math::normalized(local->rotation);

            if (!rotation) {
              m_impl->m_faulted = true;
              return context(entity.id, rotation.error());
            }

            transform = {local->position, *rotation, local->scale};
          }
        }

        if (characterBinding != m_impl->m_characters.end()) {
          auto state = m_impl->m_world.character(characterBinding->second.character);

          if (!state) {
            m_impl->m_faulted = true;
            return context(entity.id, state.error());
          }

          auto world
              = Math::compose(state->position, state->rotation, characterBinding->second.scale);
          auto local = world ? localTransform(*world, parent, entity.transform.scale)
                             : Core::Result<Transform>{std::unexpected(world.error())};

          if (!local) {
            m_impl->m_faulted = true;
            return context(entity.id, local.error());
          }

          auto rotation = Math::normalized(local->rotation);

          if (!rotation) {
            m_impl->m_faulted = true;
            return context(entity.id, rotation.error());
          }

          transform = {local->position, *rotation, local->scale};
          updates.push_back({entity.id, transform});
        }

        auto local = Math::compose(transform.position, transform.rotation, transform.scale);
        auto world = local ? Math::multiply(parent, *local)
                           : Core::Result<Math::Mat4>{std::unexpected(local.error())};

        if (!world) {
          m_impl->m_faulted = true;
          return context(entity.id, world.error());
        }

        matrices[index] = *world;
      }

      auto changed = scene.setTransforms(updates);

      if (!changed) {
        m_impl->m_faulted = true;
        return changed;
      }

      for (auto& [value, entry] : m_impl->m_entries) {
        entry.source = matrices[indices.at(value)];
      }

      for (auto& [value, entry] : m_impl->m_characters) {
        entry.source = matrices[indices.at(value)];
      }

      return {};
    } catch (const std::exception& error) {
      m_impl->m_faulted = true;
      return Core::fail(
          Core::ErrorCode::External,
          std::format("Scene physics writeback failed after advancement: {}", error.what()));
    }
  }

  auto SceneSimulation::reset(const Scene& scene) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene simulation has been moved");
    }

    auto ready = m_impl->ready();

    if (!ready) {
      return ready;
    }

    auto candidate = create(scene, m_impl->m_config);

    if (!candidate) {
      return std::unexpected(candidate.error());
    }

    m_impl.swap(candidate->m_impl);
    return {};
  }

  auto SceneSimulation::body(EntityId entity) const -> Core::Result<BodyState> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene simulation has been moved");
    }

    auto id = m_impl->resolve(entity);
    return id ? m_impl->m_world.body(*id) : Core::Result<BodyState>{std::unexpected(id.error())};
  }

  auto SceneSimulation::character(EntityId entity) const -> Core::Result<SceneCharacterState> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene simulation has been moved");
    }

    auto id = m_impl->resolveCharacter(entity);

    if (!id) {
      return std::unexpected(id.error());
    }

    auto state = m_impl->m_world.character(*id);

    if (!state) {
      return std::unexpected(state.error());
    }

    std::optional<EntityId> support;

    if (state->groundBody) {
      for (const auto& [value, entry] : m_impl->m_entries) {
        if (entry.body == *state->groundBody) {
          support = EntityId{m_impl->m_scene, value};
          break;
        }
      }
    }

    return SceneCharacterState{state->position, state->rotation,     state->velocity,
                               state->ground,   state->groundNormal, state->groundVelocity,
                               support};
  }

  auto SceneSimulation::setCharacterInput(EntityId entity, const CharacterInput& input)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene simulation has been moved");
    }

    auto id = m_impl->resolveCharacter(entity, true);
    return id ? m_impl->m_world.setCharacterInput(*id, input)
              : Core::Result<void>{std::unexpected(id.error())};
  }

  auto SceneSimulation::setCharacterYaw(EntityId entity, float yawDegrees) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene simulation has been moved");
    }

    auto id = m_impl->resolveCharacter(entity, true);
    return id ? m_impl->m_world.setCharacterYaw(*id, yawDegrees)
              : Core::Result<void>{std::unexpected(id.error())};
  }

  auto SceneSimulation::stats() const -> Core::Result<WorldStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene simulation has been moved");
    }

    auto ready = m_impl->ready();

    if (!ready) {
      return std::unexpected(ready.error());
    }

    auto result = m_impl->m_world.stats();

    if (result) {
      result->faulted = result->faulted || m_impl->m_faulted;
    }

    return result;
  }

  auto SceneSimulation::drainContactEvents() -> Core::Result<std::vector<SceneContactEvent>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene simulation has been moved");
    }

    auto ready = m_impl->ready(true);

    if (!ready) {
      return std::unexpected(ready.error());
    }

    bool consumed = false;

    try {
      auto stats = m_impl->m_world.stats();

      if (!stats) {
        return std::unexpected(stats.error());
      }

      std::vector<SceneContactEvent> result;
      result.reserve(stats->queuedContactEvents);
      auto events = m_impl->m_world.drainContactEvents();

      if (!events) {
        return std::unexpected(events.error());
      }

      consumed = true;

      for (auto& event : *events) {
        if (event.first.userTag == 0 || event.second.userTag == 0) {
          m_impl->m_faulted = true;
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Scene contact has no entity association");
        }

        result.push_back({event.phase,
                          event.endReason,
                          {m_impl->m_scene, event.first.userTag},
                          {m_impl->m_scene, event.second.userTag},
                          event.sensor,
                          event.step,
                          std::move(event.geometry)});
      }

      return result;
    } catch (const std::exception& error) {
      m_impl->m_faulted = m_impl->m_faulted || consumed;
      return Core::fail(Core::ErrorCode::External,
                        std::format("Read scene contact events: {}", error.what()));
    }
  }

  auto SceneSimulation::setVelocity(EntityId entity, Math::Vec3 velocity) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene simulation has been moved");
    }

    auto id = m_impl->resolve(entity, true);
    return id ? m_impl->m_world.setVelocity(*id, velocity)
              : Core::Result<void>{std::unexpected(id.error())};
  }

  auto SceneSimulation::setAngularVelocity(EntityId entity, Math::Vec3 velocity)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene simulation has been moved");
    }

    auto id = m_impl->resolve(entity, true);
    return id ? m_impl->m_world.setAngularVelocity(*id, velocity)
              : Core::Result<void>{std::unexpected(id.error())};
  }

  auto SceneSimulation::addImpulse(EntityId entity, Math::Vec3 impulse) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene simulation has been moved");
    }

    auto id = m_impl->resolve(entity, true);
    return id ? m_impl->m_world.addImpulse(*id, impulse)
              : Core::Result<void>{std::unexpected(id.error())};
  }

  auto SceneSimulation::moveKinematic(EntityId entity, Math::Vec3 position, Math::Quat rotation,
                                      double seconds) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene simulation has been moved");
    }

    auto id = m_impl->resolve(entity, true);
    return id ? m_impl->m_world.moveKinematic(*id, position, rotation, seconds)
              : Core::Result<void>{std::unexpected(id.error())};
  }

  auto SceneSimulation::rayCast(Math::Vec3 origin, Math::Vec3 displacement) const
      -> Core::Result<std::optional<SceneRayHit>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene simulation has been moved");
    }

    auto ready = m_impl->ready();

    if (!ready) {
      return std::unexpected(ready.error());
    }

    auto result = m_impl->m_world.rayCast(origin, displacement);

    if (!result) {
      return std::unexpected(result.error());
    }

    if (!result->has_value()) {
      return std::optional<SceneRayHit>{};
    }

    const auto& hit = **result;

    for (const auto& [value, entry] : m_impl->m_entries) {
      if (entry.body == hit.body) {
        return std::optional{
            SceneRayHit{{m_impl->m_scene, value}, hit.fraction, hit.point, hit.normal}};
      }
    }

    return Core::fail(Core::ErrorCode::External, "Scene ray hit has no entity binding");
  }
}  // namespace Dot::Physics

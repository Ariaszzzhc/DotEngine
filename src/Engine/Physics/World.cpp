module;

/** Jolt requires its configuration header before every other SDK header.
 * Jolt 要求配置头文件先于所有其他 SDK 头文件。 */
// clang-format off
#include <Jolt/Jolt.h>
// clang-format on

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <expected>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

module Dot.Physics.World;

import Dot.Physics.Body;
import Dot.Core.Result;
import Dot.Math;

namespace Dot::Physics {
  namespace {
    constexpr JPH::ObjectLayer staticLayer = 0;
    constexpr JPH::ObjectLayer movingLayer = 1;
    constexpr float maxSpeed = 500;
    constexpr float maxAngularSpeed = 0.25F * JPH::JPH_PI * 60;
    std::atomic<std::uint64_t> nextWorld{1};

    auto trace(const char* format, ...) noexcept -> void {
      std::array<char, 2048> message{};
      va_list arguments;
      va_start(arguments, format);
      std::vsnprintf(message.data(), message.size(), format, arguments);
      va_end(arguments);
      std::fprintf(stderr, "Jolt: %s\n", message.data());
    }

#ifdef JPH_ENABLE_ASSERTS
    thread_local bool updateActive{};

    class UpdateScope {
    public:
      UpdateScope() : m_previous(updateActive) {
        updateActive = true;
      }

      ~UpdateScope() {
        updateActive = m_previous;
      }

    private:
      bool m_previous;
    };

    auto reportAssert(const char* expression, const char* message, const char* file, JPH::uint line)
        -> bool {
      /** Jolt 5.6 asserts immediately before returning recoverable capacity error
       * flags. Permit only that final diagnostic; step reports the actual flags.
       * Jolt 5.6 在返回可恢复容量错误标志前触发断言；仅放行此末尾诊断，步进报告实际标志。 */
      if (updateActive && std::string_view{expression} == "errors == EPhysicsUpdateError::None"
          && std::string_view{file}.ends_with("PhysicsSystem.cpp")) {
        return false;
      }

      std::fprintf(stderr, "Jolt assertion: %s (%s) at %s:%u\n", expression, message ? message : "",
                   file, line);
      return true;
    }
#endif

    /** Keep registration alive until process exit, including between independent
     * worlds; re-registration would race workers in another world.
     * 注册保留至进程退出，包括独立世界之间的空档，避免重新注册与其他世界的线程竞争。 */
    class Runtime {
    public:
      Runtime() {
        if (JPH::Factory::sInstance != nullptr) {
          throw std::runtime_error("Jolt factory already owned by another runtime");
        }

        JPH::RegisterDefaultAllocator();
        m_factory = std::make_unique<JPH::Factory>();
        m_previousTrace = JPH::Trace;
        JPH::Trace = trace;
#ifdef JPH_ENABLE_ASSERTS
        m_previousAssert = JPH::AssertFailed;
        JPH::AssertFailed = reportAssert;
#endif
        JPH::Factory::sInstance = m_factory.get();

        try {
          JPH::RegisterTypes();
        } catch (...) {
          JPH::UnregisterTypes();
          JPH::Factory::sInstance = nullptr;
          restoreDiagnostics();
          throw;
        }
      }

      ~Runtime() {
        JPH::UnregisterTypes();
        JPH::Factory::sInstance = nullptr;
        restoreDiagnostics();
      }

    private:
      auto restoreDiagnostics() noexcept -> void {
        JPH::Trace = m_previousTrace;
#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed = m_previousAssert;
#endif
      }

      std::unique_ptr<JPH::Factory> m_factory;
      JPH::TraceFunction m_previousTrace{};
#ifdef JPH_ENABLE_ASSERTS
      JPH::AssertFailedFunction m_previousAssert{};
#endif
    };

    auto runtime() -> std::shared_ptr<Runtime> {
      static const auto owner = std::make_shared<Runtime>();
      return owner;
    }

    auto worldIdentity() -> Core::Result<std::uint64_t> {
      auto value = nextWorld.load();

      while (value != std::numeric_limits<std::uint64_t>::max()) {
        if (nextWorld.compare_exchange_weak(value, value + 1)) {
          return value;
        }
      }

      return Core::fail(Core::ErrorCode::InvalidState, "Physics world identity exhausted");
    }

    class Layers final : public JPH::BroadPhaseLayerInterface {
    public:
      auto GetNumBroadPhaseLayers() const -> JPH::uint override {
        return 2;
      }

      auto GetBroadPhaseLayer(JPH::ObjectLayer layer) const -> JPH::BroadPhaseLayer override {
        return JPH::BroadPhaseLayer{static_cast<JPH::BroadPhaseLayer::Type>(layer)};
      }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
      auto GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const -> const char* override {
        return layer.GetValue() == staticLayer ? "Static" : "Moving";
      }
#endif
    };

    class BroadFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:
      auto ShouldCollide(JPH::ObjectLayer first, JPH::BroadPhaseLayer second) const
          -> bool override {
        return first == movingLayer || second.GetValue() == movingLayer;
      }
    };

    class PairFilter final : public JPH::ObjectLayerPairFilter {
    public:
      auto ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const -> bool override {
        return first == movingLayer || second == movingLayer;
      }
    };

    auto native(Math::Vec3 value) -> JPH::Vec3 {
      return {value.x, value.y, value.z};
    }

    auto native(Math::Quat value) -> JPH::Quat {
      return {value.x, value.y, value.z, value.w};
    }

    auto vector(JPH::Vec3Arg value) -> Math::Vec3 {
      return {value.GetX(), value.GetY(), value.GetZ()};
    }

    auto quaternion(JPH::QuatArg value) -> Math::Quat {
      return {value.GetX(), value.GetY(), value.GetZ(), value.GetW()};
    }

    auto inRange(float value, float low, float high) -> bool {
      return std::isfinite(value) && value >= low && value <= high;
    }

    auto positionValid(Math::Vec3 value) -> bool {
      return inRange(value.x, -1'000'000, 1'000'000) && inRange(value.y, -1'000'000, 1'000'000)
             && inRange(value.z, -1'000'000, 1'000'000);
    }

    auto speedValid(Math::Vec3 value) -> bool {
      return Math::isFinite(value) && Math::length(value) <= maxSpeed;
    }

    auto durationValid(double seconds) -> bool {
      return std::isfinite(seconds) && seconds >= 0.000001 && seconds <= 0.25;
    }

    auto createShape(const BodyConfig& config) -> JPH::Shape::ShapeResult {
      switch (config.shape) {
        case Shape::Box:
          return JPH::BoxShapeSettings{native(config.halfExtents), 0}.Create();
        case Shape::Sphere:
          return JPH::SphereShapeSettings{config.radius}.Create();
        case Shape::Capsule:
          return JPH::CapsuleShapeSettings{config.halfHeight, config.radius}.Create();
      }

      throw std::logic_error("Unvalidated shape type");
    }

    auto nativeMotion(Motion motion) -> JPH::EMotionType {
      switch (motion) {
        case Motion::Static:
          return JPH::EMotionType::Static;
        case Motion::Kinematic:
          return JPH::EMotionType::Kinematic;
        case Motion::Dynamic:
          return JPH::EMotionType::Dynamic;
      }

      throw std::logic_error("Unvalidated motion type");
    }

    struct BodyDeleter {
      JPH::BodyInterface* interface;

      auto operator()(JPH::Body* body) const noexcept -> void {
        interface->DestroyBodyWithoutID(body);
      }
    };

    using PendingBody = std::unique_ptr<JPH::Body, BodyDeleter>;

    auto prepareBody(JPH::BodyInterface& bodies, const BodyConfig& config, std::uint64_t value)
        -> Core::Result<PendingBody> {
      auto shape = createShape(config);

      if (shape.HasError()) {
        return Core::fail(Core::ErrorCode::External,
                          std::format("Jolt shape creation failed: {}", shape.GetError()));
      }

      JPH::BodyCreationSettings settings{
          shape.Get().GetPtr(), native(config.position), native(config.rotation),
          nativeMotion(config.motion), config.motion == Motion::Static ? staticLayer : movingLayer};
      settings.mLinearVelocity = native(config.velocity);
      settings.mAngularVelocity = native(config.angularVelocity);
      settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
      settings.mMassPropertiesOverride.mMass = config.mass;
      settings.mFriction = config.friction;
      settings.mRestitution = config.restitution;
      settings.mGravityFactor = config.gravityFactor;
      settings.mIsSensor = config.sensor;
      settings.mUserData = value;
      settings.mLinearDamping = 0;
      settings.mAngularDamping = 0;
      auto* allocated = bodies.CreateBodyWithoutID(settings);

      if (!allocated) {
        return Core::fail(Core::ErrorCode::Unavailable, "Jolt body preparation failed");
      }

      return PendingBody{allocated, BodyDeleter{&bodies}};
    }
  }  // namespace

  struct World::Impl {
    struct Entry {
      JPH::BodyID nativeId;
      Motion motion;
      float mass;
      bool sensor;
    };

    std::shared_ptr<Runtime> m_runtime;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_identity;
    std::uint64_t m_nextBody{1};
    WorldConfig m_config;
    bool m_faulted{};
    Layers m_layers;
    BroadFilter m_broadFilter;
    PairFilter m_pairFilter;
    JPH::TempAllocatorImplWithMallocFallback m_temporary{16 * 1024 * 1024};
    std::unique_ptr<JPH::JobSystem> m_jobs;
    JPH::PhysicsSystem m_system;
    std::unordered_map<std::uint64_t, Entry> m_bodies;

    Impl(std::shared_ptr<Runtime> owner, std::uint64_t identity, WorldConfig config)
        : m_runtime(std::move(owner)), m_identity(identity), m_config(config) {
      if (config.workerThreads == 0) {
        m_jobs = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
      } else {
        m_jobs = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(config.workerThreads));
      }

      m_system.Init(config.maxBodies, 0, config.maxBodies * 16, config.maxBodies * 16, m_layers,
                    m_broadFilter, m_pairFilter);
      m_system.SetGravity(native(config.gravity));
      m_bodies.reserve(config.maxBodies);
    }

    ~Impl() {
      auto& bodies = m_system.GetBodyInterface();

      for (const auto& [value, entry] : m_bodies) {
        (void)value;
        bodies.RemoveBody(entry.nativeId);
        bodies.DestroyBody(entry.nativeId);
      }
    }

    auto ready(bool mutate = false) const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Physics operations require the creating thread");
      }

      if (mutate && m_faulted) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Physics world faulted during an earlier step; recreate it");
      }

      return {};
    }

    auto resolve(BodyId id, bool mutate = false) const -> Core::Result<Entry> {
      auto valid = ready(mutate);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      if (id.world != m_identity || id.value == 0) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Foreign/invalid physics body handle");
      }

      const auto found = m_bodies.find(id.value);

      if (found == m_bodies.end()) {
        return Core::fail(Core::ErrorCode::NotFound, "Physics body handle no longer exists");
      }

      return found->second;
    }
  };

  World::World(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  World::World(World&&) noexcept = default;

  auto World::operator=(World&&) noexcept -> World& = default;

  World::~World() = default;

  auto World::create(const WorldConfig& config) -> Core::Result<World> {
    if (config.maxBodies == 0 || config.maxBodies > 4096 || config.workerThreads > 8
        || !Math::isFinite(config.gravity) || Math::length(config.gravity) > 1000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid physics world limits/gravity");
    }

    auto identity = worldIdentity();

    if (!identity) {
      return std::unexpected(identity.error());
    }

    try {
      return World{std::make_unique<Impl>(runtime(), *identity, config)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        std::format("Cannot initialize physics world: {}", error.what()));
    }
  }

  auto World::createBody(const BodyConfig& config) -> Core::Result<BodyId> {
    const BodyChange change{std::nullopt, config};
    auto applied = applyChanges(std::span{&change, 1});

    if (!applied) {
      return std::unexpected(applied.error());
    }

    return *applied->front();
  }

  auto World::applyChanges(std::span<const BodyChange> changes)
      -> Core::Result<std::vector<std::optional<BodyId>>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto ready = m_impl->ready(true);

    if (!ready) {
      return std::unexpected(ready.error());
    }

    if (changes.size() > 8192) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Physics body batch exceeds 8192 changes");
    }

    if (changes.empty()) {
      return std::vector<std::optional<BodyId>>{};
    }

    auto& bodies = m_impl->m_system.GetBodyInterface();
    std::vector<std::optional<BodyId>> results;
    std::vector<std::optional<BodyConfig>> configs;
    std::vector<PendingBody> prepared;
    std::unordered_set<std::uint64_t> seen;
    auto nextValue = m_impl->m_nextBody;
    std::size_t count = m_impl->m_bodies.size();
    bool mutating = false;

    try {
      results.reserve(changes.size());
      configs.reserve(changes.size());
      prepared.reserve(changes.size());

      for (const auto& change : changes) {
        if (!change.body && !change.config) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Empty physics body change");
        }

        if (change.body) {
          auto entry = m_impl->resolve(*change.body, true);

          if (!entry) {
            return std::unexpected(entry.error());
          }

          if (!seen.insert(change.body->value).second) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Duplicate physics body change");
          }

          --count;
        }

        if (!change.config) {
          results.push_back(std::nullopt);
          configs.push_back(std::nullopt);
          continue;
        }

        auto config = validatedBodyConfig(*change.config);

        if (!config) {
          return std::unexpected(config.error());
        }

        BodyId id{};

        if (change.body) {
          id = *change.body;
        } else {
          if (nextValue == std::numeric_limits<std::uint64_t>::max()) {
            return Core::fail(Core::ErrorCode::Unavailable, "Physics body identity exhausted");
          }

          id = {m_impl->m_identity, nextValue++};
        }

        ++count;
        results.push_back(id);
        configs.push_back(*config);
      }

      if (count > m_impl->m_config.maxBodies) {
        return Core::fail(Core::ErrorCode::Unavailable, "Physics body batch exceeds capacity");
      }

      std::unordered_map<std::uint64_t, Impl::Entry> nodes;
      nodes.reserve(changes.size());

      for (std::size_t i = 0; i < changes.size(); ++i) {
        if (!configs[i]) {
          continue;
        }

        auto body = prepareBody(bodies, *configs[i], results[i]->value);

        if (!body) {
          return std::unexpected(body.error());
        }

        const auto& config = *configs[i];
        nodes.insert_or_assign(results[i]->value, Impl::Entry{JPH::BodyID{}, config.motion,
                                                              config.mass, config.sensor});
        prepared.push_back(std::move(*body));
      }

      /** Allocate map buckets/nodes and bodies before releasing any live slots.
       * Node transfer after commit starts needs no new allocations.
       * 释放活动槽前分配映射桶/节点与刚体，提交开始后的节点迁移不再进行新分配。 */
      m_impl->m_bodies.reserve(m_impl->m_config.maxBodies);
      mutating = true;
      m_impl->m_nextBody = nextValue;

      for (const auto& change : changes) {
        if (change.body) {
          const auto id = m_impl->m_bodies.at(change.body->value).nativeId;
          bodies.RemoveBody(id);
          bodies.DestroyBody(id);
          m_impl->m_bodies.erase(change.body->value);
        }
      }

      std::size_t pending = 0;

      for (std::size_t i = 0; i < changes.size(); ++i) {
        if (!configs[i]) {
          continue;
        }

        auto& body = prepared[pending++];

        if (!bodies.AssignBodyID(body.get())) {
          m_impl->m_faulted = true;
          return Core::fail(
              Core::ErrorCode::External,
              "Unexpected Jolt slot assignment failure; batch may be partially applied");
        }

        const auto nativeId = body->GetID();
        body.release();
        auto node = nodes.extract(results[i]->value);
        node.mapped().nativeId = nativeId;
        m_impl->m_bodies.insert(std::move(node));
        const auto& config = *configs[i];
        bodies.AddBody(nativeId, config.motion != Motion::Static && config.startActive
                                     ? JPH::EActivation::Activate
                                     : JPH::EActivation::DontActivate);
      }

      return results;
    } catch (const std::exception& error) {
      m_impl->m_faulted = m_impl->m_faulted || mutating;
      return Core::fail(
          Core::ErrorCode::External,
          std::format("Physics body batch failed{}: {}",
                      mutating ? " after mutation began" : " during preparation", error.what()));
    }
  }

  auto World::destroyBody(BodyId id) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto entry = m_impl->resolve(id);

    if (!entry) {
      return std::unexpected(entry.error());
    }

    auto& bodies = m_impl->m_system.GetBodyInterface();
    bodies.RemoveBody(entry->nativeId);
    bodies.DestroyBody(entry->nativeId);
    m_impl->m_bodies.erase(id.value);
    return {};
  }

  auto World::body(BodyId id) const -> Core::Result<BodyState> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto entry = m_impl->resolve(id);

    if (!entry) {
      return std::unexpected(entry.error());
    }

    const auto& bodies = m_impl->m_system.GetBodyInterface();
    JPH::RVec3 position;
    JPH::Quat rotation;
    bodies.GetPositionAndRotation(entry->nativeId, position, rotation);
    return BodyState{entry->motion,
                     vector(position),
                     quaternion(rotation),
                     vector(bodies.GetLinearVelocity(entry->nativeId)),
                     bodies.IsActive(entry->nativeId),
                     entry->sensor,
                     vector(bodies.GetAngularVelocity(entry->nativeId))};
  }

  auto World::step(double seconds) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto ready = m_impl->ready(true);

    if (!ready) {
      return std::unexpected(ready.error());
    }

    if (!durationValid(seconds)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Physics step requires finite seconds in [0.000001,0.25]");
    }

#ifdef JPH_ENABLE_ASSERTS
    const UpdateScope scope;
#endif
    const auto result = m_impl->m_system.Update(static_cast<float>(seconds),
                                                static_cast<int>(std::ceil(seconds * 60)),
                                                &m_impl->m_temporary, m_impl->m_jobs.get());

    if (result != JPH::EPhysicsUpdateError::None) {
      m_impl->m_faulted = true;
      return Core::fail(Core::ErrorCode::External,
                        std::format("Jolt step exceeded contact/pair capacity (flags={}); "
                                    "world may have advanced",
                                    static_cast<std::uint32_t>(result)));
    }

    for (const auto& [value, entry] : m_impl->m_bodies) {
      (void)entry;
      auto state = body({m_impl->m_identity, value});

      if (!state || !Math::isFinite(state->position) || !Math::isFinite(state->rotation)
          || !Math::isFinite(state->velocity) || !Math::isFinite(state->angularVelocity)) {
        m_impl->m_faulted = true;
        return Core::fail(Core::ErrorCode::External,
                          "Jolt step produced non-finite body state; world may have advanced");
      }
    }

    return {};
  }

  auto World::stats() const -> Core::Result<WorldStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto ready = m_impl->ready();

    if (!ready) {
      return std::unexpected(ready.error());
    }

    return WorldStats{m_impl->m_bodies.size(),
                      m_impl->m_system.GetNumActiveBodies(JPH::EBodyType::RigidBody),
                      m_impl->m_faulted};
  }

  auto World::setPose(BodyId id, Math::Vec3 position, Math::Quat rotation) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto entry = m_impl->resolve(id, true);

    if (!entry) {
      return std::unexpected(entry.error());
    }

    if (!positionValid(position)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid physics position");
    }

    auto normalized = Math::normalized(rotation);

    if (!normalized) {
      return std::unexpected(normalized.error());
    }

    m_impl->m_system.GetBodyInterface().SetPositionAndRotation(
        entry->nativeId, native(position), native(*normalized),
        entry->motion == Motion::Static ? JPH::EActivation::DontActivate
                                        : JPH::EActivation::Activate);
    return {};
  }

  auto World::setVelocity(BodyId id, Math::Vec3 velocity) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto entry = m_impl->resolve(id, true);

    if (!entry) {
      return std::unexpected(entry.error());
    }

    if (entry->motion == Motion::Static || !speedValid(velocity)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Velocity requires a moving body and finite speed at most 500");
    }

    m_impl->m_system.GetBodyInterface().SetLinearVelocity(entry->nativeId, native(velocity));
    return {};
  }

  auto World::setAngularVelocity(BodyId id, Math::Vec3 velocity) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto entry = m_impl->resolve(id, true);

    if (!entry) {
      return std::unexpected(entry.error());
    }

    if (entry->motion == Motion::Static || !Math::isFinite(velocity)
        || Math::length(velocity) > maxAngularSpeed) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid physics angular velocity");
    }

    m_impl->m_system.GetBodyInterface().SetAngularVelocity(entry->nativeId, native(velocity));
    return {};
  }

  auto World::addImpulse(BodyId id, Math::Vec3 impulse) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto entry = m_impl->resolve(id, true);

    if (!entry) {
      return std::unexpected(entry.error());
    }

    if (entry->motion != Motion::Dynamic || !Math::isFinite(impulse)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Impulse requires a dynamic body/finite vector");
    }

    const auto velocity = m_impl->m_system.GetBodyInterface().GetLinearVelocity(entry->nativeId);
    const double inverseMass = 1.0 / entry->mass;
    const double x = velocity.GetX() + impulse.x * inverseMass;
    const double y = velocity.GetY() + impulse.y * inverseMass;
    const double z = velocity.GetZ() + impulse.z * inverseMass;

    if (std::hypot(x, y, z) > maxSpeed) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Impulse would exceed maximum physics speed");
    }

    m_impl->m_system.GetBodyInterface().AddImpulse(entry->nativeId, native(impulse));
    return {};
  }

  auto World::moveKinematic(BodyId id, Math::Vec3 position, Math::Quat rotation, double seconds)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto entry = m_impl->resolve(id, true);

    if (!entry) {
      return std::unexpected(entry.error());
    }

    if (entry->motion != Motion::Kinematic || !positionValid(position) || !durationValid(seconds)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid kinematic body, position, or duration");
    }

    auto normalized = Math::normalized(rotation);

    if (!normalized) {
      return std::unexpected(normalized.error());
    }

    const auto state = body(id);
    const double speed = Math::length(position - state->position) / seconds;
    const auto current = state->rotation;
    const auto target = *normalized;
    const double x
        = static_cast<double>(target.x) * current.w - static_cast<double>(target.w) * current.x
          - static_cast<double>(target.y) * current.z + static_cast<double>(target.z) * current.y;
    const double y
        = static_cast<double>(target.y) * current.w - static_cast<double>(target.w) * current.y
          - static_cast<double>(target.z) * current.x + static_cast<double>(target.x) * current.z;
    const double z
        = static_cast<double>(target.z) * current.w - static_cast<double>(target.w) * current.z
          - static_cast<double>(target.x) * current.y + static_cast<double>(target.y) * current.x;
    const double w
        = static_cast<double>(current.x) * target.x + static_cast<double>(current.y) * target.y
          + static_cast<double>(current.z) * target.z + static_cast<double>(current.w) * target.w;
    /** atan2 preserves tiny rotations and ignores float quaternion length drift.
     * atan2 保留微小旋转，并忽略浮点四元数长度误差。 */
    const double angularSpeed = 2 * std::atan2(std::hypot(x, y, z), std::abs(w)) / seconds;

    if (speed > maxSpeed || angularSpeed > maxAngularSpeed) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Kinematic target exceeds linear/angular speed limits");
    }

    m_impl->m_system.GetBodyInterface().MoveKinematic(entry->nativeId, native(position),
                                                      native(target), static_cast<float>(seconds));
    return {};
  }

  auto World::rayCast(Math::Vec3 origin, Math::Vec3 displacement) const
      -> Core::Result<std::optional<RayHit>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto ready = m_impl->ready();

    if (!ready) {
      return std::unexpected(ready.error());
    }

    if (!positionValid(origin) || !Math::isFinite(displacement)
        || !positionValid(origin + displacement) || Math::length(displacement) < 0.001) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Ray endpoints require valid positions and length at least 0.001");
    }

    const JPH::RRayCast ray{native(origin), native(displacement)};
    JPH::RayCastResult hit;

    if (!m_impl->m_system.GetNarrowPhaseQuery().CastRay(ray, hit)) {
      return std::optional<RayHit>{};
    }

    const JPH::BodyLockRead locked{m_impl->m_system.GetBodyLockInterface(), hit.mBodyID};

    if (!locked.Succeeded()) {
      return Core::fail(Core::ErrorCode::External, "Jolt ray hit an unavailable body");
    }

    const auto point = ray.GetPointOnRay(hit.mFraction);
    std::optional<Math::Vec3> normal;

    if (hit.mFraction > 0) {
      const auto value = locked.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point);
      auto unit = Math::normalized(vector(value));

      if (!unit) {
        return Core::fail(Core::ErrorCode::External, "Jolt ray returned an invalid surface normal");
      }

      normal = *unit;
    }

    return std::optional{RayHit{{m_impl->m_identity, locked.GetBody().GetUserData()},
                                hit.mFraction,
                                vector(point),
                                normal}};
  }
}  // namespace Dot::Physics

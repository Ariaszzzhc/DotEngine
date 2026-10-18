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
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
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
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

module Dot.Physics.World;

import Dot.Physics.Body;
import Dot.Physics.Character;
import Dot.Core.Result;
import Dot.Math;

namespace Dot::Physics {
  namespace {
    constexpr JPH::ObjectLayer staticLayer = 0;
    constexpr JPH::ObjectLayer movingLayer = 1;
    constexpr std::uint64_t characterBit = std::uint64_t{1} << 63;

    auto collider(std::uint64_t world, std::uint64_t key) -> ColliderId {
      if ((key & characterBit) != 0) {
        return CharacterId{world, key & ~characterBit};
      }

      return BodyId{world, key};
    }

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
        case Shape::Mesh: {
          const auto view = config.mesh.view().value();
          JPH::MeshShapeSettings settings;
          settings.mTriangleVertices.reserve(view.vertices.size());
          settings.mIndexedTriangles.reserve(view.indices.size() / 3);

          for (const auto& point : view.vertices) {
            settings.mTriangleVertices.emplace_back(point.x, point.y, point.z);
          }

          for (std::size_t i = 0; i < view.indices.size(); i += 3) {
            settings.mIndexedTriangles.emplace_back(view.indices[i], view.indices[i + 1],
                                                    view.indices[i + 2]);
          }

          auto shape = settings.Create();

          if (shape.HasError()) {
            return shape;
          }

          if (shape.Get()->GetStats().mNumTriangles != config.mesh.triangles()) {
            JPH::Shape::ShapeResult error;
            error.SetError("Mesh cooking changed the validated triangle count");
            return error;
          }

          return JPH::ScaledShapeSettings(shape.Get().GetPtr(), native(config.meshScale)).Create();
        }
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

    /** Worker body callbacks and owner-thread character observations only copy
     * native data; no game code runs under native locks. Owner-side reconciliation
     * combines both providers after the step and retains one logical pair.
     * 工作线程刚体回调及所有者线程角色观察仅复制原生数据，不在原生锁内执行游戏代码。
     * 步进后由所有者合并两类来源，每个逻辑碰撞对仅保留一份。 */
    class ContactCollector final : public JPH::ContactListener,
                                   public JPH::CharacterContactListener {
    public:
      using Key = std::pair<std::uint64_t, std::uint64_t>;
      using TagLookup = std::function<std::uint64_t(std::uint64_t)>;
      using RemovalSet = std::unordered_map<std::uint64_t, ContactEndReason>;

      ContactCollector(std::uint64_t world, std::size_t maxPairs, std::size_t maxEvents,
                       TagLookup tags)
          : m_world(world), m_maxPairs(maxPairs), m_maxEvents(maxEvents), m_tags(std::move(tags)) {
      }

      auto OnContactAdded(const JPH::Body& first, const JPH::Body& second,
                          const JPH::ContactManifold& manifold,
                          JPH::ContactSettings& settings) noexcept -> void override {
        capture(first, second, manifold, settings);
      }

      auto OnContactPersisted(const JPH::Body& first, const JPH::Body& second,
                              const JPH::ContactManifold& manifold,
                              JPH::ContactSettings& settings) noexcept -> void override {
        capture(first, second, manifold, settings);
      }

      auto OnContactAdded(const JPH::CharacterVirtual* character,
                          const JPH::CharacterContact& contact,
                          JPH::CharacterContactSettings&) noexcept -> void override {
        captureCharacter(*character, contact);
      }

      auto OnContactPersisted(const JPH::CharacterVirtual* character,
                              const JPH::CharacterContact& contact,
                              JPH::CharacterContactSettings&) noexcept -> void override {
        captureCharacter(*character, contact);
      }

      auto OnCharacterContactAdded(const JPH::CharacterVirtual* character,
                                   const JPH::CharacterContact& contact,
                                   JPH::CharacterContactSettings&) noexcept -> void override {
        captureCharacter(*character, contact);
      }

      auto OnCharacterContactPersisted(const JPH::CharacterVirtual* character,
                                       const JPH::CharacterContact& contact,
                                       JPH::CharacterContactSettings&) noexcept -> void override {
        captureCharacter(*character, contact);
      }

      auto begin() -> void {
        m_pending.clear();
        m_virtual.clear();
        m_failure.store(Failure::None);
      }

      auto beginVirtual() -> void {
        m_virtual.clear();
      }

      auto retainVirtual(const JPH::CharacterVirtual& character,
                         const JPH::CharacterContact& contact) -> Core::Result<void> {
        if (contact.mHadCollision && !contact.mWasDiscarded && !contact.mIsSensorB) {
          const auto owner = character.GetUserData();
          const auto other = contact.mUserData;
          const Key key{std::min(owner, other), std::max(owner, other)};

          if (!m_virtual.contains(key) && m_virtual.size() == m_maxPairs) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Active character contact pair capacity exceeded");
          }

          m_virtual.insert(key);
          captureCharacter(character, contact);
        }

        return {};
      }

      template <typename Probe>
      auto finish(std::uint64_t step, Probe&& probe) -> Core::Result<void> {
        if (m_failure.load() != Failure::None) {
          return Core::fail(m_failure.load() == Failure::Capacity ? Core::ErrorCode::Unavailable
                                                                  : Core::ErrorCode::External,
                            "Contact callback capture failed; physics may have advanced");
        }

        for (const auto& [key, captured] : m_pending) {
          if (!m_records.contains(key)) {
            if (m_records.size() == m_maxPairs) {
              return Core::fail(Core::ErrorCode::Unavailable,
                                "Tracked contact pair capacity exceeded");
            }

            m_records.emplace(key, Record{captured.first, captured.second, captured.sensor});
          }
        }

        for (auto iterator = m_records.begin(); iterator != m_records.end();) {
          auto& record = iterator->second;
          auto observed = m_pending.find(iterator->first);

          if (observed != m_pending.end()) {
            record.native |= std::holds_alternative<ContactManifold>(observed->second.geometry);
            auto emitted
                = emit({record.announced ? ContactPhase::Stay : ContactPhase::Begin,
                        ContactEndReason::Separated, record.first, record.second,
                        observed->second.sensor, step, std::move(observed->second.geometry)});

            if (!emitted) {
              return emitted;
            }

            record.announced = true;
            record.sensor = observed->second.sensor;
          }

          const auto [nativeTouching, nativeSleeping]
              = probe(iterator->first, record.sleeping, record.native);
          record.native = nativeTouching || nativeSleeping;
          const bool virtualTouching = m_virtual.contains(iterator->first);
          const bool touching = nativeTouching || virtualTouching;
          const bool sleeping = nativeSleeping && !virtualTouching;

          if (sleeping) {
            if (!record.sleeping) {
              auto emitted = emit({ContactPhase::Sleep, ContactEndReason::Separated, record.first,
                                   record.second, record.sensor, step, std::nullopt});

              if (!emitted) {
                return emitted;
              }
            }

            record.sleeping = true;
          } else if (touching) {
            if (record.sleeping && observed == m_pending.end()) {
              auto emitted = emit({ContactPhase::Stay, ContactEndReason::Separated, record.first,
                                   record.second, record.sensor, step, std::nullopt});

              if (!emitted) {
                return emitted;
              }
            }

            record.sleeping = false;
          } else {
            auto emitted = emit({ContactPhase::End, ContactEndReason::Separated, record.first,
                                 record.second, record.sensor, step, std::nullopt});

            if (!emitted) {
              return emitted;
            }

            iterator = m_records.erase(iterator);
            continue;
          }

          ++iterator;
        }

        m_pending.clear();
        return {};
      }

      auto prepareRemoval(const RemovalSet& bodies) -> Core::Result<void> {
        std::size_t needed = 0;

        for (const auto& [key, record] : m_records) {
          (void)record;
          needed += bodies.contains(key.first) || bodies.contains(key.second);
        }

        if (needed > m_maxEvents - m_events.size()) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Drain contact events before changing body lifetime/pose");
        }

        m_events.reserve(m_events.size() + needed);
        return {};
      }

      auto remove(const RemovalSet& bodies, std::uint64_t step, bool publish) -> void {
        std::erase_if(m_virtual, [&](const auto& key) {
          return bodies.contains(key.first) || bodies.contains(key.second);
        });

        for (auto iterator = m_records.begin(); iterator != m_records.end();) {
          auto first = bodies.find(iterator->first.first);
          auto second = bodies.find(iterator->first.second);

          if (first == bodies.end() && second == bodies.end()) {
            ++iterator;
            continue;
          }

          const auto& record = iterator->second;
          const auto reason
              = (first != bodies.end() && first->second == ContactEndReason::Destroyed)
                        || (second != bodies.end() && second->second == ContactEndReason::Destroyed)
                    ? ContactEndReason::Destroyed
                : first != bodies.end() ? first->second
                                        : second->second;

          if (publish) {
            m_events.push_back({ContactPhase::End, reason, record.first, record.second,
                                record.sensor, step, std::nullopt});
          }

          iterator = m_records.erase(iterator);
        }
      }

      auto drain() -> std::vector<ContactEvent> {
        return std::exchange(m_events, {});
      }

      auto pairs() const noexcept -> std::size_t {
        return m_records.size();
      }

      auto queued() const noexcept -> std::size_t {
        return m_events.size();
      }

    private:
      enum class Failure { None, Capacity, External };

      struct Captured {
        ContactParty first;
        ContactParty second;
        bool sensor;
        ContactGeometry geometry;
      };

      struct Record {
        ContactParty first;
        ContactParty second;
        bool sensor;
        bool sleeping{};
        bool announced{};
        bool native{};
      };

      auto emit(ContactEvent event) -> Core::Result<void> {
        if (m_events.size() == m_maxEvents) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Contact event queue capacity exceeded; physics may have advanced");
        }

        m_events.push_back(std::move(event));
        return {};
      }

      auto captureCharacter(const JPH::CharacterVirtual& character,
                            const JPH::CharacterContact& contact) noexcept -> void {
        try {
          const auto owner = character.GetUserData();
          const auto other = contact.mUserData;
          const bool ownerFirst = owner < other;
          ContactParty first{collider(m_world, owner), m_tags(owner)};
          ContactParty second{collider(m_world, other), m_tags(other)};

          if (!ownerFirst) {
            std::swap(first, second);
          }

          CharacterContactGeometry geometry{
              vector(contact.mContactNormal) * (ownerFirst ? -1.0F : 1.0F),
              vector(contact.mPosition), contact.mDistance, ownerFirst};

          if (owner == other || contact.mIsSensorB || contact.mWasDiscarded
              || !Math::isFinite(geometry.normal) || !Math::isFinite(geometry.point)
              || std::abs(Math::length(geometry.normal) - 1) > 0.001
              || !std::isfinite(geometry.separation)) {
            m_failure.store(Failure::External);
            return;
          }

          const std::scoped_lock lock(m_mutex);
          const Key key{std::min(owner, other), std::max(owner, other)};
          const auto existing = m_pending.find(key);

          if (existing == m_pending.end() && m_pending.size() == m_maxPairs) {
            m_failure.store(Failure::Capacity);
            return;
          }

          /** Prefer real manifolds when both native providers observed the pair.
           * 两种原生来源均观察到同一碰撞对时，优先保留真实刚体流形。 */
          if (existing == m_pending.end()
              || !std::holds_alternative<ContactManifold>(existing->second.geometry)) {
            m_pending.insert_or_assign(key, Captured{first, second, false, geometry});
          }
        } catch (...) {
          m_failure.store(Failure::External);
        }
      }

      auto capture(const JPH::Body& firstBody, const JPH::Body& secondBody,
                   const JPH::ContactManifold& manifold,
                   const JPH::ContactSettings& settings) noexcept -> void {
        try {
          const auto firstValue = firstBody.GetUserData();
          const auto secondValue = secondBody.GetUserData();
          const bool swapped = firstValue > secondValue;
          ContactParty first{collider(m_world, firstValue), m_tags(firstValue)};
          ContactParty second{collider(m_world, secondValue), m_tags(secondValue)};

          if (swapped) {
            std::swap(first, second);
          }

          ContactManifold geometry;
          geometry.normal = vector(manifold.mWorldSpaceNormal) * (swapped ? -1.0F : 1.0F);
          geometry.penetrationDepth = manifold.mPenetrationDepth;
          if (!Math::isFinite(geometry.normal)
              || std::abs(Math::length(geometry.normal) - 1) > 0.001
              || !std::isfinite(geometry.penetrationDepth)
              || manifold.mRelativeContactPointsOn1.size()
                     != manifold.mRelativeContactPointsOn2.size()) {
            m_failure.store(Failure::External);
            return;
          }

          geometry.points.reserve(manifold.mRelativeContactPointsOn1.size());

          for (JPH::uint index = 0; index < manifold.mRelativeContactPointsOn1.size(); ++index) {
            auto firstPoint = vector(manifold.GetWorldSpaceContactPointOn1(index));
            auto secondPoint = vector(manifold.GetWorldSpaceContactPointOn2(index));

            if (!Math::isFinite(firstPoint) || !Math::isFinite(secondPoint)) {
              m_failure.store(Failure::External);
              return;
            }

            geometry.points.push_back(swapped ? ContactPoint{secondPoint, firstPoint}
                                              : ContactPoint{firstPoint, secondPoint});
          }

          const std::scoped_lock lock(m_mutex);
          const Key key{std::min(firstValue, secondValue), std::max(firstValue, secondValue)};

          if (!m_pending.contains(key) && m_pending.size() == m_maxPairs) {
            m_failure.store(Failure::Capacity);
            return;
          }

          m_pending.insert_or_assign(
              key, Captured{first, second, settings.mIsSensor, std::move(geometry)});
        } catch (...) {
          m_failure.store(Failure::External);
        }
      }

      std::uint64_t m_world;
      std::size_t m_maxPairs;
      std::size_t m_maxEvents;
      TagLookup m_tags;
      std::mutex m_mutex;
      std::atomic<Failure> m_failure{Failure::None};
      std::map<Key, Captured> m_pending;
      std::map<Key, Record> m_records;
      std::set<Key> m_virtual;
      std::vector<ContactEvent> m_events;
    };

    class OnlyBody final : public JPH::BodyFilter {
    public:
      explicit OnlyBody(JPH::BodyID id) : m_id(id) {
      }

      auto ShouldCollide(const JPH::BodyID& id) const -> bool override {
        return id == m_id;
      }

    private:
      JPH::BodyID m_id;
    };

    class SolidBodies final : public JPH::BodyFilter {
    public:
      auto ShouldCollideLocked(const JPH::Body& body) const -> bool override {
        return !body.IsSensor() && (body.GetUserData() & characterBit) == 0;
      }
    };

    class SweepBodies final : public JPH::BodyFilter {
    public:
      SweepBodies(bool includeSensors, JPH::BodyID ignore)
          : m_includeSensors(includeSensors), m_ignore(ignore) {
      }

      auto ShouldCollide(const JPH::BodyID& body) const -> bool override {
        return body != m_ignore;
      }

      auto ShouldCollideLocked(const JPH::Body& body) const -> bool override {
        return m_includeSensors || !body.IsSensor();
      }

    private:
      bool m_includeSensors;
      JPH::BodyID m_ignore;
    };

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
      settings.mMotionQuality = config.collisionDetection == CollisionDetection::Continuous
                                    ? JPH::EMotionQuality::LinearCast
                                    : JPH::EMotionQuality::Discrete;
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
      std::uint64_t userTag;
      std::size_t meshTriangles{};
    };

    struct CharacterEntry {
      std::unique_ptr<JPH::CharacterVirtual> capsule;
      CharacterProperties properties;
      CharacterInput input;
      std::uint64_t userTag;
      Math::Vec3 measuredVelocity{};
    };

    std::shared_ptr<Runtime> m_runtime;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_identity;
    std::uint64_t m_nextBody{1};
    std::uint64_t m_nextCharacter{1};
    WorldConfig m_config;
    bool m_faulted{};
    std::uint64_t m_step{};
    Layers m_layers;
    BroadFilter m_broadFilter;
    PairFilter m_pairFilter;
    JPH::TempAllocatorImplWithMallocFallback m_temporary{16 * 1024 * 1024};
    std::unique_ptr<JPH::JobSystem> m_jobs;
    ContactCollector m_contacts;
    JPH::PhysicsSystem m_system;
    std::unordered_map<std::uint64_t, Entry> m_bodies;
    JPH::CharacterVsCharacterCollisionSimple m_characterCollisions;
    std::map<std::uint64_t, CharacterEntry> m_characters;

    Impl(std::shared_ptr<Runtime> owner, std::uint64_t identity, WorldConfig config)
        : m_runtime(std::move(owner)),
          m_identity(identity),
          m_config(config),
          m_contacts(identity, (config.maxBodies + config.maxCharacters) * 16,
                     config.maxContactEvents, [this](std::uint64_t value) {
                       return (value & characterBit) == 0
                                  ? std::as_const(m_bodies).at(value).userTag
                                  : std::as_const(m_characters).at(value & ~characterBit).userTag;
                     }) {
      if (config.workerThreads == 0) {
        m_jobs = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
      } else {
        m_jobs = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(config.workerThreads));
      }

      /** Native inner proxies are prepared before publishing a batch. Reserve one
       * additional character set so replacements work at full public capacity.
       * 原生内部代理在批量发布前准备，多预留一组角色槽，使公开容量满时仍可替换。 */
      const auto slots = config.maxBodies + 2 * config.maxCharacters;
      const auto pairs = (config.maxBodies + config.maxCharacters) * 16;
      m_system.Init(slots, 0, pairs, pairs, m_layers, m_broadFilter, m_pairFilter);
      m_system.SetGravity(native(config.gravity));
      m_bodies.reserve(config.maxBodies);
      m_characterCollisions.mCharacters.reserve(config.maxCharacters);
      m_system.SetContactListener(&m_contacts);
    }

    ~Impl() {
      m_characterCollisions.mCharacters.clear();
      m_characters.clear();
      m_system.SetContactListener(nullptr);
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

    auto resolve(CharacterId id, bool mutate = false) -> Core::Result<CharacterEntry*> {
      auto valid = ready(mutate);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      if (id.world != m_identity || id.value == 0) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Foreign/invalid physics character handle");
      }

      const auto found = m_characters.find(id.value);

      if (found == m_characters.end()) {
        return Core::fail(Core::ErrorCode::NotFound, "Physics character handle no longer exists");
      }

      return &found->second;
    }

    auto nativeCollider(std::uint64_t key) const -> JPH::BodyID {
      return (key & characterBit) == 0
                 ? m_bodies.at(key).nativeId
                 : m_characters.at(key & ~characterBit).capsule->GetInnerBodyID();
    }

    auto resolveCollider(const ColliderId& id) -> Core::Result<JPH::BodyID> {
      return std::visit(
          [this](auto value) -> Core::Result<JPH::BodyID> {
            auto entry = resolve(value);

            if (!entry) {
              return std::unexpected(entry.error());
            }

            if constexpr (std::is_same_v<decltype(value), BodyId>) {
              return entry->nativeId;
            } else {
              return (*entry)->capsule->GetInnerBodyID();
            }
          },
          id);
    }

    auto refreshCharacter(JPH::CharacterVirtual& character) -> Core::Result<void> {
      character.RefreshContacts(m_system.GetDefaultBroadPhaseLayerFilter(movingLayer),
                                m_system.GetDefaultLayerFilter(movingLayer), SolidBodies{}, {},
                                m_temporary);

      if (character.GetMaxHitsExceeded()) {
        return Core::fail(Core::ErrorCode::Unavailable, "Character contact capacity exceeded");
      }

      return {};
    }

    auto prepareCharacter(const CharacterConfig& config, std::uint64_t id)
        -> Core::Result<CharacterEntry> {
      auto rotation = Math::rotationEulerDegrees({0, config.yawDegrees, 0});

      if (!rotation) {
        return std::unexpected(rotation.error());
      }

      auto shape = JPH::CapsuleShapeSettings{config.halfHeight, config.radius}.Create();

      if (shape.HasError()) {
        return Core::fail(Core::ErrorCode::External,
                          std::format("Cannot create character capsule: {}", shape.GetError()));
      }

      JPH::CharacterVirtualSettings settings;
      settings.mID = JPH::CharacterID{static_cast<JPH::uint32>(id)};
      settings.mShape = shape.Get();
      settings.mInnerBodyShape = shape.Get();
      settings.mInnerBodyLayer = movingLayer;
      settings.mShapeOffset = {0, config.radius + config.halfHeight, 0};
      settings.mSupportingVolume = JPH::Plane{JPH::Vec3::sAxisY(), -config.radius};
      settings.mMaxSlopeAngle = JPH::DegreesToRadians(config.maxSlopeDegrees);
      settings.mMass = config.mass;
      settings.mMaxStrength = config.maxStrength;
      settings.mMaxNumHits = config.maxContacts;
      auto capsule = std::make_unique<JPH::CharacterVirtual>(
          &settings, native(config.position), native(*rotation), characterBit | id, &m_system);

      if (capsule->GetInnerBodyID().IsInvalid()) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Cannot allocate character broad-phase proxy");
      }

      capsule->SetCharacterVsCharacterCollision(&m_characterCollisions);
      return CharacterEntry{std::move(capsule), config, {}, config.userTag};
    }

    auto advanceCharacters(float seconds) -> Core::Result<void> {
      const auto gravity = native(m_config.gravity);
      m_contacts.beginVirtual();

      for (auto& [id, entry] : m_characters) {
        (void)id;
        auto& character = *entry.capsule;
        const auto before = character.GetPosition();

        /** Refresh before using support: bodies/peers may have moved or been
         * removed since the last substep, and must not enable a stale jump.
         * 支撑使用前刷新，刚体/其他角色可能已移动或删除，不能用过期支撑起跳。 */
        auto refreshed = refreshCharacter(character);

        if (!refreshed) {
          return refreshed;
        }

        character.UpdateGroundVelocity();
        const auto vertical = character.GetLinearVelocity().GetY();
        const auto ground = character.GetGroundVelocity();
        auto velocity = JPH::Vec3{0, vertical, 0};

        if (character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround
            && vertical - ground.GetY() < .1F) {
          velocity = ground;

          if (entry.input.jump) {
            velocity += JPH::Vec3{0, entry.properties.jumpSpeed, 0};
          }
        }

        entry.input.jump = false;
        velocity += native(entry.input.velocity) + gravity * seconds;
        /** ExtendedUpdate retains desired motion for its stair test before
         * rejecting steep-slope velocity; pre-clipping would prevent stepping.
         * ExtendedUpdate 在坡面速度裁剪前保留台阶探测所需意图，提前裁剪会阻止登阶。 */
        character.SetLinearVelocity(velocity);
        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        settings.mWalkStairsStepUp = {0, entry.properties.stepHeight, 0};
        settings.mStickToFloorStepDown = {0, -entry.properties.stickDistance, 0};

        /** Only live simulation observes callbacks; preparation/teleport queries
         * must not publish contacts or borrow unpublished identities.
         * 仅存活仿真观察回调，准备/传送查询不得发布接触或借用未发布身份。 */
        struct ListenerScope {
          JPH::CharacterVirtual& m_character;

          ~ListenerScope() {
            m_character.SetListener(nullptr);
          }
        } listener{character};

        character.SetListener(&m_contacts);
        character.ExtendedUpdate(
            seconds, gravity, settings, m_system.GetDefaultBroadPhaseLayerFilter(movingLayer),
            m_system.GetDefaultLayerFilter(movingLayer), SolidBodies{}, {}, m_temporary);

        entry.measuredVelocity = vector((character.GetPosition() - before) / seconds);

        if (!Math::isFinite(entry.measuredVelocity) || character.GetMaxHitsExceeded()
            || !Math::isFinite(vector(character.GetPosition()))
            || !Math::isFinite(vector(character.GetLinearVelocity()))) {
          const auto position = vector(character.GetPosition());
          return Core::fail(
              Core::ErrorCode::External,
              std::format(
                  "Character {} update: contact capacity exceeded={}, limit={}, "
                  "position=({},{},{}), measured=({},{},{}), velocity=({},{},{})",
                  id, character.GetMaxHitsExceeded(), entry.properties.maxContacts, position.x,
                  position.y, position.z, entry.measuredVelocity.x, entry.measuredVelocity.y,
                  entry.measuredVelocity.z, character.GetLinearVelocity().GetX(),
                  character.GetLinearVelocity().GetY(), character.GetLinearVelocity().GetZ()));
        }
      }

      for (const auto& [id, entry] : m_characters) {
        (void)id;

        for (const auto& contact : entry.capsule->GetActiveContacts()) {
          auto retained = m_contacts.retainVirtual(*entry.capsule, contact);

          if (!retained) {
            return retained;
          }
        }
      }

      return {};
    }
  };

  World::World(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  World::World(World&&) noexcept = default;

  auto World::operator=(World&&) noexcept -> World& = default;

  World::~World() = default;

  auto World::create(const WorldConfig& config) -> Core::Result<World> {
    if (config.maxBodies == 0 || config.maxBodies > 4096 || config.workerThreads > 8
        || config.maxCharacters > 256 || config.maxContactEvents == 0
        || config.maxContactEvents > 262144 || !Math::isFinite(config.gravity)
        || Math::length(config.gravity) > 1000) {
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
    ContactCollector::RemovalSet removals;
    auto nextValue = m_impl->m_nextBody;
    std::size_t count = m_impl->m_bodies.size();
    std::size_t meshTriangles{};

    for (const auto& [key, entry] : m_impl->m_bodies) {
      (void)key;
      meshTriangles += entry.meshTriangles;
    }

    bool mutating = false;

    try {
      results.reserve(changes.size());
      configs.reserve(changes.size());
      prepared.reserve(changes.size());

      for (const auto& change : changes) {
        if ((change.preserveVelocity || change.preserveAngularVelocity)
            && (!change.body || !change.config || change.config->motion == Motion::Static)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Preserving motion requires a moving-body replacement");
        }

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

          removals.emplace(change.body->value, change.config ? ContactEndReason::Reconfigured
                                                             : ContactEndReason::Destroyed);
          --count;
          meshTriangles -= entry->meshTriangles;
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
          if (nextValue == characterBit) {
            return Core::fail(Core::ErrorCode::Unavailable, "Physics body identity exhausted");
          }

          id = {m_impl->m_identity, nextValue++};
        }

        ++count;
        meshTriangles += config->mesh.triangles();
        results.push_back(id);
        configs.push_back(*config);
      }

      if (meshTriangles > 1048576) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Physics bodies exceed 1048576 collision triangles");
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

        auto& config = *configs[i];

        if (changes[i].preserveVelocity || changes[i].preserveAngularVelocity) {
          const auto nativeId = m_impl->m_bodies.at(changes[i].body->value).nativeId;

          if (changes[i].preserveVelocity) {
            config.velocity = vector(bodies.GetLinearVelocity(nativeId));
          }

          if (changes[i].preserveAngularVelocity) {
            config.angularVelocity = vector(bodies.GetAngularVelocity(nativeId));
          }
        }

        auto body = prepareBody(bodies, config, results[i]->value);

        if (!body) {
          return std::unexpected(body.error());
        }

        nodes.insert_or_assign(results[i]->value,
                               Impl::Entry{JPH::BodyID{}, config.motion, config.mass, config.sensor,
                                           config.userTag, config.mesh.triangles()});
        prepared.push_back(std::move(*body));
      }

      /** Allocate map buckets/nodes and bodies before releasing any live slots.
       * Node transfer after commit starts needs no new allocations.
       * 释放活动槽前分配映射桶/节点与刚体，提交开始后的节点迁移不再进行新分配。 */
      m_impl->m_bodies.reserve(m_impl->m_config.maxBodies);
      auto eventsReady = m_impl->m_contacts.prepareRemoval(removals);

      if (!eventsReady) {
        return std::unexpected(eventsReady.error());
      }

      mutating = true;
      m_impl->m_contacts.remove(removals, m_impl->m_step, true);
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

  auto World::applyChanges(std::span<const BodyChange> bodies,
                           std::span<const CharacterChange> characters)
      -> Core::Result<WorldChangeResult> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto ready = m_impl->ready(true);

    if (!ready) {
      return std::unexpected(ready.error());
    }

    if (characters.size() > 512) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Character batch exceeds 512 changes");
    }

    bool committed{};

    try {
      WorldChangeResult result;
      result.characters.reserve(characters.size());
      std::unordered_set<std::uint64_t> seen;
      std::map<std::uint64_t, Impl::CharacterEntry> prepared;
      ContactCollector::RemovalSet removals;
      auto count = m_impl->m_characters.size();
      auto next = m_impl->m_nextCharacter;

      for (const auto& change : characters) {
        if ((!change.character && !change.config)
            || (change.preserveMotion && (!change.character || !change.config))) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid character batch entry");
        }

        Impl::CharacterEntry* previous{};

        if (change.character) {
          auto entry = m_impl->resolve(*change.character, true);

          if (!entry) {
            return std::unexpected(entry.error());
          }

          if (!seen.insert(change.character->value).second) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Duplicate character batch handle");
          }

          previous = *entry;
          removals.emplace(
              characterBit | change.character->value,
              change.config ? ContactEndReason::Reconfigured : ContactEndReason::Destroyed);
          --count;
        }

        if (!change.config) {
          result.characters.push_back({});
          continue;
        }

        auto config = validatedCharacterConfig(*change.config);

        if (!config) {
          return std::unexpected(config.error());
        }

        const auto gravity = m_impl->m_config.gravity;

        if (gravity.x != 0 || gravity.z != 0 || gravity.y > 0) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Y-up characters require vertical nonpositive world gravity");
        }

        if (!change.character && next >= JPH::CharacterID::cInvalidCharacterID) {
          return Core::fail(Core::ErrorCode::Unavailable, "Physics character IDs exhausted");
        }

        const auto id = change.character.value_or(CharacterId{m_impl->m_identity, next});

        if (!change.character) {
          ++next;
        }

        auto entry = m_impl->prepareCharacter(*config, id.value);

        if (!entry) {
          return std::unexpected(entry.error());
        }

        if (change.preserveMotion) {
          entry->capsule->SetLinearVelocity(previous->capsule->GetLinearVelocity());
          entry->input = previous->input;
          entry->measuredVelocity = previous->measuredVelocity;
        }

        prepared.emplace(id.value, std::move(*entry));
        result.characters.push_back(id);
        ++count;
      }

      if (count > m_impl->m_config.maxCharacters) {
        return Core::fail(Core::ErrorCode::Unavailable, "Character batch exceeds capacity");
      }

      for (const auto& change : bodies) {
        if (change.body) {
          removals.emplace(change.body->value, change.config ? ContactEndReason::Reconfigured
                                                             : ContactEndReason::Destroyed);
        }
      }

      auto eventsReady = m_impl->m_contacts.prepareRemoval(removals);

      if (!eventsReady) {
        return std::unexpected(eventsReady.error());
      }

      auto applied = applyChanges(bodies);

      if (!applied) {
        return std::unexpected(applied.error());
      }

      result.bodies = std::move(*applied);
      committed = !bodies.empty() || !characters.empty();
      m_impl->m_contacts.remove(removals, m_impl->m_step, true);
      m_impl->m_nextCharacter = next;

      for (const auto& change : characters) {
        if (change.character) {
          const auto found = m_impl->m_characters.find(change.character->value);
          m_impl->m_characterCollisions.Remove(found->second.capsule.get());
          m_impl->m_characters.erase(found);
        }
      }

      while (!prepared.empty()) {
        auto node = prepared.extract(prepared.begin());
        auto* capsule = node.mapped().capsule.get();
        m_impl->m_characters.insert(std::move(node));
        m_impl->m_characterCollisions.Add(capsule);
      }

      if (committed) {
        for (auto& [id, entry] : m_impl->m_characters) {
          (void)id;
          auto refreshed = m_impl->refreshCharacter(*entry.capsule);

          if (!refreshed) {
            m_impl->m_faulted = true;
            return Core::fail(refreshed.error().code,
                              "Physics batch published before character query failed: "
                                  + refreshed.error().message);
          }
        }
      }

      return result;
    } catch (const std::exception& error) {
      m_impl->m_faulted = m_impl->m_faulted || committed;
      return Core::fail(
          Core::ErrorCode::External,
          std::format("Physics mixed batch failed{}: {}",
                      committed ? " after publication" : " during preparation", error.what()));
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

    try {
      const ContactCollector::RemovalSet removed{{id.value, ContactEndReason::Destroyed}};

      if (!m_impl->m_faulted) {
        auto eventsReady = m_impl->m_contacts.prepareRemoval(removed);

        if (!eventsReady) {
          return eventsReady;
        }
      }

      m_impl->m_contacts.remove(removed, m_impl->m_step, !m_impl->m_faulted);
      auto& bodies = m_impl->m_system.GetBodyInterface();
      bodies.RemoveBody(entry->nativeId);
      bodies.DestroyBody(entry->nativeId);
      m_impl->m_bodies.erase(id.value);
      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        std::format("Prepare body contact cleanup: {}", error.what()));
    }
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
    const auto quality = bodies.GetMotionQuality(entry->nativeId);

    if (quality != JPH::EMotionQuality::Discrete && quality != JPH::EMotionQuality::LinearCast) {
      return Core::fail(Core::ErrorCode::External, "Unknown native collision detection mode");
    }

    return BodyState{entry->motion,
                     quality == JPH::EMotionQuality::LinearCast ? CollisionDetection::Continuous
                                                                : CollisionDetection::Discrete,
                     vector(position),
                     quaternion(rotation),
                     vector(bodies.GetLinearVelocity(entry->nativeId)),
                     bodies.IsActive(entry->nativeId),
                     entry->sensor,
                     vector(bodies.GetAngularVelocity(entry->nativeId))};
  }

  auto World::createCharacter(const CharacterConfig& config) -> Core::Result<CharacterId> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto ready = m_impl->ready(true);

    if (!ready) {
      return std::unexpected(ready.error());
    }

    auto validated = validatedCharacterConfig(config);

    if (!validated) {
      return std::unexpected(validated.error());
    }

    const auto gravity = m_impl->m_config.gravity;

    if (gravity.x != 0 || gravity.z != 0 || gravity.y > 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Y-up characters require vertical nonpositive world gravity");
    }

    if (m_impl->m_characters.size() == m_impl->m_config.maxCharacters
        || m_impl->m_nextCharacter >= JPH::CharacterID::cInvalidCharacterID) {
      return Core::fail(Core::ErrorCode::Unavailable, "Physics character capacity/IDs exhausted");
    }

    try {
      auto prepared = m_impl->prepareCharacter(*validated, m_impl->m_nextCharacter);

      if (!prepared) {
        return std::unexpected(prepared.error());
      }

      auto refreshed = m_impl->refreshCharacter(*prepared->capsule);

      if (!refreshed) {
        return std::unexpected(refreshed.error());
      }

      const CharacterId result{m_impl->m_identity, m_impl->m_nextCharacter};
      const auto [inserted, created]
          = m_impl->m_characters.emplace(result.value, std::move(*prepared));
      (void)created;
      /** The collider list is reserved at world creation; publish only after all
       * character construction, contact queries and map allocation succeed.
       * 碰撞列表在世界创建时预留，完成角色构造、接触查询与映射分配后才发布。 */
      m_impl->m_characterCollisions.Add(inserted->second.capsule.get());
      ++m_impl->m_nextCharacter;
      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        std::format("Cannot initialize character: {}", error.what()));
    }
  }

  auto World::destroyCharacter(CharacterId id) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto entry = m_impl->resolve(id);

    if (!entry) {
      return std::unexpected(entry.error());
    }

    try {
      const ContactCollector::RemovalSet removed{
          {characterBit | id.value, ContactEndReason::Destroyed}};

      if (!m_impl->m_faulted) {
        auto eventsReady = m_impl->m_contacts.prepareRemoval(removed);

        if (!eventsReady) {
          return eventsReady;
        }
      }

      m_impl->m_contacts.remove(removed, m_impl->m_step, !m_impl->m_faulted);
      m_impl->m_characterCollisions.Remove((*entry)->capsule.get());
      m_impl->m_characters.erase(id.value);
      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        std::format("Prepare character contact cleanup: {}", error.what()));
    }
  }

  auto World::character(CharacterId id) const -> Core::Result<CharacterState> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto entry = m_impl->resolve(id);

    if (!entry) {
      return std::unexpected(entry.error());
    }

    const auto& capsule = *(*entry)->capsule;
    GroundState ground;

    switch (capsule.GetGroundState()) {
      case JPH::CharacterBase::EGroundState::OnGround:
        ground = GroundState::Grounded;
        break;
      case JPH::CharacterBase::EGroundState::OnSteepGround:
        ground = GroundState::Steep;
        break;
      case JPH::CharacterBase::EGroundState::NotSupported:
        ground = GroundState::Unsupported;
        break;
      case JPH::CharacterBase::EGroundState::InAir:
        ground = GroundState::Airborne;
        break;
      default:
        return Core::fail(Core::ErrorCode::External, "Unknown native character ground state");
    }

    std::optional<ColliderId> support;
    const auto key = capsule.GetGroundUserData();

    if ((key & characterBit) != 0) {
      const auto found = m_impl->m_characters.find(key & ~characterBit);

      if (found != m_impl->m_characters.end()) {
        support = CharacterId{m_impl->m_identity, found->first};
      }
    } else {
      const auto found = m_impl->m_bodies.find(key);

      if (found != m_impl->m_bodies.end() && found->second.nativeId == capsule.GetGroundBodyID()) {
        support = BodyId{m_impl->m_identity, found->first};
      }
    }

    return CharacterState{vector(capsule.GetPosition()),
                          quaternion(capsule.GetRotation()),
                          vector(capsule.GetLinearVelocity()),
                          ground,
                          vector(capsule.GetGroundNormal()),
                          vector(capsule.GetGroundVelocity()),
                          support,
                          (*entry)->measuredVelocity};
  }

  auto World::setCharacterInput(CharacterId id, const CharacterInput& input) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto entry = m_impl->resolve(id, true);

    if (!entry) {
      return std::unexpected(entry.error());
    }

    auto valid = validateCharacterInput(input);

    if (!valid) {
      return valid;
    }

    (*entry)->input.velocity = input.velocity;
    (*entry)->input.jump = (*entry)->input.jump || input.jump;
    return {};
  }

  auto World::setCharacterYaw(CharacterId id, float yawDegrees) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto entry = m_impl->resolve(id, true);

    if (!entry) {
      return std::unexpected(entry.error());
    }

    CharacterConfig config;
    config.yawDegrees = yawDegrees;
    auto valid = validatedCharacterConfig(config);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto rotation = Math::rotationEulerDegrees({0, valid->yawDegrees, 0});

    if (!rotation) {
      return std::unexpected(rotation.error());
    }

    (*entry)->capsule->SetRotation(native(*rotation));
    return {};
  }

  auto World::teleportCharacter(CharacterId id, Math::Vec3 feet, float yawDegrees)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto entry = m_impl->resolve(id, true);

    if (!entry) {
      return std::unexpected(entry.error());
    }

    CharacterConfig config;
    config.position = feet;
    config.yawDegrees = yawDegrees;
    auto valid = validatedCharacterConfig(config);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto rotation = Math::rotationEulerDegrees({0, valid->yawDegrees, 0});

    if (!rotation) {
      return std::unexpected(rotation.error());
    }

    bool mutating{};

    try {
      const ContactCollector::RemovalSet removed{
          {characterBit | id.value, ContactEndReason::PoseChanged}};
      auto eventsReady = m_impl->m_contacts.prepareRemoval(removed);

      if (!eventsReady) {
        return eventsReady;
      }

      mutating = true;
      m_impl->m_contacts.remove(removed, m_impl->m_step, true);
      auto& capsule = *(*entry)->capsule;
      capsule.SetPosition(native(feet));
      capsule.SetRotation(native(*rotation));
      capsule.SetLinearVelocity(JPH::Vec3::sZero());
      (*entry)->input = {};
      (*entry)->measuredVelocity = {};
      auto refreshed = m_impl->refreshCharacter(capsule);

      if (!refreshed) {
        m_impl->m_faulted = true;
        return Core::fail(refreshed.error().code,
                          "Character teleport advanced pose: " + refreshed.error().message);
      }

      return {};
    } catch (const std::exception& error) {
      m_impl->m_faulted = m_impl->m_faulted || mutating;
      return Core::fail(
          Core::ErrorCode::External,
          std::format("Character teleport failed {}: {}",
                      mutating ? "after publication" : "during preparation", error.what()));
    }
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
    if (m_impl->m_step == std::numeric_limits<std::uint64_t>::max()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics contact step counter exhausted");
    }

    ++m_impl->m_step;
    m_impl->m_contacts.begin();
    auto result = JPH::EPhysicsUpdateError::None;
    const auto substeps = static_cast<int>(std::ceil(seconds * 60));

    if (m_impl->m_characters.empty()) {
      result = m_impl->m_system.Update(static_cast<float>(seconds), substeps, &m_impl->m_temporary,
                                       m_impl->m_jobs.get());
    } else {
      try {
        const auto interval = static_cast<float>(seconds / substeps);

        for (int substep = 0; substep < substeps; ++substep) {
          auto advanced = m_impl->advanceCharacters(interval);

          if (!advanced) {
            m_impl->m_faulted = true;
            return Core::fail(advanced.error().code, "Physics character step may have advanced: "
                                                         + advanced.error().message);
          }

          result = m_impl->m_system.Update(interval, 1, &m_impl->m_temporary, m_impl->m_jobs.get());

          if (result != JPH::EPhysicsUpdateError::None) {
            break;
          }
        }
      } catch (const std::exception& error) {
        m_impl->m_faulted = true;
        return Core::fail(
            Core::ErrorCode::External,
            std::format("Physics character step may have advanced: {}", error.what()));
      }
    }

    if (result != JPH::EPhysicsUpdateError::None) {
      m_impl->m_faulted = true;
      return Core::fail(Core::ErrorCode::External,
                        std::format("Jolt step exceeded contact/pair capacity (flags={}); "
                                    "world may have advanced",
                                    static_cast<std::uint32_t>(result)));
    }

    try {
      auto collected = m_impl->m_contacts.finish(
          m_impl->m_step, [&](ContactCollector::Key key, bool wasSleeping, bool hadNativeContact) {
            /** An inactive nearby proxy does not prove a virtual-only pair is
             * a sleeping native manifold. Probe native retention only after an
             * actual native observation, independently of virtual membership.
             * 不活动的邻近代理不能证明纯虚拟接触是休眠原生流形；仅实际观察过原生接触
             * 后查询其保留状态，并与虚拟接触集合分别跟踪。 */
            if (!hadNativeContact) {
              return std::pair{false, false};
            }

            const auto first = m_impl->nativeCollider(key.first);
            const auto second = m_impl->nativeCollider(key.second);
            const auto& bodies = m_impl->m_system.GetBodyInterface();
            bool touching = m_impl->m_system.WereBodiesInContact(first, second);
            bool sleeping = !bodies.IsActive(first) && !bodies.IsActive(second);

            if (sleeping && !wasSleeping) {
              /** Confirm geometry when entering dormancy; two inactive bodies
               * alone do not prove that the last contact still exists.
               * 进入休眠时确认几何，两刚体均不活动并不证明最后一次接触仍存在。 */
              const auto shape = bodies.GetShape(first);
              const auto transform = bodies.GetCenterOfMassTransform(first);
              JPH::CollideShapeSettings settings;
              settings.mMaxSeparationDistance
                  = m_impl->m_system.GetPhysicsSettings().mSpeculativeContactDistance;
              JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> hit;
              m_impl->m_system.GetNarrowPhaseQuery().CollideShape(
                  shape.GetPtr(), JPH::Vec3::sReplicate(1), transform, settings,
                  transform.GetTranslation(), hit, {}, {}, OnlyBody{second});
              sleeping = hit.HadHit();
              touching = touching && sleeping;
            }

            return std::pair{touching, sleeping};
          });

      if (!collected) {
        m_impl->m_faulted = true;
        return collected;
      }
    } catch (const std::exception& error) {
      m_impl->m_faulted = true;
      return Core::fail(
          Core::ErrorCode::External,
          std::format("Contact reconciliation failed after advancement: {}", error.what()));
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

    return WorldStats{
        m_impl->m_bodies.size(),
        static_cast<std::size_t>(std::ranges::count_if(
            m_impl->m_bodies,
            [&](const auto& item) {
              return m_impl->m_system.GetBodyInterface().IsActive(item.second.nativeId);
            })),
        m_impl->m_faulted,
        m_impl->m_contacts.pairs(),
        m_impl->m_contacts.queued(),
        m_impl->m_characters.size()};
  }

  auto World::drainContactEvents() -> Core::Result<std::vector<ContactEvent>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto ready = m_impl->ready(true);

    if (!ready) {
      return std::unexpected(ready.error());
    }

    return m_impl->m_contacts.drain();
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

    JPH::RVec3 previousPosition;
    JPH::Quat previousRotation;
    m_impl->m_system.GetBodyInterface().GetPositionAndRotation(entry->nativeId, previousPosition,
                                                               previousRotation);
    const auto previous = quaternion(previousRotation);
    const Math::Quat opposed{-previous.x, -previous.y, -previous.z, -previous.w};

    if (vector(previousPosition) != position
        || (previous != *normalized && opposed != *normalized)) {
      try {
        const ContactCollector::RemovalSet removed{{id.value, ContactEndReason::PoseChanged}};
        auto eventsReady = m_impl->m_contacts.prepareRemoval(removed);

        if (!eventsReady) {
          return eventsReady;
        }

        m_impl->m_contacts.remove(removed, m_impl->m_step, true);
      } catch (const std::exception& error) {
        return Core::fail(Core::ErrorCode::External,
                          std::format("Prepare pose contact cleanup: {}", error.what()));
      }
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

    return std::optional{RayHit{collider(m_impl->m_identity, locked.GetBody().GetUserData()),
                                hit.mFraction, vector(point), normal}};
  }

  auto World::sphereCast(Math::Vec3 origin, float radius, Math::Vec3 displacement,
                         const SphereCastFilter& filter) const
      -> Core::Result<std::optional<SphereHit>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Physics world has been moved");
    }

    auto ready = m_impl->ready();

    if (!ready) {
      return std::unexpected(ready.error());
    }

    if (!positionValid(origin) || !Math::isFinite(displacement)
        || !positionValid(origin + displacement) || Math::length(displacement) < .001
        || !std::isfinite(radius) || radius < .001F || radius > 10000) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Sphere sweep requires bounded endpoints, radius and nonzero displacement");
    }

    JPH::BodyID ignored;

    if (filter.ignoreCollider) {
      auto body = m_impl->resolveCollider(*filter.ignoreCollider);

      if (!body) {
        return std::unexpected(body.error());
      }

      ignored = *body;
    }

    try {
      const JPH::SphereShape shape{radius};
      const JPH::RShapeCast cast{&shape, JPH::Vec3::sReplicate(1),
                                 JPH::RMat44::sTranslation(native(origin)), native(displacement)};
      JPH::ShapeCastSettings settings;
      settings.mUseShrunkenShapeAndConvexRadius = true;
      settings.mReturnDeepestPoint = true;
      settings.SetBackFaceMode(JPH::EBackFaceMode::CollideWithBackFaces);
      JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
      const SweepBodies bodies{filter.includeSensors, ignored};
      m_impl->m_system.GetNarrowPhaseQuery().CastShape(cast, settings, native(origin), collector,
                                                       {}, {}, bodies);

      if (!collector.HadHit()) {
        return std::optional<SphereHit>{};
      }

      const auto& hit = collector.mHit;
      const JPH::BodyLockRead locked{m_impl->m_system.GetBodyLockInterface(), hit.mBodyID2};

      if (!locked.Succeeded()) {
        return Core::fail(Core::ErrorCode::External, "Sphere sweep hit an unavailable body");
      }

      const auto point = origin + vector(hit.mContactPointOn2);
      const auto axis = vector(-hit.mPenetrationAxis);

      if (!std::isfinite(hit.mFraction) || hit.mFraction < 0 || hit.mFraction > 1
          || !Math::isFinite(point) || !Math::isFinite(axis)
          || !std::isfinite(hit.mPenetrationDepth)) {
        return Core::fail(Core::ErrorCode::External, "Sphere sweep returned invalid contact data");
      }

      std::optional<Math::Vec3> normal;
      auto unit = Math::normalized(axis);

      if (unit) {
        normal = *unit;
      } else if (hit.mFraction > 0) {
        return Core::fail(Core::ErrorCode::External, "Sphere sweep returned an invalid normal");
      }

      return std::optional{
          SphereHit{collider(m_impl->m_identity, locked.GetBody().GetUserData()), hit.mFraction,
                    origin + displacement * hit.mFraction, point, normal,
                    hit.mFraction == 0 ? std::max(0.F, hit.mPenetrationDepth) : 0.F}};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Sphere sweep failed: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Physics

#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

import Dot.Physics.World;
import Dot.Core.Result;
import Dot.Math;

using namespace Dot::Physics;
using Dot::Math::Vec3;

namespace {
  auto ground(bool sensor = false) -> BodyConfig {
    BodyConfig config;
    config.motion = Motion::Static;
    config.shape = Shape::Box;
    config.position = {0, -0.5F, 0};
    config.halfExtents = {10, 0.5F, 10};
    config.sensor = sensor;
    return config;
  }
}  // namespace

TEST_CASE("physics.sphere_falls_collides_and_sleeps_on_ground") {
  auto world = World::create();
  REQUIRE_MESSAGE(world, (world ? "" : world.error().message));
  auto floor = world->createBody(ground());
  BodyConfig config;
  config.position = {0, 5, 0};
  auto sphere = world->createBody(config);
  REQUIRE(floor);
  REQUIRE(sphere);
  CHECK(world->stats()->bodies == 2);
  CHECK(world->stats()->active == 1);

  for (int i = 0; i < 300; ++i) {
    REQUIRE(world->step(1.0 / 60));
  }

  const auto state = world->body(*sphere);
  REQUIRE(state);
  CHECK(state->motion == Motion::Dynamic);
  CHECK(state->position.y == doctest::Approx(0.5).epsilon(0.03));
  CHECK(std::abs(state->velocity.y) < 0.02);
  CHECK_FALSE(state->active);
  CHECK(world->stats()->active == 0);
  REQUIRE(world->setVelocity(*sphere, {0, 2, 0}));
  CHECK(world->body(*sphere)->active);
  REQUIRE(world->step(1.0 / 60));
  CHECK(world->body(*sphere)->position.y > state->position.y);
  CHECK(world->body(*floor)->position == ground().position);
}

TEST_CASE("physics.sensor_has_no_collision_response") {
  auto world = World::create();
  REQUIRE(world);
  REQUIRE(world->createBody(ground(true)));
  BodyConfig config;
  config.position = {0, 2, 0};
  auto sphere = world->createBody(config);
  REQUIRE(sphere);

  for (int i = 0; i < 90; ++i) {
    REQUIRE(world->step(1.0 / 60));
  }

  CHECK(world->body(*sphere)->position.y < -5);
}

TEST_CASE("physics.impulse_uses_mass_and_preserves_velocity") {
  auto world = World::create({8, {}, 0});
  REQUIRE(world);
  BodyConfig config;
  config.mass = 2;
  config.velocity = {1, 0, 0};
  auto body = world->createBody(config);
  REQUIRE(body);
  REQUIRE(world->addImpulse(*body, {4, 0, 0}));
  CHECK(world->body(*body)->velocity.x == doctest::Approx(3));
  REQUIRE(world->step(0.25));
  CHECK(world->body(*body)->position.x == doctest::Approx(0.75));
  CHECK(world->body(*body)->velocity.x == doctest::Approx(3));
  REQUIRE(world->setVelocity(*body, {}));
  REQUIRE(world->step(0.25));
  CHECK(world->body(*body)->position.x == doctest::Approx(0.75));
}

TEST_CASE("physics.box_and_capsule_shapes_collide") {
  for (auto shape : {Shape::Box, Shape::Capsule}) {
    auto world = World::create();
    REQUIRE(world);
    REQUIRE(world->createBody(ground()));
    BodyConfig config;
    config.shape = shape;
    config.position = {0, 3, 0};
    auto body = world->createBody(config);
    REQUIRE(body);

    for (int i = 0; i < 240; ++i) {
      REQUIRE(world->step(1.0 / 60));
    }

    const float height = shape == Shape::Box ? 0.5F : 1.0F;
    CHECK(world->body(*body)->position.y == doctest::Approx(height).epsilon(0.04));
  }
}

TEST_CASE("physics.gravity_factor_and_substeps") {
  auto world = World::create({8, {0, -4, 0}, 0});
  REQUIRE(world);
  BodyConfig config;
  config.gravityFactor = 0;
  auto unchanged = world->createBody(config);
  REQUIRE(unchanged);
  config.position = {5, 0, 0};
  config.gravityFactor = 2;
  auto falling = world->createBody(config);
  REQUIRE(falling);
  REQUIRE(world->step(0.25));
  CHECK(world->body(*unchanged)->position == Vec3{});
  CHECK(world->body(*falling)->velocity.y == doctest::Approx(-2));
  CHECK(world->body(*falling)->position.y < -0.25);
  CHECK(world->body(*falling)->position.y > -0.3);
}

TEST_CASE("physics.ray_nearest_hit_normal_miss_and_inside") {
  auto world = World::create({8, {}, 0});
  REQUIRE(world);
  BodyConfig config;
  config.motion = Motion::Static;
  auto sphere = world->createBody(config);
  REQUIRE(sphere);
  config.position = {0, -3, 0};
  REQUIRE(world->createBody(config));
  auto ray = world->rayCast({0, 2, 0}, {0, -10, 0});
  REQUIRE(ray);
  REQUIRE(ray->has_value());
  CHECK((*ray)->collider == ColliderId{*sphere});
  CHECK((*ray)->fraction == doctest::Approx(0.15));
  CHECK((*ray)->point.y == doctest::Approx(0.5));
  REQUIRE((*ray)->normal);
  CHECK((*ray)->normal->y == doctest::Approx(1));
  auto miss = world->rayCast({3, 2, 0}, {0, -10, 0});
  REQUIRE(miss);
  CHECK_FALSE(miss->has_value());
  auto inside = world->rayCast({}, {1, 0, 0});
  REQUIRE(inside);
  REQUIRE(inside->has_value());
  CHECK((*inside)->fraction == 0);
  CHECK_FALSE((*inside)->normal);
  REQUIRE(world->destroyBody(*sphere));
  CHECK_FALSE(world->rayCast({}, {1, 0, 0})->has_value());
}

TEST_CASE("physics.pose_normalization_and_static_broadphase_update") {
  auto world = World::create({8, {}, 0});
  REQUIRE(world);
  BodyConfig config;
  config.motion = Motion::Static;
  config.shape = Shape::Box;
  config.rotation = {0, 0, 0, 10};
  auto body = world->createBody(config);
  REQUIRE(body);
  CHECK(world->body(*body)->rotation == Dot::Math::Quat{});
  REQUIRE(world->setPose(*body, {3, 0, 0}, {0, 0, 0, 2}));
  CHECK(world->body(*body)->position == Vec3{3, 0, 0});
  CHECK_FALSE(world->rayCast({0, 2, 0}, {0, -4, 0})->has_value());
  auto ray = world->rayCast({3, 2, 0}, {0, -4, 0});
  REQUIRE(ray);
  REQUIRE(ray->has_value());
  CHECK((*ray)->collider == ColliderId{*body});
}

TEST_CASE("physics.kinematic_target_moves_on_step") {
  auto world = World::create({8, {}, 0});
  REQUIRE(world);
  BodyConfig config;
  config.motion = Motion::Kinematic;
  auto body = world->createBody(config);
  REQUIRE(body);
  const auto rotation = Dot::Math::rotationEulerDegrees({0, 45, 0});
  REQUIRE(rotation);
  REQUIRE(world->moveKinematic(*body, {1, 0, 0}, *rotation, 0.25));
  CHECK(world->body(*body)->position == Vec3{});
  CHECK(world->body(*body)->velocity.x == doctest::Approx(4));
  REQUIRE(world->step(0.25));
  auto state = world->body(*body);
  REQUIRE(state);
  CHECK(state->position.x == doctest::Approx(1));
  CHECK(std::abs(state->rotation.y) == doctest::Approx(rotation->y).epsilon(0.001));
  REQUIRE(world->setVelocity(*body, {}));
  CHECK_FALSE(world->addImpulse(*body, {1, 0, 0}));
}

TEST_CASE("physics.invalid_world_limits_rejected") {
  for (const auto config :
       {WorldConfig{0}, WorldConfig{4097}, WorldConfig{8, {}, 9}, WorldConfig{8, {1001, 0, 0}, 0},
        WorldConfig{8, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, 0}}) {
    auto world = World::create(config);
    REQUIRE_FALSE(world);
    CHECK(world.error().code == Dot::Core::ErrorCode::InvalidArgument);
  }

  REQUIRE(World::create({1, {1000, 0, 0}, 0}));
  REQUIRE(World::create({4096, {}, 0}));
}

TEST_CASE("physics.invalid_body_fields_do_not_allocate") {
  auto world = World::create({8, {}, 0});
  REQUIRE(world);
  std::vector<BodyConfig> invalid;
  auto append = [&](auto edit) {
    BodyConfig config;
    edit(config);
    invalid.push_back(config);
  };
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  append([](auto& c) { c.motion = static_cast<Motion>(90); });
  append([](auto& c) { c.shape = static_cast<Shape>(90); });
  append([](auto& c) { c.radius = 0; });
  append([](auto& c) { c.halfHeight = 10001; });
  append([](auto& c) { c.halfExtents = {1, 0, 1}; });
  append([](auto& c) { c.mass = 0; });
  append([](auto& c) { c.mass = 1'000'001; });
  append([](auto& c) { c.friction = -0.1F; });
  append([](auto& c) { c.restitution = 1.1F; });
  append([](auto& c) { c.gravityFactor = 11; });
  append([](auto& c) { c.position = {1'000'001, 0, 0}; });
  append([](auto& c) { c.velocity = {501, 0, 0}; });
  append([](auto& c) { c.rotation = {0, 0, 0, 0}; });
  append([](auto& c) {
    c.motion = Motion::Static;
    c.velocity = {1, 0, 0};
  });
  append([&](auto& c) { c.mass = nan; });
  append([&](auto& c) { c.velocity.y = nan; });
  append([&](auto& c) { c.rotation.z = nan; });

  for (const auto& config : invalid) {
    CHECK_FALSE(world->createBody(config));
    CHECK(world->stats()->bodies == 0);
  }

  BodyConfig small;
  small.radius = 0.001F;
  small.mass = 0.01F;
  REQUIRE(world->createBody(small));
  BodyConfig large;
  large.radius = 10000;
  large.mass = 1'000'000;
  large.position = {1'000'000, 1'000'000, 1'000'000};
  REQUIRE(world->createBody(large));
}

TEST_CASE("physics.invalid_mutations_preserve_state") {
  auto world = World::create({8, {}, 0});
  REQUIRE(world);
  auto body = world->createBody();
  REQUIRE(body);
  const auto before = world->body(*body).value();
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(world->setPose(*body, {nan, 0, 0}));
  CHECK_FALSE(world->setPose(*body, {}, {0, 0, 0, 0}));
  CHECK_FALSE(world->setVelocity(*body, {300, 400, 1}));
  CHECK_FALSE(world->setVelocity(*body, {nan, 0, 0}));
  CHECK_FALSE(world->addImpulse(*body, {501, 0, 0}));
  CHECK_FALSE(world->addImpulse(*body, {nan, 0, 0}));
  CHECK_FALSE(world->moveKinematic(*body, {}, {}, 0.1));
  CHECK(world->body(*body)->position == before.position);
  CHECK(world->body(*body)->rotation == before.rotation);
  CHECK(world->body(*body)->velocity == before.velocity);
  REQUIRE(world->setVelocity(*body, {300, 400, 0}));
  CHECK(world->body(*body)->velocity.x == doctest::Approx(300));
  CHECK(world->body(*body)->velocity.y == doctest::Approx(400));
}

TEST_CASE("physics.static_operations_reject_velocity_and_impulse") {
  auto world = World::create();
  REQUIRE(world);
  auto body = world->createBody(ground());
  REQUIRE(body);
  CHECK_FALSE(world->setVelocity(*body, {}));
  CHECK_FALSE(world->addImpulse(*body, {}));
  CHECK_FALSE(world->moveKinematic(*body, {}, {}, 0.1));
  CHECK(world->body(*body)->velocity == Vec3{});
}

TEST_CASE("physics.invalid_duration_does_not_advance") {
  auto world = World::create();
  REQUIRE(world);
  auto body = world->createBody();
  REQUIRE(body);

  for (const double dt : {0.0, -1.0, 0.250001, 1e-9, std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity()}) {
    CHECK_FALSE(world->step(dt));
    CHECK(world->body(*body)->position == Vec3{});
    CHECK(world->body(*body)->velocity == Vec3{});
  }

  REQUIRE(world->step(0.000001));
  CHECK(world->body(*body)->velocity.y < 0);
}

TEST_CASE("physics.invalid_rays_distinguish_errors_from_misses") {
  auto world = World::create();
  REQUIRE(world);
  CHECK_FALSE(world->rayCast({}, {}));
  CHECK_FALSE(world->rayCast({}, {0.0001F, 0, 0}));
  CHECK_FALSE(world->rayCast({1'000'000, 0, 0}, {1, 0, 0}));
  CHECK_FALSE(world->rayCast({}, {std::numeric_limits<float>::infinity(), 0, 0}));
  auto miss = world->rayCast({}, {0.001F, 0, 0});
  REQUIRE(miss);
  CHECK_FALSE(miss->has_value());
}

TEST_CASE("physics.kinematic_limits_preserve_prior_velocity") {
  auto world = World::create({8, {}, 0});
  REQUIRE(world);
  BodyConfig config;
  config.motion = Motion::Kinematic;
  auto body = world->createBody(config);
  REQUIRE(body);
  CHECK_FALSE(world->moveKinematic(*body, {100, 0, 0}, {}, 0.1));
  CHECK_FALSE(world->moveKinematic(*body, {}, {0, 0, 0, 0}, 0.1));
  CHECK_FALSE(world->moveKinematic(*body, {}, {}, 0));
  auto rotation = Dot::Math::rotationEulerDegrees({180, 0, 0});
  REQUIRE(rotation);
  CHECK_FALSE(world->moveKinematic(*body, {}, *rotation, 0.001));
  CHECK(world->body(*body)->velocity == Vec3{});
  CHECK(world->body(*body)->rotation == Dot::Math::Quat{});
}

TEST_CASE("physics.stationary_rotated_kinematic_target_has_zero_speed") {
  auto world = World::create({8, {}, 0});
  REQUIRE(world);
  BodyConfig config;
  config.motion = Motion::Kinematic;
  config.rotation = Dot::Math::rotationEulerDegrees({17, 31, 53}).value();
  auto body = world->createBody(config);
  REQUIRE(body);
  const auto rotation = world->body(*body)->rotation;
  REQUIRE(world->moveKinematic(*body, {}, rotation, 0.000001));
  CHECK(world->body(*body)->velocity == Vec3{});
  REQUIRE(world->step(0.000001));
  const auto after = world->body(*body)->rotation;
  CHECK(after.x == doctest::Approx(rotation.x));
  CHECK(after.y == doctest::Approx(rotation.y));
  CHECK(after.z == doctest::Approx(rotation.z));
  CHECK(after.w == doctest::Approx(rotation.w));
}

TEST_CASE("physics.restitution_changes_collision_response") {
  for (const float restitution : {0.0F, 1.0F}) {
    auto world = World::create();
    REQUIRE(world);
    auto floor = ground();
    floor.restitution = restitution;
    REQUIRE(world->createBody(floor));
    BodyConfig config;
    config.position = {0, 2, 0};
    config.restitution = restitution;
    auto sphere = world->createBody(config);
    REQUIRE(sphere);

    for (int n = 0; n < 42; ++n) {
      REQUIRE(world->step(1.0 / 60));
    }

    if (restitution == 1) {
      CHECK(world->body(*sphere)->velocity.y > 2);
      CHECK(world->body(*sphere)->position.y > 0.8F);
    } else {
      CHECK(std::abs(world->body(*sphere)->velocity.y) < 0.1F);
      CHECK(world->body(*sphere)->position.y < 0.6F);
    }
  }
}

TEST_CASE("physics.capacity_stale_handles_and_foreign_worlds") {
  auto world = World::create({1, {}, 0});
  auto other = World::create({1, {}, 0});
  REQUIRE(world);
  REQUIRE(other);
  auto first = world->createBody();
  REQUIRE(first);
  auto full = world->createBody();
  REQUIRE_FALSE(full);
  CHECK(full.error().code == Dot::Core::ErrorCode::Unavailable);
  CHECK_FALSE(other->body(*first));
  CHECK_FALSE(other->destroyBody(*first));
  CHECK_FALSE(world->body({}));
  REQUIRE(world->destroyBody(*first));
  CHECK(world->stats()->bodies == 0);
  CHECK(world->stats()->active == 0);
  auto second = world->createBody();
  REQUIRE(second);
  CHECK(second->value > first->value);
  CHECK_FALSE(world->body(*first));
  CHECK_FALSE(world->destroyBody(*first));
  CHECK_FALSE(world->setVelocity(*first, {}));
  CHECK_FALSE(world->addImpulse(*first, {}));
  CHECK_FALSE(world->setPose(*first, {}));
  CHECK_FALSE(world->moveKinematic(*first, {}, {}, 0.1));
  CHECK(world->stats()->bodies == 1);
}

TEST_CASE("physics.moves_transfer_world_and_invalidate_source") {
  auto source = World::create();
  REQUIRE(source);
  auto body = source->createBody();
  REQUIRE(body);
  World moved = std::move(*source);
  REQUIRE(moved.body(*body));
  CHECK_FALSE(source->body(*body));
  CHECK_FALSE(source->stats());
  CHECK_FALSE(source->step(0.1));
  CHECK_FALSE(source->createBody());
  CHECK_FALSE(source->destroyBody(*body));
  CHECK_FALSE(source->setPose(*body, {}));
  CHECK_FALSE(source->setVelocity(*body, {}));
  CHECK_FALSE(source->addImpulse(*body, {}));
  CHECK_FALSE(source->moveKinematic(*body, {}, {}, 0.1));
  CHECK_FALSE(source->rayCast({}, {1, 0, 0}));
  auto target = World::create();
  REQUIRE(target);
  auto previous = target->createBody();
  REQUIRE(previous);
  *target = std::move(moved);
  REQUIRE(target->body(*body));
  CHECK_FALSE(target->body(*previous));
  CHECK_FALSE(moved.stats());
}

TEST_CASE("physics.owner_thread_rejects_operations") {
  auto world = World::create();
  REQUIRE(world);
  auto body = world->createBody();
  REQUIRE(body);
  std::atomic<int> rejected{};
  std::thread worker([&] {
    rejected += !world->body(*body);
    rejected += !world->stats();
    rejected += !world->createBody();
    rejected += !world->destroyBody(*body);
    rejected += !world->step(0.1);
    rejected += !world->setPose(*body, {});
    rejected += !world->setVelocity(*body, {});
    rejected += !world->addImpulse(*body, {});
    rejected += !world->moveKinematic(*body, {}, {}, 0.1);
    rejected += !world->rayCast({}, {1, 0, 0});
  });
  worker.join();
  CHECK(rejected == 10);
  CHECK(world->stats()->bodies == 1);
}

TEST_CASE("physics.repeated_lifetime_and_native_id_wrap") {
  for (int cycle = 0; cycle < 12; ++cycle) {
    auto world = World::create({1, {}, 0});
    REQUIRE(world);
    BodyId first{};

    for (int i = 0; i < 300; ++i) {
      auto body = world->createBody();
      REQUIRE(body);

      if (i == 0) {
        first = *body;
      } else {
        CHECK_FALSE(world->body(first));
      }

      REQUIRE(world->destroyBody(*body));
    }

    CHECK(world->stats()->bodies == 0);
  }
}

TEST_CASE("physics.concurrent_independent_worlds_and_job_workers") {
  std::atomic<int> passed{};
  std::vector<std::thread> owners;

  for (int i = 0; i < 3; ++i) {
    owners.emplace_back([&] {
      auto world = World::create({64, {}, 2});

      if (!world) {
        return;
      }

      for (int n = 0; n < 40; ++n) {
        BodyConfig config;
        config.position = {static_cast<float>(n) * 2, 0, 0};
        config.velocity = {0, 1, 0};

        if (!world->createBody(config)) {
          return;
        }
      }

      for (int n = 0; n < 20; ++n) {
        if (!world->step(1.0 / 60)) {
          return;
        }
      }

      if (world->stats()->bodies == 40 && world->stats()->active == 40) {
        ++passed;
      }
    });
  }

  for (auto& owner : owners) {
    owner.join();
  }

  CHECK(passed == 3);
}

TEST_CASE("physics.backend_capacity_failure_faults_mutations") {
  auto world = World::create({256, {}, 0});
  REQUIRE(world);
  BodyId first{};

  for (int n = 0; n < 256; ++n) {
    BodyConfig config;
    config.position = {static_cast<float>(n % 16) * 0.005F, static_cast<float>(n / 16) * 0.005F, 0};
    auto body = world->createBody(config);
    REQUIRE(body);

    if (n == 0) {
      first = *body;
    }
  }

  auto result = world->step(1.0 / 60);
  REQUIRE_FALSE(result);
  CHECK(result.error().code == Dot::Core::ErrorCode::External);
  CHECK(world->stats()->bodies == 256);
  CHECK(world->stats()->faulted);
  REQUIRE(world->body(first));
  CHECK_FALSE(world->step(1.0 / 60));
  CHECK_FALSE(world->createBody());
  CHECK_FALSE(world->setVelocity(first, {}));
  CHECK_FALSE(world->setPose(first, {}));
  CHECK_FALSE(world->addImpulse(first, {}));
  CHECK_FALSE(world->setAngularVelocity(first, {}));
  CHECK_FALSE(world->applyChanges({}));
  REQUIRE(world->destroyBody(first));
  CHECK(world->stats()->bodies == 255);
}

TEST_CASE("physics.continuous_shapes_hit_thin_walls_where_discrete_motion_tunnels") {
  for (const auto shape : {Shape::Box, Shape::Sphere, Shape::Capsule}) {
    for (const auto mode : {CollisionDetection::Discrete, CollisionDetection::Continuous}) {
      auto world = World::create({.gravity = {}, .maxCharacters = 0}).value();
      auto wall = ground();
      wall.position = {};
      wall.halfExtents = {.01F, 2, 2};
      const auto obstacle = world.createBody(wall).value();
      BodyConfig config;
      config.shape = shape;
      config.radius = .1F;
      config.halfHeight = .15F;
      config.halfExtents = {.1F, .1F, .1F};
      config.position = {-2, 0, 0};
      config.velocity = {240, 0, 0};
      config.collisionDetection = mode;
      const auto moving = world.createBody(config).value();
      REQUIRE(world.step(1.0 / 60));
      CHECK(world.body(moving)->collisionDetection == mode);

      if (mode == CollisionDetection::Discrete) {
        CHECK(world.body(moving)->position.x > 1.9F);
      } else {
        CHECK(world.body(moving)->position.x < -.09F);
        CHECK(world.body(moving)->position.x > -.13F);
        const auto events = world.drainContactEvents().value();
        REQUIRE_FALSE(events.empty());
        CHECK(events.front().phase == ContactPhase::Begin);
        CHECK(events.front().first.collider == ColliderId{obstacle});
        CHECK(events.front().second.collider == ColliderId{moving});
        REQUIRE(events.front().geometry);
        CHECK(std::get<Dot::Physics::ContactManifold>(*events.front().geometry).normal.x < -.99F);
      }

      CHECK_FALSE(world.stats()->faulted);
    }
  }
}

TEST_CASE("physics.continuous_incoming_body_hits_the_native_character_proxy") {
  for (const auto workers : {0U, 2U}) {
    auto world = World::create({.gravity = {}, .workerThreads = workers}).value();
    const auto character = world.createCharacter().value();
    BodyConfig config;
    config.position = {-2, 1, 0};
    config.velocity = {240, 0, 0};
    config.radius = .1F;
    config.collisionDetection = CollisionDetection::Continuous;
    const auto incoming = world.createBody(config).value();
    REQUIRE(world.step(1.0 / 60));
    CHECK(world.body(incoming)->position.x < -.4F);
    CHECK(world.body(incoming)->position.x > -.5F);
    CHECK(world.character(character)->position.x == doctest::Approx(0));
    const auto events = world.drainContactEvents().value();
    REQUIRE_FALSE(events.empty());
    CHECK(events.front().phase == ContactPhase::Begin);
    CHECK(events.front().first.collider == ColliderId{incoming});
    CHECK(events.front().second.collider == ColliderId{character});
    CHECK_FALSE(events.front().sensor);
    REQUIRE(events.front().geometry);
    CHECK(std::get<Dot::Physics::ContactManifold>(*events.front().geometry).normal.x > .99F);
    CHECK_FALSE(world.stats()->faulted);
  }
}

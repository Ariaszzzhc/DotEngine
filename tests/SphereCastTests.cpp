#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <optional>
#include <thread>
#include <utility>

import Dot.Physics.World;
import Dot.Physics.SceneSimulation;
import Dot.World.Scene;
import Dot.Math;

namespace {
  using namespace Dot::Physics;
  using Dot::Math::Vec3;

  auto box(Vec3 position = {}, bool sensor = false) -> BodyConfig {
    BodyConfig config;
    config.motion = Motion::Static;
    config.shape = Shape::Box;
    config.position = position;
    config.sensor = sensor;
    return config;
  }
}  // namespace

TEST_CASE("sphere_cast.closest_surface_reports_center_and_outward_contact_normal") {
  auto world = World::create().value();
  const auto body = world.createBody(box()).value();
  auto result = world.sphereCast({0, 0, 3}, .25F, {0, 0, -4});
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  REQUIRE(result->has_value());
  const auto hit = **result;
  CHECK(hit.body == body);
  CHECK(hit.fraction == doctest::Approx(.5625).epsilon(.001));
  CHECK(hit.center.z == doctest::Approx(.75).epsilon(.001));
  CHECK(hit.point.z == doctest::Approx(.5).epsilon(.001));
  REQUIRE(hit.normal);
  CHECK(hit.normal->z > .999F);
  CHECK(hit.penetrationDepth == 0);
  CHECK_FALSE(world.sphereCast({3, 3, 3}, .25F, {0, 0, -4})->has_value());
}

TEST_CASE("sphere_cast.radius_catches_edges_that_a_center_ray_misses") {
  auto world = World::create().value();
  const auto body = world.createBody(box()).value();
  CHECK_FALSE(world.rayCast({.7F, 0, 3}, {0, 0, -6})->has_value());
  const auto result = world.sphereCast({.7F, 0, 3}, .25F, {0, 0, -6}).value();
  REQUIRE(result);
  CHECK(result->body == body);
  CHECK(result->center.z == doctest::Approx(.65).epsilon(.01));
  REQUIRE(result->normal);
  CHECK(result->normal->x > .7F);
  CHECK(result->normal->z > .5F);
}

TEST_CASE("sphere_cast_initial_overlap_is_observable_and_never_moves_the_body") {
  auto world = World::create().value();
  auto config = box();
  config.motion = Motion::Dynamic;
  config.startActive = false;
  const auto body = world.createBody(config).value();
  const auto before = world.body(body).value();
  const auto result = world.sphereCast({0, .6F, 0}, .25F, {0, 1, 0}).value();
  REQUIRE(result);
  CHECK(result->fraction == 0);
  CHECK(result->center == Vec3{0, .6F, 0});
  CHECK(result->penetrationDepth == doctest::Approx(.15).epsilon(.01));
  REQUIRE(result->normal);
  CHECK(result->normal->y > .99F);
  const auto after = world.body(body).value();
  CHECK(after.position == before.position);
  CHECK(after.velocity == before.velocity);
  CHECK_FALSE(after.active);
  CHECK(world.drainContactEvents()->empty());
}

TEST_CASE("sphere_cast_filters_sensors_and_explicit_body_without_truncating_hits") {
  auto world = World::create().value();
  const auto solid = world.createBody(box()).value();
  const auto sensor = world.createBody(box({0, 0, 2}, true)).value();
  const auto ordinary = world.sphereCast({0, 0, 4}, .2F, {0, 0, -8}).value();
  REQUIRE(ordinary);
  CHECK(ordinary->body == solid);
  const auto sensed = world.sphereCast({0, 0, 4}, .2F, {0, 0, -8}, {true, {}}).value();
  REQUIRE(sensed);
  CHECK(sensed->body == sensor);
  CHECK(sensed->fraction < ordinary->fraction);
  CHECK_FALSE(world.sphereCast({0, 0, 4}, .2F, {0, 0, -8}, {false, solid})->has_value());
  const auto ignored = world.sphereCast({0, 0, 4}, .2F, {0, 0, -8}, {true, sensor}).value();
  REQUIRE(ignored);
  CHECK(ignored->body == solid);
}

TEST_CASE("sphere_cast_handles_curved_colliders_and_translated_worlds") {
  for (const auto shape : {Shape::Box, Shape::Sphere, Shape::Capsule}) {
    for (const auto offset : {0.F, 500000.F}) {
      auto world = World::create().value();
      auto config = box({offset, 0, offset});
      config.shape = shape;
      config.rotation = Dot::Math::rotationEulerDegrees({0, 0, 90}).value();
      const auto body = world.createBody(config).value();
      const auto hit = world.sphereCast({offset, 0, offset + 3}, .25F, {0, 0, -4}).value();
      REQUIRE(hit);
      CHECK(hit->body == body);
      CHECK(std::abs(hit->point.z - (offset + .5F)) < .01F);
      CHECK(std::abs(hit->center.z - (offset + .75F)) < .01F);
    }
  }
}

TEST_CASE("sphere_cast_rejects_invalid_ranges_handles_threads_and_moved_owners") {
  auto world = World::create().value();
  const auto body = world.createBody(box()).value();
  auto foreign = World::create().value();
  const auto other = foreign.createBody(box()).value();

  for (const auto radius : {0.F, -.1F, 10001.F, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()}) {
    CHECK_FALSE(world.sphereCast({0, 0, 3}, radius, {0, 0, -4}));
  }

  CHECK_FALSE(world.sphereCast({}, .2F, {}));
  CHECK_FALSE(world.sphereCast({}, .2F, {0, .0001F, 0}));
  CHECK_FALSE(world.sphereCast({std::numeric_limits<float>::quiet_NaN(), 0, 0}, .2F, {0, 0, -4}));
  CHECK_FALSE(world.sphereCast({}, .2F, {0, std::numeric_limits<float>::infinity(), 0}));
  CHECK_FALSE(world.sphereCast({1'000'000, 0, 0}, .2F, {1, 0, 0}));
  CHECK_FALSE(world.sphereCast({0, 0, 3}, .2F, {0, 0, -4}, {false, other}));
  CHECK_FALSE(world.sphereCast({0, 0, 3}, .2F, {0, 0, -4}, {false, BodyId{}}));
  bool rejected{};
  std::thread worker([&] { rejected = !world.sphereCast({0, 0, 3}, .2F, {0, 0, -4}); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(world);
  CHECK_FALSE(world.sphereCast({0, 0, 3}, .2F, {0, 0, -4}));
  REQUIRE(moved.sphereCast({0, 0, 3}, .2F, {0, 0, -4})->has_value());
  REQUIRE(moved.destroyBody(body));
  CHECK_FALSE(moved.sphereCast({0, 0, 3}, .2F, {0, 0, -4}, {false, body}));
  CHECK(moved.stats()->bodies == 0);
  CHECK_FALSE(moved.stats()->faulted);
}

TEST_CASE("sphere_cast_scene_maps_owned_contacts_and_checks_filter_scope") {
  auto scene = Dot::World::Scene::create().value();
  const auto entity = scene.createEntity().value();
  Dot::World::RigidBody body;
  body.motion = Motion::Static;
  body.shape = Shape::Box;
  REQUIRE(scene.setRigidBody(entity, body));
  auto simulation = SceneSimulation::create(scene).value();
  auto result = simulation.sphereCast({0, 0, 3}, .25F, {0, 0, -4}).value();
  REQUIRE(result);
  CHECK(result->entity == entity);
  CHECK(result->center.z == doctest::Approx(.75).epsilon(.001));
  CHECK_FALSE(simulation.sphereCast({0, 0, 3}, .25F, {0, 0, -4}, {false, entity})->has_value());
  auto foreign = Dot::World::Scene::create().value();
  CHECK_FALSE(
      simulation.sphereCast({0, 0, 3}, .25F, {0, 0, -4}, {false, foreign.createEntity().value()}));
  REQUIRE(scene.destroyEntity(entity));
  REQUIRE(simulation.synchronize(scene));
  CHECK_FALSE(simulation.sphereCast({0, 0, 3}, .25F, {0, 0, -4})->has_value());
  CHECK(result->entity == entity);
}

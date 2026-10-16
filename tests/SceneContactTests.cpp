#include <doctest/doctest.h>

#include <optional>
#include <thread>
#include <utility>
#include <variant>

import Dot.Physics.SceneSimulation;
import Dot.Physics.World;
import Dot.World.Scene;
import Dot.Math;

using namespace Dot::World;
using Dot::Physics::ContactEndReason;
using Dot::Physics::ContactPhase;
using Dot::Physics::SceneSimulation;

namespace {
  struct Pair {
    Scene scene;
    SceneSimulation simulation;
    EntityId floor;
    EntityId body;
  };

  auto pair(std::uint32_t capacity = 65536) -> Pair {
    auto scene = Scene::create();
    REQUIRE(scene);
    auto floor = scene->createEntity();
    auto body = scene->createEntity();
    REQUIRE(floor);
    REQUIRE(body);
    RigidBody fixed;
    fixed.motion = Dot::Physics::Motion::Static;
    fixed.shape = Dot::Physics::Shape::Box;
    REQUIRE(scene->setRigidBody(*floor, fixed));
    REQUIRE(scene->setTransform(*floor, {{0, -0.5F, 0}, {}, {10, 1, 10}}));
    REQUIRE(scene->setRigidBody(*body, RigidBody{}));
    REQUIRE(scene->setTransform(*body, {{0, 0.48F, 0}}));
    auto simulation = SceneSimulation::create(*scene, {2, {0, -9.81F, 0}, 0, capacity});
    REQUIRE(simulation);
    return {std::move(*scene), std::move(*simulation), *floor, *body};
  }
}  // namespace

TEST_CASE("scene_contacts.entities_remain_identifiable_after_removal") {
  auto fixture = pair();
  REQUIRE(fixture.simulation.step(fixture.scene, 1.0 / 60));
  auto began = fixture.simulation.drainContactEvents();
  REQUIRE(began);
  REQUIRE(began->size() == 1);
  CHECK(began->front().phase == ContactPhase::Begin);
  CHECK(began->front().first == fixture.floor);
  CHECK(began->front().second == fixture.body);
  REQUIRE(began->front().geometry);
  REQUIRE(fixture.scene.destroyEntity(fixture.body));
  REQUIRE(fixture.simulation.synchronize(fixture.scene));
  auto ended = fixture.simulation.drainContactEvents();
  REQUIRE(ended);
  REQUIRE(ended->size() == 1);
  CHECK(ended->front().phase == ContactPhase::End);
  CHECK(ended->front().endReason == ContactEndReason::Destroyed);
  CHECK(ended->front().second == fixture.body);
  CHECK_FALSE(fixture.scene.entity(ended->front().second));
  CHECK(std::get<Dot::Physics::ContactManifold>(*began->front().geometry).points.size() > 0);
  CHECK(fixture.simulation.stats()->contactPairs == 0);
}

TEST_CASE("scene_contacts.component_removal_keeps_scoped_event_association") {
  auto fixture = pair();
  REQUIRE(fixture.simulation.step(fixture.scene, 1.0 / 60));
  REQUIRE(fixture.simulation.drainContactEvents());
  REQUIRE(fixture.scene.setRigidBody(fixture.body, std::nullopt));
  REQUIRE(fixture.simulation.synchronize(fixture.scene));
  auto events = fixture.simulation.drainContactEvents();
  REQUIRE(events);
  REQUIRE(events->size() == 1);
  CHECK(events->front().second == fixture.body);
  CHECK(events->front().endReason == ContactEndReason::Destroyed);
  REQUIRE(fixture.scene.entity(fixture.body));
  CHECK_FALSE(fixture.simulation.body(fixture.body));
}

TEST_CASE("scene_contacts.reconfiguration_and_reset_have_explicit_lifetime") {
  auto fixture = pair();
  REQUIRE(fixture.simulation.step(fixture.scene, 1.0 / 60));
  REQUIRE(fixture.simulation.drainContactEvents());
  auto description = fixture.scene.entity(fixture.body)->rigidBody;
  description->mass = 2;
  REQUIRE(fixture.scene.setRigidBody(fixture.body, description));
  REQUIRE(fixture.simulation.synchronize(fixture.scene));
  auto events = fixture.simulation.drainContactEvents();
  REQUIRE(events);
  REQUIRE(events->size() == 1);
  CHECK(events->front().phase == ContactPhase::End);
  CHECK(events->front().endReason == ContactEndReason::Reconfigured);
  CHECK(events->front().second == fixture.body);
  REQUIRE(fixture.simulation.step(fixture.scene, 1.0 / 60));
  REQUIRE(fixture.simulation.reset(fixture.scene));
  CHECK(fixture.simulation.drainContactEvents()->empty());
  CHECK(fixture.simulation.stats()->contactPairs == 0);
}

TEST_CASE("scene_contacts.queue_preflight_failure_preserves_binding_and_can_retry") {
  auto fixture = pair(1);
  REQUIRE(fixture.simulation.step(fixture.scene, 1.0 / 60));
  REQUIRE(fixture.scene.destroyEntity(fixture.body));
  CHECK_FALSE(fixture.simulation.synchronize(fixture.scene));
  CHECK_FALSE(fixture.simulation.stats()->faulted);
  REQUIRE(fixture.simulation.body(fixture.body));
  auto events = fixture.simulation.drainContactEvents();
  REQUIRE(events);
  REQUIRE(events->size() == 1);
  REQUIRE(fixture.simulation.synchronize(fixture.scene));
  events = fixture.simulation.drainContactEvents();
  REQUIRE(events);
  REQUIRE(events->size() == 1);
  CHECK(events->front().phase == ContactPhase::End);
  CHECK_FALSE(fixture.simulation.body(fixture.body));
}

TEST_CASE("scene_contacts.failed_step_rejects_delivery_until_reset") {
  auto fixture = pair(1);
  REQUIRE(fixture.simulation.step(fixture.scene, 1.0 / 60));
  CHECK_FALSE(fixture.simulation.step(fixture.scene, 1.0 / 60));
  CHECK(fixture.simulation.stats()->faulted);
  CHECK_FALSE(fixture.simulation.drainContactEvents());
  REQUIRE(fixture.simulation.reset(fixture.scene));
  CHECK(fixture.simulation.drainContactEvents()->empty());
  CHECK_FALSE(fixture.simulation.stats()->faulted);
}

TEST_CASE("scene_contacts.worker_rejection_and_move_preserve_owned_queue") {
  auto fixture = pair();
  REQUIRE(fixture.simulation.step(fixture.scene, 1.0 / 60));
  bool rejected = false;
  std::thread worker([&] { rejected = !fixture.simulation.drainContactEvents(); });
  worker.join();
  CHECK(rejected);
  SceneSimulation moved = std::move(fixture.simulation);
  CHECK_FALSE(fixture.simulation.drainContactEvents());
  auto events = moved.drainContactEvents();
  REQUIRE(events);
  REQUIRE(events->size() == 1);
  CHECK(events->front().second == fixture.body);
}

TEST_CASE("scene_contacts.virtual_character_geometry_and_removed_entities_remain_owned") {
  auto scene = Scene::create().value();
  const auto floor = scene.createEntity("Floor").value();
  const auto actor = scene.createEntity("Character").value();
  RigidBody ground;
  ground.motion = Dot::Physics::Motion::Static;
  ground.shape = Dot::Physics::Shape::Box;
  REQUIRE(scene.setRigidBody(floor, ground));
  REQUIRE(scene.setTransform(floor, {{0, -.5F, 0}, {}, {10, 1, 10}}));
  REQUIRE(scene.setCharacterController(actor, CharacterController{}));
  auto prepared = SceneSimulation::create(scene);
  REQUIRE_MESSAGE(prepared, (prepared ? "" : prepared.error().message));
  auto simulation = std::move(*prepared);
  REQUIRE(simulation.step(scene, 1.0 / 60));
  const auto events = simulation.drainContactEvents().value();
  REQUIRE(events.size() == 1);
  CHECK(events.front().first == floor);
  CHECK(events.front().second == actor);
  REQUIRE(events.front().geometry);
  const auto* geometry
      = std::get_if<Dot::Physics::CharacterContactGeometry>(&*events.front().geometry);
  REQUIRE(geometry);
  CHECK(geometry->normal.y > .99F);
  REQUIRE(scene.destroyEntity(actor));
  REQUIRE(simulation.synchronize(scene));
  const auto ended = simulation.drainContactEvents().value();
  REQUIRE(ended.size() == 1);
  CHECK(ended.front().phase == ContactPhase::End);
  CHECK(ended.front().endReason == ContactEndReason::Destroyed);
  CHECK(ended.front().first == floor);
  CHECK(ended.front().second == actor);
  CHECK(geometry->point.y == doctest::Approx(0).epsilon(.001));
  CHECK_FALSE(scene.entity(actor));
}

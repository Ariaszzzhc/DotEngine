#include <doctest/doctest.h>

#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <thread>
#include <utility>
#include <variant>

import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Physics.SceneSimulation;
import Dot.Physics.World;
import Dot.Math;

namespace {
  using namespace Dot;
  using Json = nlohmann::json;
  constexpr double tick = 1.0 / 60;

  struct Fixture {
    World::Scene scene = World::Scene::create().value();
    World::EntityId floor = scene.createEntity("Floor").value();
    World::EntityId root = scene.createEntity("Parent").value();
    World::EntityId actor = scene.createEntity("Character").value();

    Fixture() {
      World::RigidBody body;
      body.motion = Physics::Motion::Static;
      body.shape = Physics::Shape::Box;
      body.halfExtents = {20, .5F, 20};
      REQUIRE(scene.setTransform(floor, {{0, -.5F, 0}}));
      REQUIRE(scene.setRigidBody(floor, body));
      REQUIRE(scene.setParent(actor, root));
      REQUIRE(scene.setTransform(actor, {{0, 2, 0}}));
      REQUIRE(scene.setCharacterController(actor, World::CharacterController{}));
    }

    auto simulation() -> Physics::SceneSimulation {
      return Physics::SceneSimulation::create(scene, {.maxBodies = 4, .maxCharacters = 1}).value();
    }

    auto step(Physics::SceneSimulation& simulation, int count = 1) -> void {
      for (int frame = 0; frame < count; ++frame) {
        auto result = simulation.step(scene, tick);
        REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      }
    }
  };
}  // namespace

TEST_CASE("scene_character.strict_roundtrip_keeps_properties_and_independent_scope") {
  Fixture fixture;
  auto properties = World::CharacterController{};
  properties.mass = 100;
  properties.maxContacts = 17;
  REQUIRE(fixture.scene.setCharacterController(fixture.actor, properties));
  const auto original = World::encodeScene(fixture.scene).value();
  auto decoded = World::decodeScene(original).value();
  const auto actor = decoded.findEntity(fixture.actor.value).value();
  CHECK(decoded.id() != fixture.scene.id());
  CHECK(decoded.entity(actor)->character == properties);
  CHECK(decoded.entity(actor)->parent->value == fixture.root.value);
  CHECK(World::encodeScene(decoded).value() == original);

  for (const auto& invalid :
       {Json(-1), Json(0), Json(4097), Json(4294967297ULL), Json(1.5), Json(true), Json("1")}) {
    auto text = Json::parse(original);
    text["entities"][2]["character"]["maxContacts"] = invalid;
    CHECK_FALSE(World::decodeScene(text.dump()));
  }

  auto text = Json::parse(original);
  text["entities"][2]["character"]["other"] = 1;
  CHECK_FALSE(World::decodeScene(text.dump()));
  text = Json::parse(original);
  text["entities"][2]["rigidBody"] = text["entities"][0]["rigidBody"];
  CHECK_FALSE(World::decodeScene(text.dump()));
  CHECK(World::encodeScene(fixture.scene).value() == original);
}

TEST_CASE("scene_character_real_landing_movement_jump_yaw_and_parent_writeback") {
  Fixture fixture;
  REQUIRE(fixture.scene.setTransform(
      fixture.root, {{3, 0, 0}, Math::rotationEulerDegrees({0, 90, 0}).value(), {2, 2, 2}}));
  auto simulation = fixture.simulation();
  fixture.step(simulation, 120);
  CHECK(simulation.character(fixture.actor)->ground == Physics::GroundState::Grounded);
  CHECK(simulation.character(fixture.actor)->groundEntity == fixture.floor);
  REQUIRE(simulation.setCharacterInput(fixture.actor, {{2, 0, 0}, true}));
  REQUIRE(simulation.setCharacterYaw(fixture.actor, 45));
  auto properties = fixture.scene.entity(fixture.actor)->character.value();
  properties.mass = 80;
  REQUIRE(fixture.scene.setCharacterController(fixture.actor, properties));
  fixture.step(simulation);
  CHECK(simulation.character(fixture.actor)->velocity.y > 5);
  CHECK(simulation.character(fixture.actor)->rotation
        == Math::rotationEulerDegrees({0, 45, 0}).value());
  fixture.step(simulation, 59);
  auto world = fixture.scene.worldTransform(fixture.actor).value();
  CHECK(world.data[12] == doctest::Approx(5).epsilon(.02));
  CHECK(fixture.scene.transform(fixture.actor)->scale == Math::Vec3{1, 1, 1});
  CHECK(simulation.stats()->characters == 1);
  CHECK(simulation.stats()->bodies == 1);
  auto pose = fixture.scene.transform(fixture.actor).value();
  pose.position = {0, 3, 0};
  REQUIRE(fixture.scene.setTransform(fixture.actor, pose));
  REQUIRE(simulation.synchronize(fixture.scene));
  CHECK(simulation.character(fixture.actor)->velocity == Math::Vec3{});
  fixture.step(simulation);
  CHECK(simulation.character(fixture.actor)->position.x == doctest::Approx(3));
}

TEST_CASE("scene_character_teleport_writes_parent_local_pose_and_clears_same_pose_intent") {
  Fixture f;
  REQUIRE(f.scene.setTransform(
      f.root, {{3, 0, 0}, Math::rotationEulerDegrees({0, 90, 0}).value(), {2, 2, 2}}));
  auto simulation = f.simulation();
  f.step(simulation, 120);
  REQUIRE(simulation.setCharacterInput(f.actor, {{2, 0, 0}, true}));
  REQUIRE(simulation.teleportCharacter(f.scene, f.actor, {5, 0, 2}, 450));
  auto state = simulation.character(f.actor).value();
  CHECK(state.position == Math::Vec3{5, 0, 2});
  CHECK(state.velocity == Math::Vec3{});
  CHECK(state.rotation.y == doctest::Approx(.70710678));
  auto world = f.scene.worldTransform(f.actor).value();
  CHECK(world.data[12] == doctest::Approx(5));
  CHECK(world.data[14] == doctest::Approx(2));
  REQUIRE(simulation.synchronize(f.scene));
  f.step(simulation);
  CHECK(simulation.character(f.actor)->position == Math::Vec3{5, 0, 2});
  REQUIRE(simulation.setCharacterInput(f.actor, {{3, 0, 0}, true}));
  REQUIRE(simulation.teleportCharacter(f.scene, f.actor, {5, 0, 2}, 90));
  f.step(simulation);
  CHECK(simulation.character(f.actor)->velocity.y <= 0);
  CHECK(simulation.character(f.actor)->position.x == doctest::Approx(5));
  REQUIRE(simulation.setCharacterInput(f.actor, {{2, 0, 0}, true}));
  CHECK_FALSE(simulation.teleportCharacter(f.scene, f.actor, {1000001, 0, 0}, 0));
  CHECK_FALSE(
      simulation.teleportCharacter(f.scene, f.actor, {}, std::numeric_limits<float>::quiet_NaN()));
  CHECK(simulation.character(f.actor)->position == Math::Vec3{5, 0, 2});
  f.step(simulation);
  CHECK(simulation.character(f.actor)->velocity.y > 5);
  CHECK(simulation.character(f.actor)->position.x > 5);
  const auto before = simulation.character(f.actor)->position;
  REQUIRE(f.scene.setTransform(f.root, {{7, 0, 0}}));
  CHECK_FALSE(simulation.teleportCharacter(f.scene, f.actor, {}, 0));
  CHECK(simulation.character(f.actor)->position == before);
  CHECK_FALSE(simulation.teleportCharacter(f.scene, f.floor, {}, 0));
  auto foreign = World::Scene::create().value();
  CHECK_FALSE(simulation.teleportCharacter(foreign, f.actor, {}, 0));
  bool denied{};
  std::thread worker([&] { denied = !simulation.teleportCharacter(f.scene, f.actor, {}, 0); });
  worker.join();
  CHECK(denied);
  auto moved = std::move(simulation);
  CHECK_FALSE(simulation.teleportCharacter(f.scene, f.actor, {}, 0));
  REQUIRE(moved.synchronize(f.scene));
  REQUIRE(moved.teleportCharacter(f.scene, f.actor, {0, 2, 0}, 0));
}

TEST_CASE("scene_character_invalid_pose_and_capacity_retain_entire_simulation") {
  Fixture fixture;
  auto simulation = fixture.simulation();
  fixture.step(simulation, 120);
  REQUIRE(simulation.setCharacterInput(fixture.actor, {{2, 0, 0}, true}));
  const auto before = simulation.character(fixture.actor).value();
  REQUIRE(fixture.scene.setTransform(fixture.floor, {{5, -.5F, 0}}));

  SUBCASE("nonuniform") {
    REQUIRE(fixture.scene.setTransform(fixture.root, {{}, {}, {1, 2, 1}}));
  }

  SUBCASE("tilted") {
    REQUIRE(fixture.scene.setTransform(fixture.root,
                                       {{}, Math::rotationEulerDegrees({10, 0, 0}).value()}));
  }

  SUBCASE("reflected") {
    REQUIRE(fixture.scene.setTransform(fixture.root, {{}, {}, {-1, 1, 1}}));
  }

  SUBCASE("too many characters") {
    const auto other = fixture.scene.createEntity().value();
    REQUIRE(fixture.scene.setCharacterController(other, World::CharacterController{}));
  }

  CHECK_FALSE(simulation.synchronize(fixture.scene));
  CHECK(simulation.body(fixture.floor)->position == Math::Vec3{0, -.5F, 0});
  CHECK(simulation.character(fixture.actor)->position == before.position);
  CHECK_FALSE(simulation.stats()->faulted);
  REQUIRE(fixture.scene.setTransform(fixture.root, {}));

  for (const auto entity : fixture.scene.characterControllers().value()) {
    if (entity != fixture.actor) {
      REQUIRE(fixture.scene.destroyEntity(entity));
    }
  }

  fixture.step(simulation);
  CHECK(simulation.character(fixture.actor)->velocity.y > 5);
}

TEST_CASE("scene_character_removed_components_moves_foreign_handles_and_reset") {
  Fixture fixture;
  auto simulation = fixture.simulation();
  auto cloned = World::decodeScene(World::encodeScene(fixture.scene).value()).value();
  CHECK_FALSE(simulation.character(cloned.findEntity(fixture.actor.value).value()));
  CHECK_FALSE(simulation.synchronize(cloned));
  bool rejected{};
  std::thread worker([&] {
    rejected = !simulation.character(fixture.actor)
               && !simulation.setCharacterInput(fixture.actor, {})
               && !simulation.setCharacterYaw(fixture.actor, 0);
  });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(simulation);
  CHECK_FALSE(simulation.character(fixture.actor));
  fixture.step(moved);
  REQUIRE(fixture.scene.setCharacterController(fixture.actor, {}));
  REQUIRE(moved.synchronize(fixture.scene));
  CHECK_FALSE(moved.character(fixture.actor));
  CHECK_FALSE(moved.setCharacterInput(fixture.actor, {}));
  CHECK(moved.stats()->characters == 0);
  REQUIRE(moved.reset(cloned));
  CHECK(moved.stats()->characters == 1);
  CHECK_FALSE(moved.character(fixture.actor));
}

TEST_CASE("scene_character_contact_failure_faults_and_reset_recovers_from_authoring") {
  Fixture fixture;
  REQUIRE(fixture.scene.setTransform(fixture.actor, {}));
  auto properties = World::CharacterController{};
  properties.maxContacts = 1;
  REQUIRE(fixture.scene.setCharacterController(fixture.actor, properties));
  auto simulation = fixture.simulation();
  const auto wall = fixture.scene.createEntity("Wall").value();
  auto body = fixture.scene.entity(fixture.floor)->rigidBody.value();
  body.halfExtents = {.2F, 1, 2};
  REQUIRE(fixture.scene.setTransform(wall, {{.5F, 1, 0}}));
  REQUIRE(fixture.scene.setRigidBody(wall, body));
  CHECK_FALSE(simulation.synchronize(fixture.scene));
  CHECK(simulation.stats()->faulted);
  CHECK(simulation.stats()->bodies == 2);
  REQUIRE(simulation.character(fixture.actor));
  CHECK_FALSE(simulation.setCharacterInput(fixture.actor, {}));
  properties.maxContacts = 256;
  REQUIRE(fixture.scene.setCharacterController(fixture.actor, properties));
  REQUIRE(simulation.reset(fixture.scene));
  CHECK_FALSE(simulation.stats()->faulted);
  fixture.step(simulation);
}

TEST_CASE("scene_character_query_filter_and_sensor_events_use_character_entity_identity") {
  Fixture fixture;
  REQUIRE(fixture.scene.setTransform(fixture.actor, {}));
  const auto sensor = fixture.scene.createEntity("Trigger").value();
  World::RigidBody volume;
  volume.motion = Physics::Motion::Static;
  volume.shape = Physics::Shape::Box;
  volume.halfExtents = {1, 1, 1};
  volume.sensor = true;
  REQUIRE(fixture.scene.setTransform(sensor, {{0, 1, 0}}));
  REQUIRE(fixture.scene.setRigidBody(sensor, volume));
  auto simulation = fixture.simulation();
  const auto ray = simulation.rayCast({0, 1, 3}, {0, 0, -6}).value();
  REQUIRE(ray);
  CHECK(ray->entity == sensor);
  const auto swept = simulation.sphereCast({0, 1, 3}, .2F, {0, 0, -6}).value();
  REQUIRE(swept);
  CHECK(swept->entity == fixture.actor);
  CHECK_FALSE(
      simulation.sphereCast({0, 1, 3}, .2F, {0, 0, -6}, {false, fixture.actor})->has_value());
  CHECK_FALSE(simulation.sphereCast({0, 1, 3}, .2F, {0, 0, -6}, {false, fixture.root}));
  CHECK_FALSE(
      simulation.sphereCast({0, 1, 3}, .2F, {0, 0, -6},
                            {false, World::EntityId{fixture.scene.id() + 1, fixture.actor.value}}));
  fixture.step(simulation);
  auto events = simulation.drainContactEvents().value();
  REQUIRE(events.size() == 2);
  const auto trigger = std::ranges::find(events, true, &Physics::SceneContactEvent::sensor);
  REQUIRE(trigger != events.end());
  CHECK(trigger->first == sensor);
  CHECK(trigger->second == fixture.actor);
  REQUIRE(trigger->geometry);
  CHECK(std::holds_alternative<Physics::ContactManifold>(*trigger->geometry));
  const auto ground = std::ranges::find(events, false, &Physics::SceneContactEvent::sensor);
  REQUIRE(ground != events.end());
  CHECK(ground->second == fixture.actor);
  REQUIRE(ground->geometry);
  CHECK(std::holds_alternative<Physics::CharacterContactGeometry>(*ground->geometry));
  REQUIRE(fixture.scene.destroyEntity(fixture.actor));
  REQUIRE(simulation.synchronize(fixture.scene));
  events = simulation.drainContactEvents().value();
  REQUIRE(events.size() == 2);

  for (const auto& event : events) {
    CHECK(event.phase == Physics::ContactPhase::End);
    CHECK(event.endReason == Physics::ContactEndReason::Destroyed);
    CHECK(event.second == fixture.actor);
    CHECK_FALSE(event.geometry);
  }
  CHECK_FALSE(simulation.sphereCast({0, 1, 3}, .2F, {0, 0, -6})->has_value());
}

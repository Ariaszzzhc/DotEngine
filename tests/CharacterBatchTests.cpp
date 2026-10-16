#include <doctest/doctest.h>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

import Dot.Physics.World;
import Dot.Math;

namespace {
  using namespace Dot::Physics;
  using Dot::Math::Vec3;

  auto floorConfig() -> BodyConfig {
    BodyConfig floor;
    floor.motion = Motion::Static;
    floor.shape = Shape::Box;
    floor.position = {0, -.5F, 0};
    floor.halfExtents = {10, .5F, 10};
    return floor;
  }
}  // namespace

TEST_CASE("character_batch.full_capacity_replacements_retain_both_handle_kinds") {
  auto world = World::create({.maxBodies = 1, .maxCharacters = 1}).value();
  const auto body = world.createBody(floorConfig()).value();
  const auto character = world.createCharacter().value();
  auto floor = floorConfig();
  floor.position.x = 1;
  CharacterConfig config;
  config.position = {2, 0, 0};
  config.radius = .45F;
  const std::array bodies{BodyChange{body, floor}};
  const std::array characters{CharacterChange{character, config}};
  auto applied = world.applyChanges(bodies, characters);
  REQUIRE_MESSAGE(applied, (applied ? "" : applied.error().message));
  CHECK(applied->bodies == std::vector<std::optional<BodyId>>{body});
  CHECK(applied->characters == std::vector<std::optional<CharacterId>>{character});
  CHECK(world.stats()->bodies == 1);
  CHECK(world.stats()->characters == 1);
  CHECK(world.body(body)->position == floor.position);
  CHECK(world.character(character)->position == config.position);
  CHECK(world.character(character)->groundCollider == std::optional<ColliderId>{body});
  CHECK_FALSE(world.stats()->faulted);
}

TEST_CASE("character_batch.failed_inputs_preserve_bodies_characters_and_next_ids") {
  auto world = World::create().value();
  const auto body = world.createBody(floorConfig()).value();
  const auto character = world.createCharacter().value();
  auto floor = floorConfig();
  floor.position.x = 4;
  CharacterConfig config;
  config.position = {3, 0, 0};
  std::array bodies{BodyChange{body, floor}};
  std::array characters{CharacterChange{character, config}};

  SUBCASE("invalid later character retains pending movement and jump") {
    REQUIRE(world.setCharacterInput(character, {{2, 0, 0}, true}));
    characters[0].config->radius = 0;
  }

  SUBCASE("invalid body does not publish prepared character") {
    bodies[0].config->mass = 0;
  }

  CHECK_FALSE(world.applyChanges(bodies, characters));
  CHECK(world.body(body)->position == floorConfig().position);
  CHECK(world.character(character)->position == Vec3{});
  CHECK(world.stats()->bodies == 1);
  CHECK(world.stats()->characters == 1);
  CHECK_FALSE(world.stats()->faulted);
  REQUIRE(world.step(1.0 / 60));

  if (characters[0].config->radius == 0) {
    CHECK(world.character(character)->velocity.y > 5);
    CHECK(world.character(character)->position.x > 0);
  }

  config.position = {8, 0, 0};
  CHECK(world.createCharacter(config)->value == character.value + 1);
  floor.position.x = 8;
  CHECK(world.createBody(floor)->value == body.value + 1);
}

TEST_CASE("character_batch_create_destroy_order_retires_old_contacts_and_handles") {
  auto world = World::create({.maxBodies = 1, .maxCharacters = 1}).value();
  const auto body = world.createBody(floorConfig()).value();
  const auto character = world.createCharacter().value();
  CharacterConfig config;
  config.position = {5, 1, 0};
  const std::array bodies{BodyChange{std::nullopt, floorConfig()}, BodyChange{body, std::nullopt}};
  const std::array characters{CharacterChange{std::nullopt, config},
                              CharacterChange{character, std::nullopt}};
  auto result = world.applyChanges(bodies, characters);
  REQUIRE(result);
  REQUIRE(result->bodies[0]);
  REQUIRE(result->characters[0]);
  CHECK_FALSE(result->bodies[1]);
  CHECK_FALSE(result->characters[1]);
  CHECK(result->bodies[0]->value > body.value);
  CHECK(result->characters[0]->value > character.value);
  CHECK_FALSE(world.body(body));
  CHECK_FALSE(world.character(character));
  REQUIRE(world.step(1.0 / 60));
  CHECK(world.character(*result->characters[0])->position.y < 1);
  REQUIRE(world.destroyCharacter(*result->characters[0]));
  REQUIRE(world.destroyBody(*result->bodies[0]));
  CHECK(world.stats()->bodies == 0);
  CHECK(world.stats()->characters == 0);
}

TEST_CASE("character_batch_motion_retention_preserves_input_and_teleports_can_reset_it") {
  auto world = World::create().value();
  REQUIRE(world.createBody(floorConfig()));
  const auto character = world.createCharacter().value();
  REQUIRE(world.setCharacterInput(character, {{3, 0, 0}, true}));
  REQUIRE(world.step(1.0 / 60));
  const auto before = world.character(character).value();
  CharacterConfig config;
  config.position = before.position;
  config.mass = 90;
  std::array changes{CharacterChange{character, config, true}};
  REQUIRE(world.applyChanges({}, changes));
  CHECK(world.character(character)->velocity == before.velocity);
  REQUIRE(world.step(1.0 / 60));
  CHECK(world.character(character)->position.x > before.position.x);
  CHECK(world.character(character)->velocity.y > 4);
  changes[0].preserveMotion = false;
  changes[0].config->position = {6, 3, 0};
  REQUIRE(world.applyChanges({}, changes));
  CHECK(world.character(character)->velocity == Vec3{});
  REQUIRE(world.step(1.0 / 60));
  CHECK(world.character(character)->position.x == doctest::Approx(6));
  CHECK(world.character(character)->velocity.y < 0);
}

TEST_CASE("character_batch_validates_handles_flags_bounds_and_independent_capacity") {
  auto world = World::create({.maxBodies = 1, .maxCharacters = 1}).value();
  auto foreign = World::create().value();
  const auto body = world.createBody(floorConfig()).value();
  const auto character = world.createCharacter().value();
  const auto other = foreign.createCharacter().value();
  CharacterConfig config;
  std::vector<CharacterChange> changes;

  SUBCASE("duplicate") {
    changes = {{character, config}, {character, std::nullopt}};
  }

  SUBCASE("foreign") {
    changes = {{other, config}};
  }

  SUBCASE("empty") {
    changes = {{}};
  }

  SUBCASE("oversized") {
    changes.resize(513);
  }

  SUBCASE("capacity") {
    changes = {{std::nullopt, config}};
  }

  SUBCASE("preserving creation") {
    changes = {{std::nullopt, config, true}};
  }

  SUBCASE("preserving destruction") {
    changes = {{character, std::nullopt, true}};
  }

  SUBCASE("stale") {
    REQUIRE(world.destroyCharacter(character));
    changes = {{character, config}};
  }

  const std::array bodies{BodyChange{body, std::nullopt}};
  CHECK_FALSE(world.applyChanges(bodies, changes));
  REQUIRE(world.body(body));
  CHECK_FALSE(world.stats()->faulted);
  REQUIRE(world.applyChanges({}, {}));
}

TEST_CASE("character_batch_postpublication_contact_failure_faults_and_allows_cleanup") {
  auto world = World::create().value();
  const auto floor = world.createBody(floorConfig()).value();
  auto wall = floorConfig();
  wall.position = {.5F, 1, 0};
  wall.halfExtents = {.2F, 1, 2};
  const std::array bodies{BodyChange{std::nullopt, wall}};
  CharacterConfig config;
  config.maxContacts = 1;
  const std::array characters{CharacterChange{std::nullopt, config}};
  auto result = world.applyChanges(bodies, characters);
  REQUIRE_FALSE(result);
  CHECK(result.error().message.find("published") != std::string::npos);
  CHECK(world.stats()->faulted);
  CHECK(world.stats()->bodies == 2);
  CHECK(world.stats()->characters == 1);
  CHECK_FALSE(world.step(1.0 / 60));
  CHECK_FALSE(world.applyChanges({}, {}));
  REQUIRE(world.destroyCharacter({floor.world, 1}));
  REQUIRE(world.destroyBody({floor.world, floor.value + 1}));
  REQUIRE(world.destroyBody(floor));
  CHECK(world.stats()->characters == 0);
  CHECK(world.stats()->bodies == 0);
}

TEST_CASE("character_batch_move_and_owner_thread_preserve_world_scope") {
  auto world = World::create().value();
  const auto character = world.createCharacter().value();
  const std::array changes{CharacterChange{character, std::nullopt}};
  auto moved = std::move(world);
  CHECK_FALSE(world.applyChanges({}, changes));
  bool rejected{};
  std::thread worker([&] { rejected = !moved.applyChanges({}, changes); });
  worker.join();
  CHECK(rejected);
  REQUIRE(moved.character(character));
  REQUIRE(moved.applyChanges({}, changes));
  CHECK(moved.stats()->characters == 0);
}

TEST_CASE("character_batch.contact_preflight_is_shared_with_body_lifetimes") {
  auto world
      = World::create({.maxBodies = 2, .gravity = {}, .maxContactEvents = 3, .maxCharacters = 1})
            .value();
  auto sensor = floorConfig();
  sensor.position = {0, 1, 0};
  sensor.halfExtents = {1, 1, 1};
  sensor.sensor = true;
  const auto first = world.createBody(sensor).value();
  sensor.position.x = .5F;
  REQUIRE(world.createBody(sensor));
  const auto character = world.createCharacter().value();
  REQUIRE(world.step(1.0 / 60));
  REQUIRE(world.stats()->queuedContactEvents == 2);
  const auto before = world.character(character)->position;
  CHECK_FALSE(world.teleportCharacter(character, {5, 0, 0}));
  CHECK_FALSE(world.destroyCharacter(character));
  CharacterConfig replacement;
  replacement.position = {5, 0, 0};
  const std::array bodies{BodyChange{first, std::nullopt}};
  const std::array characters{CharacterChange{character, replacement}};
  CHECK_FALSE(world.applyChanges(bodies, characters));
  CHECK(world.character(character)->position == before);
  CHECK(world.stats()->characters == 1);
  CHECK(world.stats()->bodies == 2);
  CHECK(world.stats()->queuedContactEvents == 2);
  CHECK_FALSE(world.stats()->faulted);
  CHECK_FALSE(world.rayCast({5, 1, 3}, {0, 0, -6})->has_value());
  REQUIRE(world.drainContactEvents()->size() == 2);
  REQUIRE(world.applyChanges(bodies, characters));
  auto events = world.drainContactEvents().value();
  REQUIRE(events.size() == 2);
  CHECK(events[0].phase == ContactPhase::End);
  CHECK(events[1].phase == ContactPhase::End);
  CHECK(events[0].endReason == ContactEndReason::Destroyed);
  CHECK(events[1].endReason == ContactEndReason::Reconfigured);
  CHECK(world.rayCast({5, 1, 3}, {0, 0, -6})->value().collider == ColliderId{character});
}

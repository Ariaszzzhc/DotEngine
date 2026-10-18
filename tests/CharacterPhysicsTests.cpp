#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <variant>

import Dot.Physics.World;
import Dot.Core.Result;
import Dot.Math;

namespace {
  using namespace Dot;
  using namespace Dot::Physics;
  constexpr double tick = 1.0 / 60;

  auto box(Math::Vec3 position, Math::Vec3 halfExtents, Motion motion = Motion::Static)
      -> BodyConfig {
    BodyConfig result;
    result.motion = motion;
    result.shape = Shape::Box;
    result.position = position;
    result.halfExtents = halfExtents;
    return result;
  }

  auto step(Physics::World& world, int frames = 1) -> void {
    for (int frame = 0; frame < frames; ++frame) {
      auto result = world.step(tick);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    }
  }

  auto floor(Physics::World& world) -> BodyId {
    return world.createBody(box({0, -.5F, 0}, {20, .5F, 20})).value();
  }

  auto grounded(Physics::World& world, Math::Vec3 position = {0, 2, 0}) -> CharacterId {
    CharacterConfig config;
    config.position = position;
    auto created = world.createCharacter(config);
    REQUIRE_MESSAGE(created, (created ? "" : created.error().message));
    step(world, 120);
    REQUIRE(world.character(*created)->ground == GroundState::Grounded);
    return *created;
  }
}  // namespace

TEST_CASE("character.physics_falls_lands_walks_and_reports_actual_support") {
  auto world = Physics::World::create().value();
  const auto support = floor(world);
  const auto character = grounded(world);
  const auto landed = world.character(character).value();
  CHECK(landed.position.y == doctest::Approx(0).epsilon(.04));
  CHECK(landed.groundCollider == std::optional<ColliderId>{support});
  CHECK(landed.groundNormal.y > .99F);
  CHECK(landed.velocity.y <= 0);
  CHECK(landed.velocity.y >= -9.81F * tick - .001F);
  CHECK(world.stats()->characters == 1);
  CHECK(world.stats()->bodies == 1);
  REQUIRE(world.setCharacterInput(character, {{3, 0, 0}, false}));
  step(world, 60);
  CHECK(world.character(character)->position.x == doctest::Approx(3).epsilon(.02));
  REQUIRE(world.setCharacterInput(character, {}));
  step(world, 30);
  CHECK(world.character(character)->position.x == doctest::Approx(3).epsilon(.02));
  CHECK(world.body(support)->position == Math::Vec3{0, -.5F, 0});
  CHECK_FALSE(world.drainContactEvents()->empty());
  CHECK(world.rayCast({3, 1, 2}, {0, 0, -4})->value().collider == ColliderId{character});
}

TEST_CASE("character.physics_jump_is_single_use_and_air_requests_do_not_buffer") {
  auto world = Physics::World::create().value();
  floor(world);
  const auto character = grounded(world);
  REQUIRE(world.setCharacterInput(character, {{}, true}));
  REQUIRE(world.setCharacterYaw(character, 450));
  CHECK_FALSE(world.setCharacterInput(character, {{0, 1, 0}, false}));
  REQUIRE(world.setCharacterInput(character, {}));
  step(world);
  CHECK(world.character(character)->velocity.y > 5);
  CHECK(world.character(character)->rotation == Math::rotationEulerDegrees({0, 90, 0}).value());
  float peak{};

  for (int frame = 0; frame < 180; ++frame) {
    if (frame == 10) {
      REQUIRE(world.setCharacterInput(character, {{}, true}));
    }

    step(world);
    peak = std::max(peak, world.character(character)->position.y);
  }

  CHECK(peak > 1.7F);
  CHECK(peak < 2.05F);
  CHECK(world.character(character)->ground == GroundState::Grounded);
  CHECK(world.character(character)->position.y == doctest::Approx(0).epsilon(.04));
  step(world, 60);
  CHECK(world.character(character)->position.y == doctest::Approx(0).epsilon(.04));
  REQUIRE(world.setCharacterInput(character, {{4, 0, 0}, true}));
  REQUIRE(world.teleportCharacter(character, {0, 0, 0}));
  CHECK(world.character(character)->velocity == Math::Vec3{});
  CHECK(world.character(character)->measuredVelocity == Math::Vec3{});
  step(world);
  CHECK(world.character(character)->position.y == doctest::Approx(0).epsilon(.04));
  CHECK(world.character(character)->position.x == doctest::Approx(0).epsilon(.001));
}

TEST_CASE("character.physics_wall_sliding_and_ceiling_block_capsule_sweeps") {
  auto world = Physics::World::create().value();
  floor(world);
  REQUIRE(world.createBody(box({2, 2, 0}, {.1F, 2, 10})));
  const auto character = grounded(world);
  REQUIRE(world.setCharacterInput(character, {{3, 0, 1}, false}));
  step(world, 120);
  auto blocked = world.character(character).value();
  CHECK(blocked.position.x > 1.3F);
  CHECK(blocked.position.x < 1.6F);
  CHECK(blocked.position.z > 1.8F);
  CHECK(blocked.velocity.x == doctest::Approx(3));
  CHECK(std::abs(blocked.measuredVelocity.x) < .01F);
  CHECK(blocked.measuredVelocity.z == doctest::Approx(1).epsilon(.002));
  REQUIRE(world.teleportCharacter(character, {0, 0, 0}, 90));
  REQUIRE(world.createBody(box({0, 2.25F, 0}, {1, .15F, 1})));
  REQUIRE(world.setCharacterInput(character, {{}, true}));
  float peak{};

  for (int frame = 0; frame < 120; ++frame) {
    step(world);
    peak = std::max(peak, world.character(character)->position.y);
  }

  CHECK(peak > .1F);
  CHECK(peak < .4F);
  CHECK(world.character(character)->ground == GroundState::Grounded);
  CHECK(world.character(character)->rotation == Math::rotationEulerDegrees({0, 90, 0}).value());
}

TEST_CASE("character.physics_step_height_enables_stairs_without_climbing_high_walls") {
  for (const auto stepHeight : {0.F, .35F}) {
    auto world = Physics::World::create().value();
    floor(world);
    REQUIRE(world.createBody(box({2, .15F, 0}, {.5F, .15F, 1})));
    REQUIRE(world.createBody(box({4, 1, 0}, {.2F, 1, 1})));
    CharacterConfig config;
    config.stepHeight = stepHeight;
    const auto character = world.createCharacter(config).value();
    step(world, 30);
    REQUIRE(world.setCharacterInput(character, {{2, 0, 0}, false}));
    float highest{};

    for (int frame = 0; frame < 180; ++frame) {
      step(world);
      highest = std::max(highest, world.character(character)->position.y);
    }

    const auto result = world.character(character).value();

    if (stepHeight == 0) {
      CHECK(result.position.x < 1.5F);
      CHECK(highest < .1F);
    } else {
      CHECK(result.position.x > 3);
      CHECK(result.position.x < 3.6F);
      CHECK(highest > .25F);
      CHECK(highest < .4F);
    }
  }
}

TEST_CASE("character.physics_sensors_do_not_support_and_removed_ground_does_not_enable_jump") {
  auto world = Physics::World::create().value();
  auto sensor = box({0, -.5F, 0}, {20, .5F, 20});
  sensor.sensor = true;
  REQUIRE(world.createBody(sensor));
  const auto character = world.createCharacter().value();
  step(world, 60);
  CHECK(world.character(character)->position.y < -4);
  CHECK(world.character(character)->ground == GroundState::Airborne);
  CHECK_FALSE(world.character(character)->groundCollider);
  REQUIRE(world.teleportCharacter(character, {0, 0, 0}));
  const auto support = floor(world);
  step(world, 30);
  REQUIRE(world.character(character)->ground == GroundState::Grounded);
  REQUIRE(world.destroyBody(support));
  REQUIRE(world.setCharacterInput(character, {{}, true}));
  step(world);
  CHECK(world.character(character)->velocity.y < 0);
  CHECK_FALSE(world.character(character)->groundCollider);
}

TEST_CASE("character.physics_moving_platform_velocity_is_actual_support_motion") {
  auto world = Physics::World::create().value();
  const auto platform = world.createBody(box({0, -.5F, 0}, {5, .5F, 5}, Motion::Kinematic)).value();
  const auto character = grounded(world);
  REQUIRE(world.setVelocity(platform, {1, 0, 0}));
  step(world, 60);
  const auto state = world.character(character).value();
  CHECK(world.body(platform)->position.x == doctest::Approx(1).epsilon(.02));
  CHECK(state.position.x == doctest::Approx(1).epsilon(.03));
  CHECK(state.groundCollider == std::optional<ColliderId>{platform});
  CHECK(state.groundVelocity.x == doctest::Approx(1));
  CHECK(state.measuredVelocity.x == doctest::Approx(1).epsilon(.002));
  CHECK(std::abs(state.measuredVelocity.y) < .002F);
}

TEST_CASE("character.physics_handles_moves_capacity_and_thread_failures_preserve_state") {
  WorldConfig config;
  config.maxCharacters = 1;
  auto world = Physics::World::create(config).value();
  auto other = Physics::World::create().value();
  floor(world);
  const auto character = grounded(world);
  CHECK_FALSE(world.createCharacter());
  const auto original = world.character(character).value();
  CHECK_FALSE(other.character(character));
  CHECK_FALSE(other.destroyCharacter(character));
  CHECK_FALSE(world.teleportCharacter(character, {std::numeric_limits<float>::infinity(), 0, 0}));
  CHECK_FALSE(world.setCharacterYaw(character, std::numeric_limits<float>::quiet_NaN()));
  CHECK(world.character(character)->position == original.position);
  bool queryFailed{};
  bool mutationFailed{};
  bool destroyFailed{};
  std::thread worker([&] {
    queryFailed = !world.character(character);
    mutationFailed = !world.setCharacterInput(character, {{1, 0, 0}, true});
    destroyFailed = !world.destroyCharacter(character);
  });
  worker.join();
  CHECK(queryFailed);
  CHECK(mutationFailed);
  CHECK(destroyFailed);
  CHECK(world.character(character)->position == original.position);
  auto moved = std::move(world);
  CHECK_FALSE(world.character(character));
  CHECK_FALSE(world.createCharacter());
  CHECK_FALSE(world.destroyCharacter(character));
  CHECK_FALSE(world.setCharacterInput(character, {}));
  CHECK_FALSE(world.teleportCharacter(character, {}));
  CHECK_FALSE(world.setCharacterYaw(character, 90));
  CHECK(moved.character(character)->position == original.position);
  REQUIRE(moved.destroyCharacter(character));
  CHECK_FALSE(moved.character(character));
  CHECK_FALSE(moved.setCharacterInput(character, {}));
  CHECK_FALSE(moved.destroyCharacter(character));
  CHECK(moved.stats()->characters == 0);
  const auto next = moved.createCharacter().value();
  CHECK(next.value > character.value);
  CHECK(next.world == character.world);
  CHECK_FALSE(moved.character(character));
}

TEST_CASE("character.physics_invalid_creation_and_disabled_capacity_preserve_body_world") {
  WorldConfig config;
  config.maxCharacters = 257;
  CHECK_FALSE(Physics::World::create(config));
  config.maxCharacters = 0;
  auto disabled = Physics::World::create(config).value();
  REQUIRE(disabled.createBody(box({0, 0, 0}, {1, 1, 1})));
  auto unavailable = disabled.createCharacter();
  REQUIRE_FALSE(unavailable);
  CHECK(unavailable.error().code == Core::ErrorCode::Unavailable);
  CHECK(disabled.stats()->bodies == 1);
  CHECK_FALSE(disabled.stats()->faulted);

  for (const auto gravity : {Math::Vec3{1, -9, 0}, Math::Vec3{0, -9, 1}, Math::Vec3{0, 1, 0}}) {
    config.maxCharacters = 1;
    config.gravity = gravity;
    auto world = Physics::World::create(config).value();
    CHECK_FALSE(world.createCharacter());
    CHECK(world.stats()->characters == 0);
    REQUIRE(world.createBody());
    REQUIRE(world.step(tick));
    CHECK_FALSE(world.stats()->faulted);
  }

  auto world = Physics::World::create().value();
  CharacterConfig invalid;
  invalid.radius = 0;
  CHECK_FALSE(world.createCharacter(invalid));
  CHECK(world.stats()->characters == 0);
  const auto character = world.createCharacter().value();
  CHECK(character.value == 1);
  const auto original = world.character(character).value();
  CHECK_FALSE(world.step(std::numeric_limits<double>::quiet_NaN()));
  CHECK(world.character(character)->position == original.position);
}

TEST_CASE("character.physics_capsules_collide_with_each_other_and_release_borrowed_contacts") {
  for (int iteration = 0; iteration < 8; ++iteration) {
    auto world = Physics::World::create().value();
    floor(world);
    const auto first = grounded(world, {-2, 0, 0});
    const auto second = grounded(world, {2, 0, 0});
    REQUIRE(world.setCharacterInput(first, {{2, 0, 0}, false}));
    REQUIRE(world.setCharacterInput(second, {{-2, 0, 0}, false}));
    step(world, 120);
    const auto a = world.character(first).value();
    const auto b = world.character(second).value();
    CHECK(b.position.x - a.position.x > .68F);
    CHECK(a.position.x < b.position.x);
    REQUIRE(world.destroyCharacter(second));
    step(world, 60);
    CHECK(world.character(first)->position.x > a.position.x + 1.8F);
    CHECK(world.stats()->characters == 1);
    REQUIRE(world.destroyCharacter(first));
    CHECK(world.stats()->characters == 0);
  }
}

TEST_CASE("character.physics_walkable_slope_climbs_and_steep_slope_rejects_ascent") {
  for (const auto angle : {30.F, 65.F}) {
    auto world = Physics::World::create().value();
    const auto radians = angle * 3.14159265358979323846F / 180;
    auto ramp = box({0, -.2F / std::cos(radians), 0}, {10, .2F, 3});
    ramp.rotation = Math::rotationEulerDegrees({0, 0, angle}).value();
    const auto support = world.createBody(ramp).value();
    CharacterConfig config;
    config.position = {0, 1, 0};
    config.maxSlopeDegrees = 45;
    config.stepHeight = 0;
    const auto character = world.createCharacter(config).value();
    step(world, 30);
    const auto initial = world.character(character).value();
    REQUIRE(world.setCharacterInput(character, {{3, 0, 0}, false}));
    step(world, 45);
    const auto final = world.character(character).value();

    if (angle == 30) {
      CHECK(final.ground == GroundState::Grounded);
      CHECK(final.groundCollider == std::optional<ColliderId>{support});
      CHECK(final.position.x > initial.position.x + 1);
      CHECK(final.position.y > initial.position.y + .5F);
      CHECK(final.groundNormal.y == doctest::Approx(std::cos(radians)).epsilon(.01));
    } else {
      CHECK(final.ground != GroundState::Grounded);
      CHECK(final.position.x < initial.position.x + .05F);
    }
  }
}

TEST_CASE("character.physics_contact_budget_failures_are_explicit_and_fault_only_after_mutation") {
  auto world = Physics::World::create().value();
  floor(world);
  REQUIRE(world.createBody(box({.5F, 1, 0}, {.2F, 1, 2})));
  CharacterConfig config;
  config.position = {0, 0, 0};
  config.maxContacts = 1;
  auto failed = world.createCharacter(config);
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("contact capacity") != std::string::npos);
  CHECK(world.stats()->characters == 0);
  CHECK_FALSE(world.stats()->faulted);
  config.position = {-3, 0, 0};
  const auto character = world.createCharacter(config).value();

  SUBCASE("teleport contact query fails after pose change") {
    auto teleported = world.teleportCharacter(character, {0, 0, 0});
    REQUIRE_FALSE(teleported);
    CHECK(teleported.error().message.find("advanced pose") != std::string::npos);
  }

  SUBCASE("step contact query fails after earlier movement") {
    REQUIRE(world.setCharacterInput(character, {{3, 0, 0}, false}));
    bool stepFailed{};

    for (int frame = 0; frame < 120; ++frame) {
      auto result = world.step(tick);

      if (!result) {
        CHECK(result.error().message.find("contact capacity") != std::string::npos);
        stepFailed = true;
        break;
      }
    }

    REQUIRE(stepFailed);
    CHECK(world.character(character)->position.x > -2);
  }

  CHECK(world.stats()->faulted);
  REQUIRE(world.character(character));
  CHECK_FALSE(world.step(tick));
  CHECK_FALSE(world.setCharacterInput(character, {}));
  CHECK_FALSE(world.setCharacterYaw(character, 0));
  CHECK_FALSE(world.createCharacter());
  REQUIRE(world.destroyCharacter(character));
  CHECK(world.stats()->characters == 0);
}

TEST_CASE("character.physics_pushes_dynamic_body_and_rides_a_vertical_platform") {
  SUBCASE("virtual capsule applies actual impulses to dynamic body") {
    auto world = Physics::World::create().value();
    floor(world);
    auto movable = box({2, .3F, 0}, {.3F, .3F, .3F}, Motion::Dynamic);
    movable.mass = 5;
    movable.friction = .1F;
    const auto body = world.createBody(movable).value();
    const auto character = grounded(world);
    REQUIRE(world.setCharacterInput(character, {{2, 0, 0}, false}));
    step(world, 150);
    CHECK(world.body(body)->position.x > 3);
    CHECK(world.character(character)->position.x > 2);
  }

  SUBCASE("upward support motion remains part of grounded velocity") {
    auto world = Physics::World::create().value();
    const auto platform
        = world.createBody(box({0, -.5F, 0}, {5, .5F, 5}, Motion::Kinematic)).value();
    const auto character = grounded(world);
    REQUIRE(world.setVelocity(platform, {0, 1, 0}));
    step(world, 60);
    const auto state = world.character(character).value();
    CHECK(state.position.y == doctest::Approx(1).epsilon(.03));
    CHECK(state.groundCollider == std::optional<ColliderId>{platform});
    CHECK(state.groundVelocity.y == doctest::Approx(1));
    REQUIRE(world.setCharacterInput(character, {{}, true}));
    step(world);
    CHECK(world.character(character)->velocity.y > 6);
    CHECK(world.character(character)->position.y > 1.05F);
  }
}

TEST_CASE("character.physics_substeps_advance_bodies_and_characters_together") {
  auto coarse = Physics::World::create().value();
  auto fine = Physics::World::create().value();
  floor(coarse);
  floor(fine);
  CharacterConfig config;
  config.position = {0, 4, 0};
  const auto first = coarse.createCharacter(config).value();
  const auto second = fine.createCharacter(config).value();
  auto body = BodyConfig{};
  body.position = {5, 4, 0};
  const auto firstBody = coarse.createBody(body).value();
  const auto secondBody = fine.createBody(body).value();
  REQUIRE(coarse.setCharacterInput(first, {{2, 0, 0}, false}));
  REQUIRE(fine.setCharacterInput(second, {{2, 0, 0}, false}));
  REQUIRE(coarse.step(.25));
  step(fine, 15);
  CHECK(coarse.character(first)->position.x
        == doctest::Approx(fine.character(second)->position.x).epsilon(.0001));
  CHECK(coarse.character(first)->position.y
        == doctest::Approx(fine.character(second)->position.y).epsilon(.0001));
  CHECK(coarse.body(firstBody)->position.y
        == doctest::Approx(fine.body(secondBody)->position.y).epsilon(.0001));
}

TEST_CASE("character.colliders_query_identity_filter_pose_and_lifetime_are_native") {
  auto world = Physics::World::create({.maxBodies = 1, .gravity = {}, .maxCharacters = 1}).value();
  const auto body = world.createBody(box({4, 1, 0}, {.5F, 1, .5F})).value();
  const auto character = world.createCharacter().value();
  CHECK(character.value == body.value);
  auto ray = world.rayCast({0, 1, 3}, {0, 0, -6}).value();
  REQUIRE(ray);
  CHECK(ray->collider == ColliderId{character});
  CHECK(ray->fraction == doctest::Approx((3 - .35) / 6).epsilon(.01));
  REQUIRE(ray->normal);
  CHECK(ray->normal->z > .99F);
  auto swept = world.sphereCast({0, 1, 3}, .25F, {0, 0, -6}).value();
  REQUIRE(swept);
  CHECK(swept->collider == ColliderId{character});
  CHECK(swept->center.z == doctest::Approx(.6).epsilon(.01));
  CHECK_FALSE(world.sphereCast({0, 1, 3}, .25F, {0, 0, -6}, {false, character})->has_value());
  CHECK(world.stats()->bodies == 1);
  CHECK(world.stats()->active == 0);
  CHECK(world.stats()->characters == 1);
  REQUIRE(world.teleportCharacter(character, {2, 0, 0}, 90));
  CHECK_FALSE(world.rayCast({0, 1, 3}, {0, 0, -6})->has_value());
  CHECK(world.rayCast({2, 1, 3}, {0, 0, -6})->value().collider == ColliderId{character});
  auto moved = std::move(world);
  REQUIRE(moved.destroyCharacter(character));
  CHECK_FALSE(moved.rayCast({2, 1, 3}, {0, 0, -6})->has_value());
  CHECK_FALSE(moved.sphereCast({0, 1, 3}, .25F, {0, 0, -6}, {false, character}));
  const auto replacement = moved.createCharacter().value();
  CHECK(replacement.value > character.value);
  auto foreign = Physics::World::create({.gravity = {}}).value();
  const auto other = foreign.createCharacter().value();
  CHECK_FALSE(moved.sphereCast({0, 1, 3}, .25F, {0, 0, -6}, {false, other}));
}

TEST_CASE("character.colliders_sensor_events_preserve_tags_and_lifecycle_reasons") {
  auto world = Physics::World::create({.gravity = {}, .workerThreads = 2}).value();
  auto volume = box({0, 1, 0}, {1, 1, 1});
  volume.sensor = true;
  volume.userTag = 41;
  const auto sensor = world.createBody(volume).value();
  CharacterConfig config;
  config.userTag = 73;
  const auto character = world.createCharacter(config).value();
  step(world);
  auto events = world.drainContactEvents().value();
  REQUIRE(events.size() == 1);
  CHECK(events[0].phase == ContactPhase::Begin);
  CHECK(events[0].first.collider == ColliderId{sensor});
  CHECK(events[0].second.collider == ColliderId{character});
  CHECK(events[0].first.userTag == 41);
  CHECK(events[0].second.userTag == 73);
  CHECK(events[0].sensor);
  REQUIRE(events[0].geometry);
  CHECK_FALSE(std::get<Dot::Physics::ContactManifold>(*events[0].geometry).points.empty());
  step(world);
  events = world.drainContactEvents().value();
  REQUIRE(events.size() == 1);
  CHECK(events[0].phase == ContactPhase::Stay);
  REQUIRE(world.teleportCharacter(character, {4, 0, 0}));
  events = world.drainContactEvents().value();
  REQUIRE(events.size() == 1);
  CHECK(events[0].phase == ContactPhase::End);
  CHECK(events[0].endReason == ContactEndReason::PoseChanged);
  CHECK_FALSE(events[0].geometry);
  step(world);
  CHECK(world.drainContactEvents()->empty());
  REQUIRE(world.teleportCharacter(character, {}));
  step(world);
  REQUIRE(world.drainContactEvents()->size() == 1);
  config.userTag = 99;
  const CharacterChange change{character, config};
  REQUIRE(world.applyChanges({}, std::span{&change, 1}));
  events = world.drainContactEvents().value();
  REQUIRE(events.size() == 1);
  CHECK(events[0].phase == ContactPhase::End);
  CHECK(events[0].endReason == ContactEndReason::Reconfigured);
  CHECK(events[0].second.userTag == 73);
  step(world);
  events = world.drainContactEvents().value();
  REQUIRE(events.size() == 1);
  CHECK(events[0].phase == ContactPhase::Begin);
  CHECK(events[0].second.userTag == 99);
  REQUIRE(world.destroyCharacter(character));
  events = world.drainContactEvents().value();
  REQUIRE(events.size() == 1);
  CHECK(events[0].endReason == ContactEndReason::Destroyed);
  CHECK(events[0].second.collider == ColliderId{character});
  CHECK(events[0].second.userTag == 99);
  step(world);
  CHECK(world.drainContactEvents()->empty());
}

TEST_CASE("character.colliders_incoming_body_contacts_and_pushes_without_passing_through") {
  auto world = Physics::World::create({.gravity = {}}).value();
  const auto character = world.createCharacter().value();
  BodyConfig config;
  config.position = {-2, 1, 0};
  config.radius = .25F;
  config.velocity = {20, 0, 0};
  config.friction = 0;
  const auto incoming = world.createBody(config).value();
  bool touched{};
  unsigned providers{};
  std::size_t begins{}, ends{}, handoffs{};
  bool open{};
  std::optional<std::size_t> previousProvider;

  for (int frame = 0; frame < 60; ++frame) {
    step(world);
    CHECK(world.character(character)->position.x - world.body(incoming)->position.x > .55F);

    std::size_t observations{};

    for (const auto& event : world.drainContactEvents().value()) {
      if (!event.sensor && event.first.collider == ColliderId{incoming}
          && event.second.collider == ColliderId{character}) {
        begins += event.phase == ContactPhase::Begin;
        ends += event.phase == ContactPhase::End;
        touched |= event.phase == ContactPhase::Begin;

        if (event.phase == ContactPhase::Begin) {
          CHECK_FALSE(open);
          open = true;
          previousProvider.reset();
        } else if (event.phase == ContactPhase::End) {
          CHECK(open);
          open = false;
          previousProvider.reset();
        } else {
          CHECK(event.phase == ContactPhase::Stay);
          CHECK(open);
        }

        if (event.geometry) {
          ++observations;
          const auto provider = event.geometry->index();

          if (previousProvider && *previousProvider != provider) {
            CHECK(event.phase == ContactPhase::Stay);
            ++handoffs;
          }

          previousProvider = provider;
          providers |= std::holds_alternative<ContactManifold>(*event.geometry) ? 1U : 2U;
        }
      }
    }

    CHECK(observations <= 1);
  }

  CHECK(touched);
  CHECK(providers == 3);
  CHECK(begins > 0);
  CHECK(ends <= begins);
  CHECK(handoffs >= 2);
  const auto feet = world.character(character)->position;
  const auto center = world.body(incoming)->position;
  INFO("character x ", feet.x, " incoming x ", center.x);
  CHECK(feet.x - center.x > .55F);
  CHECK(feet.x - center.x < .8F);
  CHECK(feet.x > 0);
  CHECK(feet.x < 2);
  REQUIRE(world.destroyBody(incoming));
  step(world, 30);
  CHECK(world.character(character)->position.x == doctest::Approx(feet.x).epsilon(.01));
  CHECK_FALSE(world.stats()->faulted);
}

TEST_CASE("character.colliders_peer_support_identity_cannot_survive_removed_owner") {
  auto world = Physics::World::create().value();
  floor(world);
  const auto bottom = grounded(world, {});
  const auto top = grounded(world, {0, 3, 0});
  CHECK(world.character(top)->groundCollider == std::optional<ColliderId>{bottom});
  CHECK(world.character(top)->position.y > 1.7F);
  REQUIRE(world.destroyCharacter(bottom));
  CHECK_FALSE(world.character(top)->groundCollider);
  REQUIRE(world.setCharacterInput(top, {{}, true}));
  step(world);
  CHECK(world.character(top)->velocity.y < 0);
  step(world, 120);
  CHECK(world.character(top)->position.y == doctest::Approx(0).epsilon(.02));
}

TEST_CASE("character.contacts_ground_constraints_preserve_tags_geometry_and_lifetime") {
  auto world = Physics::World::create({.workerThreads = 2}).value();
  auto ground = box({0, -.5F, 0}, {20, .5F, 20});
  ground.userTag = 11;
  const auto support = world.createBody(ground).value();
  CharacterConfig config;
  config.userTag = 17;
  const auto character = world.createCharacter(config).value();
  std::size_t begins{}, stays{};
  std::optional<ContactEvent> owned;

  for (int frame = 0; frame < 60; ++frame) {
    step(world);
    const auto events = world.drainContactEvents().value();
    REQUIRE(events.size() == 1);
    const auto& event = events.front();
    CHECK(event.first.collider == ColliderId{support});
    CHECK(event.second.collider == ColliderId{character});
    CHECK(event.first.userTag == 11);
    CHECK(event.second.userTag == 17);
    CHECK_FALSE(event.sensor);
    begins += event.phase == ContactPhase::Begin;
    stays += event.phase == ContactPhase::Stay;
    REQUIRE(event.geometry);
    const auto* geometry = std::get_if<CharacterContactGeometry>(&*event.geometry);
    REQUIRE(geometry);
    CHECK(geometry->normal.y > .99F);
    CHECK(geometry->point.y == doctest::Approx(0).epsilon(.001));
    CHECK(std::abs(geometry->separation) < .03F);
    CHECK_FALSE(geometry->characterIsFirst);
    owned = event;
  }

  CHECK(begins == 1);
  CHECK(stays == 59);
  CHECK(world.stats()->contactPairs == 1);
  const auto queued = world.stats()->queuedContactEvents;
  config.position = {100, 0, 0};
  const auto unrelated = world.createCharacter(config).value();
  CHECK(world.stats()->queuedContactEvents == queued);
  REQUIRE(world.destroyCharacter(unrelated));
  CHECK(world.stats()->queuedContactEvents == queued);
  auto moved = std::move(world);
  REQUIRE(moved.destroyBody(support));
  const auto ended = moved.drainContactEvents().value();
  REQUIRE(ended.size() == 1);
  CHECK(ended.front().phase == ContactPhase::End);
  CHECK(ended.front().endReason == ContactEndReason::Destroyed);
  CHECK(ended.front().first.userTag == 11);
  CHECK(ended.front().second.userTag == 17);
  CHECK_FALSE(ended.front().geometry);
  CHECK(std::get<CharacterContactGeometry>(*owned->geometry).normal.y > .99F);
  step(moved);
  CHECK(moved.drainContactEvents()->empty());
}

TEST_CASE("character.contacts_wall_constraints_begin_stay_and_end_without_duplicate_pairs") {
  auto world = Physics::World::create().value();
  floor(world);
  const auto wall = world.createBody(box({2, 2, 0}, {.1F, 2, 10})).value();
  const auto character = grounded(world);
  REQUIRE(world.drainContactEvents());
  REQUIRE(world.setCharacterInput(character, {{3, 0, 0}, false}));
  std::size_t begins{}, stays{}, ends{}, virtualObservations{};

  for (int frame = 0; frame < 180; ++frame) {
    if (frame == 90) {
      REQUIRE(world.setCharacterInput(character, {{-3, 0, 0}, false}));
    }

    step(world);
    std::size_t observations{};

    for (const auto& event : world.drainContactEvents().value()) {
      if (event.first.collider != ColliderId{wall}
          || event.second.collider != ColliderId{character}) {
        continue;
      }

      begins += event.phase == ContactPhase::Begin;
      stays += event.phase == ContactPhase::Stay;
      ends += event.phase == ContactPhase::End;

      if (event.geometry) {
        ++observations;

        if (const auto* geometry = std::get_if<CharacterContactGeometry>(&*event.geometry)) {
          ++virtualObservations;
          CHECK(geometry->normal.x < -.99F);
          CHECK(geometry->point.x == doctest::Approx(1.9).epsilon(.001));
        }
      }
    }

    CHECK(observations <= 1);
  }

  CHECK(begins == 1);
  CHECK(stays > 10);
  CHECK(ends == 1);
  CHECK(virtualObservations > 10);
}

TEST_CASE("character.contacts_peer_constraints_keep_owner_order_and_removal_identity") {
  auto world = Physics::World::create({.gravity = {}, .workerThreads = 2}).value();
  CharacterConfig config;
  config.userTag = 21;
  const auto first = world.createCharacter(config).value();
  config.position = {.9F, 0, 0};
  config.userTag = 34;
  const auto second = world.createCharacter(config).value();
  REQUIRE(world.setCharacterInput(first, {{2, 0, 0}, false}));
  std::size_t begins{}, virtualObservations{};

  for (int frame = 0; frame < 60; ++frame) {
    step(world);
    const auto events = world.drainContactEvents().value();
    CHECK(events.size() <= 1);

    for (const auto& event : events) {
      begins += event.phase == ContactPhase::Begin;
      CHECK(event.phase != ContactPhase::End);
      CHECK(event.first.collider == ColliderId{first});
      CHECK(event.second.collider == ColliderId{second});
      CHECK(event.first.userTag == 21);
      CHECK(event.second.userTag == 34);
      REQUIRE(event.geometry);

      if (const auto* geometry = std::get_if<CharacterContactGeometry>(&*event.geometry)) {
        ++virtualObservations;
        CHECK(geometry->normal.x > .99F);
        CHECK(Math::isFinite(geometry->point));
      }
    }
  }

  CHECK(begins == 1);
  CHECK(virtualObservations > 1);
  REQUIRE(world.destroyCharacter(second));
  const auto ended = world.drainContactEvents().value();
  REQUIRE(ended.size() == 1);
  CHECK(ended.front().phase == ContactPhase::End);
  CHECK(ended.front().endReason == ContactEndReason::Destroyed);
  CHECK(ended.front().second.collider == ColliderId{second});
  CHECK(ended.front().second.userTag == 34);
  step(world);
  CHECK(world.drainContactEvents()->empty());
}

TEST_CASE("character.contacts_queue_preflight_preserves_pose_and_overflow_faults_delivery") {
  auto world = Physics::World::create({.maxContactEvents = 1}).value();
  const auto support = floor(world);
  const auto character = world.createCharacter().value();
  step(world);
  REQUIRE(world.stats()->queuedContactEvents == 1);
  const auto position = world.character(character)->position;
  CHECK_FALSE(world.teleportCharacter(character, {4, 0, 0}));
  CHECK_FALSE(world.destroyBody(support));
  CHECK(world.character(character)->position == position);
  CHECK_FALSE(world.stats()->faulted);
  const auto begin = world.drainContactEvents().value();
  REQUIRE(begin.size() == 1);
  CHECK(begin.front().phase == ContactPhase::Begin);
  REQUIRE(world.teleportCharacter(character, position));
  const auto ended = world.drainContactEvents().value();
  REQUIRE(ended.size() == 1);
  CHECK(ended.front().endReason == ContactEndReason::PoseChanged);
  step(world);
  CHECK_FALSE(world.step(tick));
  CHECK(world.stats()->faulted);
  CHECK_FALSE(world.drainContactEvents());
  CHECK(world.character(character));
  CHECK_FALSE(world.setCharacterInput(character, {}));
  REQUIRE(world.destroyCharacter(character));
  REQUIRE(world.destroyBody(support));
}

TEST_CASE(
    "character.contacts_predictive_proximity_is_not_collision_and_substeps_close_transients") {
  SUBCASE("stationary predictive shell does not announce an unsolved encounter") {
    auto world = Physics::World::create({.gravity = {}}).value();
    REQUIRE(world.createBody(box({2, 2, 0}, {.1F, 2, 10})));
    CharacterConfig config;
    config.position = {1.5F, 0, 0};
    const auto character = world.createCharacter(config).value();
    step(world, 10);
    CHECK(world.drainContactEvents()->empty());
    REQUIRE(world.setCharacterInput(character, {{3, 0, 0}, false}));
    step(world, 10);
    CHECK_FALSE(world.drainContactEvents()->empty());
  }

  SUBCASE("a contact crossed during one subdivided step begins and ends once") {
    auto world = Physics::World::create().value();
    const auto support = world.createBody(box({0, -.5F, 0}, {.5F, .5F, 1})).value();
    const auto character = world.createCharacter().value();
    REQUIRE(world.setCharacterInput(character, {{20, 0, 0}, false}));
    REQUIRE(world.step(.25));
    const auto events = world.drainContactEvents().value();
    REQUIRE(events.size() == 2);
    CHECK(events[0].phase == ContactPhase::Begin);
    CHECK(events[1].phase == ContactPhase::End);
    CHECK(events[0].step == 1);
    CHECK(events[1].step == 1);
    CHECK(events[0].first.collider == ColliderId{support});
    CHECK(events[0].second.collider == ColliderId{character});
    REQUIRE(events[0].geometry);
    CHECK(std::holds_alternative<CharacterContactGeometry>(*events[0].geometry));
    CHECK_FALSE(events[1].geometry);
    CHECK(world.stats()->contactPairs == 0);
  }
}

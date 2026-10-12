#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

import Dot.Physics.World;
import Dot.Core.Result;
import Dot.Math;

using namespace Dot::Physics;

TEST_CASE("physics_batch.full_capacity_replacement_retains_handle") {
  auto world = World::create({1, {}, 0});
  REQUIRE(world);
  auto body = world->createBody();
  REQUIRE(body);
  BodyConfig replacement;
  replacement.shape = Shape::Box;
  replacement.position = {2, 0, 0};
  replacement.mass = 2;
  replacement.angularVelocity = {0, 1, 0};
  const BodyChange change{*body, replacement};
  auto applied = world->applyChanges(std::span{&change, 1});
  REQUIRE(applied);
  REQUIRE(applied->size() == 1);
  CHECK(applied->front() == *body);
  CHECK(world->stats()->bodies == 1);
  CHECK(world->body(*body)->position == replacement.position);
  CHECK(world->body(*body)->angularVelocity == replacement.angularVelocity);
  REQUIRE(world->addImpulse(*body, {2, 0, 0}));
  CHECK(world->body(*body)->velocity.x == doctest::Approx(1));
  auto hit = world->rayCast({2, 2, 0}, {0, -4, 0});
  REQUIRE(hit);
  REQUIRE(hit->has_value());
  CHECK((*hit)->body == *body);
}

TEST_CASE("physics_batch.create_destroy_replace_results_follow_input_order") {
  auto world = World::create({2, {}, 0});
  REQUIRE(world);
  auto first = world->createBody();
  auto second = world->createBody();
  REQUIRE(first);
  REQUIRE(second);
  BodyConfig config;
  config.position = {3, 0, 0};
  const std::array changes{BodyChange{std::nullopt, config}, BodyChange{*first, std::nullopt},
                           BodyChange{*second, config}};
  auto applied = world->applyChanges(changes);
  REQUIRE(applied);
  CHECK(applied->size() == 3);
  REQUIRE((*applied)[0]);
  CHECK((*applied)[0]->value > second->value);
  CHECK_FALSE((*applied)[1]);
  CHECK((*applied)[2] == *second);
  CHECK_FALSE(world->body(*first));
  CHECK(world->stats()->bodies == 2);
  CHECK(world->body(*second)->position == config.position);
}

TEST_CASE("physics_batch.invalid_later_change_preserves_all_bodies_and_ids") {
  auto world = World::create({2, {}, 0});
  REQUIRE(world);
  auto body = world->createBody();
  REQUIRE(body);
  BodyConfig good;
  good.position = {2, 0, 0};
  BodyConfig bad;
  bad.mass = 0;
  const std::array changes{BodyChange{*body, good}, BodyChange{std::nullopt, bad}};
  CHECK_FALSE(world->applyChanges(changes));
  CHECK(world->body(*body)->position == Dot::Math::Vec3{});
  CHECK(world->stats()->bodies == 1);
  auto next = world->createBody(good);
  REQUIRE(next);
  CHECK(next->value == body->value + 1);
}

TEST_CASE("physics_batch_capacity_duplicate_empty_foreign_and_stale_failures") {
  auto world = World::create({1, {}, 0});
  auto foreign = World::create();
  REQUIRE(world);
  REQUIRE(foreign);
  auto body = world->createBody();
  auto other = foreign->createBody();
  REQUIRE(body);
  REQUIRE(other);
  BodyConfig config;
  const BodyChange create{std::nullopt, config};
  CHECK_FALSE(world->applyChanges(std::span{&create, 1}));
  const std::array duplicate{BodyChange{*body, config}, BodyChange{*body, std::nullopt}};
  CHECK_FALSE(world->applyChanges(duplicate));
  const BodyChange invalid{};
  CHECK_FALSE(world->applyChanges(std::span{&invalid, 1}));
  const BodyChange different{*other, config};
  CHECK_FALSE(world->applyChanges(std::span{&different, 1}));
  REQUIRE(world->destroyBody(*body));
  const BodyChange stale{*body, config};
  CHECK_FALSE(world->applyChanges(std::span{&stale, 1}));
  std::vector<BodyChange> tooMany(8193);
  CHECK_FALSE(world->applyChanges(tooMany));
  REQUIRE(world->applyChanges({}));
  CHECK(world->stats()->bodies == 0);
}

TEST_CASE("physics_batch.unmodified_body_preserves_sleep_and_angular_motion") {
  auto world = World::create({2, {}, 0});
  REQUIRE(world);
  BodyConfig asleep;
  asleep.startActive = false;
  auto sleeper = world->createBody(asleep);
  BodyConfig moving;
  moving.position = {5, 0, 0};
  moving.angularVelocity = {0, 1, 0};
  auto spinner = world->createBody(moving);
  REQUIRE(sleeper);
  REQUIRE(spinner);
  const BodyChange removed{*spinner, std::nullopt};
  REQUIRE(world->applyChanges(std::span{&removed, 1}));
  CHECK_FALSE(world->body(*sleeper)->active);
  REQUIRE(world->setAngularVelocity(*sleeper, {0, 2, 0}));
  CHECK(world->body(*sleeper)->active);
  REQUIRE(world->step(0.1));
  CHECK(world->body(*sleeper)->rotation.y > 0.09F);
  CHECK(world->body(*sleeper)->angularVelocity.y == doctest::Approx(2));
}

TEST_CASE("physics_batch.motion_type_changes_and_repeated_resource_lifetime") {
  auto world = World::create({1, {}, 0});
  REQUIRE(world);
  auto body = world->createBody();
  REQUIRE(body);

  for (int i = 0; i < 300; ++i) {
    BodyConfig config;
    config.motion = i % 3 == 0 ? Motion::Static : i % 3 == 1 ? Motion::Kinematic : Motion::Dynamic;
    config.shape = i % 2 == 0 ? Shape::Box : Shape::Capsule;
    config.sensor = i % 2 == 0;
    const BodyChange change{*body, config};
    REQUIRE(world->applyChanges(std::span{&change, 1}));
    CHECK(world->body(*body)->motion == config.motion);
    CHECK(world->body(*body)->sensor == config.sensor);
    CHECK(world->stats()->bodies == 1);
  }

  REQUIRE(world->destroyBody(*body));
  CHECK(world->stats()->bodies == 0);
}

TEST_CASE("physics_batch.angular_input_limits_and_failed_setters") {
  auto world = World::create();
  REQUIRE(world);
  BodyConfig config;
  config.angularVelocity = {0, 50, 0};
  CHECK_FALSE(world->createBody(config));
  config.angularVelocity.y = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(world->createBody(config));
  config.angularVelocity = {0, 1, 0};
  config.motion = Motion::Static;
  CHECK_FALSE(world->createBody(config));
  config.angularVelocity = {};
  auto fixed = world->createBody(config);
  REQUIRE(fixed);
  CHECK_FALSE(world->setAngularVelocity(*fixed, {}));
  config.motion = Motion::Dynamic;
  config.position = {5, 0, 0};
  config.angularVelocity = {0, 15 * std::numbers::pi_v<float>, 0};
  auto body = world->createBody(config);
  REQUIRE(body);
  const auto before = world->body(*body)->angularVelocity;
  CHECK_FALSE(world->setAngularVelocity(*body, {0, 50, 0}));
  CHECK_FALSE(world->setAngularVelocity(*body, {0, std::numeric_limits<float>::infinity(), 0}));
  CHECK(world->body(*body)->angularVelocity == before);
  REQUIRE(world->setAngularVelocity(*body, {}));
  CHECK(world->body(*body)->angularVelocity == Dot::Math::Vec3{});
}

TEST_CASE("physics_batch.owner_thread_move_and_stale_angular_operations") {
  auto world = World::create();
  REQUIRE(world);
  auto body = world->createBody();
  REQUIRE(body);
  const BodyChange change{*body, BodyConfig{}};
  bool rejected = false;
  std::thread worker([&] {
    rejected = !world->applyChanges(std::span{&change, 1}) && !world->setAngularVelocity(*body, {});
  });
  worker.join();
  CHECK(rejected);
  World moved = std::move(*world);
  CHECK_FALSE(world->applyChanges({}));
  CHECK_FALSE(world->setAngularVelocity(*body, {}));
  REQUIRE(moved.destroyBody(*body));
  CHECK_FALSE(moved.setAngularVelocity(*body, {}));
}

TEST_CASE("physics_batch.trusted_motion_preservation_and_invalid_flags") {
  auto world = World::create({1, {0, -1000, 0}, 0});
  REQUIRE(world);
  BodyConfig initial;
  initial.velocity = {500, 0, 0};
  initial.angularVelocity = {0, 1, 0};
  auto body = world->createBody(initial);
  REQUIRE(body);
  REQUIRE(world->step(0.1));
  const auto state = world->body(*body).value();
  BodyConfig config;
  config.position = state.position;
  config.rotation = state.rotation;
  config.mass = 2;
  const BodyChange replacement{*body, config, true, true};
  REQUIRE(world->applyChanges(std::span{&replacement, 1}));
  const auto after = world->body(*body).value();
  CHECK(after.velocity.x == doctest::Approx(state.velocity.x));
  CHECK(after.velocity.y == doctest::Approx(state.velocity.y));
  CHECK(after.angularVelocity == state.angularVelocity);
  const BodyChange creation{std::nullopt, config, true, true};
  CHECK_FALSE(world->applyChanges(std::span{&creation, 1}));
  const BodyChange deletion{*body, std::nullopt, true, true};
  CHECK_FALSE(world->applyChanges(std::span{&deletion, 1}));
  config.motion = Motion::Static;
  const BodyChange fixed{*body, config, true, true};
  CHECK_FALSE(world->applyChanges(std::span{&fixed, 1}));
  CHECK(world->stats()->bodies == 1);
}

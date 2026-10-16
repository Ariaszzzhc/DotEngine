#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

import Dot.Physics.World;
import Dot.Core.Result;
import Dot.Math;

using namespace Dot::Physics;

namespace {
  struct Pair {
    World world;
    BodyId floor;
    BodyId ball;
  };

  auto pair(std::uint32_t maxEvents = 65536, std::uint32_t workers = 0, bool sensor = false)
      -> Pair {
    auto world = World::create({8, {0, -9.81F, 0}, workers, maxEvents});
    REQUIRE(world);
    BodyConfig fixed;
    fixed.motion = Motion::Static;
    fixed.shape = Shape::Box;
    fixed.halfExtents = {10, 0.5F, 10};
    fixed.position = {0, -0.5F, 0};
    fixed.userTag = 111;
    fixed.sensor = sensor;
    auto floor = world->createBody(fixed);
    BodyConfig moving;
    moving.position = {0, 0.48F, 0};
    moving.userTag = 222;
    auto ball = world->createBody(moving);
    REQUIRE(floor);
    REQUIRE(ball);
    return {std::move(*world), *floor, *ball};
  }
}  // namespace

TEST_CASE("contacts.begin_stay_owned_geometry_and_runtime_tags") {
  auto fixture = pair();
  REQUIRE(fixture.world.step(1.0 / 60));
  auto events = fixture.world.drainContactEvents();
  REQUIRE(events);
  REQUIRE(events->size() == 1);
  auto event = events->front();
  CHECK(event.phase == ContactPhase::Begin);
  CHECK(event.step == 1);
  CHECK(event.first.collider == ColliderId{fixture.floor});
  CHECK(event.second.collider == ColliderId{fixture.ball});
  CHECK(event.first.userTag == 111);
  CHECK(event.second.userTag == 222);
  CHECK_FALSE(event.sensor);
  REQUIRE(event.geometry);
  CHECK(std::get<Dot::Physics::ContactManifold>(*event.geometry).normal.y == doctest::Approx(1));
  CHECK(std::get<Dot::Physics::ContactManifold>(*event.geometry).penetrationDepth >= 0);
  REQUIRE_FALSE(std::get<Dot::Physics::ContactManifold>(*event.geometry).points.empty());
  CHECK(Dot::Math::isFinite(
      std::get<Dot::Physics::ContactManifold>(*event.geometry).points.front().first));
  CHECK(fixture.world.stats()->contactPairs == 1);
  CHECK(fixture.world.stats()->queuedContactEvents == 0);
  REQUIRE(fixture.world.step(1.0 / 60));
  auto next = fixture.world.drainContactEvents();
  REQUIRE(next);
  REQUIRE(next->size() == 1);
  CHECK(next->front().phase == ContactPhase::Stay);
  CHECK(next->front().step == 2);
  CHECK(std::get<Dot::Physics::ContactManifold>(*event.geometry).normal.y == doctest::Approx(1));
  CHECK(std::get<Dot::Physics::ContactManifold>(*event.geometry).points.size() > 0);
}

TEST_CASE("contacts.sleep_does_not_emit_false_separation_and_wake_keeps_pair") {
  auto fixture = pair();
  int begin = 0;
  int sleep = 0;
  int end = 0;

  for (int i = 0; i < 180; ++i) {
    REQUIRE(fixture.world.step(1.0 / 60));
    auto events = fixture.world.drainContactEvents();
    REQUIRE(events);

    for (const auto& event : *events) {
      begin += event.phase == ContactPhase::Begin;
      sleep += event.phase == ContactPhase::Sleep;
      end += event.phase == ContactPhase::End;
    }
  }

  CHECK(begin == 1);
  CHECK(sleep == 1);
  CHECK(end == 0);
  CHECK_FALSE(fixture.world.body(fixture.ball)->active);
  CHECK(fixture.world.stats()->contactPairs == 1);
  REQUIRE(fixture.world.addImpulse(fixture.ball, {0, 3, 0}));

  for (int i = 0; i < 12; ++i) {
    REQUIRE(fixture.world.step(1.0 / 60));
    auto events = fixture.world.drainContactEvents();
    REQUIRE(events);

    for (const auto& event : *events) {
      CHECK(event.phase != ContactPhase::Begin);

      if (event.phase == ContactPhase::End) {
        ++end;
        CHECK(event.endReason == ContactEndReason::Separated);
        CHECK_FALSE(event.geometry);
      }
    }
  }

  CHECK(end == 1);
  CHECK(fixture.world.stats()->contactPairs == 0);
}

TEST_CASE("contacts.destroyed_body_event_retains_owned_handles_and_tags") {
  auto fixture = pair();
  REQUIRE(fixture.world.step(1.0 / 60));
  REQUIRE(fixture.world.drainContactEvents());
  REQUIRE(fixture.world.destroyBody(fixture.ball));
  auto events = fixture.world.drainContactEvents();
  REQUIRE(events);
  REQUIRE(events->size() == 1);
  CHECK(events->front().phase == ContactPhase::End);
  CHECK(events->front().endReason == ContactEndReason::Destroyed);
  CHECK(events->front().second.collider == ColliderId{fixture.ball});
  CHECK(events->front().second.userTag == 222);
  CHECK_FALSE(events->front().geometry);
  CHECK_FALSE(fixture.world.body(fixture.ball));
  REQUIRE(fixture.world.step(1.0 / 60));
  CHECK(fixture.world.drainContactEvents()->empty());
  CHECK(fixture.world.stats()->contactPairs == 0);
}

TEST_CASE("contacts.reconfiguration_ends_old_association_and_begins_new_one") {
  auto fixture = pair();
  REQUIRE(fixture.world.step(1.0 / 60));
  REQUIRE(fixture.world.drainContactEvents());
  BodyConfig config;
  config.position = {0, 0.48F, 0};
  config.userTag = 333;
  const BodyChange change{fixture.ball, config};
  REQUIRE(fixture.world.applyChanges(std::span{&change, 1}));
  auto ended = fixture.world.drainContactEvents();
  REQUIRE(ended);
  REQUIRE(ended->size() == 1);
  CHECK(ended->front().endReason == ContactEndReason::Reconfigured);
  CHECK(ended->front().second.userTag == 222);
  REQUIRE(fixture.world.step(1.0 / 60));
  auto began = fixture.world.drainContactEvents();
  REQUIRE(began);
  REQUIRE_FALSE(began->empty());
  CHECK(began->front().phase == ContactPhase::Begin);
  CHECK(began->front().second.userTag == 333);
  CHECK(began->front().second.collider == ColliderId{fixture.ball});
}

TEST_CASE("contacts.pose_edit_preflight_is_transactional_and_unchanged_pose_keeps_pair") {
  auto fixture = pair();
  REQUIRE(fixture.world.step(1.0 / 60));
  const auto state = fixture.world.body(fixture.ball).value();
  CHECK_FALSE(fixture.world.setPose(fixture.ball, {std::numeric_limits<float>::infinity(), 0, 0}));
  REQUIRE(fixture.world.setPose(fixture.ball, state.position, state.rotation));
  auto events = fixture.world.drainContactEvents();
  REQUIRE(events);
  CHECK(events->size() == 1);
  REQUIRE(fixture.world.setPose(fixture.ball, {0, 3, 0}));
  events = fixture.world.drainContactEvents();
  REQUIRE(events);
  REQUIRE(events->size() == 1);
  CHECK(events->front().phase == ContactPhase::End);
  CHECK(events->front().endReason == ContactEndReason::PoseChanged);
  CHECK(fixture.world.stats()->contactPairs == 0);
}

TEST_CASE("contacts.full_queue_rejects_lifecycle_changes_before_mutation") {
  auto fixture = pair(1);
  REQUIRE(fixture.world.step(1.0 / 60));
  CHECK_FALSE(fixture.world.destroyBody(fixture.ball));
  CHECK_FALSE(fixture.world.setPose(fixture.ball, {0, 3, 0}));
  const BodyChange change{fixture.ball, BodyConfig{}};
  CHECK_FALSE(fixture.world.applyChanges(std::span{&change, 1}));
  CHECK(fixture.world.stats()->bodies == 2);
  CHECK_FALSE(fixture.world.stats()->faulted);
  REQUIRE(fixture.world.drainContactEvents());
  REQUIRE(fixture.world.destroyBody(fixture.ball));
  auto events = fixture.world.drainContactEvents();
  REQUIRE(events);
  REQUIRE(events->size() == 1);
  CHECK(events->front().endReason == ContactEndReason::Destroyed);
}

TEST_CASE("contacts.queue_overflow_after_step_faults_and_rejects_incomplete_stream") {
  auto fixture = pair(1);
  REQUIRE(fixture.world.step(1.0 / 60));
  auto failed = fixture.world.step(1.0 / 60);
  REQUIRE_FALSE(failed);
  CHECK(failed.error().code == Dot::Core::ErrorCode::Unavailable);
  CHECK(fixture.world.stats()->faulted);
  CHECK_FALSE(fixture.world.drainContactEvents());
  REQUIRE(fixture.world.destroyBody(fixture.ball));
  CHECK(fixture.world.stats()->contactPairs == 0);
}

TEST_CASE("contacts.sensor_reports_begin_and_separation_without_response") {
  auto fixture = pair(65536, 0, true);
  int begin = 0;
  int end = 0;

  for (int i = 0; i < 90; ++i) {
    REQUIRE(fixture.world.step(1.0 / 60));
    auto events = fixture.world.drainContactEvents();
    REQUIRE(events);

    for (const auto& event : *events) {
      CHECK(event.sensor);
      begin += event.phase == ContactPhase::Begin;
      end += event.phase == ContactPhase::End;
    }
  }

  CHECK(begin == 1);
  CHECK(end == 1);
  CHECK(fixture.world.body(fixture.ball)->position.y < -5);
}

TEST_CASE("contacts.worker_capture_pair_order_and_owned_stream_across_move") {
  auto fixture = pair(65536, 2);
  REQUIRE(fixture.world.step(1.0 / 60));
  bool rejected = false;
  std::thread worker([&] { rejected = !fixture.world.drainContactEvents(); });
  worker.join();
  CHECK(rejected);
  World moved = std::move(fixture.world);
  CHECK_FALSE(fixture.world.drainContactEvents());
  auto events = moved.drainContactEvents();
  REQUIRE(events);
  REQUIRE(events->size() == 1);
  CHECK(std::get<BodyId>(events->front().first.collider).value
        < std::get<BodyId>(events->front().second.collider).value);
  CHECK(events->front().first.userTag == 111);
  REQUIRE(moved.step(1.0 / 60));
  CHECK(std::get<Dot::Physics::ContactManifold>(*events->front().geometry).points.size() > 0);
}

TEST_CASE("contacts.native_id_churn_cannot_alias_public_pair_identity") {
  auto fixture = pair();
  REQUIRE(fixture.world.step(1.0 / 60));
  REQUIRE(fixture.world.drainContactEvents());
  REQUIRE(fixture.world.destroyBody(fixture.ball));
  REQUIRE(fixture.world.drainContactEvents());

  for (int i = 0; i < 300; ++i) {
    auto body = fixture.world.createBody();
    REQUIRE(body);
    REQUIRE(fixture.world.destroyBody(*body));
  }

  BodyConfig config;
  config.position = {0, 0.48F, 0};
  config.userTag = 999;
  auto replacement = fixture.world.createBody(config);
  REQUIRE(replacement);
  REQUIRE(fixture.world.step(1.0 / 60));
  auto events = fixture.world.drainContactEvents();
  REQUIRE(events);
  REQUIRE_FALSE(events->empty());
  CHECK(events->front().phase == ContactPhase::Begin);
  CHECK(events->front().second.collider == ColliderId{*replacement});
  CHECK(events->front().second.userTag == 999);
  CHECK(events->front().second.collider != ColliderId{fixture.ball});
}

TEST_CASE("contacts.invalid_capacity_and_invalid_step_preserve_queued_events") {
  WorldConfig config;
  config.maxContactEvents = 0;
  CHECK_FALSE(World::create(config));
  config.maxContactEvents = 262145;
  CHECK_FALSE(World::create(config));
  auto fixture = pair();
  REQUIRE(fixture.world.step(1.0 / 60));
  CHECK_FALSE(fixture.world.step(0));
  auto events = fixture.world.drainContactEvents();
  REQUIRE(events);
  REQUIRE(events->size() == 1);
  CHECK(events->front().step == 1);
}

TEST_CASE("contacts.public_pair_order_controls_normal_and_point_orientation") {
  auto world = World::create();
  REQUIRE(world);
  auto dummy = world->createBody();
  BodyConfig moving;
  moving.position = {0, 0.48F, 0};
  moving.userTag = 22;
  auto ball = world->createBody(moving);
  REQUIRE(dummy);
  REQUIRE(ball);
  REQUIRE(world->destroyBody(*dummy));
  BodyConfig fixed;
  fixed.motion = Motion::Static;
  fixed.shape = Shape::Box;
  fixed.position = {0, -0.5F, 0};
  fixed.userTag = 33;
  auto floor = world->createBody(fixed);
  REQUIRE(floor);
  REQUIRE(world->step(1.0 / 60));
  auto events = world->drainContactEvents();
  REQUIRE(events);
  REQUIRE(events->size() == 1);
  CHECK(events->front().first.collider == ColliderId{*ball});
  CHECK(events->front().second.collider == ColliderId{*floor});
  REQUIRE(events->front().geometry);
  CHECK(std::get<Dot::Physics::ContactManifold>(*events->front().geometry).normal.y
        == doctest::Approx(-1));
  CHECK(events->front().first.userTag == 22);
  CHECK(events->front().second.userTag == 33);
  REQUIRE_FALSE(std::get<Dot::Physics::ContactManifold>(*events->front().geometry).points.empty());
  const auto point
      = std::get<Dot::Physics::ContactManifold>(*events->front().geometry).points.front();
  CHECK(point.first.y <= point.second.y);
}

TEST_CASE("contacts.worker_pair_order_is_deterministic_with_multiple_contacts") {
  auto fixture = pair(65536, 2);
  BodyConfig config;
  config.position = {3, 0.48F, 0};
  auto other = fixture.world.createBody(config);
  REQUIRE(other);
  REQUIRE(fixture.world.step(1.0 / 60));
  auto events = fixture.world.drainContactEvents();
  REQUIRE(events);
  REQUIRE(events->size() == 2);
  CHECK((*events)[0].second.collider == ColliderId{fixture.ball});
  CHECK((*events)[1].second.collider == ColliderId{*other});
  CHECK(fixture.world.stats()->contactPairs == 2);
}

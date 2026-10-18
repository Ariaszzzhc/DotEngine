#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <future>
#include <limits>
#include <stop_token>
#include <utility>
#include <vector>

import Dot.Navigation.Surface;
import Dot.Navigation.Movement;
import Dot.Math;
import Dot.Core.Result;

namespace {
  using namespace Dot::Navigation;
  using Dot::Math::Vec3;

  auto floor(InputGeometry& geometry, float left, float right, float low, float high, float y = 0)
      -> void {
    const auto base = static_cast<std::uint32_t>(geometry.vertices.size());
    geometry.vertices.insert(geometry.vertices.end(),
                             {{left, y, low}, {right, y, low}, {right, y, high}, {left, y, high}});

    for (const auto index : {0U, 2U, 1U, 0U, 3U, 2U}) {
      geometry.indices.push_back(base + index);
    }
  }

  auto box(InputGeometry& geometry, Vec3 minimum, Vec3 maximum) -> void {
    const auto base = static_cast<std::uint32_t>(geometry.vertices.size());
    geometry.vertices.insert(geometry.vertices.end(), {{minimum.x, minimum.y, minimum.z},
                                                       {maximum.x, minimum.y, minimum.z},
                                                       {maximum.x, minimum.y, maximum.z},
                                                       {minimum.x, minimum.y, maximum.z},
                                                       {minimum.x, maximum.y, minimum.z},
                                                       {maximum.x, maximum.y, minimum.z},
                                                       {maximum.x, maximum.y, maximum.z},
                                                       {minimum.x, maximum.y, maximum.z}});

    for (const auto index :
         {0U, 1U, 2U, 0U, 2U, 3U, 4U, 6U, 5U, 4U, 7U, 6U, 0U, 4U, 5U, 0U, 5U, 1U,
          1U, 5U, 6U, 1U, 6U, 2U, 2U, 6U, 7U, 2U, 7U, 3U, 3U, 7U, 4U, 3U, 4U, 0U}) {
      geometry.indices.push_back(base + index);
    }
  }

  auto room(bool obstacle = true) -> InputGeometry {
    InputGeometry result;
    floor(result, -6, 6, -6, 6);

    if (obstacle) {
      box(result, {-.75F, 0, -.75F}, {.75F, 3, .75F});
    }

    return result;
  }

  auto built(const InputGeometry& geometry, BuildSettings settings = {}) -> Surface {
    auto result = Surface::build(geometry, settings);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return std::move(*result);
  }
}  // namespace

TEST_CASE("navigation.surface_routes_around_actual_obstacle_and_across_tile_seams") {
  BuildSettings settings;
  settings.tileCells = 32;
  const auto surface = built(room(), settings);
  CHECK(surface.stats()->tiles > 1);
  const auto result = surface.route({{-4, 0, 0}, {4, 0, 0}});
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->start.x == doctest::Approx(-4));
  CHECK(result->goal.x == doctest::Approx(4));
  CHECK(result->points.size() >= 3);
  CHECK(std::ranges::any_of(result->points, [](Vec3 point) { return std::abs(point.z) > 1; }));
  CHECK_FALSE(surface.route({{0, 0, 0}, {4, 0, 0}}));
  CHECK_FALSE(surface.route({{-4, 0, 0}, {4, 0, 0}, {.5F, .5F, .5F}, 2}));
  CHECK_FALSE(surface.route({{-40, 0, 0}, {4, 0, 0}}));
}

TEST_CASE("navigation.disconnected_islands_and_stacked_floors_never_claim_complete_routes") {
  InputGeometry islands;
  floor(islands, -5, -1, -4, 4);
  floor(islands, 1, 5, -4, 4);
  const auto separated = built(islands);
  CHECK_FALSE(separated.route({{-3, 0, 0}, {3, 0, 0}}));
  REQUIRE(separated.route({{-3, 0, -2}, {-3, 0, 2}}));
  InputGeometry stacked;
  floor(stacked, -5, 5, -5, 5);
  floor(stacked, -5, 5, -5, 5, 3);
  const auto levels = built(stacked);
  REQUIRE(levels.route({{-3, 0, 0}, {3, 0, 0}}));
  REQUIRE(levels.route({{-3, 3, 0}, {3, 3, 0}}));
  CHECK_FALSE(levels.route({{-3, 0, 0}, {3, 3, 0}}));
}

TEST_CASE("navigation.agent_clearance_and_input_failures_are_explicit") {
  auto geometry = room(false);
  box(geometry, {-6, .8F, -6}, {6, 1, 6});
  const auto low = built(geometry);
  CHECK_FALSE(low.route({{-3, 0, 0}, {3, 0, 0}, {.2F, .2F, .2F}}));
  auto bad = room();
  bad.indices[0] = 99999;
  CHECK_FALSE(Surface::build(bad, {}));
  bad = room();
  bad.vertices[0].x = std::numeric_limits<float>::infinity();
  CHECK_FALSE(Surface::build(bad, {}));
  bad = room();
  bad.indices[0] = bad.indices[1];
  CHECK_FALSE(Surface::build(bad, {}));
  auto settings = BuildSettings{};
  settings.cellSize = 0;
  CHECK_FALSE(Surface::build(room(), settings));
  settings = {};
  settings.tileCells = 33;
  CHECK_FALSE(Surface::build(room(), settings));
  settings = {};
  settings.maxClimb = settings.agentHeight;
  CHECK_FALSE(Surface::build(room(), settings));
  InputGeometry huge;
  floor(huge, -10000, 10000, -10000, 10000);
  CHECK_FALSE(Surface::build(huge, {}));
  std::stop_source stop;
  stop.request_stop();
  CHECK_FALSE(Surface::build(room(), {}, stop.get_token()));
  REQUIRE(Surface::build(room(), {}));
}

TEST_CASE("navigation.portable_description_rejects_malformed_topology_before_native_loading") {
  const auto surface = built(room());
  const auto original = *surface.description().value();
  auto candidate = original;
  candidate.tiles.front().polygons.front().vertices.front() = 65535;
  CHECK_FALSE(Surface::create(candidate));
  candidate = original;
  candidate.tiles.front().polygons.front().detailTriangles.front()[0] = 255;
  CHECK_FALSE(Surface::create(candidate));
  candidate = original;
  candidate.tiles.push_back(candidate.tiles.front());
  CHECK_FALSE(Surface::create(candidate));
  candidate = original;
  std::ranges::reverse(candidate.tiles.front().polygons.front().vertices);
  CHECK_FALSE(Surface::create(candidate));
  candidate = original;
  candidate.tiles.front().vertices.front()[1] = 8191;
  CHECK_FALSE(Surface::create(candidate));
  candidate = original;
  candidate.source = "invalid";
  CHECK_FALSE(Surface::create(candidate));
  std::stop_source stop;
  stop.request_stop();
  CHECK_FALSE(Surface::create(original, stop.get_token()));
  REQUIRE(Surface::create(original));
  REQUIRE(surface.route({{-4, 0, 0}, {4, 0, 0}}));
}

TEST_CASE("navigation.immutable_surface_owns_geometry_and_queries_use_independent_scratch") {
  auto geometry = room();
  auto surface = built(geometry);
  const auto retained = surface;
  const auto* description = surface.description().value();
  geometry.vertices.clear();
  auto moved = std::move(surface);
  CHECK_FALSE(surface.route({{-4, 0, 0}, {4, 0, 0}}));
  CHECK(description == retained.description().value());
  std::vector<std::future<void>> workers;

  for (int i = 0; i < 4; ++i) {
    workers.push_back(std::async(std::launch::async, [shared = retained] {
      for (int n = 0; n < 8; ++n) {
        REQUIRE(shared.route({{-4, 0, 0}, {4, 0, 0}}));
      }
    }));
  }

  for (auto& worker : workers) {
    worker.get();
  }

  REQUIRE(moved.route({{-4, 0, 0}, {4, 0, 0}}));
  CHECK_FALSE(Surface{}.stats());
}

TEST_CASE("navigation.local_boundaries_and_traces_keep_obstacles_seams_and_floor_identity") {
  BuildSettings settings;
  settings.tileCells = 32;
  const auto surface = built(room(), settings);
  const auto boundary = surface.boundary({-2, 0, 0}, 3);
  REQUIRE_MESSAGE(boundary, (boundary ? "" : boundary.error().message));
  CHECK_FALSE(boundary->walls.empty());
  const auto blocked = surface.trace({-4, 0, 0}, {4, 0, 0});
  REQUIRE_MESSAGE(blocked, (blocked ? "" : blocked.error().message));
  CHECK_FALSE(blocked->reached);
  CHECK(blocked->position.x < -.75F);
  const auto clear = surface.trace({-4, 0, 3}, {4, 0, 3});
  REQUIRE(clear);
  CHECK(clear->reached);
  CHECK(clear->position.x == doctest::Approx(4));
  InputGeometry floors;
  floor(floors, -5, 5, -5, 5);
  floor(floors, -5, 5, -5, 5, 3);
  const auto stacked = built(floors);
  REQUIRE(stacked.trace({-3, 0, 0}, {3, 3, 0}));
  CHECK_FALSE(stacked.trace({-3, 0, 0}, {3, 3, 0})->reached);
  CHECK_FALSE(surface.boundary({-2, 0, 0}, 0));
  CHECK_FALSE(surface.boundary({200, 0, 0}, 1));
  CHECK_FALSE(surface.trace({200, 0, 0}, {}));
  CHECK_FALSE(Surface{}.trace({}, {}));
  CHECK_FALSE(Surface{}.boundary({}, 1));
}

TEST_CASE("navigation.movement_reports_measured_arrival_and_retains_goals_on_request_failure") {
  MovementBody observed{{1, {-4, 0, -3}, {}, {}, .35F, 1.8F}, true};
  auto movement = Movement::create(built(room())).value();
  CHECK_FALSE(movement.state(observed)->has_value());
  REQUIRE(movement.move(observed, {{4, 0, -3}}));
  const auto initial = observed.body.position;
  auto intent = movement.advance({&observed, 1}, 1.0 / 60);
  REQUIRE(intent);
  CHECK(intent->size() == 1);
  CHECK(intent->front().velocity.x > 0);
  CHECK(observed.body.position == initial);
  CHECK_FALSE(movement.move(observed, {{100, 0, -3}}));
  CHECK(movement.state(observed)->value().request.goal.x == 4);
  CHECK(movement.state(observed)->value().phase == MovePhase::Moving);

  for (int i = 0; i < 60; ++i) {
    REQUIRE(movement.advance({&observed, 1}, 1.0 / 60));
  }

  CHECK(movement.state(observed)->value().blocked);
  CHECK(movement.state(observed)->value().phase == MovePhase::Moving);
  observed.body.position = {4, 0, -3};
  observed.grounded = false;
  CHECK(movement.state(observed)->value().phase == MovePhase::Moving);
  observed.grounded = true;
  CHECK(movement.state(observed)->value().phase == MovePhase::Arrived);
  REQUIRE(movement.advance({&observed, 1}, 1.0 / 60));
  CHECK(movement.state(observed)->value().velocity == Vec3{});
  REQUIRE(movement.stop(1));
  REQUIRE(movement.stop(1));
  CHECK_FALSE(movement.state(observed)->has_value());
}

TEST_CASE("navigation.movement_owns_state_and_snapshot_validation_is_atomic") {
  MovementBody observed{{1, {-4, 0, -3}, {}, {}, .35F, 1.8F}, true};
  auto movement = Movement::create(built(room())).value();
  REQUIRE(movement.move(observed, {{4, 0, -3}}));
  CHECK_FALSE(movement.advance({&observed, 1}, 0));
  CHECK_FALSE(movement.advance({&observed, 1}, 1.001));
  REQUIRE(movement.advance({&observed, 1}, 1));
  const std::array duplicate{observed, observed};
  CHECK_FALSE(movement.advance(duplicate, 1.0 / 60));
  CHECK(movement.entities()->size() == 1);
  auto retained = std::move(movement);
  CHECK_FALSE(movement.state(observed));
  REQUIRE(retained.advance({&observed, 1}, 1.0 / 60));
  const auto saved = retained.state(observed)->value();
  observed.body.position = {4, 0, 3};
  REQUIRE(retained.advance({&observed, 1}, 1.0 / 60));
  CHECK(retained.state(observed)->value().phase == MovePhase::Moving);
  CHECK(saved.request.goal == Vec3{4, 0, -3});
  REQUIRE(retained.fail(
      1, Dot::Core::fail(Dot::Core::ErrorCode::Unavailable, "Blocked source").error()));
  REQUIRE(retained.advance({&observed, 1}, 1.0 / 60));
  CHECK(retained.state(observed)->value().phase == MovePhase::Failed);
  CHECK(retained.state(observed)->value().velocity == Vec3{});
  REQUIRE(retained.advance({}, 1.0 / 60));
  CHECK(retained.entities()->empty());
  InputGeometry stacked;
  floor(stacked, -5, 5, -5, 5);
  floor(stacked, -5, 5, -5, 5, 3);
  auto floors = Movement::create(built(stacked)).value();
  observed.body.position = {-3, 0, 0};
  REQUIRE(floors.move(observed, {{3, 0, 0}}));
  observed.body.position.y = 3;
  REQUIRE(floors.advance({&observed, 1}, 1.0 / 60));
  CHECK(floors.state(observed)->value().phase == MovePhase::Failed);
  CHECK(floors.state(observed)->value().velocity == Vec3{});
}

TEST_CASE("navigation.movement_plans_all_agents_before_publication_in_identity_order") {
  const auto surface = built(room(false));
  auto forward = Movement::create(surface).value();
  auto reverse = Movement::create(surface).value();
  std::array observations{MovementBody{{1, {-1, 0, -3}, {}, {}, .35F, 1.8F}, true},
                          MovementBody{{2, {1, 0, -3}, {}, {}, .35F, 1.8F}, true}};
  const std::array requests{MoveRequest{{3, 0, -3}}, MoveRequest{{-3, 0, -3}}};

  for (std::size_t index = 0; index < 2; ++index) {
    REQUIRE(forward.move(observations[index], requests[index]));
    REQUIRE(reverse.move(observations[1 - index], requests[1 - index]));
  }

  const auto first = forward.advance(observations, 1.0 / 60).value();
  std::ranges::reverse(observations);
  const auto second = reverse.advance(observations, 1.0 / 60).value();
  REQUIRE(first.size() == 2);
  REQUIRE(second.size() == 2);
  CHECK(first[0].entity == 1);
  CHECK(first[1].entity == 2);
  CHECK(first[0].velocity == second[0].velocity);
  CHECK(first[1].velocity == second[1].velocity);
}

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
import Dot.Math;

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

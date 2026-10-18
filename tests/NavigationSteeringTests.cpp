#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <future>
#include <limits>
#include <vector>

import Dot.Navigation.Steering;
import Dot.Navigation.Surface;
import Dot.Math;

namespace {
  using namespace Dot::Navigation;
  using Dot::Math::Vec3;

  auto body(std::uint64_t id, Vec3 position, Vec3 velocity = {}, Vec3 desired = {})
      -> SteeringBody {
    return {id, position, velocity, desired, .35F, 1.8F};
  }
}  // namespace

TEST_CASE("navigation_steering.measured_dynamic_circle_changes_velocity_without_moving_input") {
  auto self = body(1, {-1.5F, 0, 0}, {1, 0, 0}, {1, 0, 0});
  std::vector<SteeringBody> others{body(2, {0, 0, 0})};
  const auto original = self.position;
  const auto result = steer(self, others, {}, {1.2F});
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->neighbors == 1);
  CHECK(result->samples > 0);
  CHECK(Dot::Math::length(result->velocity) <= 1.2001);
  CHECK((std::abs(result->velocity.z) > .05F || result->velocity.x < .8F));
  CHECK(self.position == original);
  others[0].position.y = 3;
  const auto upper = steer(self, others, {}, {1.2F});
  REQUIRE(upper);
  CHECK(upper->neighbors == 0);
  others[0].position = {20, 0, 0};
  CHECK(steer(self, others, {}, {1.2F})->neighbors == 0);
  self.position = {};
  others[0].position = {.65F, 0, 0};
  const auto touching = steer(self, others, {}, {1.2F, .5F});
  REQUIRE(touching);
  CHECK(touching->neighbors == 1);
}

TEST_CASE("navigation_steering.reciprocal_head_on_agents_choose_lateral_or_slowing_motion") {
  const std::vector<SteeringBody> agents{body(1, {-1.5F, 0, 0}, {1, 0, 0}, {1, 0, 0}),
                                         body(2, {1.5F, 0, 0}, {-1, 0, 0}, {-1, 0, 0})};
  const auto a = steer(agents[0], agents, {}, {1.2F});
  const auto b = steer(agents[1], agents, {}, {1.2F});
  REQUIRE(a);
  REQUIRE(b);
  CHECK(a->neighbors == 1);
  CHECK(b->neighbors == 1);
  CHECK((std::abs(a->velocity.z) > .02F || a->velocity.x < .8F));
  CHECK((std::abs(b->velocity.z) > .02F || b->velocity.x > -.8F));
}

TEST_CASE("navigation_steering.boundaries_invalid_values_capacity_and_lifetime_are_explicit") {
  auto self = body(1, {}, {1, 0, 0}, {1, 0, 0});
  const std::vector<WallSegment> walls{{{.3F, 0, -3}, {.3F, 0, 3}}};
  const auto result = steer(self, {}, walls, {1.2F});
  REQUIRE(result);
  CHECK(result->velocity.x < .9F);
  auto bad = self;
  bad.position.x = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(steer(bad, {}, {}));
  bad = self;
  bad.velocity.y = 1;
  CHECK_FALSE(steer(bad, {}, {}));
  CHECK_FALSE(steer(self, {&bad, 1}, {}));
  std::vector<SteeringBody> duplicates{body(2, {2, 0, 0}), body(2, {3, 0, 0})};
  CHECK_FALSE(steer(self, duplicates, {}));
  std::vector<SteeringBody> crowded;

  for (std::uint64_t index = 0; index < 65; ++index) {
    crowded.push_back(body(index + 2, {1, 0, 1}));
  }

  CHECK_FALSE(steer(self, crowded, {}));
  const std::vector<WallSegment> invalid{{{}, {}}};
  CHECK_FALSE(steer(self, {}, invalid));
  CHECK_FALSE(steer(self, {}, {}, {0}));
  REQUIRE(steer(self, {}, {}, {1.2F}));
}

TEST_CASE("navigation_steering.parallel_queries_own_their_vendor_scratch") {
  auto task = [] {
    const auto self = body(1, {-1, 0, 0}, {}, {1, 0, 0});
    const std::vector<SteeringBody> others{body(2, {1, 0, 0})};
    const auto expected = steer(self, others, {}, {1.2F}).value().velocity;

    for (int index = 0; index < 8; ++index) {
      REQUIRE(steer(self, others, {}, {1.2F})->velocity == expected);
    }

    return expected;
  };
  auto first = std::async(std::launch::async, task);
  auto second = std::async(std::launch::async, task);
  CHECK(first.get() == second.get());
}

TEST_CASE("navigation_steering.vendor_candidate_tolerance_publishes_exact_speed_caps") {
  using namespace Dot::Navigation;

  const SteeringBody measured{
      1, {}, {-.21269998F, 0, -1.21290004F}, {.401105464F, 0, -1.1097362F}, .35F, 1.83F};
  const SteeringBody neighbor{
      2, {-.130800068F, 0, -1.82850003F}, {.576300025F, 0, .260400057F}, {}, .35F, 1.83F};
  const WallSegment wall{{2, 0, -3}, {2, 0, 3}};
  const auto turning = steer(measured, {&neighbor, 1}, {&wall, 1}, {1.18F});
  REQUIRE_MESSAGE(turning, (turning ? "" : turning.error().message));
  CHECK(Dot::Math::length(turning->velocity) <= 1.18F + 1e-6F);

  for (const auto speed : {1.1F, 1.18F, 1.26F, 1.5F}) {
    for (int step = 0; step < 96; ++step) {
      const auto angle = float(step) * .06544984695F;
      const Dot::Math::Vec3 wanted{std::sin(angle) * speed, 0, std::cos(angle) * speed};
      const SteeringBody body{1, {}, wanted * .7F, wanted, .35F, 1.83F};
      std::array others{
          SteeringBody{2, {.9F, 0, .7F}, {}, {}, .35F, 1.83F},
          SteeringBody{3, {-.8F, 0, -.9F}, wanted * -.5F, wanted * -.5F, .35F, 1.83F}};
      const auto sampled = steer(body, others, {}, {speed});
      REQUIRE_MESSAGE(sampled, (sampled ? "" : sampled.error().message));
      CHECK(Dot::Math::length(sampled->velocity) <= speed + 1e-6F);
      std::ranges::reverse(others);
      const auto reordered = steer(body, others, {}, {speed});
      REQUIRE(reordered);
      CHECK(sampled->velocity == reordered->velocity);
    }
  }
}

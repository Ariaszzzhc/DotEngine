#include <doctest/doctest.h>

#include <limits>

import Dot.Graphics.Lighting;
import Dot.Math;
import Dot.Core.Result;

using namespace Dot::Graphics;

TEST_CASE("lighting.validated_owned_frames_normalize_ray_directions") {
  Lighting source;
  source.viewPosition = {1, 2, 3};
  source.lights = {{.direction = {0, 0, -4}},
                   {.kind = LightKind::Point, .position = {2, 3, 4}},
                   {.kind = LightKind::Spot, .direction = {0, -2, 0}}};
  auto result = validatedLighting(source);
  REQUIRE(result);
  CHECK(result->lights[0].direction == Dot::Math::Vec3{0, 0, -1});
  CHECK(result->lights[2].direction == Dot::Math::Vec3{0, -1, 0});
  CHECK(source.lights[0].direction.z == -4);
  source.lights.clear();
  CHECK(result->lights.size() == 3);
  CHECK(validatedLighting({}));
  source.viewDirection = Dot::Math::Vec3{0, 0, -3};
  CHECK(validatedLighting(source)->viewDirection == Dot::Math::Vec3{0, 0, -1});
  source.viewDirection = Dot::Math::Vec3{};
  CHECK_FALSE(validatedLighting(source));
}

TEST_CASE("lighting.malformed_counts_positions_rays_and_properties_fail") {
  Lighting source;
  source.lights.resize(64);
  CHECK(validatedLighting(source));
  source.lights.emplace_back();
  CHECK_FALSE(validatedLighting(source));
  source.lights.resize(1);
  source.viewPosition.x = 1'000'001;
  CHECK_FALSE(validatedLighting(source));
  source.viewPosition = {};

  for (const auto& invalid :
       {Light{.kind = static_cast<LightKind>(99)}, Light{.direction = {}},
        Light{.position = {0, 1'000'001, 0}}, Light{.color = {-1, 1, 1}}, Light{.intensity = -1},
        Light{.intensity = 1'000'001}, Light{.range = 0},
        Light{.innerConeDegrees = 30, .outerConeDegrees = 30}, Light{.outerConeDegrees = 91}}) {
    source.lights[0] = invalid;
    CHECK_FALSE(validatedLighting(source));
  }

  source.lights[0] = {};
  source.lights[0].intensity = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(validatedLighting(source));
  source.lights[0] = {};
  source.lights[0].direction.x = std::numeric_limits<float>::infinity();
  CHECK_FALSE(validatedLighting(source));
}

TEST_CASE("lighting.shadow_source_type_count_and_settings_validate_transactionally") {
  Lighting lighting;
  Light light;
  light.shadows = ShadowSettings{};
  lighting.lights = {light};
  REQUIRE(validatedLighting(lighting));
  lighting.lights[0].kind = LightKind::Spot;
  CHECK_FALSE(validatedLighting(lighting));
  lighting.lights[0].kind = LightKind::Point;
  CHECK_FALSE(validatedLighting(lighting));
  lighting.lights[0] = light;
  lighting.lights.push_back(light);
  CHECK_FALSE(validatedLighting(lighting));
  lighting.lights.pop_back();
  lighting.lights[0].shadows->resolution = 129;
  CHECK_FALSE(validatedLighting(lighting));
  lighting.lights[0].shadows.reset();
  CHECK(validatedLighting(lighting));
}

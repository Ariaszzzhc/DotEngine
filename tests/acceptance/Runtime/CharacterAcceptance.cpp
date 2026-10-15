#include <algorithm>
#include <cmath>
#include <expected>
#include <format>
#include <print>

import Dot.Core.Result;
import Dot.Physics.World;

namespace Dot::Runtime {
  namespace {
    auto runCharacter() -> Core::Result<void> {
      auto world = Physics::World::create();

      if (!world) {
        return std::unexpected(world.error());
      }

      Physics::BodyConfig floor;
      floor.motion = Physics::Motion::Static;
      floor.shape = Physics::Shape::Box;
      floor.position = {0, -.5F, 0};
      floor.halfExtents = {10, .5F, 10};
      auto wall = floor;
      wall.position = {3, 2, 0};
      wall.halfExtents = {.2F, 2, 10};
      auto support = world->createBody(floor);
      auto obstruction = world->createBody(wall);

      if (!support || !obstruction) {
        return Core::fail(Core::ErrorCode::External, "Character acceptance body setup failed");
      }

      Physics::CharacterConfig config;
      config.position = {0, 3, 0};
      auto character = world->createCharacter(config);

      if (!character) {
        return std::unexpected(character.error());
      }

      for (int frame = 0; frame < 120; ++frame) {
        auto advanced = world->step(1.0 / 60);

        if (!advanced) {
          return advanced;
        }
      }

      auto landed = world->character(*character);

      if (!landed || landed->ground != Physics::GroundState::Grounded
          || landed->groundBody != *support || std::abs(landed->position.y) > .04F) {
        return Core::fail(Core::ErrorCode::External, "Character acceptance did not land on floor");
      }

      auto requested = world->setCharacterInput(*character, {{3, 0, 0}, true});

      if (!requested) {
        return requested;
      }

      float peak{};

      for (int frame = 0; frame < 180; ++frame) {
        auto advanced = world->step(1.0 / 60);

        if (!advanced) {
          return advanced;
        }

        auto state = world->character(*character);

        if (!state) {
          return std::unexpected(state.error());
        }

        peak = std::max(peak, state->position.y);
      }

      auto final = world->character(*character);

      if (!final || final->ground != Physics::GroundState::Grounded || peak < 1.7F || peak > 2.05F
          || final->position.x < 2.2F || final->position.x > 2.5F
          || std::abs(final->position.y) > .04F) {
        return Core::fail(
            Core::ErrorCode::External,
            std::format("Character acceptance jump/wall/landing failed: peak={}", peak));
      }

      auto destroyed = world->destroyCharacter(*character);

      if (!destroyed || world->character(*character) || world->stats()->characters != 0) {
        return Core::fail(Core::ErrorCode::External,
                          "Character acceptance ownership/stale handle check failed");
      }

      std::println(
          "Character acceptance passed: jump peak={:.3f}, wall stop={:.3f}, landed, "
          "owned capsule released",
          peak, final->position.x);
      return {};
    }
  }  // namespace

  auto testCharacter() -> int {
    auto result = runCharacter();

    if (!result) {
      std::println(stderr, "{}", result.error().message);
      return 1;
    }

    return 0;
  }
}  // namespace Dot::Runtime

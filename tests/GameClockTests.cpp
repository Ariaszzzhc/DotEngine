#include <doctest/doctest.h>

#include <string>
#include <thread>
#include <utility>

import Dot.Script.GameClock;

TEST_CASE("game_clock.strict_pause_noops_and_bindings_preserve_owned_state") {
  using namespace Dot::Script;
  auto clock = GameClock::create().value();
  CHECK_FALSE(clock.snapshot()->paused);
  CHECK(clock.snapshot()->revision == 1);
  REQUIRE(clock.perform(R"({"command":"set-paused","paused":false})"));
  CHECK(clock.snapshot()->revision == 1);
  auto bindings = clock.bindings().value();
  CHECK(bindings[0].access == HostAccess::ReadOnly);
  CHECK(bindings[1].access == HostAccess::ReadWrite);
  REQUIRE(bindings[1].callback("[true]"));
  CHECK(clock.snapshot()->paused);
  CHECK(clock.snapshot()->revision == 2);
  const auto before = clock.perform(R"({"command":"snapshot"})").value();

  for (const auto* request :
       {R"({"command":"snapshot","extra":1})", R"({"command":"set-paused","paused":0})",
        R"({"command":"set-paused","paused":true,"paused":false})", R"({"command":"set-paused"})",
        R"({"command":"unknown"})"}) {
    CHECK_FALSE(clock.perform(request));
    CHECK(clock.perform(R"({"command":"snapshot"})").value() == before);
  }

  CHECK_FALSE(clock.perform(std::string(1025, ' ')));
  auto boundary = std::string(R"({"command":"snapshot"})");
  boundary.resize(1024, ' ');
  REQUIRE(clock.perform(boundary));
  boundary.push_back(' ');
  CHECK_FALSE(clock.perform(boundary));
  CHECK_FALSE(bindings[1].callback("[]"));
  CHECK_FALSE(bindings[1].callback("[false,false]"));
  CHECK_FALSE(bindings[0].callback("[false]"));
  auto trailing = std::string(R"({"command":"set-paused","paused":false})");
  trailing.push_back('\0');
  trailing += "ignored";
  REQUIRE_FALSE(clock.perform(trailing));
  CHECK(clock.perform(R"({"command":"snapshot"})").value() == before);
  const std::string trailingArgs("[false]\0ignored", 15);
  REQUIRE_FALSE(bindings[1].callback(trailingArgs));
  CHECK(clock.perform(R"({"command":"snapshot"})").value() == before);
  REQUIRE(bindings[1].callback("[true]"));
  CHECK(clock.snapshot()->revision == 2);
  auto detached = clock.snapshot().value();
  detached.paused = false;
  CHECK_FALSE(detached.paused);
  CHECK(clock.snapshot()->paused);
}

TEST_CASE("game_clock.moves_retirement_and_thread_affinity_keep_final_observations") {
  auto clock = Dot::Script::GameClock::create().value();
  auto copy = clock;
  auto bindings = clock.bindings().value();
  auto moved = std::move(clock);
  CHECK_FALSE(clock.snapshot());
  CHECK_FALSE(clock.perform(R"({"command":"snapshot"})"));
  bool denied{};
  std::thread worker([&] {
    denied = !moved.snapshot() && !moved.perform(R"({"command":"set-paused","paused":true})");
  });
  worker.join();
  CHECK(denied);
  REQUIRE(moved.perform(R"({"command":"set-paused","paused":true})"));
  copy.retire();
  moved.retire();
  CHECK(copy.snapshot()->paused);
  REQUIRE(bindings[0].callback("[]"));
  CHECK_FALSE(bindings[1].callback("[false]"));
  CHECK_FALSE(moved.perform(R"({"command":"set-paused","paused":true})"));
  auto fresh = Dot::Script::GameClock::create().value();
  CHECK_FALSE(fresh.snapshot()->paused);
  CHECK(fresh.snapshot()->revision == 1);
}

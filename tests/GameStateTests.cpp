#include <doctest/doctest.h>

#include <future>
#include <limits>
#include <string>
#include <utility>

import Dot.Game.State;
import Dot.Core.Result;

namespace {
  using namespace Dot::Game;
}  // namespace

TEST_CASE("game_state.owned_values_semantic_revision_and_compare_replace") {
  auto state = State::create().value();
  CHECK(state.snapshot()->revision == 1);
  CHECK(state.snapshot()->data == "{}");
  CHECK_FALSE(state.get("missing").value().has_value());
  REQUIRE(state.set("profile/health", "null"));
  CHECK(state.get("profile/health").value().value() == "null");
  CHECK_FALSE(state.get("profile").value().has_value());
  auto version = state.set("玩家", R"({"health":100,"bag":["晶体",true,3.5]})").value();
  auto before = state.snapshot().value();
  CHECK(before.revision == version);
  CHECK(state.set("玩家", R"({"bag":["晶体",true,3.5],"health":100.0})").value() == version);
  CHECK(state.remove("not-there").value() == version);
  CHECK_FALSE(state.replace("{}", version - 1));
  CHECK(state.snapshot()->data == before.data);
  REQUIRE(state.replace(R"({"level":"scenes/next.dotscene"})", version));
  CHECK(state.snapshot()->revision == version + 1);
  CHECK(state.get("level").value().value() == R"("scenes/next.dotscene")");
  CHECK(before.data.find("玩家") != std::string::npos);
  CHECK(state.remove("level").value() == version + 2);
  CHECK(state.snapshot()->data == "{}");
}

TEST_CASE("game_state.invalid_json_numbers_names_and_depth_never_publish") {
  auto state = State::create(R"({"checkpoint":7})").value();
  const auto before = state.snapshot().value();

  for (const auto* value :
       {"[]", "null", "0", R"({"a":1,"a":2})", R"({"x":{"a":1,"a":2}})",
        R"({"x":9007199254740992})", R"({"x":-9007199254740992})", R"({"x":1e999})", R"({"x":NaN})",
        R"({"x":"\ud800"})", R"({"":1})", R"({"a\u0000b":1})", R"({"x":1} trailing)"}) {
    CHECK_FALSE(state.replace(value, before.revision));
    CHECK(state.snapshot()->data == before.data);
    CHECK(state.snapshot()->revision == before.revision);
  }

  for (const auto& key : {std::string{}, std::string(129, 'x'), std::string("a\0b", 3),
                          std::string("a\nb"), std::string("\xc0\xaf", 2)}) {
    CHECK_FALSE(state.get(key));
    CHECK_FALSE(state.remove(key));
    CHECK_FALSE(state.set(key, "1"));
  }

  CHECK_FALSE(state.set("nested", R"({"dup":1,"dup":2})"));
  std::string deep(40, '[');
  deep += "0";
  deep += std::string(40, ']');
  CHECK_FALSE(state.set("deep", deep));
  CHECK_FALSE(state.replace("{\"deep\":" + deep + "}", before.revision));
  CHECK(state.snapshot()->data == before.data);
  REQUIRE(state.set("safe", "9007199254740991"));
  REQUIRE(state.set("negative", "-9007199254740991"));
  REQUIRE(state.set("large-id", R"("18446744073709551614")"));
  REQUIRE(state.set("unicode-null", R"("\u0000")"));
}

TEST_CASE("game_state.byte_entry_and_encoding_limits_preserve_last_good_revision") {
  auto state = State::create("{}", {16, 1}).value();
  REQUIRE(state.set("x", R"("12345678")"));
  const auto before = state.snapshot().value();
  CHECK(before.data.size() == 16);
  CHECK_FALSE(state.set("y", "0"));
  CHECK_FALSE(state.set("x", R"("123456789")"));
  CHECK(state.snapshot()->revision == before.revision);
  CHECK(state.snapshot()->data == before.data);
  CHECK_FALSE(State::create("{}", {1, 1}));
  CHECK_FALSE(State::create("{}", {1024 * 1024 + 1, 1}));
  CHECK_FALSE(State::create("{}", {16, 0}));
  CHECK_FALSE(State::create("{}", {16, 4097}));
  CHECK_FALSE(normalizeStateData(std::string("{\"x\":\"\xff\"}", 9)));
  REQUIRE(state.replace("{}", before.revision));
  CHECK(state.snapshot()->revision == before.revision + 1);
  CHECK(normalizeStateData(" { \"b\":2,\"a\":1 } ").value() == R"({"a":1,"b":2})");
}

TEST_CASE("game_state.copy_move_thread_and_snapshot_lifetimes_are_explicit") {
  auto original = State::create(R"({"visits":1})").value();
  auto copy = original;
  auto snapshot = original.snapshot().value();
  REQUIRE(copy.set("visits", "2"));
  CHECK(original.get("visits").value().value() == "2");
  CHECK(snapshot.data == R"({"visits":1})");
  auto foreign = std::async(std::launch::async, [&] {
    return !copy.snapshot() && !copy.get("visits") && !copy.set("visits", "3")
           && !copy.remove("visits") && !copy.replace("{}", 2);
  });
  CHECK(foreign.get());
  auto moved = std::move(original);
  CHECK_FALSE(original.snapshot());
  CHECK_FALSE(original.get("visits"));
  CHECK_FALSE(original.set("visits", "4"));
  CHECK_FALSE(original.remove("visits"));
  CHECK_FALSE(original.replace("{}", 2));
  CHECK(moved.snapshot()->revision == 2);
  auto discarded
      = std::async(std::launch::async, [owned = std::move(moved)] { return !owned.snapshot(); });
  CHECK(discarded.get());
  REQUIRE(copy.set("visits", "3"));
  CHECK(copy.snapshot()->revision == 3);
}

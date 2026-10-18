#include <doctest/doctest.h>

#include <algorithm>
#include <future>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

import Dot.Script.GameData;
import Dot.Script.Host;
import Dot.Core.Result;

namespace {
  using Json = nlohmann::json;
  using Dot::Script::GameData;

  auto perform(GameData& data, const Json& request) -> Json {
    auto result = data.perform(request.dump());
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return Json::parse(*result);
  }
}  // namespace

TEST_CASE("game_data.strict_protocol_owned_state_conflicts_and_explicit_missing_storage") {
  auto data = GameData::create().value();
  auto original = perform(data, {{"command", "snapshot"}});
  CHECK(original == Json{{"revision", "1"}, {"data", Json::object()}});
  CHECK(perform(data, {{"command", "get"}, {"key", "missing"}})["present"] == false);
  auto changed = perform(data, {{"command", "set"}, {"key", "玩家"}, {"value", nullptr}});
  CHECK(changed["revision"] == "2");
  CHECK(perform(data, {{"command", "get"}, {"key", "玩家"}})
        == Json{{"present", true}, {"value", nullptr}});
  CHECK_FALSE(data.perform(R"({"command":"replace","revision":"1","data":{}})"));
  CHECK(perform(data, {{"command", "snapshot"}})["revision"] == "2");
  CHECK(perform(data, {{"command", "remove"}, {"key", "玩家"}})["revision"] == "3");
  CHECK(original["data"].empty());

  for (const auto* request :
       {"[]", "{}", R"({"command":"snapshot","extra":1})", R"({"command":"set","key":"x"})",
        R"({"command":"set","key":"x","value":1,"value":2})",
        R"({"command":"set","key":"x","value":9007199254740992})",
        R"({"command":"replace","revision":3,"data":{}})",
        R"({"command":"load","slot":"a","revision":"03"})",
        R"({"command":"save","slot":"a","overwrite":1})", R"({"command":"unknown"})"}) {
    CHECK_FALSE(data.perform(request));
  }

  auto unavailable = data.perform(R"({"command":"list-saves"})");
  REQUIRE_FALSE(unavailable);
  CHECK(unavailable.error().code == Dot::Core::ErrorCode::Unavailable);
  CHECK(perform(data, {{"command", "snapshot"}})["revision"] == "3");
}

TEST_CASE("game_data.shared_moves_threads_and_retirement_keep_only_owned_observations") {
  auto data = GameData::create().value();
  auto shared = data;
  auto moved = std::move(data);
  CHECK_FALSE(data.perform(R"({"command":"snapshot"})"));
  CHECK_FALSE(data.bindings());
  REQUIRE(moved.perform(R"({"command":"set","key":"count","value":2})"));
  CHECK(perform(shared, {{"command", "get"}, {"key", "count"}})["value"] == 2);
  auto foreign = std::async(std::launch::async, [&] {
    return !shared.perform(R"({"command":"snapshot"})") && !shared.bindings();
  });
  CHECK(foreign.get());
  auto bindings = shared.bindings().value();
  REQUIRE(moved.retire());
  CHECK(perform(shared, {{"command", "snapshot"}})["data"]["count"] == 2);
  CHECK_FALSE(shared.perform(R"({"command":"set","key":"count","value":3})"));
  CHECK_FALSE(shared.perform(R"({"command":"list-saves"})"));
  CHECK_FALSE(shared.bindings());
  auto writer = std::ranges::find(bindings, "gameState.set", &Dot::Script::HostFunction::name);
  REQUIRE(writer != bindings.end());
  CHECK_FALSE(writer->callback(R"(["count",4])"));
}

TEST_CASE("game_data.host_transport_and_preparation_access_are_explicit") {
  auto data = GameData::create().value();
  auto bindings = data.bindings().value();
  REQUIRE(bindings.size() == 14);

  for (auto& binding : bindings) {
    CHECK_FALSE(binding.callback("{}"));
    CHECK_FALSE(binding.callback("[true,true,true]"));
    const bool read = binding.name == "gameState.get" || binding.name == "gameState.snapshot"
                      || binding.name == "saves.jobs" || binding.name == "saves.status";
    CHECK((binding.access == Dot::Script::HostAccess::ReadOnly) == read);
  }

  auto setter = std::ranges::find(bindings, "gameState.set", &Dot::Script::HostFunction::name);
  auto reader = std::ranges::find(bindings, "gameState.get", &Dot::Script::HostFunction::name);
  REQUIRE(setter != bindings.end());
  REQUIRE(reader != bindings.end());
  REQUIRE(setter->callback(R"(["value",{"x":[1,true,null]}])"));
  auto response = reader->callback(R"(["value"])").value().value();
  CHECK(Json::parse(response)["value"]["x"] == Json::array({1, true, nullptr}));
  Dot::Script::GameDataConfig limits;
  limits.limits.saves.state.bytes = 2;
  auto bounded = GameData::create(limits).value();
  CHECK_FALSE(bounded.perform(R"({"command":"set","key":"x","value":1})"));
  CHECK(perform(bounded, {{"command", "snapshot"}})["revision"] == "1");
  limits.limits.saves.slots = 0;
  CHECK_FALSE(GameData::create(limits));
}

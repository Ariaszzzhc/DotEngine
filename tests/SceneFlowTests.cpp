#include <doctest/doctest.h>

#include <future>
#include <nlohmann/json.hpp>
#include <optional>
#include <source_location>
#include <string>
#include <utility>

import Dot.Core.Result;
import Dot.Script.SceneFlow;
import Dot.World.Scene;

namespace {
  using namespace Dot::Script;
  using Json = nlohmann::json;

  auto failure() -> Dot::Core::Error {
    return {Dot::Core::ErrorCode::Io, "owned load failure", std::source_location::current()};
  }
}  // namespace

TEST_CASE("scene_flow.queues_at_most_one_request_and_preserves_location_until_commit") {
  auto flow = SceneFlow::create(10, "scenes/start.dotscene").value();
  CHECK_FALSE(flow.pending().value());
  CHECK_FALSE(flow.latest().value());
  CHECK_FALSE(flow.request("scenes/next.dotscene", 11));
  auto id = flow.request("scenes/next.dotscene", 10).value();
  CHECK(flow.pending()->value().id == id);
  CHECK(flow.status(id)->state == SceneRequestState::Pending);
  CHECK_FALSE(flow.request("scenes/other.dotscene", 10));
  CHECK_FALSE(flow.begin(id + 1));
  REQUIRE(flow.begin(id));
  CHECK_FALSE(flow.pending().value());
  CHECK(flow.status(id)->state == SceneRequestState::Loading);
  CHECK(flow.location()->scope == 10);
  CHECK_FALSE(flow.request("scenes/other.dotscene", 10));
  CHECK_FALSE(flow.commit(id, 10, "scenes/next.dotscene"));
  CHECK_FALSE(flow.commit(id, 20, "scenes/other.dotscene"));
  CHECK(flow.location()->path == "scenes/start.dotscene");
  REQUIRE(flow.fail(id, failure()));
  CHECK(flow.status(id)->state == SceneRequestState::Failed);
  CHECK_FALSE(flow.status(id)->committed);
  CHECK(flow.status(id)->error->message == "owned load failure");
  CHECK_FALSE(flow.fail(id, failure()));
  auto next = flow.request("scenes/next.dotscene", 10).value();
  CHECK(next > id);
  REQUIRE(flow.begin(next));
  REQUIRE(flow.commit(next, 20, "scenes/next.dotscene"));
  CHECK(flow.location()->scope == 20);
  CHECK(flow.location()->path == "scenes/next.dotscene");
  CHECK(flow.status(next)->state == SceneRequestState::Succeeded);
  CHECK(flow.status(next)->committed);
  CHECK_FALSE(flow.request("scenes/again.dotscene", 10));
  auto following = flow.request("scenes/again.dotscene", 20).value();
  REQUIRE(flow.fail(next, failure()));
  CHECK(flow.status(next)->committed);
  flow.retire();
  CHECK(flow.status(following)->state == SceneRequestState::Cancelled);
  CHECK_FALSE(flow.request("scenes/again.dotscene", 20));
}

TEST_CASE("scene_flow.host_is_strict_and_bound_reads_keep_their_realm_location") {
  auto flow = SceneFlow::create(1, std::nullopt).value();
  auto old = flow.bindings(1, std::nullopt).value();
  auto candidate = flow.bindings(2, "scenes/Next.dotscene").value();
  REQUIRE(old.size() == 6);
  CHECK(old[0].access == HostAccess::ReadOnly);
  CHECK(old[1].access == HostAccess::ReadWrite);
  CHECK(old[2].access == HostAccess::ReadOnly);
  auto current = Json::parse(old[0].callback("[]").value().value());
  CHECK(current["scope"] == "1");
  CHECK(current["path"].is_null());
  CHECK(Json::parse(candidate[0].callback("[]").value().value())["scope"] == "2");

  for (const auto* args :
       {"{}", "null", "[0]", "[\"x\"]", "[] trailing", "[null]", "[\"x\",\"y\"]"}) {
    CHECK_FALSE(old[0].callback(args));
  }

  for (const auto* args :
       {"[]", "[0]", "[null]", "[true]", "[\"01\"]", "[\"0\"]", "[\"18446744073709551616\"]"}) {
    CHECK_FALSE(old[2].callback(args));
  }

  for (const auto* path :
       {"", "../escape.dotscene", "/root.dotscene", "scenes/a.json", "scenes/CON.dotscene",
        "scenes/.dot-stage-1/x.dotscene", "scenes\\next.dotscene"}) {
    CHECK_FALSE(flow.request(path, 1));
  }

  CHECK_FALSE(old[1].callback("[null]"));
  auto id = Json::parse(old[1].callback("[\"scenes/Next.dotscene\"]").value().value())
                .get<std::string>();
  auto observed = Json::parse(old[2].callback("[\"" + id + "\"]").value().value());
  CHECK(observed["state"] == "pending");
  CHECK_FALSE(observed["committed"].get<bool>());
  const auto number = std::stoull(id);
  REQUIRE(flow.begin(number));
  REQUIRE(flow.commit(number, 2, "scenes/Next.dotscene"));
  CHECK_FALSE(old[1].callback("[\"scenes/Again.dotscene\"]"));
  CHECK(Json::parse(old[0].callback("[]").value().value())["path"].is_null());
  CHECK(Json::parse(candidate[0].callback("[]").value().value())["path"] == "scenes/Next.dotscene");
  CHECK(Json::parse(old[2].callback("[\"" + id + "\"]").value().value())["state"] == "succeeded");
  flow.retire();
  CHECK_FALSE(candidate[1].callback("[\"scenes/Again.dotscene\"]"));
  CHECK(candidate[0].callback("[]"));
}

TEST_CASE("scene_flow.retains_bounded_owned_results_and_thread_move_lifetime_contracts") {
  CHECK_FALSE(SceneFlow::create(0, std::nullopt));
  CHECK_FALSE(SceneFlow::create(1, "../x.dotscene"));
  auto flow = SceneFlow::create(1, std::nullopt).value();
  auto copy = flow;
  auto bindings = flow.bindings(1, std::nullopt).value();
  auto foreign = std::async(std::launch::async, [&] {
    return !flow.location() && !flow.latest() && !flow.pending()
           && !flow.request("scenes/a.dotscene", 1) && !flow.begin(1) && !flow.status(1)
           && !flow.bindings(1, std::nullopt) && !bindings[0].callback("[]");
  });
  CHECK(foreign.get());

  for (std::uint64_t i = 1; i <= 20; ++i) {
    auto id = copy.request("scenes/a.dotscene", i).value();
    REQUIRE(flow.begin(id));
    REQUIRE(flow.commit(id, i + 1, "scenes/a.dotscene"));
  }

  CHECK_FALSE(flow.status(1));
  CHECK_FALSE(flow.status(4));
  CHECK(flow.status(5));
  CHECK(flow.status(20));
  auto retained = flow.status(20).value();
  auto moved = std::move(flow);
  CHECK_FALSE(flow.latest());
  CHECK_FALSE(flow.location());
  CHECK_FALSE(flow.status(20));
  CHECK_FALSE(flow.request("scenes/a.dotscene", 21));
  CHECK_FALSE(flow.bindings(21, "scenes/a.dotscene"));
  CHECK(moved.location()->scope == 21);
  auto id = moved.request("scenes/end.dotscene", 21).value();
  copy.retire();
  CHECK(moved.status(id)->state == SceneRequestState::Cancelled);
  CHECK(retained.state == SceneRequestState::Succeeded);
  CHECK(retained.path == "scenes/a.dotscene");
  CHECK_FALSE(moved.commit(id, 22, "scenes/end.dotscene"));
}

TEST_CASE("scene_flow.additive_ownership_survives_history_and_replacement_retires_it") {
  auto flow = SceneFlow::create(10, "scenes/main.dotscene").value();
  const auto add = flow.append("assets/room.dotscene", 10).value();
  CHECK(flow.status(add)->kind == SceneRequestKind::Append);
  REQUIRE(flow.begin(add));
  CHECK_FALSE(flow.commit(add, 20, "assets/room.dotscene"));
  REQUIRE(flow.commitContent(add, Dot::World::SceneAttachment{{10, 20}, {{10, 20}, {10, 21}}}));
  CHECK(flow.location()->scope == 10);
  CHECK(flow.location()->path == "scenes/main.dotscene");
  CHECK(flow.status(add)->root == Dot::World::EntityId{10, 20});

  for (int index = 0; index < 20; ++index) {
    auto rejected = flow.request("scenes/missing.dotscene", 10).value();
    REQUIRE(flow.begin(rejected));
    REQUIRE(flow.fail(rejected, failure()));
  }

  CHECK_FALSE(flow.status(add));
  REQUIRE(flow.instances()->size() == 1);
  REQUIRE(flow.members({10, 20})->size() == 2);
  CHECK_FALSE(flow.remove({11, 20}));
  auto remove = flow.remove({10, 20}).value();
  REQUIRE(flow.begin(remove));
  CHECK(flow.status(remove)->kind == SceneRequestKind::Remove);
  REQUIRE(flow.fail(remove, failure()));
  CHECK(flow.instances()->size() == 1);
  remove = flow.remove({10, 20}).value();
  REQUIRE(flow.begin(remove));
  REQUIRE(flow.commitContent(remove));
  CHECK(flow.instances()->empty());
  CHECK(flow.status(remove)->committed);
  const auto again = flow.append("assets/room.dotscene", 10).value();
  REQUIRE(flow.begin(again));
  REQUIRE(flow.commitContent(again, Dot::World::SceneAttachment{{10, 30}, {{10, 30}}}));
  const auto cut = flow.request("scenes/next.dotscene", 10).value();
  REQUIRE(flow.begin(cut));
  REQUIRE(flow.commit(cut, 11, "scenes/next.dotscene"));
  CHECK(flow.instances()->empty());
  CHECK_FALSE(flow.remove({10, 30}));
}

TEST_CASE("scene_flow.additive_host_scope_access_and_limits_are_strict") {
  auto flow = SceneFlow::create(1, std::nullopt).value();
  auto bindings = flow.bindings(1, std::nullopt).value();
  REQUIRE(bindings.size() == 6);
  CHECK(bindings[3].access == HostAccess::ReadWrite);
  CHECK(bindings[4].access == HostAccess::ReadWrite);
  CHECK(bindings[5].access == HostAccess::ReadOnly);
  CHECK_FALSE(bindings[3].callback("[42]"));
  CHECK_FALSE(bindings[4].callback(R"([{"scene":"1","value":"2","extra":1}])"));
  CHECK_FALSE(bindings[4].callback(R"([{"scene":"2","value":"2"}])"));
  CHECK_FALSE(bindings[5].callback("[1]"));

  for (std::uint64_t value = 1; value <= 64; ++value) {
    auto id = flow.append("assets/room.dotscene", 1).value();
    REQUIRE(flow.begin(id));
    REQUIRE(flow.commitContent(id, Dot::World::SceneAttachment{{1, value}, {{1, value}}}));
  }

  CHECK_FALSE(flow.append("assets/room.dotscene", 1));
  CHECK(Json::parse(bindings[5].callback("[]").value().value()).size() == 64);
  auto remove = Json::parse(bindings[4].callback(R"([{"scene":"1","value":"1"}])").value().value())
                    .get<std::string>();
  REQUIRE(flow.begin(std::stoull(remove)));
  REQUIRE(flow.commitContent(std::stoull(remove)));
  auto last = flow.append("assets/room.dotscene", 1).value();
  flow.retire();
  CHECK(flow.status(last)->state == SceneRequestState::Cancelled);
  CHECK_FALSE(bindings[3].callback(R"(["assets/room.dotscene"])"));
}

#include <doctest/doctest.h>

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Control;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;

namespace {
  using namespace Dot::Core;
  using Json = nlohmann::json;

  struct Fixture {
    StagedDirectory parent
        = StagedDirectory::create(FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Control control = Control::create(parent.files().value(), R"({"kind":"test"})").value();
    FileSystem files = control.files().value();

    auto submit(std::string id, std::string request = R"({"command":"inspect"})")
        -> std::filesystem::path {
      auto receipt = submitControlRequest(files.root(), id, request);
      REQUIRE_MESSAGE(receipt, (receipt ? "" : receipt.error().message));
      return *receipt;
    }

    auto response(std::string id) -> Json {
      return Json::parse(
          files.readText(std::filesystem::path{"responses"} / (id + ".json")).value());
    }
  };
}  // namespace

TEST_CASE("control.requests_execute_once_in_order_and_owned_receipts_survive_later_calls") {
  Fixture f;
  const auto receipt = f.submit("b");
  CHECK_FALSE(std::filesystem::exists(receipt));
  CHECK(f.submit("b") == receipt);
  f.submit("a", R"({"command":"change"})");
  CHECK_FALSE(submitControlRequest(f.files.root(), "b", R"({"command":"different"})"));
  std::vector<std::string> calls;
  const auto handler = [&](std::string_view request) -> Result<std::string> {
    calls.emplace_back(request);
    return R"({"ok":true,"value":"独立结果"})";
  };
  CHECK(f.control.process(handler).value() == 2);
  CHECK(Json::parse(calls[0])["command"] == "change");
  CHECK(Json::parse(calls[1])["command"] == "inspect");
  const auto first = f.response("b");
  CHECK(first["source"] == R"({"command":"inspect"})");
  CHECK(first["response"]["value"] == "独立结果");
  CHECK(f.control.process(handler).value() == 0);
  CHECK(f.submit("b") == receipt);
  CHECK(calls.size() == 2);
  CHECK(f.response("b") == first);
  CHECK_FALSE(f.control.failure().value());
}

TEST_CASE("control.strict_metadata_requests_and_disk_errors_do_not_dispatch_fake_success") {
  Fixture f;
  CHECK_FALSE(Control::create(f.parent.files().value(), R"({"state":"ready"})"));
  CHECK_FALSE(Control::create(f.parent.files().value(), "[]"));

  for (const auto* value : {"[]", "{} {}", R"({"x":1,"x":2})"}) {
    CHECK_FALSE(submitControlRequest(f.files.root(), "bad", value));
  }

  for (const auto* id : {"", "../outside", "name.json", "中文", "CON"}) {
    CHECK_FALSE(submitControlRequest(f.files.root(), id, "{}"));
  }

  f.submit("Mixed");
  CHECK_FALSE(submitControlRequest(f.files.root(), "mixed", R"({"command":"inspect"})"));
  REQUIRE(std::filesystem::remove(f.files.root() / "requests/Mixed.json"));
  REQUIRE(f.files.writeTextAtomic("requests/damaged.json", "not JSON"));
  std::size_t calls{};
  auto processed = f.control.process([&](std::string_view) -> Result<std::string> {
    ++calls;
    return fail(ErrorCode::Io, "Expected external failure");
  });
  REQUIRE(processed);
  CHECK(*processed == 1);
  CHECK(calls == 0);
  CHECK(f.response("damaged")["response"]["ok"] == false);
  f.submit("failure");
  CHECK(f.control
            .process([&](std::string_view) -> Result<std::string> {
              ++calls;
              return fail(ErrorCode::Io, "Expected external failure");
            })
            .value()
        == 1);
  CHECK(f.response("failure")["response"]["error"]["code"] == "Io");
  CHECK_FALSE(f.control.failure().value());
}

TEST_CASE("control.bounded_batches_pending_capacity_and_stop_predicate_preserve_requests") {
  Fixture f;

  for (int id = 0; id < 20; ++id) {
    f.submit(std::to_string(id));
  }

  std::size_t calls{};
  const auto handler = [&](std::string_view) -> Result<std::string> {
    ++calls;
    return R"({"ok":true})";
  };
  CHECK(f.control.process(handler).value() == 16);
  CHECK(calls == 16);
  CHECK(f.control.process(handler, [&] { return calls < 17; }).value() == 1);
  CHECK(calls == 17);
  CHECK(f.control.process(handler).value() == 3);

  for (int id = 20; id < 276; ++id) {
    f.submit(std::to_string(id));
  }

  CHECK_FALSE(submitControlRequest(f.files.root(), "overflow", "{}"));
  CHECK_FALSE(std::filesystem::exists(f.files.root() / "requests/overflow.json"));
  REQUIRE(f.files.writeTextAtomic("requests/injected.json", "{}"));
  CHECK_FALSE(f.control.process(handler));
  CHECK(calls == 20);
  CHECK_FALSE(f.control.failure().value());
  REQUIRE(std::filesystem::remove(f.files.root() / "requests/injected.json"));
  CHECK(f.control.process(handler).value() == 16);
}

TEST_CASE("control.post_dispatch_response_failures_are_terminal_without_replay") {
  for (int failure = 0; failure < 4; ++failure) {
    Fixture f;
    f.submit("one");
    std::size_t effects{};
    auto result = f.control.process([&](std::string_view) -> Result<std::string> {
      ++effects;

      if (failure == 0) {
        throw std::runtime_error("after effect");
      }

      if (failure == 1) {
        return "[]";
      }

      if (failure == 3) {
        throw 7;
      }

      REQUIRE(std::filesystem::create_directory(f.files.root() / "responses/one.json"));
      return R"({"ok":true})";
    });
    REQUIRE_FALSE(result);
    CHECK(result.error().message.find("may have committed") != std::string::npos);
    CHECK(f.control.failure().value().has_value());
    CHECK_FALSE(f.control.process([&](std::string_view) -> Result<std::string> {
      ++effects;
      return R"({"ok":true})";
    }));
    CHECK(effects == 1);
  }
}

TEST_CASE("control.owner_reentry_move_and_close_keep_parent_and_release_owned_directory") {
  Fixture f;
  const auto root = f.files.root();
  bool rejected{};
  std::thread other([&] { rejected = !f.control.files() && !f.control.close(); });
  other.join();
  CHECK(rejected);
  f.submit("one");
  auto moved = std::move(f.control);
  CHECK_FALSE(f.control.files());
  REQUIRE(moved.process([&](std::string_view) -> Result<std::string> {
    CHECK_FALSE(moved.process([](std::string_view) -> Result<std::string> { return "{}"; }));
    CHECK_FALSE(moved.close());
    return R"({"ok":true})";
  }));
  REQUIRE(moved.close());
  REQUIRE(moved.close());
  CHECK_FALSE(std::filesystem::exists(root));
  CHECK(f.parent.files());
  CHECK_FALSE(submitControlRequest(root, "after", "{}"));
}

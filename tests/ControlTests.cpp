#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
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
    FileSystem replies = control.receipts().value();

    auto submit(std::string id, std::string request = R"({"command":"inspect"})")
        -> std::filesystem::path {
      auto receipt = submitControlRequest(files.root(), id, request);
      REQUIRE_MESSAGE(receipt, (receipt ? "" : receipt.error().message));
      return *receipt;
    }

    auto response(std::string id) -> Json {
      return Json::parse(
          replies.readText(std::filesystem::path{"responses"} / (id + ".json")).value());
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

      REQUIRE(std::filesystem::create_directory(f.replies.root() / "responses/one.json"));
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

TEST_CASE("control.orphan_receipts_never_acknowledge_a_new_request") {
  Fixture f;
  REQUIRE(f.replies.writeTextAtomic("responses/new.json",
                                    R"({"source":"{}","response":{"ok":true,"old":true}})"));
  CHECK_FALSE(submitControlRequest(f.files.root(), "new", R"({"command":"change"})"));
  CHECK_FALSE(std::filesystem::exists(f.files.root() / "requests/new.json"));
  REQUIRE(std::filesystem::remove(f.replies.root() / "responses/new.json"));
  f.submit("new", R"({"command":"change"})");
  std::size_t effects{};
  REQUIRE(f.control.process([&](std::string_view) -> Result<std::string> {
    ++effects;
    return R"({"ok":true,"new":true})";
  }));
  CHECK(effects == 1);
  CHECK(f.response("new")["response"]["new"] == true);
}

TEST_CASE("control.response_spelling_and_nonregular_leaves_reject_before_queue_or_dispatch") {
  for (int kind = 0; kind < 4; ++kind) {
#if defined(_WIN32)
    if (kind == 1 || kind == 2) {
      continue;
    }
#endif

    Fixture f;
    f.submit("one");
    const auto receipt = f.replies.root() / "responses/one.json";

    if (kind == 0) {
      REQUIRE(std::filesystem::create_directory(receipt));
    } else if (kind == 1) {
      REQUIRE(f.files.writeTextAtomic("existing.json", "{}"));
      std::filesystem::create_symlink(f.files.root() / "existing.json", receipt);
    } else if (kind == 2) {
      std::filesystem::create_symlink(f.files.root() / "missing.json", receipt);
    } else {
      REQUIRE(f.replies.writeTextAtomic("responses/One.json", "{}"));
    }

    CHECK_FALSE(acknowledgeControlResponse(receipt));
    CHECK_FALSE(submitControlRequest(f.files.root(), "next", "{}"));
    CHECK_FALSE(submitControlRequest(f.files.root(), "one", R"({"command":"inspect"})"));
    CHECK_FALSE(std::filesystem::exists(f.files.root() / "requests/next.json"));
    std::size_t effects{};
    const auto handler = [&](std::string_view) -> Result<std::string> {
      ++effects;
      return R"({"ok":true})";
    };
    CHECK_FALSE(f.control.process(handler));
    CHECK(effects == 0);
    CHECK_FALSE(f.control.failure().value());
    REQUIRE(std::filesystem::remove(kind == 3 ? f.replies.root() / "responses/One.json" : receipt));
    REQUIRE(f.control.process(handler));
    CHECK(effects == 1);
    CHECK(f.response("one")["response"]["ok"] == true);
  }
}

TEST_CASE("control.malformed_request_leaves_reject_new_publication_and_recover_after_repair") {
  Fixture f;
  REQUIRE(std::filesystem::create_directory(f.files.root() / "requests/bad.json"));
  CHECK_FALSE(submitControlRequest(f.files.root(), "next", "{}"));
  CHECK_FALSE(std::filesystem::exists(f.files.root() / "requests/next.json"));
  REQUIRE(std::filesystem::remove(f.files.root() / "requests/bad.json"));
  REQUIRE(f.files.writeTextAtomic("requests/invalid.name.json", "{}"));
  CHECK_FALSE(submitControlRequest(f.files.root(), "next", "{}"));
  REQUIRE(std::filesystem::remove(f.files.root() / "requests/invalid.name.json"));
  REQUIRE(f.files.writeTextAtomic("requests/Case.JSON", "{}"));
  CHECK_FALSE(submitControlRequest(f.files.root(), "Case", "{}"));
  REQUIRE(std::filesystem::remove(f.files.root() / "requests/Case.JSON"));
  f.submit("next", "{}");
  CHECK(f.control.process([](std::string_view) -> Result<std::string> { return "{}"; }).value()
        == 1);
}

TEST_CASE("control.full_retained_history_preserves_retries_and_rejects_new_or_overflow_entries") {
  Fixture f;
  const auto requests = f.files.resolve("requests").value();
  const auto responses = f.replies.resolve("responses").value();

  for (int i = 0; i < 4096; ++i) {
    const auto name = std::to_string(i) + ".json";
    std::ofstream request(requests / name);
    std::ofstream response(responses / name);
    request << "{}";
    response << R"({"source":"{}","response":{"ok":true}})";
    request.close();
    response.close();
    REQUIRE(request);
    REQUIRE(response);
  }

  std::size_t effects{};
  const auto handler = [&](std::string_view) -> Result<std::string> {
    ++effects;
    return R"({"ok":true})";
  };
  CHECK(f.control.process(handler).value() == 0);
  CHECK(f.submit("300", "{}") == responses / "300.json");
  CHECK_FALSE(submitControlRequest(f.files.root(), "overflow", "{}"));
  CHECK_FALSE(std::filesystem::exists(requests / "overflow.json"));
  REQUIRE(f.replies.writeTextAtomic("responses/extra.json", "{}"));
  CHECK_FALSE(f.control.process(handler));
  CHECK_FALSE(submitControlRequest(f.files.root(), "300", "{}"));
  CHECK(effects == 0);
  CHECK_FALSE(f.control.failure().value());
  REQUIRE(std::filesystem::remove(responses / "extra.json"));
  REQUIRE(std::filesystem::remove(requests / "300.json"));
  CHECK_FALSE(submitControlRequest(f.files.root(), "new", "{}"));
  CHECK_FALSE(std::filesystem::exists(requests / "new.json"));
  REQUIRE(f.files.writeTextAtomic("requests/injected.json", "{}"));
  CHECK_FALSE(f.control.process(handler));
  CHECK(effects == 0);
  CHECK_FALSE(f.control.failure().value());
  REQUIRE(std::filesystem::remove(requests / "injected.json"));
  REQUIRE(f.files.writeTextAtomic("requests/300.json", "{}"));
  REQUIRE(std::filesystem::remove(responses / "300.json"));
  CHECK(f.control.process(handler).value() == 1);
  CHECK(effects == 1);
  CHECK(f.response("300")["response"]["ok"] == true);
}

TEST_CASE(
    "control.retired_receipts_preserve_close_outcome_and_unprocessed_tail_until_explicit_clear") {
  auto parent
      = StagedDirectory::create(FileSystem::open(std::filesystem::temp_directory_path()).value())
            .value();
  const auto storage = parent.files().value();
  REQUIRE(storage.writeTextAtomic("user.txt", "preserved"));
  std::filesystem::path endpoint, archive, reply;
  std::size_t effects{};

  {
    auto control = Control::create(storage).value();
    endpoint = control.files()->root();
    archive = control.receipts()->root();
    reply = submitControlRequest(endpoint, "a-close", R"({"command":"close"})").value();
    REQUIRE(submitControlRequest(endpoint, "z-tail", R"({"command":"change"})"));
    CHECK_FALSE(clearControlReceipts(archive));
    bool running = true;
    REQUIRE(control.process(
        [&](std::string_view) -> Result<std::string> {
          ++effects;
          running = false;
          return R"({"ok":true,"closing":true})";
        },
        [&] { return running; }));
    REQUIRE(control.close());
    REQUIRE(control.close());
    CHECK_FALSE(control.process([](std::string_view) -> Result<std::string> { return "{}"; }));
    CHECK(control.receipts()->root() == archive);
  }

  REQUIRE_FALSE(std::filesystem::exists(endpoint));
  auto files = FileSystem::open(archive).value();
  const auto record
      = Json::parse(files.readText(std::filesystem::path("responses") / reply.filename()).value());
  CHECK(record["source"] == R"({"command":"close"})");
  CHECK(record["response"]["closing"] == true);
  auto status = inspectControlReceipts(archive).value();
  CHECK(status.closed);
  CHECK_FALSE(status.fault);
  CHECK(status.uncertain.empty());
  REQUIRE(status.unprocessed);
  CHECK(*status.unprocessed == std::vector<std::string>{"z-tail"});
  CHECK(effects == 1);
  REQUIRE(acknowledgeControlResponse(reply));
  REQUIRE(clearControlReceipts(archive));
  CHECK_FALSE(std::filesystem::exists(archive));
  CHECK_FALSE(std::filesystem::exists(storage.root() / ".dot-stage-control"));
  CHECK(storage.readText("user.txt").value() == "preserved");
}

TEST_CASE("control.consumption_recovers_reply_capacity_without_reexecuting_or_reusing_identity") {
  Fixture f;
  const auto reply = f.submit("once");
  std::size_t effects{};
  const auto handler = [&](std::string_view) -> Result<std::string> {
    ++effects;
    return R"({"ok":true,"value":"retained native result"})";
  };
  REQUIRE(f.control.process(handler));
  const auto bytes = inspectControlReceipts(f.replies.root())->bytes;
  REQUIRE(acknowledgeControlResponse(reply));
  REQUIRE(acknowledgeControlResponse(reply));
  CHECK(f.response("once") == Json{{"consumed", true}});
  CHECK(inspectControlReceipts(f.replies.root())->bytes < bytes);
  CHECK(f.submit("once") == reply);
  CHECK(f.control.process(handler).value() == 0);
  CHECK(effects == 1);
  CHECK_FALSE(submitControlRequest(f.files.root(), "once", R"({"command":"other"})"));
  CHECK_FALSE(acknowledgeControlResponse(f.replies.root() / "responses/unknown.json"));
  CHECK(
      Json::parse(performControlReceiptAction("control-status", f.replies.root()).value())["state"]
      == "ready");
  CHECK_FALSE(performControlReceiptAction("clear-control", f.replies.root()));
  CHECK_FALSE(performControlReceiptAction("unknown", f.replies.root()));
  REQUIRE(f.control.close());
  CHECK(Json::parse(performControlReceiptAction("ack-control", reply).value())["consumed"] == true);
  CHECK(
      Json::parse(performControlReceiptAction("clear-control", f.replies.root()).value())["cleared"]
      == true);
}

TEST_CASE("control.reply_budget_pauses_before_effects_and_consumption_resumes_pending_work") {
  auto parent
      = StagedDirectory::create(FileSystem::open(std::filesystem::temp_directory_path()).value())
            .value();
  ControlLimits limits;
  limits.responseBytes = 1024;
  limits.receiptBytes = 6 * 1024 * 1024 + limits.responseBytes + 4096;
  auto control = Control::create(parent.files().value(), "{}", limits).value();
  const auto endpoint = control.files()->root();
  const auto archive = control.receipts()->root();
  const auto request = Json{{"payload", std::string(1024 * 1024 - 128, 'x')}}.dump();
  auto first = submitControlRequest(endpoint, "a", request).value();
  auto second = submitControlRequest(endpoint, "b", request).value();
  std::size_t effects{};
  const auto handler = [&](std::string_view) -> Result<std::string> {
    ++effects;
    return "{}";
  };
  CHECK(control.process(handler).value() == 1);
  CHECK(effects == 1);
  CHECK(inspectControlReceipts(archive)->blocked);
  CHECK_FALSE(control.failure().value());
  CHECK_FALSE(std::filesystem::exists(second));
  CHECK(control.process(handler).value() == 0);
  REQUIRE(acknowledgeControlResponse(first));
  CHECK(control.process(handler).value() == 1);
  CHECK(effects == 2);
  CHECK_FALSE(inspectControlReceipts(archive)->blocked);
  CHECK(std::filesystem::exists(second));
  REQUIRE(control.close());
  REQUIRE(clearControlReceipts(archive));
}

TEST_CASE("control.retained_session_limit_requires_explicit_release_and_rejects_invalid_limits") {
  auto parent
      = StagedDirectory::create(FileSystem::open(std::filesystem::temp_directory_path()).value())
            .value();
  auto files = parent.files().value();
  ControlLimits limits;
  limits.sessions = 1;
  auto first = Control::create(files, "{}", limits).value();
  const auto archive = first.receipts()->root();
  REQUIRE(first.close());
  CHECK_FALSE(Control::create(files, "{}", limits));
  REQUIRE(clearControlReceipts(archive));
  auto next = Control::create(files, "{}", limits).value();
  const auto nextArchive = next.receipts()->root();
  REQUIRE(next.close());
  REQUIRE(clearControlReceipts(nextArchive));
  limits.sessions = 0;
  CHECK_FALSE(Control::create(files, "{}", limits));
  limits = {};
  limits.responseBytes = 1;
  CHECK_FALSE(Control::create(files, "{}", limits));
  limits = {};
  limits.receiptBytes = 1024;
  CHECK_FALSE(Control::create(files, "{}", limits));
  CHECK_FALSE(Control::create(files, R"({"receipts":"injected"})"));
  CHECK(std::filesystem::is_empty(files.root()));
}

TEST_CASE(
    "control.retirement_preserves_uncertain_effects_separately_from_known_unprocessed_requests") {
  Fixture f;
  f.submit("a-effect");
  f.submit("z-unprocessed");
  std::size_t effects{};
  CHECK_FALSE(f.control.process([&](std::string_view) -> Result<std::string> {
    ++effects;
    throw std::runtime_error("after committed mutation");
  }));
  REQUIRE(f.control.close());
  auto status = inspectControlReceipts(f.replies.root()).value();
  CHECK(status.closed);
  REQUIRE(status.fault);
  CHECK(status.fault->message.find("may have committed") != std::string::npos);
  CHECK(status.uncertain == std::vector<std::string>{"a-effect"});
  REQUIRE(status.unprocessed);
  CHECK(*status.unprocessed == std::vector<std::string>{"z-unprocessed"});
  CHECK(effects == 1);
  REQUIRE(clearControlReceipts(f.replies.root()));
}

TEST_CASE("control.archive_cleanup_rejects_active_forged_and_unowned_data") {
  Fixture f;
  const auto reply = f.submit("one");
  REQUIRE(
      f.control.process([](std::string_view) -> Result<std::string> { return R"({"ok":true})"; }));
  const auto metadata = f.replies.readText("session.json").value();
  auto forged = Json::parse(metadata);
  forged["control"] = "/not-the-owned-control";
  REQUIRE(f.replies.writeTextAtomic("session.json", forged.dump()));
  CHECK_FALSE(inspectControlReceipts(f.replies.root()));
  CHECK_FALSE(acknowledgeControlResponse(reply));
  CHECK_FALSE(clearControlReceipts(f.replies.root()));
  REQUIRE(f.replies.writeTextAtomic("session.json", metadata));
  REQUIRE(f.control.close());
  CHECK_FALSE(clearControlReceipts(f.parent.files()->root()));
  REQUIRE(f.replies.writeTextAtomic("user.txt", "do not remove"));
  CHECK_FALSE(clearControlReceipts(f.replies.root()));
  CHECK(f.replies.readText("user.txt").value() == "do not remove");
  REQUIRE(std::filesystem::remove(f.replies.root() / "user.txt"));
  REQUIRE(f.replies.writeTextAtomic("responses/unowned.json", R"({"personal":true})"));
  CHECK_FALSE(clearControlReceipts(f.replies.root()));
  CHECK_FALSE(acknowledgeControlResponse(f.replies.root() / "responses/unowned.json"));
  REQUIRE(std::filesystem::remove(f.replies.root() / "responses/unowned.json"));
  REQUIRE(clearControlReceipts(f.replies.root()));
}

TEST_CASE("control.failed_retirement_keeps_endpoint_owned_and_retry_never_dispatches_again") {
  Fixture f;
  f.submit("one");
  std::size_t effects{};
  REQUIRE(f.control.process([&](std::string_view) -> Result<std::string> {
    ++effects;
    return "{}";
  }));
  const auto metadata = f.replies.readText("session.json").value();
  REQUIRE(std::filesystem::remove(f.replies.root() / "session.json"));
  REQUIRE(std::filesystem::create_directory(f.replies.root() / "session.json"));
  CHECK_FALSE(f.control.close());
  CHECK(std::filesystem::exists(f.files.root()));
  CHECK_FALSE(f.control.process([&](std::string_view) -> Result<std::string> {
    ++effects;
    return "{}";
  }));
  REQUIRE(std::filesystem::remove(f.replies.root() / "session.json"));
  REQUIRE(f.replies.writeTextAtomic("session.json", metadata));
  REQUIRE(f.control.close());
  CHECK_FALSE(std::filesystem::exists(f.files.root()));
  CHECK(effects == 1);
  REQUIRE(clearControlReceipts(f.replies.root()));
}

TEST_CASE("control.lost_processed_receipts_never_replay_successful_effects") {
  Fixture f;
  const auto receipt = f.submit("one");
  std::size_t effects{};
  const auto handler = [&](std::string_view) -> Result<std::string> {
    ++effects;
    return "{}";
  };
  REQUIRE(f.control.process(handler));
  REQUIRE(std::filesystem::remove(receipt));
  CHECK_FALSE(f.control.process(handler));
  CHECK(effects == 1);
  REQUIRE(f.control.failure().value());
  REQUIRE(f.control.close());
  const auto status = inspectControlReceipts(f.replies.root()).value();
  CHECK(status.closed);
  CHECK(status.uncertain == std::vector<std::string>{"one"});
  REQUIRE(status.unprocessed);
  CHECK(status.unprocessed->empty());
  REQUIRE(clearControlReceipts(f.replies.root()));
}

TEST_CASE("control.unexposed_unused_owners_clean_empty_archives_on_initialization_failure") {
  auto parent
      = StagedDirectory::create(FileSystem::open(std::filesystem::temp_directory_path()).value())
            .value();

  {
    auto unused = Control::create(parent.files().value()).value();
  }

  CHECK(std::filesystem::is_empty(parent.files()->root()));
}

TEST_CASE("control.missing_live_endpoint_reports_unknown_unprocessed_work_on_retirement") {
  Fixture f;
  f.submit("pending");
  REQUIRE(std::filesystem::remove_all(f.files.root()) > 0);
  CHECK_FALSE(f.control.close());
  const auto status = inspectControlReceipts(f.replies.root()).value();
  CHECK(status.closed);
  CHECK(status.fault.has_value());
  CHECK_FALSE(status.unprocessed);
  CHECK(status.uncertain.empty());
  REQUIRE(clearControlReceipts(f.replies.root()));
}

TEST_CASE("control.partial_archive_cleanup_can_retry_without_removing_unowned_data") {
  for (const bool removeMetadata : {false, true}) {
    Fixture f;
    REQUIRE(f.control.close());
    REQUIRE(std::filesystem::remove(f.replies.root() / "responses"));

    if (removeMetadata) {
      REQUIRE(std::filesystem::remove(f.replies.root() / "session.json"));
    }

    REQUIRE(clearControlReceipts(f.replies.root()));
    REQUIRE(clearControlReceipts(f.replies.root()));
    CHECK_FALSE(std::filesystem::exists(f.replies.root()));
    CHECK(std::filesystem::is_empty(f.parent.files()->root()));
  }
}

TEST_CASE("control.archive_directory_and_metadata_aliases_reject_without_deleting_targets") {
  for (const bool metadata : {false, true}) {
    Fixture f;
    REQUIRE(f.control.close());
    const auto name = metadata ? "session.json" : "responses";
    const auto owned = f.replies.root() / name;
    const auto target = f.replies.root() / "retained-user-data";
    std::filesystem::rename(owned, target);
    std::filesystem::create_symlink(target, owned);
    CHECK_FALSE(inspectControlReceipts(f.replies.root()));
    CHECK_FALSE(clearControlReceipts(f.replies.root()));
    CHECK(std::filesystem::exists(target));
    REQUIRE(std::filesystem::remove(owned));
    std::filesystem::rename(target, owned);
    REQUIRE(clearControlReceipts(f.replies.root()));
  }

  Fixture f;
  const auto receipt = f.submit("one");
  REQUIRE(f.control.process([](std::string_view) -> Result<std::string> { return "{}"; }));
  const auto original = f.replies.readText("responses/one.json").value();
  const auto target = f.replies.root() / "responses/preserved.json";
  std::filesystem::rename(receipt, target);
  std::filesystem::create_symlink(target, receipt);
  CHECK_FALSE(acknowledgeControlResponse(receipt));
  CHECK(f.replies.readText("responses/preserved.json").value() == original);
  REQUIRE(std::filesystem::remove(receipt));
  std::filesystem::rename(target, receipt);
  REQUIRE(f.control.close());
  REQUIRE(clearControlReceipts(f.replies.root()));
}

TEST_CASE("control.client_consumption_and_product_polling_share_atomic_complete_receipts") {
  Fixture f;
  std::vector<std::filesystem::path> replies;

  for (int i = 0; i < 32; ++i) {
    replies.push_back(f.submit("request-" + std::to_string(i)));
  }

  const auto handler = [](std::string_view) -> Result<std::string> {
    return Json{{"ok", true}, {"payload", std::string(64 * 1024, 'x')}}.dump();
  };
  REQUIRE(f.control.process(handler).value() == 16);
  REQUIRE(f.control.process(handler).value() == 16);
  std::atomic<bool> finished{};
  std::optional<Error> clientError;
  std::optional<Error> productError;
  std::jthread client([&] {
    for (const auto& reply : replies) {
      auto consumed = acknowledgeControlResponse(reply);

      if (!consumed) {
        clientError = consumed.error();
        break;
      }
    }

    finished.store(true);
  });

  do {
    auto processed = f.control.process(handler);

    if (!processed) {
      productError = processed.error();
      break;
    }

    CHECK(*processed == 0);
    std::this_thread::yield();
  } while (!finished.load());

  client.join();
  INFO((clientError ? clientError->message : ""));
  INFO((productError ? productError->message : ""));
  REQUIRE_FALSE(clientError);
  REQUIRE_FALSE(productError);

  for (int i = 0; i < 32; ++i) {
    CHECK(f.response("request-" + std::to_string(i)) == (Json{{"consumed", true}}));
  }

  REQUIRE(f.control.close());
  REQUIRE(clearControlReceipts(f.replies.root()));
}

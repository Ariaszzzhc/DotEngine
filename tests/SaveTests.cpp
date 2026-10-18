#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <future>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Core.Result;
import Dot.Game.State;
import Dot.Game.Saves;
import Dot.Game.SaveQueue;
import Dot.Script.GameData;

namespace {
  using namespace Dot;
  using Json = nlohmann::json;

  struct Fixture {
    Core::StagedDirectory storage{
        Core::StagedDirectory::create(
            Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
            .value()};

    auto files() const -> Core::FileSystem {
      return storage.files().value();
    }
  };

  auto await(Game::SaveQueue& queue, std::uint64_t id) -> Game::SaveJob {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    for (;;) {
      auto value = queue.take(id).value();

      if (value.state != Game::SaveJobState::Pending
          && value.state != Game::SaveJobState::Running) {
        return value;
      }

      REQUIRE(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  auto await(Script::GameData& data, const Json& ticket) -> Json {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto request = ticket;
    request["command"] = "take-save";

    for (;;) {
      auto value = Json::parse(data.perform(request.dump()).value());

      if (value["state"] != "pending" && value["state"] != "running") {
        return value;
      }

      REQUIRE(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

}  // namespace

TEST_CASE("saves.atomic_round_trip_explicit_overwrite_and_load_revision_provenance") {
  Fixture fixture;
  auto files = fixture.files();
  auto store = Game::Saves::open(files).value();
  auto state
      = Game::State::create(R"({"level":"scenes/main.dotscene","玩家":{"health":100}})").value();
  auto initial = state.snapshot().value();
  auto saved = store.write("checkpoint", initial, false);
  REQUIRE_MESSAGE(saved, (saved ? "" : saved.error().message));
  CHECK(saved->name == "checkpoint");
  CHECK(saved->bytes == files.readText("checkpoint.dotsave")->size());
  CHECK(store.read("checkpoint")->data == initial.data);
  CHECK(store.read("checkpoint")->revision == initial.revision);
  const auto original = files.readText("checkpoint.dotsave").value();
  REQUIRE(state.set("level", R"("scenes/studio.dotscene")"));
  CHECK_FALSE(store.write("checkpoint", state.snapshot().value(), false));
  CHECK(files.readText("checkpoint.dotsave").value() == original);
  REQUIRE(store.write("checkpoint", state.snapshot().value(), true));
  const auto newer = store.read("checkpoint").value();
  REQUIRE(state.set("health", "25"));
  const auto current = state.snapshot().value();
  CHECK_FALSE(state.replace(newer.data, newer.revision));
  CHECK(state.snapshot()->data == current.data);
  CHECK(state.replace(newer.data, current.revision).value() == current.revision + 1);
  CHECK(state.snapshot()->data == newer.data);
  REQUIRE(store.write("autosave", state.snapshot().value(), false));
  auto listed = store.list().value();
  REQUIRE(listed.size() == 2);
  CHECK(listed[0].name == "autosave");
  CHECK(listed[1].name == "checkpoint");
  REQUIRE(store.remove("autosave"));
  CHECK_FALSE(store.read("autosave"));
  CHECK_FALSE(store.remove("autosave"));
  CHECK(store.list()->size() == 1);
}

TEST_CASE("saves.reopen_owns_read_bytes_and_does_not_auto_mutate_state") {
  Fixture fixture;
  auto files = fixture.files();
  auto state = Game::State::create(R"({"health":80})").value();

  {
    auto store = Game::Saves::open(files).value();
    REQUIRE(store.write("slot1", state.snapshot().value(), false));
  }

  auto store = Game::Saves::open(files).value();
  auto loaded = store.read("slot1").value();
  REQUIRE(state.set("health", "20"));
  const auto before = state.snapshot().value();
  REQUIRE(files.writeTextAtomic("slot1.dotsave", "damaged"));
  CHECK_FALSE(store.read("slot1"));
  CHECK(state.snapshot()->revision == before.revision);
  CHECK(state.snapshot()->data == before.data);
  CHECK(loaded.data == R"({"health":80})");
  CHECK(store.list()->front().name == "slot1");
  REQUIRE(store.remove("slot1"));
  CHECK(store.list()->empty());
}

TEST_CASE("saves.strict_corrupt_envelopes_fail_without_touching_live_state") {
  Fixture fixture;
  auto files = fixture.files();
  auto store = Game::Saves::open(files).value();
  auto state = Game::State::create(R"({"progress":2})").value();
  auto snapshot = state.snapshot().value();
  REQUIRE(store.write("quick", snapshot, false));
  auto original = Json::parse(files.readText("quick.dotsave").value());

  for (const auto* bytes :
       {"{}", "[]", "null", "not json",
        R"({"format":"dot-save","slot":"quick","revision":"1","data":{"x":1,"x":2}})"}) {
    REQUIRE(files.writeTextAtomic("quick.dotsave", bytes));
    CHECK_FALSE(store.read("quick"));
    CHECK(state.snapshot()->data == snapshot.data);
    CHECK(state.snapshot()->revision == snapshot.revision);
  }

  for (int index = 0; index < 9; ++index) {
    auto broken = original;

    switch (index) {
      case 0:
        broken["format"] = "unknown";
        break;
      case 1:
        broken["slot"] = "different";
        break;
      case 2:
        broken["revision"] = "01";
        break;
      case 3:
        broken["revision"] = 1;
        break;
      case 4:
        broken["revision"] = "18446744073709551615";
        break;
      case 5:
        broken["data"] = nullptr;
        break;
      case 6:
        broken["data"]["unsafe"] = 9007199254740992ULL;
        break;
      case 7:
        broken["extra"] = true;
        break;
      case 8:
        broken.erase("data");
        break;
    }

    REQUIRE(files.writeTextAtomic("quick.dotsave", broken.dump()));
    CHECK_FALSE(store.read("quick"));
  }

  REQUIRE(files.writeTextAtomic("quick.dotsave", original.dump()));
  CHECK(store.read("quick"));
}

TEST_CASE("saves.invalid_names_snapshots_and_capacity_preserve_existing_files") {
  Fixture fixture;
  auto files = fixture.files();
  Game::SaveLimits limits;
  limits.slots = 1;
  limits.state = {64, 4};
  auto store = Game::Saves::open(files, limits).value();
  auto state = Game::State::create(R"({"ok":true})").value().snapshot().value();
  REQUIRE(store.write("quick", state, false));
  const auto before = files.readText("quick.dotsave").value();

  for (const auto& slot : {std::string{}, std::string("UPPER"), std::string("../escape"),
                           std::string("nul"), std::string("com1"), std::string("two words"),
                           std::string("../a"), std::string("a/b"), std::string(65, 'a')}) {
    CHECK_FALSE(store.write(slot, state, true));
    CHECK_FALSE(store.read(slot));
    CHECK_FALSE(store.remove(slot));
  }

  CHECK_FALSE(store.write("second", state, false));
  CHECK_FALSE(store.write("quick", {0, "{}"}, true));
  CHECK_FALSE(store.write("quick", {std::numeric_limits<std::uint64_t>::max(), "{}"}, true));
  CHECK_FALSE(store.write("quick", {2, "[]"}, true));
  CHECK_FALSE(store.write("quick", {2, "{\"large\":\"" + std::string(70, 'x') + "\"}"}, true));
  CHECK(files.readText("quick.dotsave").value() == before);
  REQUIRE(store.write("quick", {2, R"({"ok":false})"}, true));
  REQUIRE(files.writeTextAtomic("second.dotsave", before));
  CHECK(store.list()->size() == 2);
  CHECK_FALSE(store.write("third", state, false));
  REQUIRE(store.remove("second"));
  REQUIRE(files.writeTextAtomic("unrelated.txt", "leave me"));
  REQUIRE(files.writeTextAtomic(".dot-save-orphan", "leave me"));
  CHECK(store.list()->size() == 1);
  CHECK(files.readText("unrelated.txt").value() == "leave me");
  CHECK_FALSE(Game::Saves::open(files, {{64, 4}, 0}));
  CHECK_FALSE(Game::Saves::open(files, {{64, 4}, 1025}));
}

TEST_CASE("saves.rejects_case_aliases_symlinks_directories_and_oversized_payloads") {
  Fixture fixture;
  auto files = fixture.files();
  auto store = Game::Saves::open(files).value();
  REQUIRE(files.writeTextAtomic("Quick.DOTSAVE", "not canonical"));
  CHECK_FALSE(store.list());
  CHECK_FALSE(store.write("quick", {1, "{}"}, true));
  CHECK_FALSE(store.read("quick"));
  CHECK_FALSE(store.remove("quick"));
  REQUIRE(files.readText("Quick.DOTSAVE").value() == "not canonical");
  std::filesystem::remove(files.root() / "Quick.DOTSAVE");
  REQUIRE(files.createDirectories("folder.dotsave"));
  CHECK_FALSE(store.list());
  CHECK_FALSE(store.write("folder", {1, "{}"}, true));
  std::filesystem::remove(files.root() / "folder.dotsave");
  REQUIRE(files.writeTextAtomic("target.txt", "keep target"));
  std::error_code error;
  std::filesystem::create_symlink(files.root() / "target.txt", files.root() / "linked.dotsave",
                                  error);

  if (!error) {
    CHECK_FALSE(store.read("linked"));
    CHECK_FALSE(store.write("linked", {1, "{}"}, true));
    CHECK_FALSE(store.remove("linked"));
    CHECK(files.readText("target.txt").value() == "keep target");
    std::filesystem::remove(files.root() / "linked.dotsave");
  } else {
    WARN_MESSAGE(false, "Symlink fixture unavailable: ", error.message());
  }

  REQUIRE(files.writeTextAtomic("large.dotsave", std::string(256 * 1024 + 513, ' ')));
  CHECK(store.list()->front().bytes == 256 * 1024 + 513);
  CHECK_FALSE(store.read("large"));
  REQUIRE(store.remove("large"));
}

TEST_CASE("saves.foreign_calls_moves_and_missing_roots_fail_without_implicit_creation") {
  Fixture fixture;
  auto files = fixture.files();
  auto store = Game::Saves::open(files).value();
  REQUIRE(store.write("one", {1, "{}"}, false));
  auto foreign = std::async(std::launch::async, [&] {
    return !store.list() && !store.read("one") && !store.write("one", {2, "{}"}, true)
           && !store.remove("one");
  });
  CHECK(foreign.get());
  auto moved = std::move(store);
  CHECK_FALSE(store.list());
  CHECK_FALSE(store.read("one"));
  CHECK_FALSE(store.write("one", {2, "{}"}, true));
  CHECK_FALSE(store.remove("one"));
  CHECK(moved.read("one"));
  REQUIRE(files.createDirectories("saves"));
  auto root = Core::FileSystem::open(files.root() / "saves").value();
  auto detached = Game::Saves::open(root).value();
  std::filesystem::remove(root.root());
  CHECK_FALSE(detached.list());
  CHECK_FALSE(detached.write("save", {1, "{}"}, false));
  CHECK_FALSE(Game::Saves::open(root));
  CHECK_FALSE(std::filesystem::exists(root.root()));
}

#if !defined(_WIN32)
TEST_CASE("saves.failed_atomic_replace_retains_previous_slot_and_cleans_pending_files") {
  Fixture fixture;
  auto files = fixture.files();
  auto store = Game::Saves::open(files).value();
  REQUIRE(store.write("quick", {1, R"({"health":100})"}, false));
  const auto before = files.readText("quick.dotsave").value();
  const auto original = std::filesystem::status(files.root()).permissions();

  struct Restore {
    std::filesystem::path path;
    std::filesystem::perms permissions;

    ~Restore() {
      std::error_code ignored;
      std::filesystem::permissions(path, permissions, ignored);
    }
  } restore{files.root(), original};

  std::filesystem::permissions(
      files.root(), std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
  auto probe = files.writeTextAtomic("probe", "permission probe");

  if (probe) {
    WARN_MESSAGE(false,
                 "Current credentials bypass directory write permissions; failure fixture skipped");
  } else {
    auto rejected = store.write("quick", {2, R"({"health":0})"}, true);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Core::ErrorCode::Io);
    CHECK(files.readText("quick.dotsave").value() == before);
    CHECK(store.list()->size() == 1);
    CHECK(std::distance(std::filesystem::directory_iterator(files.root()),
                        std::filesystem::directory_iterator{})
          == 1);
  }
}
#endif

TEST_CASE("game_data.disk_load_is_explicit_and_conflicts_or_corruption_never_publish") {
  Fixture fixture;
  auto files = fixture.files();
  Script::GameDataConfig config;
  config.saves = files;
  auto data = Script::GameData::create(config).value();
  const auto call = [&](const Json& request) {
    auto result = data.perform(request.dump());
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    auto value = Json::parse(*result);

    if (value.is_object() && value.contains("id")) {
      value = await(data, value);
      REQUIRE_MESSAGE(value["state"] == "succeeded", value.dump());
      return value["result"];
    }

    return value;
  };
  call({{"command", "set"}, {"key", "score"}, {"value", 7}});
  const auto saved = call({{"command", "save"}, {"slot", "checkpoint"}, {"overwrite", false}});
  CHECK(saved["name"] == "checkpoint");
  CHECK(saved["bytes"].is_string());
  auto snapshot = call({{"command", "snapshot"}});
  call({{"command", "set"}, {"key", "score"}, {"value", 99}});
  CHECK(call({{"command", "read-save"}, {"slot", "checkpoint"}})["data"]["score"] == 7);
  CHECK(call({{"command", "snapshot"}})["data"]["score"] == 99);
  CHECK_FALSE(data.perform(
      Json{{"command", "load"}, {"slot", "checkpoint"}, {"revision", snapshot["revision"]}}
          .dump()));
  snapshot = call({{"command", "snapshot"}});
  call({{"command", "load"}, {"slot", "checkpoint"}, {"revision", snapshot["revision"]}});
  snapshot = call({{"command", "snapshot"}});
  CHECK(snapshot["data"]["score"] == 7);
  REQUIRE(files.writeTextAtomic("checkpoint.dotsave", "damaged"));
  const auto corrupt = Json::parse(
      data.perform(
              Json{{"command", "load"}, {"slot", "checkpoint"}, {"revision", snapshot["revision"]}}
                  .dump())
          .value());
  CHECK(await(data, corrupt)["state"] == "failed");
  CHECK(call({{"command", "snapshot"}}) == snapshot);
  CHECK(call({{"command", "list-saves"}}).size() == 1);
  call({{"command", "delete-save"}, {"slot", "checkpoint"}});
  CHECK(call({{"command", "list-saves"}}).empty());
}

TEST_CASE("save_queue.fifo_owned_snapshots_terminal_receipts_and_cross_thread_boundaries") {
  Fixture f;
  auto queue = Game::SaveQueue::create(f.files()).value();
  auto state = Game::State::create(R"({"score":7})").value();
  const auto initial = state.snapshot().value();
  auto write = queue.submit({Game::SaveOperation::Write, "checkpoint", initial, false}).value();
  REQUIRE(state.set("score", "99"));
  auto read = queue.submit({Game::SaveOperation::Read, "checkpoint", {}, false}).value();
  auto list = queue.submit({Game::SaveOperation::List, "", {}, false}).value();
  auto completed = await(queue, write);
  REQUIRE(completed.state == Game::SaveJobState::Succeeded);
  CHECK(std::get<Game::SaveSlot>(completed.output).name == "checkpoint");
  CHECK_FALSE(queue.take(write));
  CHECK(std::get<Game::StateSnapshot>(await(queue, read).output).data == initial.data);
  CHECK(std::get<std::vector<Game::SaveSlot>>(await(queue, list).output).size() == 1);
  auto removed = queue.submit({Game::SaveOperation::Remove, "checkpoint", {}, false}).value();
  CHECK(await(queue, removed).state == Game::SaveJobState::Succeeded);
  CHECK(queue.jobs()->empty());
  auto foreign
      = std::async(std::launch::async, [&] { return !queue.jobs() && !queue.status(removed); });
  CHECK(foreign.get());
  auto moved = std::move(queue);
  CHECK_FALSE(queue.jobs());
  REQUIRE(moved.finish());
  CHECK_FALSE(moved.submit({Game::SaveOperation::List, "", {}, false}));
}

TEST_CASE(
    "save_queue.failed_mutations_preserve_old_bytes_and_shutdown_reports_unconsumed_failure") {
  Fixture f;
  auto store = Game::Saves::open(f.files()).value();
  auto data = Game::State::create(R"({"health":80})").value();
  const auto snapshot = data.snapshot().value();
  REQUIRE(store.write("checkpoint", snapshot, false));
  const auto before = f.files().readText("checkpoint.dotsave").value();
  auto queue = Game::SaveQueue::create(f.files()).value();
  const auto refused
      = queue.submit({Game::SaveOperation::Write, "checkpoint", snapshot, false}).value();
  CHECK_FALSE(queue.finish());
  const auto failure = queue.status(refused).value();
  CHECK(failure.state == Game::SaveJobState::Failed);
  REQUIRE(failure.error);
  CHECK(failure.error->message.find("checkpoint") != std::string::npos);
  CHECK(f.files().readText("checkpoint.dotsave").value() == before);
  CHECK(await(queue, refused).state == Game::SaveJobState::Failed);
  CHECK(queue.finish());
}

TEST_CASE("save_queue.reservations_include_completed_jobs_and_invalid_inputs_never_enqueue") {
  Fixture f;
  Game::SaveQueueLimits limits;
  limits.jobs = 1;
  auto queue = Game::SaveQueue::create(f.files(), limits).value();
  auto id = queue.submit({Game::SaveOperation::List, "", {}, false}).value();
  CHECK_FALSE(queue.submit({Game::SaveOperation::List, "", {}, false}));
  CHECK(queue.jobs()->size() == 1);
  CHECK(await(queue, id).state == Game::SaveJobState::Succeeded);
  CHECK(queue.jobs()->empty());
  CHECK_FALSE(queue.status(id));

  for (const auto& request : std::vector<Game::SaveRequest>{
           {Game::SaveOperation::Read, "../escape", {}, false},
           {Game::SaveOperation::Read, "UPPER", {}, false},
           {Game::SaveOperation::List, "unexpected", {}, false},
           {Game::SaveOperation::Write, "slot", {}, false},
           {Game::SaveOperation::Write, "slot", Game::StateSnapshot{0, "{}"}, false},
           {Game::SaveOperation::Write, "slot", Game::StateSnapshot{1, "{broken}"}, false},
           {Game::SaveOperation::Read, "slot", {}, true}}) {
    CHECK_FALSE(queue.submit(request));
    CHECK(queue.jobs()->empty());
  }

  limits.bytes = 1024;
  auto bounded = Game::SaveQueue::create(f.files(), limits).value();
  CHECK_FALSE(bounded.submit({Game::SaveOperation::Read, "slot", {}, false}));
  limits.jobs = 0;
  CHECK_FALSE(Game::SaveQueue::create(f.files(), limits));
  limits.jobs = 257;
  CHECK_FALSE(Game::SaveQueue::create(f.files(), limits));
}

TEST_CASE("save_queue.queued_cancellation_does_not_erase_running_or_completed_mutations") {
  Fixture f;
  auto queue = Game::SaveQueue::create(f.files()).value();
  const auto snapshot = Game::State::create(R"({"value":1})")->snapshot().value();
  std::vector<std::uint64_t> ids;

  for (int i = 0; i < 16; ++i) {
    ids.push_back(
        queue.submit({Game::SaveOperation::Write, "slot-" + std::to_string(i), snapshot, false})
            .value());
  }

  std::vector<bool> cancelled;

  for (const auto id : ids) {
    cancelled.push_back(queue.cancel(id).has_value());
  }

  REQUIRE(queue.finish());

  for (std::size_t i = 0; i < ids.size(); ++i) {
    const auto result = await(queue, ids[i]);
    CHECK((result.state == Game::SaveJobState::Cancelled) == cancelled[i]);
    CHECK(std::filesystem::exists(f.files().root() / ("slot-" + std::to_string(i) + ".dotsave"))
          == !cancelled[i]);
  }

  CHECK(queue.jobs()->empty());
}

TEST_CASE(
    "game_data.background_load_publication_rechecks_revision_and_explicit_snapshot_is_owned") {
  Fixture f;
  Script::GameDataConfig config;
  config.saves = f.files();
  auto data = Script::GameData::create(config).value();
  const auto call
      = [&](const Json& request) { return Json::parse(data.perform(request.dump()).value()); };
  call({{"command", "set"}, {"key", "score"}, {"value", 1}});
  const auto before = call({{"command", "snapshot"}});
  const auto write
      = call({{"command", "save"},
              {"slot", "checkpoint"},
              {"overwrite", false},
              {"snapshot", {{"revision", before["revision"]}, {"data", {{"score", 7}}}}}});
  CHECK(call({{"command", "snapshot"}}) == before);
  CHECK(await(data, write)["state"] == "succeeded");
  const auto load
      = call({{"command", "load"}, {"slot", "checkpoint"}, {"revision", before["revision"]}});
  call({{"command", "set"}, {"key", "score"}, {"value", 99}});
  const auto failed = await(data, load);
  CHECK(failed["state"] == "failed");
  CHECK(failed["error"]["code"] == "InvalidState");
  CHECK(call({{"command", "snapshot"}})["data"]["score"] == 99);
  CHECK_FALSE(data.perform(
      Json{{"command", "take-save"}, {"id", load["id"]}, {"operation", "load"}}.dump()));
  auto other = Script::GameData::create(config).value();
  CHECK_FALSE(other.perform(
      Json{{"command", "take-save"}, {"id", write["id"]}, {"operation", "write"}}.dump()));
  REQUIRE(data.retire());
  CHECK_FALSE(data.perform(R"({"command":"save","slot":"late","overwrite":true})"));
  CHECK(call({{"command", "save-jobs"}}).empty());
}

TEST_CASE("save_queue.initial_disk_failure_can_retry_and_destruction_drains_owned_work") {
  Fixture f;
  REQUIRE(f.files().createDirectories("saves"));
  const auto root = Core::FileSystem::open(f.files().root() / "saves").value();
  std::filesystem::rename(root.root(), f.files().root() / "hidden");

  struct Restore {
    std::filesystem::path from;
    std::filesystem::path to;

    ~Restore() {
      std::error_code error;

      if (std::filesystem::exists(from, error)) {
        std::filesystem::rename(from, to, error);
      }
    }
  } restore{f.files().root() / "hidden", root.root()};

  const auto snapshot = Game::State::create(R"({"value":1})")->snapshot().value();

  {
    auto queue = Game::SaveQueue::create(root).value();
    const auto failed
        = queue.submit({Game::SaveOperation::Write, "checkpoint", snapshot, false}).value();
    CHECK(await(queue, failed).state == Game::SaveJobState::Failed);
    CHECK_FALSE(std::filesystem::exists(root.root()));
    std::filesystem::rename(restore.from, restore.to);
    const auto retry
        = queue.submit({Game::SaveOperation::Write, "checkpoint", snapshot, false}).value();
    CHECK(await(queue, retry).state == Game::SaveJobState::Succeeded);
    REQUIRE(queue.submit({Game::SaveOperation::Write, "drained", snapshot, false}));
  }

  const auto store = Game::Saves::open(root).value();
  CHECK(store.read("checkpoint")->data == snapshot.data);
  CHECK(store.read("drained")->data == snapshot.data);
}

TEST_CASE("game_data.quiescent_foreign_retirement_drains_writes_and_invalidates_retained_handles") {
  Fixture f;
  Script::GameDataConfig config;
  config.saves = f.files();
  config.initial = R"({"score":7})";
  auto data = Script::GameData::create(config).value();
  auto retained = data;
  auto ticket = Json::parse(
      data.perform(R"({"command":"save","slot":"checkpoint","overwrite":false})").value());
  auto retirement = std::async(std::launch::async, [&] { return data.retire(); });
  REQUIRE(retirement.get());
  CHECK(Game::Saves::open(f.files())->read("checkpoint")->data == config.initial);
  CHECK_FALSE(retained.perform(R"({"command":"set","key":"score","value":0})"));
  CHECK(await(retained, ticket)["state"] == "succeeded");
  CHECK(Json::parse(retained.perform(R"({"command":"snapshot"})").value())["data"]["score"] == 7);
}

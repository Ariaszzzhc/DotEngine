#include <doctest/doctest.h>

#include <filesystem>
#include <future>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Core.Result;
import Dot.Game.State;
import Dot.Game.Saves;

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

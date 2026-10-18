#include <doctest/doctest.h>

#if defined(__APPLE__)
#  include <sys/stat.h>
#endif

#include <expected>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <utility>

import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Core.Result;

namespace {
  struct Parent {
    std::filesystem::path root;

    Parent() {
      std::random_device random;
      root = std::filesystem::temp_directory_path()
             / ("dot-stage-test-" + std::to_string(random()) + "-" + std::to_string(random()));
      REQUIRE(std::filesystem::create_directory(root));
    }

    ~Parent() {
      std::error_code ignored;
      std::filesystem::remove_all(root, ignored);
    }

    auto files() const -> Dot::Core::FileSystem {
      return Dot::Core::FileSystem::open(root).value();
    }
  };
}  // namespace

TEST_CASE("staged_directory_owned_unpublished_contents_are_removed_and_moves_transfer_ownership") {
  Parent parent;
  std::filesystem::path path;

  {
    auto staging = Dot::Core::StagedDirectory::create(parent.files()).value();
    auto files = staging.files().value();
    path = files.root();
    REQUIRE(files.writeTextAtomic("value.txt", "owned"));
    auto moved = std::move(staging);
    CHECK_FALSE(staging.files());
    CHECK_FALSE(staging.publish("unused"));
    CHECK(moved.files()->readText("value.txt").value() == "owned");
  }

  CHECK_FALSE(std::filesystem::exists(path));
  CHECK(std::filesystem::is_empty(parent.root));
}

TEST_CASE("staged_directory_publication_is_complete_no_replace_and_retryable") {
  Parent parent;
  REQUIRE(parent.files().createDirectories("existing"));
  REQUIRE(parent.files().writeTextAtomic("existing/user.txt", "untouched"));
  auto staging = Dot::Core::StagedDirectory::create(parent.files()).value();
  auto files = staging.files().value();
  REQUIRE(files.createDirectories("nested"));
  REQUIRE(files.writeTextAtomic("nested/project.txt", "complete"));
  CHECK_FALSE(staging.publish("existing"));
  CHECK(parent.files().readText("existing/user.txt").value() == "untouched");
  CHECK(staging.files()->readText("nested/project.txt").value() == "complete");
  auto published = staging.publish(std::filesystem::path{std::u8string{u8"游戏"}});
  REQUIRE(published);
  CHECK(published->readText("nested/project.txt").value() == "complete");
  CHECK_FALSE(staging.files());
  CHECK_FALSE(staging.publish("twice"));
}

TEST_CASE("staged_directory_existing_empty_directories_files_and_symlinks_are_not_replaced") {
  Parent parent;
  REQUIRE(parent.files().createDirectories("empty"));
  REQUIRE(parent.files().writeTextAtomic("file", "user"));
  std::filesystem::create_directory_symlink(parent.root / "empty", parent.root / "link");
  auto staging = Dot::Core::StagedDirectory::create(parent.files()).value();
  REQUIRE(staging.files()->writeTextAtomic("payload", "data"));

  for (const auto* name : {"empty", "file", "link"}) {
    auto result = staging.publish(name);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Dot::Core::ErrorCode::Io);
  }

  CHECK(std::filesystem::is_empty(parent.root / "empty"));
  CHECK(std::filesystem::is_symlink(parent.root / "link"));
  CHECK(parent.files().readText("file").value() == "user");
  REQUIRE(staging.publish("final"));
}

TEST_CASE("staged_directory_invalid_names_and_concurrent_publish_preserve_the_winner") {
  Parent parent;
  auto first = Dot::Core::StagedDirectory::create(parent.files()).value();
  auto second = Dot::Core::StagedDirectory::create(parent.files()).value();
  REQUIRE(first.files()->writeTextAtomic("value", "first"));
  REQUIRE(second.files()->writeTextAtomic("value", "second"));

  for (const auto& name : {std::string{}, std::string{"."}, std::string{".."}, std::string{"a/b"},
                           std::string{"/absolute"}, std::string{"a\\b"}, std::string{"x\0y", 3}}) {
    CHECK_FALSE(first.publish(name));
  }

  bool a = false;
  bool b = false;
  std::thread one([&] { a = first.publish("race").has_value(); });
  std::thread two([&] { b = second.publish("race").has_value(); });
  one.join();
  two.join();
  CHECK(a != b);
  CHECK(parent.files().readText("race/value").value() == (a ? "first" : "second"));
}

TEST_CASE("staged_directory.explicit_discard_releases_ownership_and_failure_can_retry") {
  Parent parent;
  auto staging = Dot::Core::StagedDirectory::create(parent.files()).value();
  const auto root = staging.files()->root();
  REQUIRE(staging.files()->writeTextAtomic("owned.txt", "owned"));
#if !defined(_WIN32)
  const auto permissions = std::filesystem::status(root).permissions();

  struct Restore {
    std::filesystem::path path;
    std::filesystem::perms permissions;

    ~Restore() {
      std::error_code ignored;
      std::filesystem::permissions(path, permissions, ignored);
    }
  } restore{root, permissions};

  std::filesystem::permissions(
      root, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
  auto rejected = staging.discard();

  if (!rejected) {
    CHECK(rejected.error().code == Dot::Core::ErrorCode::Io);
    CHECK(rejected.error().message.find("temporary directory") != std::string::npos);
    CHECK(staging.files());
    std::filesystem::permissions(root, permissions);
  } else {
    WARN_MESSAGE(false,
                 "Current credentials bypass directory permissions; failure fixture skipped");
  }
#endif
  REQUIRE(staging.discard());
  CHECK_FALSE(staging.files());
  CHECK_FALSE(std::filesystem::exists(root));
  REQUIRE(staging.discard());
  CHECK(std::filesystem::is_empty(parent.root));
}

TEST_CASE("staged_directory.replacement_owns_previous_and_can_restore_across_parents") {
  Parent parent;
  REQUIRE(parent.files().createDirectories("assets/model"));
  REQUIRE(parent.files().writeTextAtomic("assets/model/value", "old"));
  REQUIRE(parent.files().createDirectories("private"));
  const auto hidden = Dot::Core::FileSystem::open(parent.root / "private").value();
  const auto assets = Dot::Core::FileSystem::open(parent.root / "assets").value();
  auto staging = Dot::Core::StagedDirectory::create(hidden).value();
  REQUIRE(staging.files()->writeTextAtomic("value", "new"));
  auto previous = staging.replace(assets, "model");
  REQUIRE(previous);
  CHECK_FALSE(staging.files());
  CHECK(parent.files().readText("assets/model/value").value() == "new");
  CHECK(previous->files()->readText("value").value() == "old");
  const auto backup = previous->files()->root();

  {
    auto displaced = previous->replace(assets, "model");
    REQUIRE(displaced);
    CHECK(displaced->files()->readText("value").value() == "new");
    CHECK(parent.files().readText("assets/model/value").value() == "old");
    CHECK_FALSE(previous->files());
  }

  CHECK_FALSE(std::filesystem::exists(backup));
  CHECK(std::filesystem::is_empty(hidden.root()));
  CHECK(parent.files().readText("assets/model/value").value() == "old");
}

TEST_CASE("staged_directory.replacement_rejects_invalid_targets_without_losing_either_tree") {
  Parent parent;
  REQUIRE(parent.files().createDirectories("model"));
  REQUIRE(parent.files().writeTextAtomic("model/value", "old"));
  REQUIRE(parent.files().writeTextAtomic("file", "user"));
  std::filesystem::create_directory_symlink(parent.root / "model", parent.root / "link");
  auto staging = Dot::Core::StagedDirectory::create(parent.files()).value();
  REQUIRE(staging.files()->writeTextAtomic("value", "new"));

  for (const auto* name : {"", ".", "..", "a/b", "a\\b", "/absolute", "absent", "file", "link"}) {
    CHECK_FALSE(staging.replace(name));
  }

  CHECK_FALSE(staging.replace(staging.files()->root().filename()));
  CHECK(staging.files()->readText("value").value() == "new");
  CHECK(parent.files().readText("model/value").value() == "old");
  CHECK(parent.files().readText("file").value() == "user");
}

#if defined(__APPLE__)
TEST_CASE("staged_directory.failed_publication_restores_original_name_and_retains_candidate") {
  Parent parent;
  REQUIRE(parent.files().createDirectories("model"));
  REQUIRE(parent.files().writeTextAtomic("model/value", "old"));
  auto staging = Dot::Core::StagedDirectory::create(parent.files()).value();
  REQUIRE(staging.files()->writeTextAtomic("value", "new"));
  const auto root = staging.files()->root();

  struct Flags {
    std::filesystem::path path;

    ~Flags() {
      chflags(path.c_str(), 0);
    }
  } flags{root};

  REQUIRE(chflags(root.c_str(), UF_IMMUTABLE) == 0);
  auto rejected = staging.replace("model");
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().message.find("original directory restored") != std::string::npos);
  CHECK(parent.files().readText("model/value").value() == "old");
  CHECK(staging.files()->readText("value").value() == "new");
  CHECK(std::distance(std::filesystem::directory_iterator(parent.root),
                      std::filesystem::directory_iterator{})
        == 2);
}
#endif

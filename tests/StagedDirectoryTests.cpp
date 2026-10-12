#include <doctest/doctest.h>

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

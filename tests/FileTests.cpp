#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

import Dot.Core.Files;
import Dot.Core.Result;

namespace {
  namespace Fs = std::filesystem;
  using Dot::Core::ErrorCode;
  using Dot::Core::FileSystem;

  struct Directory {
    Fs::path base;
    Fs::path root;
    Fs::path outside;

    Directory() {
      static std::atomic<unsigned> sequence{0};

      for (int attempt = 0; attempt < 64; ++attempt) {
        base = Fs::current_path()
               / std::format("file-test-{}-{}",
                             std::chrono::steady_clock::now().time_since_epoch().count(),
                             sequence.fetch_add(1));

        if (Fs::create_directory(base)) {
          root = base / "project";
          outside = base / "outside";
          Fs::create_directory(root);
          Fs::create_directory(outside);
          return;
        }
      }

      throw std::runtime_error("Cannot create test directory");
    }

    ~Directory() {
      std::error_code ignored;
      Fs::remove_all(base, ignored);
    }

    auto files() const -> FileSystem {
      return FileSystem::open(root).value();
    }
  };
}  // namespace

TEST_CASE("files.root_validation") {
  Directory directory;
  auto files = FileSystem::open(directory.root);
  REQUIRE(files.has_value());
  CHECK(files->root() == Fs::canonical(directory.root));
  CHECK_FALSE(FileSystem::open({}).has_value());
  auto nulRoot = directory.root.native();
  nulRoot.push_back(Fs::path::value_type{});
  nulRoot.append(Fs::path{"suffix"}.native());
  CHECK_FALSE(FileSystem::open(Fs::path{nulRoot}));
  CHECK_FALSE(FileSystem::open(directory.base / "missing").has_value());
  std::ofstream(directory.root / "file.txt") << "file";
  CHECK_FALSE(FileSystem::open(directory.root / "file.txt").has_value());
}

TEST_CASE("files.path_constraints") {
  Directory directory;
  const auto files = directory.files();
  CHECK_FALSE(files.resolve({}).has_value());
  CHECK_FALSE(files.resolve(directory.outside).has_value());
  CHECK_FALSE(files.resolve("../outside/file.txt").has_value());
  CHECK_FALSE(files.resolve("../../escape.txt").has_value());
  auto nested = files.resolve("assets/../scenes/main.json");
  REQUIRE(nested.has_value());
  CHECK(*nested == files.root() / "scenes/main.json");
}

TEST_CASE("files.create_directories") {
  Directory directory;
  const auto files = directory.files();
  REQUIRE(files.createDirectories("assets/textures").has_value());
  CHECK(Fs::is_directory(directory.root / "assets/textures"));
  CHECK(files.createDirectories("assets/textures").has_value());
  CHECK_FALSE(files.createDirectories("../outside/blocked").has_value());
  CHECK_FALSE(Fs::exists(directory.outside / "blocked"));
}

TEST_CASE("files.text_round_trip_and_replacement") {
  Directory directory;
  const auto files = directory.files();
  REQUIRE(files.writeTextAtomic("scene.json", "{\"name\":\"Scene\"}").has_value());
  auto content = files.readText("scene.json");
  REQUIRE(content.has_value());
  CHECK(*content == "{\"name\":\"Scene\"}");
  REQUIRE(files.writeTextAtomic("scene.json", "Short").has_value());
  content = files.readText("scene.json");
  REQUIRE(content.has_value());
  CHECK(*content == "Short");
  CHECK(std::distance(Fs::directory_iterator(directory.root), Fs::directory_iterator{}) == 1);
}

TEST_CASE("files.binary_and_empty_round_trip") {
  Directory directory;
  const auto files = directory.files();
  const std::vector<std::byte> bytes{std::byte{0}, std::byte{255}, std::byte{10}};
  REQUIRE(files.writeAtomic("binary.dat", bytes).has_value());
  auto content = files.readBytes("binary.dat");
  REQUIRE(content.has_value());
  CHECK(*content == bytes);
  REQUIRE(files.writeAtomic("binary.dat", {}).has_value());
  content = files.readBytes("binary.dat", 0);
  REQUIRE(content.has_value());
  CHECK(content->empty());
  REQUIRE(files.readText("binary.dat").has_value());
  CHECK(files.readText("binary.dat")->empty());
}

TEST_CASE("files.unicode_paths_and_content") {
  Directory directory;
  const auto files = directory.files();
  const Fs::path filename{u8"场景-世界.json"};
  REQUIRE(files.writeTextAtomic(filename, "世界 🌏").has_value());
  auto content = files.readText(filename);
  REQUIRE(content.has_value());
  CHECK(*content == "世界 🌏");
}

TEST_CASE("files.text_preserves_embedded_null") {
  Directory directory;
  const auto files = directory.files();
  const std::string input{"a\0b", 3};
  REQUIRE(files.writeTextAtomic("text.dat", input).has_value());
  const auto content = files.readText("text.dat");
  REQUIRE(content.has_value());
  CHECK(*content == input);
}

TEST_CASE("files.read_limits_and_missing_file") {
  Directory directory;
  const auto files = directory.files();
  auto missing = files.readText("missing.txt");
  REQUIRE_FALSE(missing.has_value());
  CHECK(missing.error().code == ErrorCode::NotFound);
  REQUIRE(files.writeTextAtomic("data.txt", "12345").has_value());
  CHECK_FALSE(files.readText("data.txt", 4).has_value());
  CHECK(files.readText("data.txt", 5).has_value());
  CHECK_FALSE(files.readText(".").has_value());
}

TEST_CASE("files.failed_write_cleans_temporary_file") {
  Directory directory;
  const auto files = directory.files();
  Fs::create_directory(directory.root / "destination");
  CHECK_FALSE(files.writeTextAtomic("destination", "Cannot replace directory").has_value());
  CHECK(Fs::is_directory(directory.root / "destination"));
  CHECK(std::distance(Fs::directory_iterator(directory.root), Fs::directory_iterator{}) == 1);
  CHECK_FALSE(files.writeTextAtomic("missing/parent.txt", "No parent").has_value());
  CHECK_FALSE(files.writeTextAtomic("../outside/blocked.txt", "Blocked").has_value());
  CHECK_FALSE(Fs::exists(directory.outside / "blocked.txt"));
}

#if !defined(_WIN32)
TEST_CASE("files.symlink_escape_is_rejected") {
  Directory directory;
  const auto files = directory.files();
  Fs::create_directory_symlink(directory.outside, directory.root / "link");
  CHECK_FALSE(files.resolve("link/escape.txt").has_value());
  CHECK_FALSE(files.writeTextAtomic("link/escape.txt", "Blocked").has_value());
  CHECK_FALSE(Fs::exists(directory.outside / "escape.txt"));
}

TEST_CASE("files.replacement_preserves_permissions") {
  Directory directory;
  const auto files = directory.files();
  REQUIRE(files.writeTextAtomic("game.txt", "Old").has_value());
  Fs::permissions(directory.root / "game.txt",
                  Fs::perms::owner_read | Fs::perms::owner_write | Fs::perms::group_read);
  const auto expected = Fs::status(directory.root / "game.txt").permissions();
  REQUIRE(files.writeTextAtomic("game.txt", "New").has_value());
  CHECK(Fs::status(directory.root / "game.txt").permissions() == expected);
}
#endif

TEST_CASE("files.concurrent_replacement_has_complete_snapshots") {
  Directory directory;
  const auto files = directory.files();
  const std::string oldContent(16 * 1024, 'a');
  const std::string newContent(32 * 1024, 'b');
  REQUIRE(files.writeTextAtomic("save.dat", oldContent).has_value());
  std::atomic<bool> done{false};
  std::atomic<unsigned> failures{0};
  std::jthread writer([&] {
    for (int iteration = 0; iteration < 30; ++iteration) {
      if (!files.writeTextAtomic("save.dat", iteration % 2 ? oldContent : newContent)) {
        ++failures;
      }
    }

    done = true;
  });

  do {
    const auto snapshot = files.readText("save.dat");

    if (!snapshot || (*snapshot != oldContent && *snapshot != newContent)) {
      ++failures;
    }
  } while (!done.load());

  writer.join();
  CHECK(failures.load() == 0);
  CHECK(std::distance(Fs::directory_iterator(directory.root), Fs::directory_iterator{}) == 1);
}

TEST_CASE("files.exclusive_creation_keeps_complete_binary_empty_and_unicode_contents") {
  Directory d;
  auto files = d.files();
  const std::vector<std::byte> data{std::byte{0}, std::byte{255}, std::byte{10}};
  REQUIRE(files.createAtomic("binary.dat", data));
  CHECK(files.readBytes("binary.dat").value() == data);
  const auto conflict = files.createTextAtomic("binary.dat", "overwrite");
  REQUIRE_FALSE(conflict);
  CHECK(conflict.error().code == ErrorCode::InvalidState);
  CHECK(files.readBytes("binary.dat").value() == data);
  REQUIRE(files.createAtomic("empty.dat", {}));
  CHECK(files.readBytes("empty.dat", 0)->empty());
  REQUIRE(files.createTextAtomic(Fs::path{u8"姿态.json"}, "reference 世界"));
  CHECK(files.readText(Fs::path{u8"姿态.json"}).value() == "reference 世界");
  CHECK(std::distance(Fs::directory_iterator(d.root), Fs::directory_iterator{}) == 3);
}

TEST_CASE("files.exclusive_creation_rejects_occupied_paths_and_cleans_failed_staging") {
  Directory d;
  auto files = d.files();
  REQUIRE(files.createTextAtomic("original.dat", "original"));
  Fs::create_hard_link(d.root / "original.dat", d.root / "alias.dat");
  REQUIRE(Fs::create_directory(d.root / "directory"));
  CHECK_FALSE(files.createTextAtomic("alias.dat", "changed"));
  CHECK(files.readText("original.dat").value() == "original");
  CHECK_FALSE(files.createTextAtomic("directory", "changed"));
  CHECK(Fs::is_directory(d.root / "directory"));

  for (const auto& path : {Fs::path{}, Fs::path{"."}, Fs::path{".."}, Fs::path{"trailing/"},
                           Fs::path{"../outside/escape.dat"}, Fs::path{"missing/file.dat"},
                           d.outside / "absolute.dat", Fs::path{std::string{"nul\0tail", 8}}}) {
    CHECK_FALSE(files.createTextAtomic(path, "blocked"));
  }

  CHECK_FALSE(Fs::exists(d.root / "nul"));
  CHECK(std::distance(Fs::directory_iterator(d.root), Fs::directory_iterator{}) == 3);
  auto copy = files;
  auto moved = std::move(files);
  CHECK(copy.root() == moved.root());
  copy = std::move(moved);
  CHECK_FALSE(moved.resolve("original.dat"));
  moved = copy;
  CHECK_FALSE(files.createTextAtomic("moved.dat", "blocked"));
  CHECK_FALSE(files.writeTextAtomic("moved.dat", "blocked"));
  CHECK_FALSE(files.resolve("moved.dat"));
  CHECK(moved.readText("original.dat").value() == "original");
}

#if !defined(_WIN32)
TEST_CASE("files.exclusive_creation_preserves_final_symlinks_including_dangling_targets") {
  Directory d;
  const auto files = d.files();
  REQUIRE(files.createTextAtomic("real.dat", "original"));
  Fs::create_symlink("real.dat", d.root / "linked.dat");
  Fs::create_symlink("missing.dat", d.root / "dangling.dat");
  Fs::create_symlink(d.outside / "outside.dat", d.root / "outside-link.dat");
  Fs::create_directory_symlink(d.outside, d.root / "external-parent");
  CHECK_FALSE(files.createTextAtomic("linked.dat", "changed"));
  CHECK_FALSE(files.createTextAtomic("dangling.dat", "changed"));
  CHECK_FALSE(files.createTextAtomic("outside-link.dat", "changed"));
  CHECK_FALSE(files.createTextAtomic("external-parent/changed.dat", "changed"));
  CHECK(files.readText("real.dat").value() == "original");
  CHECK(Fs::is_symlink(Fs::symlink_status(d.root / "dangling.dat")));
  CHECK_FALSE(Fs::exists(d.root / "missing.dat"));
  CHECK_FALSE(Fs::exists(d.outside / "outside.dat"));
  CHECK(std::distance(Fs::directory_iterator(d.root), Fs::directory_iterator{}) == 5);
  REQUIRE(Fs::create_directory(d.root / "nested"));
  Fs::create_directory_symlink("nested", d.root / "internal-parent");
  REQUIRE(files.createTextAtomic("internal-parent/allowed.dat", "inside"));
  CHECK(files.readText("nested/allowed.dat").value() == "inside");
}
#endif

TEST_CASE("files.concurrent_exclusive_creators_have_one_winner_and_no_partial_reader_snapshot") {
  Directory d;
  constexpr std::size_t count = 8;
  std::array<std::string, count> payloads;

  for (std::size_t i = 0; i < count; ++i) {
    payloads[i] = std::to_string(i) + std::string((i + 1) * 64 * 1024, char('a' + i));
  }

  std::barrier start{static_cast<std::ptrdiff_t>(count + 1)};
  std::atomic<unsigned> completed{};
  std::atomic<unsigned> winners{};
  std::atomic<unsigned> failures{};
  std::vector<std::jthread> writers;

  for (std::size_t i = 0; i < count; ++i) {
    writers.emplace_back([&, i] {
      const auto files = FileSystem::open(d.root).value();
      start.arrive_and_wait();
      const auto result = files.createTextAtomic("shared.dat", payloads[i]);

      if (result) {
        ++winners;
      } else if (result.error().code != ErrorCode::InvalidState) {
        ++failures;
      }

      ++completed;
    });
  }

  start.arrive_and_wait();
  const auto reader = d.files();

  do {
    auto snapshot = reader.readText("shared.dat");

    if (snapshot) {
      if (std::ranges::find(payloads, *snapshot) == payloads.end()) {
        ++failures;
      }
    } else if (snapshot.error().code != ErrorCode::NotFound) {
      ++failures;
    }
  } while (completed.load() != count);

  writers.clear();
  CHECK(winners.load() == 1);
  CHECK(failures.load() == 0);
  const auto saved = reader.readText("shared.dat").value();
  CHECK(std::ranges::find(payloads, saved) != payloads.end());
  CHECK(std::distance(Fs::directory_iterator(d.root), Fs::directory_iterator{}) == 1);
}

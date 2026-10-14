#include <doctest/doctest.h>

#include <filesystem>
#include <thread>

import Dot.Platform.ExternalFiles;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;

TEST_CASE("external_file.invalid_paths_and_worker_calls_never_launch_an_application") {
  auto parent = Dot::Core::FileSystem::open(std::filesystem::temp_directory_path()).value();
  auto storage = Dot::Core::StagedDirectory::create(parent).value();
  auto files = storage.files().value();
  REQUIRE(files.writeTextAtomic("owned.txt", "test"));
  CHECK_FALSE(Dot::Platform::openExternalFile(files.root()));
  CHECK_FALSE(Dot::Platform::openExternalFile(files.root() / "missing.txt"));
  bool rejected = false;
  std::thread worker(
      [&] { rejected = !Dot::Platform::openExternalFile(files.root() / "owned.txt"); });
  worker.join();
  CHECK(rejected);
  CHECK(files.readText("owned.txt").value() == "test");
}

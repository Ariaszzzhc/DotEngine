#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <utility>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Project.ScenePreparation;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace {
  using namespace Dot;

  struct Fixture {
    Core::StagedDirectory storage
        = Core::StagedDirectory::create(
              Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Core::FileSystem files = storage.files().value();
    World::Scene scene = World::Scene::create("Prepared").value();

    Fixture() {
      REQUIRE(files.createDirectories("scenes"));
      REQUIRE(files.createDirectories("assets"));
      auto node = scene.createEntity("Panel").value();
      REQUIRE(scene.setMeshRenderer(node, World::MeshRenderer{}));
      REQUIRE(World::saveScene(files, "scenes/main.dotscene", scene));
    }
  };

  auto wait(Project::ScenePreparation& job) -> void {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};

    while (!job.ready().value()) {
      REQUIRE(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  }
}  // namespace

TEST_CASE("scene_preparation.owned_proof_rehydrates_on_calling_thread_and_matches_exact_content") {
  Fixture f;
  auto job = Project::ScenePreparation::begin(f.files, "scenes/main.dotscene").value();
  wait(job);
  auto result = job.take();
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  auto prepared = std::move(*result);
  CHECK_FALSE(job.take());
  auto scene = prepared.scene().value();
  CHECK(scene.name().value() == "Prepared");
  CHECK(prepared.accepts(f.files, scene).value());
  auto copy = prepared;
  REQUIRE(scene.rename("Changed"));
  CHECK_FALSE(prepared.accepts(f.files, scene).value());
  REQUIRE(f.files.createDirectories("other"));
  auto other = Core::FileSystem::open(f.files.root() / "other").value();
  CHECK_FALSE(prepared.accepts(other, copy.scene().value()).value());
  bool owned{};
  std::thread caller([&] {
    auto local = copy.scene();
    owned = local && local->entities()->size() == 1 && copy.accepts(f.files, *local).value();
  });
  caller.join();
  CHECK(owned);
  auto moved = std::move(prepared);
  CHECK_FALSE(prepared.scene());
  CHECK_FALSE(prepared.accepts(f.files, scene));
  CHECK(moved.scene()->name().value() == "Prepared");
}

TEST_CASE("scene_preparation.strict_paths_bad_sources_and_assets_reject_without_fallback") {
  Fixture f;

  for (const auto* path : {"../outside.dotscene", "scenes/no.txt", ".dot-stage-x/a.dotscene",
                           "scenes/.dot-stage-x/a.dotscene"}) {
    CHECK_FALSE(Project::ScenePreparation::begin(f.files, path));
  }

  REQUIRE(f.files.writeTextAtomic("scenes/broken.dotscene", "{}"));

  for (const auto* path :
       {"scenes/missing.dotscene", "scenes/broken.dotscene", "scenes/MAIN.dotscene"}) {
    auto job = Project::ScenePreparation::begin(f.files, path).value();
    wait(job);
    CHECK_FALSE(job.take());
    CHECK_FALSE(job.take());
  }

  auto node = f.scene.entities()->front().id;
  REQUIRE(f.scene.setMaterial(node, World::MaterialBinding{"assets/missing.dotmaterial"}));
  auto job = Project::ScenePreparation::begin(f.files, "scenes/main.dotscene",
                                              World::encodeScene(f.scene).value())
                 .value();
  wait(job);
  auto failed = job.take();
  REQUIRE_FALSE(failed);
  CHECK(failed.error().code == Core::ErrorCode::NotFound);
  CHECK(World::loadScene(f.files, "scenes/main.dotscene")->entities()->front().material
        == std::nullopt);
}

TEST_CASE("scene_preparation.initial_bytes_moves_owner_cancellation_and_quiescent_release") {
  Fixture f;
  REQUIRE(f.scene.rename("Unsaved initial"));
  auto bytes = World::encodeScene(f.scene).value();
  auto original = Project::ScenePreparation::begin(f.files, "scenes/main.dotscene", bytes).value();
  auto job = std::move(original);
  CHECK_FALSE(original.ready());
  bool rejected{};
  std::thread caller([&] { rejected = !job.ready() && !job.take(); });
  caller.join();
  CHECK(rejected);
  wait(job);
  auto prepared = job.take().value();
  REQUIRE(f.files.writeTextAtomic("scenes/main.dotscene", "externally changed after completion"));
  CHECK(prepared.scene()->name().value() == "Unsaved initial");

  for (int index = 0; index < 1000; ++index) {
    REQUIRE(f.scene.createEntity("Lifetime node"));
  }

  bytes = World::encodeScene(f.scene).value();
  auto stopped = Project::ScenePreparation::begin(f.files, "scenes/main.dotscene", bytes).value();
  stopped.requestStop();
  wait(stopped);
  auto result = stopped.take();
  CHECK((result || result.error().code == Core::ErrorCode::Unavailable));
  auto pending = Project::ScenePreparation::begin(f.files, "scenes/main.dotscene", bytes).value();
  std::thread release([owned = std::move(pending)]() mutable { owned.requestStop(); });
  release.join();
  CHECK_FALSE(pending.ready());
}

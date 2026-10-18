#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Project.ScenePreparation;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Animation.Files;
import Dot.Animation.ControllerFiles;
import Dot.Animation.ScenePlayers;
import Dot.Math;
import Dot.UI.Files;
import Dot.Script.Hud;

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
  CHECK_FALSE(prepared.animationClips());
  CHECK_FALSE(prepared.accepts(f.files, scene));
  CHECK(moved.scene()->name().value() == "Prepared");
}

TEST_CASE("scene_preparation.initial_animation_clips_are_unique_owned_and_thread_portable") {
  Fixture f;
  const auto target = f.scene.entities()->front().id;
  const auto otherTarget = f.scene.createEntity("Other target").value();
  const auto owner = f.scene.createEntity("Player").value();
  const auto other = f.scene.createEntity("Other player").value();
  auto clip = Animation::Clip::create({"Move",
                                       {{0,
                                         Animation::Path::Translation,
                                         Animation::Interpolation::Linear,
                                         {{0, {0, 0, 0, 0}}, {2, {2, 0, 0, 0}}}}}})
                  .value();
  REQUIRE(Animation::saveClip(f.files, "assets/move.dotanimation", clip));
  REQUIRE(f.scene.setAnimationPlayer(
      owner, World::AnimationPlayer{{"assets/move.dotanimation"}, {{0, target.value}}}));
  REQUIRE(f.scene.setAnimationPlayer(
      other, World::AnimationPlayer{{"assets/move.dotanimation"}, {{0, otherTarget.value}}}));
  REQUIRE(World::saveScene(f.files, "scenes/main.dotscene", f.scene));
  auto job = Project::ScenePreparation::begin(f.files, "scenes/main.dotscene").value();
  wait(job);
  auto prepared = job.take().value();
  REQUIRE(prepared.animationClips()->size() == 1);
  CHECK(prepared.animationClips()->front().asset == "assets/move.dotanimation");
  bool advanced{};
  std::thread caller([copy = prepared, files = f.files, target = target.value,
                      otherTarget = otherTarget.value, &advanced] {
    auto scene = std::make_shared<World::Scene>(copy.scene().value());
    auto players
        = Animation::ScenePlayers::createPrepared(scene, files, copy.animationClips().value());
    advanced = players && players->advance(.5)
               && scene->transform(scene->findEntity(target).value())->position.x == .5F
               && scene->transform(scene->findEntity(otherTarget).value())->position.x == .5F;
  });
  caller.join();
  CHECK(advanced);
  CHECK(f.scene.transform(target)->position.x == 0);
  REQUIRE(f.files.writeTextAtomic("assets/move.dotanimation",
                                  "changed after the owned data was consumed"));
  CHECK(prepared.animationClips()->front().value.sample(.5)->front().value.x == .5F);
}

TEST_CASE(
    "scene_preparation.animation_sources_and_shared_reference_budgets_fail_without_fallback") {
  Fixture f;
  const auto target = f.scene.entities()->front().id;
  const auto owner = f.scene.createEntity("Player").value();
  World::AnimationPlayer properties{{"assets/missing.dotanimation"}, {{0, target.value}}};
  properties.enabled = false;
  REQUIRE(f.scene.setAnimationPlayer(owner, properties));
  auto disabled = Project::ScenePreparation::begin(f.files, "scenes/main.dotscene",
                                                   World::encodeScene(f.scene).value())
                      .value();
  wait(disabled);
  CHECK(disabled.take()->animationClips()->empty());
  properties.enabled = true;
  REQUIRE(f.files.writeTextAtomic("assets/bad.dotanimation", "{}"));

  for (const auto* path :
       {"assets/missing.dotanimation", "assets/bad.dotanimation", "assets/BAD.dotanimation"}) {
    properties.clips = {path};
    REQUIRE(f.scene.setAnimationPlayer(owner, properties));
    auto job = Project::ScenePreparation::begin(f.files, "scenes/main.dotscene",
                                                World::encodeScene(f.scene).value())
                   .value();
    wait(job);
    CHECK_FALSE(job.take());
  }

  Animation::ClipDescription description;
  description.name = "Reference budget";

  for (std::uint32_t index = 0; index < 15001; ++index) {
    const auto path = static_cast<Animation::Path>(index % 3);
    const auto value = path == Animation::Path::Rotation ? Dot::Math::Vec4{0, 0, 0, 1}
                       : path == Animation::Path::Scale  ? Dot::Math::Vec4{1, 1, 1, 0}
                                                         : Dot::Math::Vec4{};
    description.tracks.push_back({index / 3, path, Animation::Interpolation::Step, {{0, value}}});
  }

  auto clip = Animation::Clip::create(std::move(description)).value();
  REQUIRE(Animation::saveClip(f.files, "assets/budget.dotanimation", clip));
  properties.clips = {"assets/budget.dotanimation"};
  REQUIRE(f.scene.setAnimationPlayer(owner, properties));
  const auto other = f.scene.createEntity("Other player").value();
  REQUIRE(f.scene.setAnimationPlayer(other, properties));
  auto job = Project::ScenePreparation::begin(f.files, "scenes/main.dotscene",
                                              World::encodeScene(f.scene).value())
                 .value();
  wait(job);
  auto failed = job.take();
  REQUIRE_FALSE(failed);
  CHECK(failed.error().code == Core::ErrorCode::Unavailable);
  CHECK(failed.error().message.find("channel budget") != std::string::npos);
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

TEST_CASE("scene_preparation.controllers_are_owned_shared_and_required_without_disk_fallback") {
  Fixture f;
  auto target = f.scene.entities()->front().id;
  auto owner = f.scene.createEntity("Player").value();
  const auto clip = Animation::Clip::create({"Move",
                                             {{0,
                                               Animation::Path::Translation,
                                               Animation::Interpolation::Linear,
                                               {{0, {0, 0, 0, 0}}, {2, {2, 0, 0, 0}}}}}})
                        .value();
  REQUIRE(Animation::saveClip(f.files, "assets/move.dotanimation", clip));
  World::AnimationPlayer properties{{"assets/move.dotanimation"}, {{0, target.value}}};
  properties.controller = "assets/motion.dotcontroller";
  REQUIRE(f.scene.setAnimationPlayer(owner, properties));
  auto missing = Project::ScenePreparation::begin(f.files, "scenes/main.dotscene",
                                                  World::encodeScene(f.scene).value())
                     .value();
  wait(missing);
  CHECK_FALSE(missing.take());
  auto graph = Animation::Controller::create(
                   {"Graph", "Idle", {}, {{"Idle", properties.clips.front()}}, {}})
                   .value();
  REQUIRE(Animation::saveController(f.files, *properties.controller, graph));
  auto other = f.scene.createEntity("Other").value();
  properties.targets.front().entity = other.value;
  REQUIRE(f.scene.setAnimationPlayer(other, properties));
  auto job = Project::ScenePreparation::begin(f.files, "scenes/main.dotscene",
                                              World::encodeScene(f.scene).value())
                 .value();
  wait(job);
  auto prepared = job.take().value();
  REQUIRE(prepared.animationControllers()->size() == 1);
  CHECK(prepared.animationControllers()->front().asset == *properties.controller);
  REQUIRE(std::filesystem::remove(f.files.root() / *properties.controller));
  bool advanced{};
  std::thread caller([&] {
    auto scene = std::make_shared<World::Scene>(prepared.scene().value());
    auto players = Animation::ScenePlayers::createPrepared(
        scene, f.files, prepared.animationClips().value(), prepared.animationControllers().value());
    advanced = players && players->advance(.5)
               && players->state(scene->findEntity(owner.value).value())
                          ->controller->playback.elapsedSeconds
                      == .5;
  });
  caller.join();
  CHECK(advanced);
  auto moved = std::move(prepared);
  CHECK_FALSE(prepared.animationControllers());
  CHECK(moved.animationControllers()->front().value.description().value()->name == "Graph");
}

TEST_CASE("scene_preparation.ui_defaults_are_owned_and_missing_resources_fail_before_publication") {
  Fixture f;
  REQUIRE(f.scene.setUi("assets/menu.dotui"));
  auto missing = Project::ScenePreparation::begin(f.files, "scenes/main.dotscene",
                                                  World::encodeScene(f.scene).value())
                     .value();
  wait(missing);
  CHECK_FALSE(missing.take());
  auto layout = UI::Layout::create({"Prepared UI", {UI::Node{"panel"}}}).value();
  REQUIRE(UI::saveLayout(f.files, "assets/menu.dotui", layout));
  auto job = Project::ScenePreparation::begin(f.files, "scenes/main.dotscene",
                                              World::encodeScene(f.scene).value())
                 .value();
  wait(job);
  auto proof = job.take().value();
  REQUIRE(proof.ui().value());
  CHECK(proof.ui().value()->path == "assets/menu.dotui");
  REQUIRE(std::filesystem::remove(f.files.root() / "assets/menu.dotui"));
  bool owned{};
  std::thread consumer([&] {
    auto hud = Script::Hud::create(8, proof.ui().value());
    owned = hud && hud->draws()->size() == 1 && hud->draws()->front().key == "panel";
  });
  consumer.join();
  CHECK(owned);
  auto moved = std::move(proof);
  CHECK_FALSE(proof.ui());
  CHECK(moved.ui().value()->value == layout);
  auto empty = Fixture{};
  auto none = Project::ScenePreparation::begin(empty.files, "scenes/main.dotscene").value();
  wait(none);
  CHECK_FALSE(none.take()->ui().value());
}

TEST_CASE("scene_preparation.synchronous_proof_uses_same_path_and_initial_boundaries") {
  Fixture f;
  const auto initial = World::encodeScene(f.scene).value();

  for (const auto* path :
       {"", "../escape.dotscene", "scenes/main.txt", ".dot-stage-secret/scene.dotscene"}) {
    CHECK_FALSE(Project::PreparedScene::create(f.files, path, initial, {}));
    CHECK_FALSE(Project::ScenePreparation::begin(f.files, path, initial));
  }

  CHECK_FALSE(Project::PreparedScene::create(f.files, "scenes/main.dotscene", std::string{}, {}));
  auto proof = Project::PreparedScene::create(f.files, "scenes/main.dotscene", initial, {});
  REQUIRE(proof);
  CHECK(proof->accepts(f.files, f.scene).value());
  std::stop_source stopped;
  stopped.request_stop();
  CHECK_FALSE(Project::PreparedScene::create(f.files, "scenes/main.dotscene", initial,
                                             stopped.get_token()));
}

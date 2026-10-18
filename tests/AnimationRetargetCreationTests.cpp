#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

import Dot.Animation.Files;
import Dot.Animation.Source;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Editor.AnimationRetarget;
import Dot.Editor.Resources;
import Dot.Project;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace {
  using namespace Dot;
  using Json = nlohmann::json;

  struct Fixture {
    Core::StagedDirectory storage
        = Core::StagedDirectory::create(
              Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Project::Project project
        = Project::Project::create(storage.files().value(), "retarget").value();
    Core::FileSystem files = project.files().value();
    World::Scene source = World::Scene::create().value();
    World::Scene target = World::Scene::create().value();
    World::Scene authoring = World::Scene::create().value();
    Editor::RetargetProfile profile{"assets/source.dotscene",
                                    1,
                                    "assets/target.dotscene",
                                    1,
                                    "assets/source.dotanimation",
                                    {"Transferred",
                                     {{1, 7, "Source joint", "Target joint"}},
                                     {10, 1},
                                     Animation::RetargetTranslation{7, 1}}};

    Fixture() {
      REQUIRE(files.createDirectories("assets"));
      const auto a = source.createEntity("Source owner").value();
      const auto b = source.createEntity("Source joint").value();
      REQUIRE(source.setParent(b, a));
      REQUIRE(source.setTransform(b, {{0, 1, 0}}));
      REQUIRE(source.setAnimationPlayer(
          a, World::AnimationPlayer{{profile.clip}, {{0, a.value}, {1, b.value}}}));
      const auto c = target.createEntity("Target owner").value();
      const auto d = target.createEntity("Target joint").value();
      REQUIRE(target.setParent(d, c));
      REQUIRE(target.setTransform(d, {{0, 2, 0}}));
      REQUIRE(target.setAnimationPlayer(c, World::AnimationPlayer{{"assets/original.dotanimation"},
                                                                  {{0, c.value}, {7, d.value}}}));
      REQUIRE(World::saveScene(files, profile.sourceScene, source));
      REQUIRE(World::saveScene(files, profile.targetScene, target));
      const auto motion = Animation::Clip::create({"Source",
                                                   {{1,
                                                     Animation::Path::Translation,
                                                     Animation::Interpolation::Linear,
                                                     {{0, {0, 1, 0, 0}}, {.5F, {0, 2, 0, 0}}}}}})
                              .value();
      const auto original = Animation::Clip::create({"Original",
                                                     {{7,
                                                       Animation::Path::Translation,
                                                       Animation::Interpolation::Linear,
                                                       {{0, {0, 2, 0, 0}}, {1, {0, 2, 0, 0}}}}}})
                                .value();
      REQUIRE(Animation::saveClip(files, profile.clip, motion));
      REQUIRE(Animation::saveClip(files, "assets/original.dotanimation", original));
      authoring = World::decodeScene(World::encodeScene(target).value()).value();
    }

    auto begin() -> Core::Result<Editor::AnimationRetarget> {
      return Editor::AnimationRetarget::begin(project, authoring, 1,
                                              "assets/transferred.dotanimation", profile);
    }
  };

  auto await(Editor::AnimationRetarget& job) -> Core::Result<Editor::RetargetCreation> {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};

    for (;;) {
      auto ready = job.ready();
      REQUIRE(ready);

      if (*ready) {
        return job.take();
      }

      REQUIRE(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  }
}  // namespace

TEST_CASE("animation_retarget_creation.saved_profile_is_strict_portable_bounded_and_round_trips") {
  Fixture f;
  const auto text = Editor::encodeRetargetProfile(f.profile).value();
  CHECK(Editor::encodeRetargetProfile(Editor::decodeRetargetProfile(text).value()).value() == text);
  const auto initial = Json::parse(text);

  for (const auto* field : {"sourceScene", "targetScene", "sourceEntity", "clip", "name", "rate",
                            "mappings", "translation"}) {
    auto bad = initial;
    bad.erase(field);
    CHECK_FALSE(Editor::decodeRetargetProfile(bad.dump()));
  }

  auto bad = initial;
  bad["sourceEntity"] = "01";
  CHECK_FALSE(Editor::decodeRetargetProfile(bad.dump()));
  bad = initial;
  bad["rate"]["numerator"] = true;
  CHECK_FALSE(Editor::decodeRetargetProfile(bad.dump()));
  bad = initial;
  bad["mappings"].push_back(bad["mappings"][0]);
  CHECK_FALSE(Editor::decodeRetargetProfile(bad.dump()));
  bad = initial;
  bad["mappings"][0]["extra"] = 1;
  CHECK_FALSE(Editor::decodeRetargetProfile(bad.dump()));
  bad = initial;
  bad["sourceScene"] = "../source.dotscene";
  CHECK_FALSE(Editor::decodeRetargetProfile(bad.dump()));
  bad = initial;
  bad["translation"]["target"] = 1;
  CHECK_FALSE(Editor::decodeRetargetProfile(bad.dump()));
  CHECK(Editor::encodeRetargetProfile(f.profile).value() == text);
  auto store = Editor::ResourceStore::open(f.project).value();
  REQUIRE(store.createText("assets/transfer.dotretarget", text));
  auto document = store.openText("assets/transfer.dotretarget").value();
  CHECK(document.snapshot()->text == text);
}

TEST_CASE(
    "animation_retarget_creation.worker_freezes_authoring_and_returns_owned_preflight_without_"
    "writes") {
  Fixture f;
  const auto owner = f.authoring.findEntity(1).value();
  REQUIRE(f.authoring.renameEntity(owner, "Instance label"));
  REQUIRE(f.authoring.setTransform(owner, {{3, 0, 2}, {}, {.5F, .5F, .5F}}));
  auto properties = f.authoring.entity(owner)->animation.value();
  properties.enabled = false;
  REQUIRE(f.authoring.setAnimationPlayer(owner, properties));
  const auto before = World::encodeScene(f.authoring).value();
  auto job = f.begin().value();
  auto created = await(job);
  REQUIRE_MESSAGE(created, (created ? "" : created.error().message));
  CHECK(created->report.frames == 6);
  CHECK(created->report.emittedNodes == 1);
  CHECK(created->animation.player.clips.size() == 2);
  CHECK(created->animation.player.clips.back() == "assets/transferred.dotanimation");
  CHECK_FALSE(created->animation.player.enabled);
  CHECK(created->animation.player.targets == properties.targets);
  CHECK(World::encodeScene(f.authoring).value() == before);
  CHECK_FALSE(std::filesystem::exists(f.files.root() / "assets/transferred.dotanimation"));
  REQUIRE(Editor::validateRetargetInputs(f.files, *created));
  CHECK_FALSE(job.take());
  auto store = Editor::ResourceStore::open(f.project).value();
  REQUIRE(store.createText("assets/transferred.dotanimation", created->animation.text));
  CHECK_FALSE(store.createText("assets/transferred.dotanimation", "overwrite"));
  CHECK(Animation::loadClip(f.files, "assets/transferred.dotanimation")->duration().value() == .5);
}

TEST_CASE(
    "animation_retarget_creation.reference_names_helpers_and_other_writers_fail_without_"
    "publication") {
  Fixture f;
  const auto owner = f.authoring.findEntity(1).value();
  const auto joint = f.authoring.findEntity(2).value();
  auto malformed = Animation::captureRetargetRig(f.target, f.target.findEntity(1).value()).value();
  malformed.nodes[1].parent = 999;
  CHECK_FALSE(Editor::validateRetargetTarget(f.authoring, 1, malformed));
  REQUIRE(f.authoring.renameEntity(joint, "Different joint"));
  auto rejected = f.begin().value();
  CHECK_FALSE(await(rejected));
  REQUIRE(f.authoring.renameEntity(joint, "Target joint"));
  REQUIRE(f.authoring.setRigidBody(joint, World::RigidBody{}));
  auto conflict = f.begin().value();
  CHECK_FALSE(await(conflict));
  REQUIRE(f.authoring.setRigidBody(joint, {}));
  auto other = f.authoring.createEntity("Other player").value();
  REQUIRE(f.authoring.setAnimationPlayer(other, f.authoring.entity(owner)->animation));
  auto duplicate = f.begin().value();
  CHECK_FALSE(await(duplicate));
  CHECK_FALSE(std::filesystem::exists(f.files.root() / "assets/transferred.dotanimation"));
}

TEST_CASE(
    "animation_retarget_creation.changed_reference_clip_and_metadata_reject_current_input_"
    "provenance") {
  Fixture f;
  auto job = f.begin().value();
  auto value = await(job).value();
  const auto source = f.files.readText(f.profile.sourceScene).value();
  REQUIRE(f.files.writeTextAtomic(f.profile.sourceScene, source + "\n"));
  CHECK_FALSE(Editor::validateRetargetInputs(f.files, value));
  REQUIRE(f.files.writeTextAtomic(f.profile.sourceScene, source));
  REQUIRE(Editor::validateRetargetInputs(f.files, value));
  const auto clip = f.files.readText(f.profile.clip).value();
  REQUIRE(f.files.writeTextAtomic(f.profile.clip, "invalid later source"));
  CHECK_FALSE(Editor::validateRetargetInputs(f.files, value));
  CHECK(value.animation.clip.sample(.5)->size() == 3);
  REQUIRE(f.files.writeTextAtomic(f.profile.clip, clip));
  auto metadata = f.project.description().value();
  metadata.name = "Changed project";
  REQUIRE(f.project.save(metadata));
  CHECK_FALSE(Editor::validateRetargetInputs(f.files, value));
  CHECK_FALSE(std::filesystem::exists(f.files.root() / "assets/transferred.dotanimation"));
}

TEST_CASE(
    "animation_retarget_creation.move_owner_thread_cancel_and_failures_release_without_files") {
  Fixture f;
  auto original = f.begin().value();
  auto job = std::move(original);
  CHECK_FALSE(original.ready());
  CHECK_FALSE(original.take());
  bool rejected{};
  std::thread thread([&] { rejected = !job.ready() && !job.take(); });
  thread.join();
  CHECK(rejected);
  job.requestStop();
  auto cancelled = await(job);

  if (!cancelled) {
    CHECK(cancelled.error().code == Core::ErrorCode::Unavailable);
    CHECK(cancelled.error().message == "Animation retarget cancelled");
  }

  CHECK_FALSE(std::filesystem::exists(f.files.root() / "assets/transferred.dotanimation"));
  f.profile.clip = "assets/missing.dotanimation";
  auto missing = f.begin().value();
  auto failed = await(missing);
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("missing.dotanimation") != std::string::npos);
  f.profile.sourceScene = "assets/../source.dotscene";
  CHECK_FALSE(f.begin());
}

TEST_CASE(
    "animation_retarget_creation.native_clip_publication_keeps_the_separate_text_and_native_"
    "limits") {
  Fixture f;
  auto dense = Animation::Clip::create({"Long motion",
                                        {{1,
                                          Animation::Path::Translation,
                                          Animation::Interpolation::Linear,
                                          {{0, {0, 1, 0, 0}}, {30, {0, 2, 0, 0}}}}}})
                   .value();
  REQUIRE(Animation::saveClip(f.files, f.profile.clip, dense));
  auto sourceBytes = f.files.readText(f.profile.clip).value();
  sourceBytes.append(7 * 1024 * 1024, ' ');
  REQUIRE(f.files.writeTextAtomic(f.profile.clip, sourceBytes));
  f.profile.settings.rate = {240, 1};
  auto job = f.begin().value();
  auto created = await(job);
  REQUIRE_MESSAGE(created, (created ? "" : created.error().message));
  REQUIRE(created->animation.text.size() > 1024 * 1024);
  CHECK(created->clipBytes == sourceBytes);
  CHECK(Editor::validateRetargetInputs(f.files, *created));
  auto textStore = Editor::ResourceStore::open(f.project).value();
  CHECK_FALSE(textStore.createText("assets/transferred.dotanimation", created->animation.text));
  CHECK_FALSE(std::filesystem::exists(f.files.root() / "assets/transferred.dotanimation"));
  Editor::ResourceLimits nativeLimits;
  nativeLimits.textBytes = 64 * 1024 * 1024;
  auto nativeStore = Editor::ResourceStore::open(f.project, nativeLimits).value();
  REQUIRE(nativeStore.createText("assets/transferred.dotanimation", created->animation.text));
  CHECK(Animation::loadClip(f.files, "assets/transferred.dotanimation")->duration().value() == 30);
  CHECK_FALSE(nativeStore.createText("assets/transferred.dotanimation", created->animation.text));
}

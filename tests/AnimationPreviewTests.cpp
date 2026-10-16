#include <doctest/doctest.h>

#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>

import Dot.Animation.Clip;
import Dot.Animation.Files;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Editor.AnimationPreview;
import Dot.Project;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace {
  using namespace Dot;

  struct Fixture {
    Core::StagedDirectory storage
        = Core::StagedDirectory::create(
              Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Project::Project project = Project::Project::create(storage.files().value(), "game").value();
    Core::FileSystem files = project.files().value();
    World::Scene source = World::Scene::create().value();
    World::EntityId owner = source.createEntity("Player").value();
    World::EntityId target = source.createEntity("Target").value();
    World::AnimationPlayer properties{{"assets/move.dotanimation", "assets/offset.dotanimation"},
                                      {{0, target.value}}};

    Fixture() {
      for (std::size_t index = 0; index < 2; ++index) {
        const float start = static_cast<float>(index * 20);
        auto clip
            = Animation::Clip::create({"Move " + std::to_string(index),
                                       {{0,
                                         Animation::Path::Translation,
                                         Animation::Interpolation::Linear,
                                         {{0, {start, 0, 0, 0}}, {2, {start + 2, 0, 0, 0}}}}}})
                  .value();
        Animation::saveClip(files, properties.clips[index], clip).value();
      }

      source.setTransform(target, {{9, 0, 0}}).value();
      source.setAnimationPlayer(owner, properties).value();
    }

    auto create() -> Editor::AnimationPreview {
      return Editor::AnimationPreview::create(source, files, owner.value).value();
    }

    auto x(const Editor::AnimationPreview& preview) -> float {
      const auto* scene = preview.scene().value();
      return scene->transform(scene->findEntity(target.value).value())->position.x;
    }
  };
}  // namespace

TEST_CASE("animation_preview.isolated_scrubbing_play_pause_switch_and_bind_pose") {
  Fixture fixture;
  const auto before = World::encodeScene(fixture.source).value();
  auto preview = fixture.create();
  CHECK(preview.scene().value()->id() != fixture.source.id());
  CHECK(preview.stats()->source == fixture.source.id());
  CHECK_FALSE(preview.stats()->player.playback.playing);
  CHECK(fixture.x(preview) == 0);
  const auto generation = preview.stats()->generation;
  REQUIRE(preview.advance(.5));
  CHECK(preview.stats()->generation == generation);
  REQUIRE(preview.seek(.75));
  CHECK(fixture.x(preview) == doctest::Approx(.75));
  REQUIRE(preview.play(0, false));
  REQUIRE(preview.advance(.25));
  CHECK(fixture.x(preview) == doctest::Approx(1));
  REQUIRE(preview.pause());
  REQUIRE(preview.advance(.5));
  CHECK(fixture.x(preview) == doctest::Approx(1));
  REQUIRE(preview.play(1, true));
  CHECK(fixture.x(preview) == 20);
  REQUIRE(preview.configure(-1, false));
  REQUIRE(preview.play(1, true));
  CHECK(preview.stats()->player.playback.time == 2);
  REQUIRE(preview.advance(.5));
  CHECK(fixture.x(preview) == doctest::Approx(21.5));
  REQUIRE(preview.bindPose());
  CHECK_FALSE(preview.stats()->player.playback.playing);
  CHECK(preview.stats()->player.playback.time == 0);
  CHECK(fixture.x(preview) == 9);
  CHECK(preview.stats()->bindPose);
  REQUIRE(preview.play(1, false));
  CHECK_FALSE(preview.stats()->bindPose);
  CHECK(fixture.x(preview) == 22);
  CHECK(World::encodeScene(fixture.source).value() == before);
}

TEST_CASE("animation_preview.invalid_controls_do_not_change_pose_clock_or_generation") {
  Fixture fixture;
  auto preview = fixture.create();
  REQUIRE(preview.seek(.5));
  const auto before = World::encodeScene(*preview.scene().value()).value();
  const auto generation = preview.stats()->generation;
  CHECK_FALSE(preview.play(2, true));
  CHECK_FALSE(preview.seek(-1));
  CHECK_FALSE(preview.seek(2.01));
  CHECK_FALSE(preview.seek(std::numeric_limits<double>::infinity()));
  CHECK_FALSE(preview.configure(17, true));
  CHECK_FALSE(preview.configure(std::numeric_limits<double>::quiet_NaN(), true));
  CHECK_FALSE(preview.advance(-.1));
  CHECK_FALSE(preview.advance(1.01));
  CHECK_FALSE(preview.advance(std::numeric_limits<double>::quiet_NaN()));
  CHECK(preview.stats()->generation == generation);
  CHECK(preview.stats()->player.playback.time == .5);
  CHECK_FALSE(preview.stats()->error);
  CHECK(World::encodeScene(*preview.scene().value()).value() == before);
  bool rejected{};
  std::thread worker([&] {
    rejected = !preview.scene() && !preview.stats() && !preview.play(0, true) && !preview.pause()
               && !preview.seek(0) && !preview.bindPose() && !preview.configure(1, true)
               && !preview.advance(.1);
  });
  worker.join();
  CHECK(rejected);
  auto* stable = preview.scene().value();
  auto moved = std::move(preview);
  CHECK_FALSE(preview.scene());
  CHECK_FALSE(preview.stats());
  CHECK_FALSE(preview.play(0, true));
  CHECK_FALSE(preview.pause());
  CHECK_FALSE(preview.seek(0));
  CHECK_FALSE(preview.bindPose());
  CHECK_FALSE(preview.configure(1, true));
  CHECK_FALSE(preview.advance(.1));
  CHECK(moved.scene().value() == stable);
  REQUIRE(moved.play(0, true));
}

TEST_CASE("animation_preview.owns_clips_and_poses_after_source_and_files_retire") {
  Fixture fixture;
  auto other = fixture.source.createEntity("Other player").value();
  auto target = fixture.source.createEntity("Other target").value();
  auto properties = fixture.properties;
  properties.targets[0].entity = target.value;
  REQUIRE(fixture.source.setAnimationPlayer(other, properties));
  REQUIRE(fixture.source.setTransform(target, {{12, 0, 0}}));
  auto preview = fixture.create();
  fixture.source = World::Scene::create().value();
  REQUIRE(std::filesystem::remove_all(fixture.files.root() / "assets") > 0);
  REQUIRE(preview.play(0, true));
  REQUIRE(preview.advance(.5));
  CHECK(fixture.x(preview) == doctest::Approx(.5));
  auto scene = preview.scene().value();
  CHECK(scene->transform(scene->findEntity(target.value).value())->position.x == 12);
}

TEST_CASE("animation_preview.validates_assets_enabled_players_and_native_authority") {
  Fixture fixture;
  CHECK_FALSE(Editor::AnimationPreview::create(fixture.source, fixture.files, 999));
  CHECK_FALSE(
      Editor::AnimationPreview::create(fixture.source, fixture.files, fixture.target.value));
  auto properties = fixture.properties;
  properties.enabled = false;
  REQUIRE(fixture.source.setAnimationPlayer(fixture.owner, properties));
  CHECK_FALSE(Editor::AnimationPreview::create(fixture.source, fixture.files, fixture.owner.value));
  properties = fixture.properties;
  properties.clips[0] = "assets/missing.dotanimation";
  REQUIRE(fixture.source.setAnimationPlayer(fixture.owner, properties));
  CHECK_FALSE(Editor::AnimationPreview::create(fixture.source, fixture.files, fixture.owner.value));
  REQUIRE(fixture.source.setAnimationPlayer(fixture.owner, fixture.properties));
  REQUIRE(fixture.source.setRigidBody(fixture.target, World::RigidBody{}));
  CHECK_FALSE(Editor::AnimationPreview::create(fixture.source, fixture.files, fixture.owner.value));
  REQUIRE(fixture.source.setRigidBody(fixture.target, {}));
  auto other = fixture.source.createEntity("Conflict").value();
  REQUIRE(fixture.source.setAnimationPlayer(other, fixture.properties));
  CHECK_FALSE(Editor::AnimationPreview::create(fixture.source, fixture.files, fixture.owner.value));
}

TEST_CASE("animation_preview.native_sample_failure_retains_pose_and_faults_until_recreation") {
  Fixture fixture;
  auto clip = Animation::Clip::create({"Singular sample",
                                       {{0,
                                         Animation::Path::Scale,
                                         Animation::Interpolation::Linear,
                                         {{0, {1, 1, 1, 0}}, {2, {-1, 1, 1, 0}}}}}})
                  .value();
  REQUIRE(Animation::saveClip(fixture.files, fixture.properties.clips[0], clip));
  auto preview = fixture.create();
  REQUIRE(preview.play(0, true));
  const auto before = World::encodeScene(*preview.scene().value()).value();
  CHECK_FALSE(preview.advance(1));
  CHECK(preview.stats()->error);
  CHECK(preview.stats()->player.playback.time == 0);
  CHECK(World::encodeScene(*preview.scene().value()).value() == before);
  CHECK_FALSE(preview.play(0, true));
  CHECK_FALSE(preview.seek(0));
  CHECK_FALSE(preview.advance(.1));
  auto recreated = fixture.create();
  CHECK_FALSE(recreated.stats()->error);
  REQUIRE(recreated.play(1, true));
  REQUIRE(recreated.advance(.25));
}

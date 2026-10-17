#include <doctest/doctest.h>

#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

import Dot.Animation.Files;
import Dot.Animation.Source;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Editor.AnimationCreation;
import Dot.Editor.AnimationDocument;
import Dot.Editor.AnimationPreview;
import Dot.Editor.Resources;
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
    Project::Project project = Project::Project::create(storage.files().value(), "project").value();
    Core::FileSystem files = project.files().value();
    World::Scene scene = World::Scene::create().value();
    World::EntityId owner = scene.createEntity("Animated object").value();
    std::string path{"assets/pose.dotanimation"};

    auto prepare(double duration = 2) -> Core::Result<Editor::AnimationCreation> {
      return Editor::preparePoseAnimation(scene, files, owner.value, path, "Authoring pose",
                                          duration);
    }
  };
}  // namespace

TEST_CASE("animation_creation.ordinary_object_seeds_local_trs_without_scene_or_file_mutation") {
  Fixture f;
  REQUIRE(f.scene.setTransform(f.owner, {{3, -2, 7}, {}, {2, 3, 4}}));
  const auto before = World::encodeScene(f.scene).value();
  auto created = f.prepare().value();
  CHECK(created.player.clips == std::vector<std::string>{f.path});
  CHECK(created.player.targets == std::vector<Animation::Target>{{0, f.owner.value}});
  CHECK(created.player.clip == 0);
  CHECK(created.clip.duration().value() == 2);
  const auto description = created.clip.description().value();
  REQUIRE(description->tracks.size() == 3);

  for (const auto& track : description->tracks) {
    REQUIRE(track.keys.size() == 2);
    CHECK(track.node == 0);
    CHECK(track.keys.front().time == 0);
    CHECK(track.keys.back().time == 2);
    CHECK(track.keys.front().value == track.keys.back().value);
  }

  CHECK(description->tracks[0].keys[0].value.x == 3);
  CHECK(description->tracks[1].keys[0].value.w == 1);
  CHECK(description->tracks[2].keys[0].value.z == 4);
  CHECK(World::encodeScene(f.scene).value() == before);
  CHECK_FALSE(std::filesystem::exists(f.files.root() / f.path));
  CHECK(Animation::decodeClip(created.text)->description().value()->name == "Authoring pose");
}

TEST_CASE(
    "animation_creation.appends_library_and_preserves_sparse_bindings_and_disabled_settings") {
  Fixture f;
  auto target = f.scene.createEntity("Joint").value();
  auto original = Animation::Clip::create({"Original",
                                           {{17,
                                             Animation::Path::Translation,
                                             Animation::Interpolation::Linear,
                                             {{0, {9, 0, 0, 0}}}}}})
                      .value();
  REQUIRE(Animation::saveClip(f.files, "assets/original.dotanimation", original));
  Animation::PlayerProperties properties{
      {"assets/original.dotanimation"}, {{17, target.value}}, 0, -2, false, false, false};
  REQUIRE(f.scene.setAnimationPlayer(f.owner, properties));
  REQUIRE(f.scene.setTransform(target, {{4, 5, 6}}));
  const auto before = World::encodeScene(f.scene).value();
  auto created = f.prepare().value();
  properties.clips.push_back(f.path);
  properties.clip = 1;
  CHECK(created.player == properties);
  CHECK(created.clip.description().value()->tracks[0].node == 17);
  CHECK(created.clip.description().value()->tracks[0].keys[0].value.x == 4);
  CHECK(World::encodeScene(f.scene).value() == before);
}

TEST_CASE("animation_creation.rejects_boundaries_and_duplicate_paths_without_files") {
  Fixture f;

  for (double value :
       {0., -1., 86400.01, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::denorm_min()}) {
    CHECK_FALSE(f.prepare(value));
  }

  REQUIRE(f.prepare(86400));
  CHECK_FALSE(Editor::preparePoseAnimation(f.scene, f.files, 999, f.path, "Pose", 1));

  for (const auto& path :
       {"pose.dotanimation", "assets/../pose.dotanimation", "assets/pose.json"}) {
    CHECK_FALSE(Editor::preparePoseAnimation(f.scene, f.files, f.owner.value, path, "Pose", 1));
  }

  CHECK_FALSE(Editor::preparePoseAnimation(f.scene, f.files, f.owner.value, f.path, "", 1));
  Animation::PlayerProperties properties{{f.path}, {{0, f.owner.value}}};
  REQUIRE(f.scene.setAnimationPlayer(f.owner, properties));
  CHECK_FALSE(f.prepare());
  CHECK_FALSE(std::filesystem::exists(f.files.root() / f.path));
}

TEST_CASE("animation_creation.preflights_physics_other_writers_and_missing_libraries") {
  Fixture f;
  REQUIRE(f.scene.setRigidBody(f.owner, World::RigidBody{}));
  CHECK_FALSE(f.prepare());
  REQUIRE(f.scene.setRigidBody(f.owner, {}));
  auto parent = f.scene.createEntity("Physics parent").value();
  REQUIRE(f.scene.setParent(f.owner, parent));
  REQUIRE(f.scene.setRigidBody(parent, World::RigidBody{}));
  CHECK_FALSE(f.prepare());
  REQUIRE(f.scene.setRigidBody(parent, {}));
  auto created = f.prepare().value();
  REQUIRE(Animation::saveClip(f.files, f.path, created.clip));
  auto other = f.scene.createEntity("Other writer").value();
  REQUIRE(f.scene.setAnimationPlayer(other, created.player));
  CHECK_FALSE(f.prepare());
  REQUIRE(f.scene.setAnimationPlayer(other, {}));
  auto missing = created.player;
  missing.clips = {"assets/missing.dotanimation"};
  missing.enabled = false;
  REQUIRE(f.scene.setAnimationPlayer(f.owner, missing));
  CHECK_FALSE(f.prepare());
}

TEST_CASE("animation_creation.publication_is_exclusive_and_document_editing_round_trips") {
  Fixture f;
  auto created = f.prepare().value();
  auto resources = Editor::ResourceStore::open(f.project).value();
  REQUIRE(resources.createText(f.path, created.text));
  CHECK_FALSE(resources.createText(f.path, "overwrite"));
  CHECK(f.files.readText(f.path).value() == created.text);
  auto document = Editor::AnimationDocument::open(f.project, f.path).value();
  std::vector<Animation::ClipEdit> changes{Animation::RenameClip{"Edited pose"}};
  REQUIRE(document.edit(changes, document.snapshot()->revision));
  REQUIRE(document.undo(document.snapshot()->revision));
  CHECK_FALSE(document.snapshot()->dirty);
  REQUIRE(document.redo(document.snapshot()->revision));
  REQUIRE(document.save(document.snapshot()->revision));
  CHECK(Animation::loadClip(f.files, f.path)->description().value()->name == "Edited pose");
  REQUIRE(f.scene.setAnimationPlayer(f.owner, created.player));
  auto preview = Editor::AnimationPreview::create(f.scene, f.files, f.owner.value).value();
  REQUIRE(preview.seek(1));
  CHECK(preview.stats()->player.name == "Edited pose");
}

TEST_CASE("animation_creation.owns_prepared_data_after_source_retirement_and_repeated_failures") {
  Fixture f;
  auto created = f.prepare().value();

  for (int i = 0; i < 8; ++i) {
    REQUIRE(f.scene.setRigidBody(f.owner, World::RigidBody{}));
    CHECK_FALSE(f.prepare());
    REQUIRE(f.scene.setRigidBody(f.owner, {}));
    REQUIRE(f.prepare());
  }

  f.scene = World::Scene::create().value();
  REQUIRE(std::filesystem::remove_all(f.files.root() / "assets") > 0);
  CHECK(created.clip.sample(1)->size() == 3);
  CHECK(created.player.targets[0].entity == f.owner.value);
  CHECK(Animation::decodeClip(created.text)->duration().value() == 2);
}

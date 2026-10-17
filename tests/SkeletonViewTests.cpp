#include <doctest/doctest.h>

#include <filesystem>
#include <optional>
#include <thread>
#include <vector>

import Dot.Animation.Skin;
import Dot.Animation.SkinFiles;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Editor.SkeletonView;
import Dot.Math;
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
    World::Scene scene = World::Scene::create().value();
    World::EntityId mesh = scene.createEntity("Mesh").value();
    World::EntityId root = scene.createEntity("Root").value();
    World::EntityId tip = scene.createEntity("手部 / Hand").value();
    World::SkinBinding binding{"assets/skeleton.dotskin", {tip.value, root.value}};
    Animation::Skin skin = Animation::Skin::create({std::vector<Math::Mat4>(2),
                                                    std::vector<Animation::SkinInfluence>(1)})
                               .value();

    Fixture() {
      files.createDirectories("assets").value();
      scene.setMeshRenderer(mesh, World::MeshRenderer{}).value();
      scene.setTransform(root, {{.1F, .1F, .5F}}).value();
      scene.setTransform(tip, {{.2F, .2F, 0}}).value();
      scene.setParent(tip, root).value();
      scene.setSkin(mesh, binding).value();
      Animation::saveSkin(files, binding.asset, skin).value();
    }
  };
}  // namespace

TEST_CASE("skeleton_view.live_frames_selection_and_focus_are_read_only") {
  Fixture fixture;
  Editor::SkeletonView viewer;
  const auto before = World::encodeScene(fixture.scene).value();
  CHECK(viewer.render(fixture.scene, fixture.files, {}, 0)->points.empty());
  REQUIRE(viewer.set(fixture.scene, fixture.files, fixture.mesh.value));
  REQUIRE(viewer.render(fixture.scene, fixture.files, {}, 1));
  auto state = viewer.stats().value();
  REQUIRE(state.frame);
  CHECK(state.frame->pose.joints.size() == 2);
  CHECK(state.frame->overlay.lines.size() == 1);
  CHECK(state.frame->overlay.points.size() == 2);
  CHECK(state.frame->projected[0]->x == doctest::Approx(.65));
  CHECK(state.frame->projected[0]->y == doctest::Approx(.35));
  REQUIRE(viewer.select(fixture.scene, fixture.files, fixture.tip.value));
  CHECK_FALSE(viewer.stats()->frame);
  REQUIRE(viewer.render(fixture.scene, fixture.files, {}, 2));
  CHECK(viewer.stats()->frame->overlay.points[0].radius == 4);
  CHECK(viewer.stats()->frame->overlay.lines[0].thickness == 3);
  auto bounds = viewer.bounds(fixture.scene, fixture.files, fixture.tip.value).value();
  CHECK(bounds.minimum.x < .1F);
  CHECK(bounds.maximum.x > .3F);
  auto root = viewer.bounds(fixture.scene, fixture.files, fixture.root.value).value();
  CHECK(root.minimum.x == doctest::Approx(.07));
  CHECK(root.maximum.x == doctest::Approx(.13));
  CHECK(World::encodeScene(fixture.scene).value() == before);
  REQUIRE(viewer.set(fixture.scene, fixture.files, {}));
  CHECK_FALSE(viewer.stats()->entity);
  CHECK_FALSE(viewer.stats()->selected);
  CHECK_FALSE(viewer.stats()->frame);
  CHECK_FALSE(viewer.bounds(fixture.scene, fixture.files, {}));
  CHECK_FALSE(viewer.select(fixture.scene, fixture.files, {}));
}

TEST_CASE("skeleton_view.invalid_controls_preserve_state_and_render_errors_recover") {
  Fixture fixture;
  Editor::SkeletonView viewer;
  REQUIRE(viewer.set(fixture.scene, fixture.files, fixture.mesh.value));
  REQUIRE(viewer.select(fixture.scene, fixture.files, fixture.tip.value));
  REQUIRE(viewer.render(fixture.scene, fixture.files, {}, 1));
  CHECK_FALSE(viewer.set(fixture.scene, fixture.files, 999));
  CHECK_FALSE(viewer.set(fixture.scene, fixture.files, fixture.root.value));
  CHECK_FALSE(viewer.select(fixture.scene, fixture.files, fixture.mesh.value));
  CHECK_FALSE(viewer.bounds(fixture.scene, fixture.files, fixture.mesh.value));
  CHECK(viewer.stats()->entity == fixture.mesh.value);
  CHECK(viewer.stats()->selected == fixture.tip.value);
  REQUIRE(viewer.stats()->frame);
  CHECK(viewer.stats()->frame->serial == 1);
  CHECK_FALSE(viewer.render(fixture.scene, fixture.files, {}, 0));
  CHECK_FALSE(viewer.stats()->frame);
  CHECK(viewer.stats()->error);
  Math::Mat4 singular;
  singular.data.fill(0);
  CHECK_FALSE(viewer.render(fixture.scene, fixture.files, singular, 2));
  REQUIRE(viewer.render(fixture.scene, fixture.files, {}, 3));
  CHECK_FALSE(viewer.stats()->error);
  CHECK(viewer.stats()->selected == fixture.tip.value);
  auto disabled = fixture.binding;
  disabled.enabled = false;
  REQUIRE(fixture.scene.setSkin(fixture.mesh, disabled));
  CHECK_FALSE(viewer.render(fixture.scene, fixture.files, {}, 4));
  CHECK_FALSE(viewer.stats()->frame);
  REQUIRE(fixture.scene.setSkin(fixture.mesh, fixture.binding));
  REQUIRE(viewer.render(fixture.scene, fixture.files, {}, 5));
}

TEST_CASE("skeleton_view.cache_owns_assets_until_invalidation_and_keys_project_roots") {
  Fixture fixture;
  Fixture other;
  Editor::SkeletonView viewer;
  REQUIRE(viewer.set(fixture.scene, fixture.files, fixture.mesh.value));
  REQUIRE(std::filesystem::remove(fixture.files.root() / fixture.binding.asset));
  REQUIRE(viewer.render(fixture.scene, fixture.files, {}, 1));
  REQUIRE(viewer.invalidateAssets());
  CHECK_FALSE(viewer.stats()->frame);
  CHECK_FALSE(viewer.render(fixture.scene, fixture.files, {}, 2));
  REQUIRE(Animation::saveSkin(fixture.files, fixture.binding.asset, fixture.skin));
  REQUIRE(viewer.render(fixture.scene, fixture.files, {}, 3));
  REQUIRE(std::filesystem::remove(other.files.root() / other.binding.asset));
  CHECK_FALSE(viewer.render(other.scene, other.files, {}, 4));
  REQUIRE(Animation::saveSkin(other.files, other.binding.asset, other.skin));
  REQUIRE(viewer.render(other.scene, other.files, {}, 5));
  CHECK(viewer.stats()->frame->pose.owner.scene == other.scene.id());
}

TEST_CASE("skeleton_view.rebinds_scene_scopes_and_retires_obsolete_joint_selection") {
  Fixture fixture;
  Editor::SkeletonView viewer;
  REQUIRE(viewer.set(fixture.scene, fixture.files, fixture.mesh.value));
  REQUIRE(viewer.select(fixture.scene, fixture.files, fixture.tip.value));
  REQUIRE(viewer.render(fixture.scene, fixture.files, {}, 1));
  const auto old = viewer.stats().value();
  auto clone = World::decodeScene(World::encodeScene(fixture.scene).value()).value();
  auto tip = clone.findEntity(fixture.tip.value).value();
  REQUIRE(clone.setTransform(tip, {{.4F, .2F, 0}}));
  REQUIRE(viewer.render(clone, fixture.files, {}, 2));
  CHECK(viewer.stats()->frame->pose.owner.scene == clone.id());
  CHECK(viewer.stats()->frame->pose.joints[0].position.x == doctest::Approx(.5));
  auto replacement = clone.createEntity("Replacement").value();
  auto binding = fixture.binding;
  binding.joints[0] = replacement.value;
  REQUIRE(clone.setSkin(clone.findEntity(fixture.mesh.value).value(), binding));
  REQUIRE(viewer.render(clone, fixture.files, {}, 3));
  CHECK_FALSE(viewer.stats()->selected);
  fixture.scene = World::Scene::create().value();
  CHECK(old.frame->pose.joints[0].name == "手部 / Hand");
  CHECK(old.frame->pose.joints[0].position.x == doctest::Approx(.3));
}

TEST_CASE("skeleton_view.rejects_foreign_threads_without_touching_owner_state") {
  Fixture fixture;
  Editor::SkeletonView viewer;
  REQUIRE(viewer.set(fixture.scene, fixture.files, fixture.mesh.value));
  REQUIRE(viewer.render(fixture.scene, fixture.files, {}, 1));
  bool rejected{};
  std::thread worker([&] {
    rejected = !viewer.stats() && !viewer.set(fixture.scene, fixture.files, {})
               && !viewer.select(fixture.scene, fixture.files, {})
               && !viewer.bounds(fixture.scene, fixture.files, {})
               && !viewer.render(fixture.scene, fixture.files, {}, 2) && !viewer.invalidateAssets();
  });
  worker.join();
  CHECK(rejected);
  CHECK(viewer.stats()->frame->serial == 1);
}

TEST_CASE("skeleton_view.projects_owned_delayed_pose_and_rejects_malformed_snapshots") {
  Fixture f;
  Editor::SkeletonView viewer;
  REQUIRE(viewer.set(f.scene, f.files, f.mesh.value));
  auto pose = viewer.sample(f.scene, f.files).value();
  REQUIRE(pose);
  CHECK_FALSE(viewer.stats()->frame);
  REQUIRE(f.scene.setTransform(f.tip, {{.6F, .2F, 0}}));
  REQUIRE(viewer.renderPose(pose, {}, 10));
  CHECK(viewer.stats()->frame->pose.joints[0].position.x == doctest::Approx(.3));
  CHECK(viewer.stats()->frame->projected[0]->x == doctest::Approx(.65));
  auto malformed = *pose;
  malformed.joints[0].parent = 0;
  CHECK_FALSE(viewer.renderPose(malformed, {}, 11));
  CHECK_FALSE(viewer.stats()->frame);
  malformed = *pose;
  malformed.joints[0].entity = malformed.joints[1].entity;
  CHECK_FALSE(viewer.renderPose(malformed, {}, 12));
  REQUIRE(viewer.render(f.scene, f.files, {}, 13));
  CHECK(viewer.stats()->frame->pose.joints[0].position.x == doctest::Approx(.7));
  CHECK(viewer.stats()->frame->serial == 13);
  bool rejected{};
  std::thread worker(
      [&] { rejected = !viewer.sample(f.scene, f.files) && !viewer.renderPose(pose, {}, 14); });
  worker.join();
  CHECK(rejected);
}

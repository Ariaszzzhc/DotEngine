#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Animation.Skeleton;
import Dot.Animation.Skin;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Math;

namespace {
  using namespace Dot;

  struct Fixture {
    World::Scene scene{World::Scene::create().value()};
    World::EntityId mesh{scene.createEntity("Mesh").value()};
    World::EntityId root{scene.createEntity("Root").value()};
    World::EntityId helper{scene.createEntity("Unbound helper").value()};
    World::EntityId tip{scene.createEntity("手部 / Hand").value()};
    World::EntityId other{scene.createEntity("Other root").value()};
    World::SkinBinding binding{"assets/skeleton.dotskin", {tip.value, root.value, other.value}};
    Animation::Skin skin{Animation::Skin::create(
                             {std::vector<Math::Mat4>(3), std::vector<Animation::SkinInfluence>(1)})
                             .value()};

    Fixture() {
      scene.setMeshRenderer(mesh, World::MeshRenderer{}).value();
      scene.setTransform(mesh, {{3, 0, 0}}).value();
      scene.setTransform(root, {{10, 0, 0}}).value();
      scene.setTransform(helper, {{0, 1, 0}}).value();
      scene.setTransform(tip, {{0, 1, 0}}).value();
      scene.setTransform(other, {{-4, 0, 0}}).value();
      scene.setParent(helper, root).value();
      scene.setParent(tip, helper).value();
      scene.setSkin(mesh, binding).value();
    }
  };
}  // namespace

TEST_CASE("skeleton.samples_real_world_positions_order_multiple_roots_and_unbound_helpers") {
  Fixture fixture;
  const auto before = World::encodeScene(fixture.scene).value();
  auto pose = Animation::sampleSkeleton(fixture.scene, fixture.mesh, fixture.skin);
  REQUIRE_MESSAGE(pose, (pose ? "" : pose.error().message));
  REQUIRE(pose->joints.size() == 3);
  CHECK(pose->owner == fixture.mesh);
  CHECK(pose->asset == fixture.binding.asset);
  CHECK(pose->joints[0].entity == fixture.tip);
  CHECK(pose->joints[0].name == "手部 / Hand");
  CHECK(pose->joints[0].position == Math::Vec3{10, 2, 0});
  REQUIRE(pose->joints[0].parent);
  CHECK(*pose->joints[0].parent == 1);
  CHECK(pose->joints[0].parentLinks == 2);
  CHECK(pose->joints[1].position == Math::Vec3{10, 0, 0});
  CHECK_FALSE(pose->joints[1].parent);
  CHECK(pose->joints[1].parentLinks == 0);
  CHECK_FALSE(pose->joints[2].parent);
  CHECK(pose->joints[2].position == Math::Vec3{-4, 0, 0});
  CHECK(World::encodeScene(fixture.scene).value() == before);
}

TEST_CASE("skeleton.snapshots_own_names_positions_and_topology_after_live_hierarchy_changes") {
  Fixture fixture;
  auto old = Animation::sampleSkeleton(fixture.scene, fixture.mesh, fixture.skin).value();
  REQUIRE(fixture.scene.setParent(fixture.helper, fixture.other));
  REQUIRE(fixture.scene.renameEntity(fixture.tip, "Changed"));
  auto current = Animation::sampleSkeleton(fixture.scene, fixture.mesh, fixture.skin).value();
  CHECK(current.joints[0].parent == 2);
  CHECK(current.joints[0].position == Math::Vec3{-4, 2, 0});
  CHECK(current.joints[0].name == "Changed");
  fixture.scene = World::Scene::create().value();
  fixture.skin = {};
  CHECK(old.joints[0].name == "手部 / Hand");
  CHECK(old.joints[0].parent == 1);
  CHECK(old.joints[0].position == Math::Vec3{10, 2, 0});
}

TEST_CASE("skeleton.rejects_disabled_missing_foreign_and_mismatched_bindings_without_substitutes") {
  Fixture fixture;
  CHECK_FALSE(Animation::sampleSkeleton(fixture.scene, fixture.root, fixture.skin));
  CHECK_FALSE(Animation::sampleSkeleton(fixture.scene, fixture.mesh, {}));
  auto shortSkin = Animation::Skin::create(
                       {std::vector<Math::Mat4>(2), std::vector<Animation::SkinInfluence>(1)})
                       .value();
  CHECK_FALSE(Animation::sampleSkeleton(fixture.scene, fixture.mesh, shortSkin));
  auto binding = fixture.binding;
  binding.enabled = false;
  REQUIRE(fixture.scene.setSkin(fixture.mesh, binding));
  CHECK_FALSE(Animation::sampleSkeleton(fixture.scene, fixture.mesh, fixture.skin));
  REQUIRE(fixture.scene.setSkin(fixture.mesh, fixture.binding));
  auto foreign = World::Scene::create().value();
  CHECK_FALSE(Animation::sampleSkeleton(foreign, fixture.mesh, fixture.skin));
  REQUIRE(fixture.scene.destroyEntity(fixture.mesh));
  CHECK_FALSE(Animation::sampleSkeleton(fixture.scene, fixture.mesh, fixture.skin));
}

TEST_CASE("skeleton.obeys_scene_thread_scope_and_move_lifetime_without_devices") {
  Fixture fixture;
  bool rejected{};
  std::thread worker(
      [&] { rejected = !Animation::sampleSkeleton(fixture.scene, fixture.mesh, fixture.skin); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(fixture.scene);
  CHECK_FALSE(Animation::sampleSkeleton(fixture.scene, fixture.mesh, fixture.skin));
  REQUIRE(Animation::sampleSkeleton(moved, fixture.mesh, fixture.skin));
}

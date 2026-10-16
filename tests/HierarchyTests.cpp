#include <doctest/doctest.h>

#include <array>
#include <future>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

import Dot.Core.Result;
import Dot.Math;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace {
  using namespace Dot;
  using namespace Dot::World;

  auto near(const Math::Mat4& actual, const Math::Mat4& expected) -> void {
    for (std::size_t i = 0; i < actual.data.size(); ++i) {
      CHECK(actual.data[i] == doctest::Approx(expected.data[i]).epsilon(0.00002).scale(1));
    }
  }

  auto snapshot(const Scene& scene) -> std::string {
    return encodeScene(scene).value();
  }
}  // namespace

TEST_CASE("hierarchy.world_reparent_batches_preserve_original_poses_and_local_default") {
  auto scene = Scene::create().value();
  const auto a = scene.createEntity("A").value();
  const auto b = scene.createEntity("B").value();
  const auto c = scene.createEntity("C").value();
  REQUIRE(scene.setTransform(a, {{4, 1, 0}}));
  REQUIRE(scene.setTransform(b, {{2, 3, 0}}));
  REQUIRE(scene.setTransform(c, {{-3, 2, 1}}));
  REQUIRE(scene.setParent(b, a));
  const auto aWorld = scene.worldTransform(a).value();
  const auto bWorld = scene.worldTransform(b).value();
  const auto cWorld = scene.worldTransform(c).value();
  const std::array updates{ParentUpdate{a, b}, ParentUpdate{b, {}}, ParentUpdate{c, a}};
  REQUIRE(scene.setParents(updates, ParentSpace::World));
  near(scene.worldTransform(a).value(), aWorld);
  near(scene.worldTransform(b).value(), bWorld);
  near(scene.worldTransform(c).value(), cWorld);
  const auto before = snapshot(scene);
  CHECK_FALSE(scene.setParent(b, c, ParentSpace::World));
  CHECK(snapshot(scene) == before);
  const auto cLocal = scene.transform(c).value();
  REQUIRE(scene.setParent(c, std::nullopt));
  CHECK(scene.transform(c)->position == cLocal.position);
  CHECK(scene.worldTransform(c)->data[12] == cLocal.position.x);
}

TEST_CASE("hierarchy.group_overlapping_subtrees_preserves_scope_components_and_world_poses") {
  auto scene = Scene::create().value();
  const auto parent = scene.createEntity("Parent").value();
  const auto a = scene.createEntity("A").value();
  const auto child = scene.createEntity("Child").value();
  const auto b = scene.createEntity("B").value();
  REQUIRE(scene.setTransform(parent, {{5, -2, 1}, {}, {2, 3, 4}}));
  REQUIRE(scene.setTransform(a, {{1, 2, 0}}));
  REQUIRE(scene.setTransform(child, {{0, 1, 0}}));
  REQUIRE(scene.setTransform(b, {{-1, 0, 0}}));
  REQUIRE(scene.setParents(
      std::array{ParentUpdate{a, parent}, ParentUpdate{b, parent}, ParentUpdate{child, a}}));
  REQUIRE(scene.setMeshRenderer(a, MeshRenderer{}));
  REQUIRE(scene.setCamera(child, Camera{}));
  REQUIRE(scene.setScript(b, ScriptComponent{"actor.ts", "Actor", R"({"literal":"id:2"})"}));
  const auto scope = scene.id();
  const auto aWorld = scene.worldTransform(a).value();
  const auto bWorld = scene.worldTransform(b).value();
  const auto childWorld = scene.worldTransform(child).value();
  const auto selected = std::array{a, child, b};
  const auto group = scene.groupEntities(selected, "组 Group").value();
  CHECK(scene.id() == scope);
  CHECK(scene.entity(group)->parent == parent);
  CHECK(scene.entity(a)->parent == group);
  CHECK(scene.entity(b)->parent == group);
  CHECK(scene.entity(child)->parent == a);
  CHECK(scene.entity(a)->mesh.has_value());
  CHECK(scene.entity(b)->script->properties == R"({"literal":"id:2"})");
  near(scene.worldTransform(a).value(), aWorld);
  near(scene.worldTransform(b).value(), bWorld);
  near(scene.worldTransform(child).value(), childWorld);
  const auto groupWorld = scene.worldTransform(group).value();
  CHECK(groupWorld.data[12] == doctest::Approx(bWorld.data[12]));
  const auto roots = scene.ungroupEntities(std::span{&group, 1}).value();
  REQUIRE(roots.size() == 2);
  CHECK(roots[0] == a);
  CHECK(roots[1] == b);
  CHECK_FALSE(scene.entity(group));
  CHECK(scene.entity(a)->parent == parent);
  near(scene.worldTransform(a).value(), aWorld);
  near(scene.worldTransform(child).value(), childWorld);
  CHECK(scene.createEntity()->value > group.value);
}

TEST_CASE("hierarchy.ungroup_nested_containers_is_one_world_preserving_batch") {
  auto scene = Scene::create().value();
  auto outer = scene.createEntity("Outer").value();
  auto inner = scene.createEntity("Inner").value();
  auto a = scene.createEntity("A").value();
  auto b = scene.createEntity("B").value();
  REQUIRE(scene.setTransform(outer, {{3, 2, 1}, {}, {2, 2, 2}}));
  REQUIRE(scene.setTransform(inner, {{1, 0, 0}}));
  REQUIRE(scene.setTransform(a, {{2, 1, 0}}));
  REQUIRE(scene.setParents(
      std::array{ParentUpdate{inner, outer}, ParentUpdate{a, inner}, ParentUpdate{b, outer}}));
  auto beforeA = scene.worldTransform(a).value();
  auto beforeB = scene.worldTransform(b).value();
  auto removed = scene.ungroupEntities(std::array{inner, outer}).value();
  CHECK(removed == std::vector<EntityId>{a, b});
  CHECK(scene.entityCount().value() == 2);
  CHECK_FALSE(scene.entity(a)->parent);
  CHECK_FALSE(scene.entity(b)->parent);
  near(scene.worldTransform(a).value(), beforeA);
  near(scene.worldTransform(b).value(), beforeB);
}

TEST_CASE(
    "hierarchy.unrepresentable_shear_and_referenced_or_functional_containers_do_not_change_state") {
  auto scene = Scene::create().value();
  auto parent = scene.createEntity("Scaled").value();
  auto child = scene.createEntity("Rotated").value();
  const auto rotation = Math::normalized(Math::Quat{0, 0, .38268343F, .92387953F}).value();
  REQUIRE(scene.setTransform(parent, {{}, {}, {2, 1, 1}}));
  REQUIRE(scene.setTransform(child, {{}, rotation}));
  REQUIRE(scene.setParent(child, parent));
  auto before = snapshot(scene);
  CHECK_FALSE(scene.setParent(child, std::nullopt, ParentSpace::World));
  CHECK_FALSE(scene.ungroupEntities(std::span{&parent, 1}));
  CHECK(snapshot(scene) == before);
  REQUIRE(scene.setParent(child, parent, ParentSpace::World));
  CHECK(snapshot(scene) == before);
  auto other = scene.createEntity("Other").value();
  before = snapshot(scene);
  CHECK_FALSE(scene.groupEntities(std::array{child, other}, "Invalid shear"));
  CHECK(snapshot(scene) == before);
  REQUIRE(scene.setMeshRenderer(parent, MeshRenderer{}));
  CHECK_FALSE(scene.ungroupEntities(std::span{&parent, 1}));
  REQUIRE(scene.setMeshRenderer(parent, std::nullopt));
  REQUIRE(scene.setMeshRenderer(other, MeshRenderer{}));
  REQUIRE(scene.setSkin(other, SkinBinding{"assets/test.dotskin", {parent.value}}));
  before = snapshot(scene);
  CHECK_FALSE(scene.ungroupEntities(std::span{&parent, 1}));
  CHECK(snapshot(scene) == before);
  REQUIRE(scene.setSkin(other, std::nullopt));
  AnimationPlayer player;
  player.clips = {"assets/test.dotanimation"};
  player.targets = {{0, parent.value}};
  REQUIRE(scene.setAnimationPlayer(other, player));
  before = snapshot(scene);
  auto rejected = scene.ungroupEntities(std::span{&parent, 1});
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().message.find("animation target") != std::string::npos);
  CHECK(snapshot(scene) == before);
}

TEST_CASE(
    "hierarchy.group_depth_failure_returns_only_unpublished_identity_and_high_ids_never_wrap") {
  auto scene = Scene::create().value();
  std::vector<EntityId> nodes;
  std::vector<ParentUpdate> parents;

  for (std::size_t i = 0; i < 256; ++i) {
    nodes.push_back(scene.createEntity("Node").value());

    if (i > 0) {
      parents.push_back({nodes[i], nodes[i - 1]});
    }
  }

  REQUIRE(scene.setParents(parents));
  const auto before = snapshot(scene);
  CHECK_FALSE(scene.groupEntities(std::span{nodes.data(), 1}, "Too deep"));
  CHECK(snapshot(scene) == before);
  CHECK(scene.createEntity()->value == 257);
  auto sparse = Scene::create().value();
  const auto node = sparse.createEntity().value();
  const auto high = std::numeric_limits<std::uint64_t>::max() - 1;
  const auto group = sparse.groupEntities(std::span{&node, 1}, "Last", high).value();
  CHECK(group.value == high);
  REQUIRE(sparse.ungroupEntities(std::span{&group, 1}));
  CHECK_FALSE(sparse.createEntity());
  auto full = Scene::create().value();
  const auto root = full.createEntity().value();

  for (std::size_t i = 1; i < 10000; ++i) {
    REQUIRE(full.createEntity());
  }

  auto rejected = full.groupEntities(std::span{&root, 1}, "Over capacity");
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().code == Core::ErrorCode::Unavailable);
  CHECK(full.entityCount().value() == 10000);
  CHECK_FALSE(full.entity(root)->parent);
}

TEST_CASE("hierarchy.owner_move_and_invalid_selection_contracts_preserve_existing_state") {
  auto source = Scene::create().value();
  auto node = source.createEntity().value();
  auto other = Scene::create().value();
  auto foreign = other.createEntity().value();
  const auto before = snapshot(source);
  CHECK_FALSE(source.groupEntities({}, "Empty"));
  CHECK_FALSE(source.ungroupEntities({}));
  CHECK_FALSE(source.groupEntities(std::array{node, node}, "Duplicate"));
  CHECK_FALSE(source.groupEntities(std::array{node, foreign}, "Foreign"));
  CHECK_FALSE(source.ungroupEntities(std::array{node, foreign}));
  CHECK_FALSE(source.setParent(node, foreign, ParentSpace::World));
  CHECK_FALSE(source.setParents({}, static_cast<ParentSpace>(99)));
  auto worker = std::async(std::launch::async, [&] {
    return !source.groupEntities(std::span{&node, 1}, "Thread")
           && !source.ungroupEntities(std::span{&node, 1})
           && !source.setParent(node, {}, ParentSpace::World);
  });
  CHECK(worker.get());
  CHECK(snapshot(source) == before);
  auto moved = std::move(source);
  CHECK_FALSE(source.groupEntities(std::span{&node, 1}, "Moved"));
  CHECK_FALSE(source.ungroupEntities(std::span{&node, 1}));
  REQUIRE(moved.groupEntities(std::span{&node, 1}, "Owned"));
}

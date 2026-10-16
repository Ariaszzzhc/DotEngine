#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <utility>

import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.World.SceneContent;
import Dot.World.SceneLinks;

namespace {
  using namespace Dot::World;
  using Json = nlohmann::json;

  struct Fixture {
    Scene source = Scene::create("Panels").value();
    Scene target = Scene::create("Level").value();

    Fixture() {
      const auto panel = source.createEntity("Panel").value();
      const auto child = source.createEntity("Child").value();
      REQUIRE(source.setParent(child, panel));
      REQUIRE(source.setMeshRenderer(panel, MeshRenderer{}));
      REQUIRE(target.createEntity("Existing"));
      auto linked = prepareLinkedScene(target, source, "assets/panels.dotscene", 2, {{10, 0, 0}});
      REQUIRE_MESSAGE(linked, (linked ? "" : linked.error().message));
      target = std::move(linked->scene);
    }

    auto update(bool revert = false) -> SceneLinkUpdate {
      auto result = prepareSceneLinkUpdate(
          target, source, {target.id(), 2},
          target.nextEntityValue()->value_or(std::numeric_limits<std::uint64_t>::max()), revert);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }
  };

  auto text(const Scene& scene) -> std::string {
    return encodeScene(scene).value();
  }
}  // namespace

TEST_CASE("scene_links.native_roundtrip_owns_baseline_and_stable_disjoint_mappings") {
  Fixture f;
  const auto first = f.target.sceneLinks().value();
  REQUIRE(first.size() == 1);
  CHECK(first[0].root == 2);
  CHECK(first[0].mapping == std::vector<SceneLinkMapping>{{1, 3, {1}}, {2, 4, {2}}});
  auto restored = decodeScene(text(f.target)).value();
  CHECK(restored.sceneLinks().value() == first);
  auto second = prepareLinkedScene(f.target, f.source, "assets/panels.dotscene", 5).value();
  CHECK(second.scene.sceneLinks()->size() == 2);
  CHECK(second.scene.sceneLinks()->at(1).mapping[0].target == 6);
  auto copy = prepareSceneInstance(second.scene, f.target, 8).value();
  CHECK(copy.scene.sceneLinks()->size() == 2);
  auto flattened = prepareLinkedScene(f.source, f.target, "assets/level.dotscene", 3).value();
  CHECK(flattened.scene.sceneLinks()->size() == 1);
  CHECK_FALSE(Json::parse(flattened.scene.sceneLinks()->front().baseline).contains("links"));
  auto snapshot = f.target.sceneLinks().value();
  snapshot[0].asset = "assets/other.dotscene";
  CHECK(f.target.sceneLinks().value() == first);
}

TEST_CASE("scene_links.refresh_merges_properties_keeps_local_additions_and_is_repeatable") {
  Fixture f;
  const auto local = f.target.createEntity("Local addition").value();
  REQUIRE(f.target.setParent(local, EntityId{f.target.id(), 2}));
  REQUIRE(f.target.setTransform({f.target.id(), 3}, {{7, 0, 0}}));
  REQUIRE(f.source.renameEntity({f.source.id(), 1}, "Updated panel"));
  REQUIRE(f.source.setTransform({f.source.id(), 1}, {{2, 0, 0}, {}, {2, 3, 4}}));
  MeshRenderer hidden;
  hidden.visible = false;
  REQUIRE(f.source.setMeshRenderer({f.source.id(), 1}, hidden));
  const auto added = f.source.createEntity("New source node").value();
  REQUIRE(f.source.setParent(added, EntityId{f.source.id(), 1}));
  const auto before = text(f.target);
  auto updated = f.update();
  CHECK(text(f.target) == before);
  CHECK(updated.created == std::vector<std::uint64_t>{6});
  auto panel = updated.scene.entity({updated.scene.id(), 3}).value();
  CHECK(panel.name == "Updated panel");
  CHECK(panel.transform.position.x == 7);
  CHECK(panel.transform.scale.y == 3);
  CHECK_FALSE(panel.mesh->visible);
  CHECK(updated.scene.entity({updated.scene.id(), 6})->parent->value == 3);
  CHECK(updated.scene.findEntity(local.value));
  CHECK(updated.scene.transform(updated.root)->position.x == 10);
  f.target = std::move(updated.scene);
  auto repeated = f.update();
  CHECK(text(repeated.scene) == text(f.target));
  CHECK(repeated.created.empty());
}

TEST_CASE("scene_links.local_deletion_and_revert_retain_reserved_identity_and_placement") {
  Fixture f;
  REQUIRE(f.target.destroyEntity({f.target.id(), 4}));
  REQUIRE(f.source.renameEntity({f.source.id(), 2}, "Updated child"));
  auto refreshed = f.update();
  CHECK_FALSE(refreshed.scene.findEntity(4));
  f.target = std::move(refreshed.scene);
  auto reverted = f.update(true);
  CHECK(reverted.scene.entity({reverted.scene.id(), 4})->name == "Updated child");
  CHECK(reverted.scene.nextEntityValue().value() == 5);
  CHECK(reverted.scene.transform(reverted.root)->position.x == 10);
}

TEST_CASE("scene_links.source_deletion_conflicts_are_atomic_and_empty_sources_remain_linked") {
  Fixture f;
  REQUIRE(f.target.renameEntity({f.target.id(), 4}, "Local child override"));
  REQUIRE(f.source.destroyEntity({f.source.id(), 2}));
  const auto before = text(f.target);
  auto failed = prepareSceneLinkUpdate(f.target, f.source, {f.target.id(), 2}, 5);
  REQUIRE_FALSE(failed);
  CHECK(failed.error().code == Dot::Core::ErrorCode::InvalidState);
  CHECK(text(f.target) == before);
  auto reverted = f.update(true);
  CHECK_FALSE(reverted.scene.findEntity(4));
  f.target = std::move(reverted.scene);
  REQUIRE(f.source.destroyEntity({f.source.id(), 1}));
  auto empty = f.update();
  CHECK(empty.scene.entityCount().value() == 2);
  CHECK(empty.scene.sceneLinks()->front().mapping.empty());
  CHECK(decodeScene(text(empty.scene))->sceneLinks()->size() == 1);
}

TEST_CASE("scene_links.dangling_local_parent_prevents_source_node_removal_without_effects") {
  Fixture f;
  const auto child = f.target.createEntity("Local child").value();
  REQUIRE(f.target.setParent(child, EntityId{f.target.id(), 4}));
  REQUIRE(f.source.destroyEntity({f.source.id(), 2}));
  const auto before = text(f.target);
  CHECK_FALSE(prepareSceneLinkUpdate(f.target, f.source, {f.target.id(), 2}, 6));
  CHECK(text(f.target) == before);
  CHECK_FALSE(f.target.ungroupEntities(std::array{EntityId{f.target.id(), 2}}));
  REQUIRE(f.target.destroyEntity({f.target.id(), 2}));
  CHECK(f.target.sceneLinks()->empty());
}

TEST_CASE("scene_links.source_skin_animation_and_primary_flags_remap_to_stable_values") {
  Fixture f;
  const auto sourceMesh = EntityId{f.source.id(), 1};
  const auto joint = f.source.createEntity("Joint").value();
  REQUIRE(f.source.setSkin(sourceMesh, SkinBinding{"assets/body.dotskin", {joint.value}}));
  AnimationPlayer animation;
  animation.clips = {"assets/idle.dotanimation"};
  animation.targets = {{0, joint.value}};
  REQUIRE(f.source.setAnimationPlayer(sourceMesh, animation));
  REQUIRE(f.source.setCamera(joint, Camera{.primary = true}));
  REQUIRE(f.source.setAudioListener(joint, AudioListener{true}));
  auto updated = f.update();
  auto mesh = updated.scene.entity({updated.scene.id(), 3}).value();
  CHECK(mesh.skin->joints == std::vector<std::uint64_t>{5});
  CHECK(mesh.animation->targets[0].entity == 5);
  CHECK_FALSE(updated.scene.entity({updated.scene.id(), 5})->camera->primary);
  CHECK_FALSE(updated.scene.entity({updated.scene.id(), 5})->audioListener->primary);
}

TEST_CASE("scene_links.source_allocation_regression_or_retired_value_reuse_is_rejected") {
  Fixture f;
  REQUIRE(f.source.reserveEntityValuesBefore(10));
  auto updated = f.update();
  f.target = std::move(updated.scene);
  auto replacement = Scene::create().value();
  REQUIRE(replacement.createEntity());
  CHECK_FALSE(prepareSceneLinkUpdate(f.target, replacement, {f.target.id(), 2}, 5));
  REQUIRE(replacement.createEntityWithId("Reused source identity", 5));
  REQUIRE(replacement.reserveEntityValuesBefore(10));
  CHECK_FALSE(prepareSceneLinkUpdate(f.target, replacement, {f.target.id(), 2}, 5));
}

TEST_CASE("scene_links.refresh_is_scoped_to_one_instance_even_when_sources_are_shared") {
  Fixture f;
  auto second = prepareLinkedScene(f.target, f.source, "assets/panels.dotscene", 5).value();
  f.target = std::move(second.scene);
  const auto other = f.target.sceneLinks()->at(1);
  REQUIRE(f.source.renameEntity({f.source.id(), 1}, "New source name"));
  REQUIRE(f.source.createEntity("Added source node"));
  auto updated = f.update();
  CHECK(updated.created == std::vector<std::uint64_t>{8});
  CHECK(updated.scene.entity({updated.scene.id(), 3})->name == "New source name");
  CHECK(updated.scene.entity({updated.scene.id(), 6})->name == "Panel");
  CHECK(updated.scene.sceneLinks()->at(1) == other);
}

TEST_CASE("scene_links.incompatible_merged_components_fail_before_any_publication") {
  Fixture f;
  REQUIRE(f.target.setCharacterController({f.target.id(), 3}, CharacterController{}));
  REQUIRE(f.source.setRigidBody({f.source.id(), 1}, RigidBody{}));
  const auto before = text(f.target);
  CHECK_FALSE(prepareSceneLinkUpdate(f.target, f.source, {f.target.id(), 2}, 5));
  CHECK(text(f.target) == before);
  auto reverted = f.update(true);
  CHECK(reverted.scene.entity({reverted.scene.id(), 3})->rigidBody.has_value());
  CHECK_FALSE(reverted.scene.entity({reverted.scene.id(), 3})->character);
}

TEST_CASE("scene_links.exhausted_target_allows_nonallocating_refresh_and_rejects_new_nodes") {
  Fixture f;
  const auto maximum = std::numeric_limits<std::uint64_t>::max();
  REQUIRE(f.target.reserveEntityValuesBefore(maximum - 1));
  REQUIRE(f.source.createEntity("Last target slot"));
  auto last = f.update();
  CHECK(last.created == std::vector<std::uint64_t>{maximum - 1});
  CHECK_FALSE(last.scene.nextEntityValue().value());
  f.target = std::move(last.scene);
  REQUIRE(f.source.renameEntity({f.source.id(), 1}, "No allocation"));
  auto refreshed = f.update();
  CHECK(refreshed.scene.entity({refreshed.scene.id(), 3})->name == "No allocation");
  REQUIRE(f.source.createEntity("Too many identities"));
  CHECK_FALSE(prepareSceneLinkUpdate(f.target, f.source, {f.target.id(), 2}, maximum));
}

TEST_CASE("scene_links.strict_metadata_owner_and_failed_replacement_preserve_state") {
  Fixture f;
  const auto original = f.target.sceneLinks().value();

  for (int failure = 0; failure < 7; ++failure) {
    auto invalid = original;

    switch (failure) {
      case 0:
        invalid[0].root = 99;
        break;
      case 1:
        invalid[0].asset = "../source.dotscene";
        break;
      case 2:
        invalid[0].mapping[0].target = 5;
        break;
      case 3:
        invalid[0].mapping[1] = invalid[0].mapping[0];
        break;
      case 4:
        invalid[0].baseline = "{}";
        break;
      case 5:
        invalid[0].baseline = text(f.target);
        break;
      case 6:
        invalid.push_back(invalid[0]);
        break;
    }

    CHECK_FALSE(f.target.setSceneLinks(invalid));
    CHECK(f.target.sceneLinks().value() == original);
  }

  bool rejected{};
  std::thread worker(
      [&] { rejected = !f.target.sceneLinks() && !f.target.setSceneLinks(original); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(f.target);
  CHECK_FALSE(f.target.sceneLinks());
  CHECK_FALSE(f.target.setSceneLinks(original));
  CHECK(moved.sceneLinks().value() == original);
  auto invalid = Json::parse(text(moved));
  invalid["links"][0]["unexpected"] = true;
  CHECK_FALSE(decodeScene(invalid.dump()));
  CHECK_FALSE(prepareSceneLinkUpdate(moved, f.source, {f.source.id(), 2}, 5));
}

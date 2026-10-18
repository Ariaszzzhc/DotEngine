#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <utility>

import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.World.SceneContent;
import Dot.World.SceneLinks;
import Dot.World.SceneSources;
import Dot.World.SceneResolver;

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

TEST_CASE("scene_links.apply_field_prepares_source_and_rebases_only_selected_override") {
  Fixture f;
  REQUIRE(f.target.renameEntity({f.target.id(), 3}, "Shared panel"));
  REQUIRE(f.target.setTransform({f.target.id(), 3}, {{7, 0, 0}}));
  const auto before = text(f.target);
  const auto sourceBefore = text(f.source);
  auto snapshot = captureSceneSource(f.source, "assets/panels.dotscene").value();
  auto applied = prepareSceneFieldApply(f.target, snapshot, {f.target.id(), 2}, {f.target.id(), 3},
                                        "name", 5);
  REQUIRE_MESSAGE(applied, (applied ? "" : applied.error().message));
  CHECK(applied->sourceChanged);
  CHECK(applied->source.entity({applied->source.id(), 1})->name == "Shared panel");
  const auto& result = applied->instance.scene;
  CHECK(result.entity({result.id(), 3})->transform.position.x == 7);
  auto changes = inspectSceneOverrides(result, {result.id(), 2}).value();
  CHECK(changes.total == 1);
  CHECK(changes.fields == std::map<std::string, std::size_t>{{"transform", 1}});
  CHECK(text(f.target) == before);
  CHECK(text(f.source) == sourceBefore);
  auto next = captureSceneSource(applied->source, "assets/panels.dotscene").value();
  auto repeat = prepareSceneFieldApply(result, next, {result.id(), 2}, {result.id(), 3}, "name", 5);
  REQUIRE(repeat);
  CHECK_FALSE(repeat->sourceChanged);
  CHECK(text(repeat->instance.scene) == text(result));
  auto reserved
      = prepareSceneFieldApply(result, next, {result.id(), 2}, {result.id(), 3}, "name", 99)
            .value();
  CHECK(reserved.instance.scene.nextEntityValue()->value() == 99);
}

TEST_CASE("scene_links.apply_field_conflicts_and_native_reference_boundaries_are_atomic") {
  Fixture f;
  REQUIRE(f.target.renameEntity({f.target.id(), 3}, "Local"));
  REQUIRE(f.source.renameEntity({f.source.id(), 1}, "External"));
  const auto before = text(f.target);
  const auto sourceBefore = text(f.source);
  auto snapshot = captureSceneSource(f.source, "assets/panels.dotscene").value();
  CHECK_FALSE(prepareSceneFieldApply(f.target, snapshot, {f.target.id(), 2}, {f.target.id(), 3},
                                     "name", 5));
  CHECK_FALSE(
      prepareSceneFieldApply(f.target, snapshot, {f.target.id(), 2}, {f.target.id(), 3}, "id", 5));
  CHECK_FALSE(prepareSceneFieldApply(f.target, snapshot, {f.target.id(), 2}, {f.target.id(), 2},
                                     "name", 5));
  CHECK_FALSE(prepareSceneFieldApply(f.target, snapshot, {f.target.id(), 2}, {f.target.id(), 3},
                                     "name", 4));
  CHECK_FALSE(prepareSceneFieldApply(f.target, snapshot, {f.target.id(), 2}, {f.source.id(), 1},
                                     "name", 5));
  CHECK(text(f.target) == before);
  CHECK(text(f.source) == sourceBefore);
  REQUIRE(f.target.setParent({f.target.id(), 4}, std::nullopt));
  CHECK_FALSE(prepareSceneFieldApply(f.target, snapshot, {f.target.id(), 2}, {f.target.id(), 4},
                                     "parent", 5));
  AnimationPlayer animation;
  animation.clips = {"assets/idle.dotanimation"};
  animation.targets = {{0, 1}};
  REQUIRE(f.target.setAnimationPlayer({f.target.id(), 3}, animation));
  CHECK_FALSE(prepareSceneFieldApply(f.target, snapshot, {f.target.id(), 2}, {f.target.id(), 3},
                                     "animation", 5));
}

TEST_CASE("scene_links.apply_native_references_and_opaque_script_values_in_source_space") {
  Fixture f;
  auto snapshot = captureSceneSource(f.source, "assets/panels.dotscene").value();
  AnimationPlayer animation;
  animation.clips = {"assets/idle.dotanimation"};
  animation.targets = {{0, 4}};
  REQUIRE(f.target.setAnimationPlayer({f.target.id(), 3}, animation));
  auto applied = prepareSceneFieldApply(f.target, snapshot, {f.target.id(), 2}, {f.target.id(), 3},
                                        "animation", 5)
                     .value();
  CHECK(applied.source.entity({applied.source.id(), 1})->animation->targets.front().entity == 2);
  CHECK(inspectSceneOverrides(applied.instance.scene, applied.instance.root)->total == 0);
  auto& target = applied.instance.scene;
  REQUIRE(
      target.setScript({target.id(), 3}, ScriptComponent{"actor.ts", "Actor", R"({"value":4})"}));
  auto current = captureSceneSource(applied.source, "assets/panels.dotscene").value();
  auto script
      = prepareSceneFieldApply(target, current, {target.id(), 2}, {target.id(), 3}, "script", 5)
            .value();
  CHECK(script.source.entity({script.source.id(), 1})->script->properties == R"({"value":4})");
  CHECK(inspectSceneOverrides(script.instance.scene, script.instance.root)->total == 0);
}

TEST_CASE("scene_links.apply_camera_preserves_source_primary_and_rejects_local_primary") {
  Fixture f;
  REQUIRE(f.source.setCamera({f.source.id(), 1}, Camera{.primary = true}));
  f.target = std::move(f.update().scene);
  auto camera = f.target.entity({f.target.id(), 3})->camera.value();
  camera.verticalFovDegrees = 45;
  REQUIRE(f.target.setCamera({f.target.id(), 3}, camera));
  auto snapshot = captureSceneSource(f.source, "assets/panels.dotscene").value();
  auto applied = prepareSceneFieldApply(f.target, snapshot, {f.target.id(), 2}, {f.target.id(), 3},
                                        "camera", 5)
                     .value();
  CHECK(applied.source.entity({applied.source.id(), 1})->camera->primary);
  CHECK_FALSE(applied.instance.scene.entity({applied.instance.scene.id(), 3})->camera->primary);
  CHECK(inspectSceneOverrides(applied.instance.scene, applied.instance.root)->total == 0);
  camera.primary = true;
  REQUIRE(f.target.setCamera({f.target.id(), 3}, camera));
  CHECK_FALSE(prepareSceneFieldApply(f.target, snapshot, {f.target.id(), 2}, {f.target.id(), 3},
                                     "camera", 5));
}

TEST_CASE("scene_links.apply_nested_field_keeps_link_identity_and_later_source_updates") {
  Fixture f;
  auto assembly = Scene::create("Assembly").value();
  assembly = std::move(prepareLinkedScene(assembly, f.source, "assets/leaf.dotscene", 1)->scene);
  std::map<std::string, std::string> saved{{"assets/leaf.dotscene", text(f.source)},
                                           {"assets/assembly.dotscene", text(assembly)}};
  const auto resolve = [&] {
    return resolveSceneSource("assets/assembly.dotscene",
                              [&](std::string_view asset, std::size_t) {
                                return Dot::Core::Result<std::string>{saved.at(std::string(asset))};
                              })
        .value();
  };
  auto current = resolve();
  auto level = Scene::create("Level").value();
  auto linked = prepareLinkedScene(level, current.source, 1).value();
  auto& target = linked.scene;
  const auto links = target.sceneLinks().value();
  const auto mapped
      = std::ranges::find(links[0].mapping, SceneSourceKey{1, 1}, &SceneLinkMapping::key);
  REQUIRE(mapped != links[0].mapping.end());
  const auto value = mapped->target;
  REQUIRE(target.renameEntity({target.id(), value}, "Assembly-local panel"));
  auto applied = prepareSceneFieldApply(target, current.source, linked.root, {target.id(), value},
                                        "name", target.nextEntityValue()->value())
                     .value();
  CHECK(applied.source.sceneLinks()->size() == 1);
  CHECK(f.source.entity({f.source.id(), 1})->name == "Panel");
  saved["assets/assembly.dotscene"] = text(applied.source);
  REQUIRE(f.source.renameEntity({f.source.id(), 2}, "Leaf child updated"));
  saved["assets/leaf.dotscene"] = text(f.source);
  current = resolve();
  auto updated
      = prepareSceneLinkUpdate(applied.instance.scene, current.source, applied.instance.root,
                               applied.instance.scene.nextEntityValue()->value())
            .value();
  CHECK(updated.scene.entity({updated.scene.id(), value})->name == "Assembly-local panel");
  CHECK(inspectSceneOverrides(updated.scene, updated.root)->total == 0);
  CHECK(updated.scene.sceneLinks()->front().mapping[1].key == SceneSourceKey{1, 1});
}

TEST_CASE("scene_links.apply_equal_nested_source_does_not_persist_temporary_namespace_ids") {
  Fixture f;
  auto assembly = Scene::create("Assembly").value();
  assembly = std::move(prepareLinkedScene(assembly, f.source, "assets/leaf.dotscene", 1)->scene);
  std::map<std::string, std::string> saved{{"assets/leaf.dotscene", text(f.source)},
                                           {"assets/assembly.dotscene", text(assembly)}};
  const auto resolve = [&] {
    return resolveSceneSource("assets/assembly.dotscene",
                              [&](std::string_view asset, std::size_t) {
                                return Dot::Core::Result<std::string>{saved.at(std::string(asset))};
                              })
        .value();
  };
  auto original = resolve();
  auto scene = Scene::create("Level").value();
  auto linked = prepareLinkedScene(scene, original.source, 1).value();
  const auto mapping = linked.scene.sceneLinks()->front().mapping;
  const auto leaf
      = std::ranges::find(mapping, SceneSourceKey{1, 1}, &SceneLinkMapping::key)->target;
  REQUIRE(linked.scene.renameEntity({linked.scene.id(), leaf}, "Same edit"));
  REQUIRE(assembly.renameEntity({assembly.id(), 2}, "Same edit"));
  saved["assets/assembly.dotscene"] = text(assembly);
  REQUIRE(f.source.createEntity("New inherited node"));
  saved["assets/leaf.dotscene"] = text(f.source);
  auto current = resolve();
  auto applied = prepareSceneFieldApply(linked.scene, current.source, linked.root,
                                        {linked.scene.id(), leaf}, "name", 5)
                     .value();
  CHECK_FALSE(applied.sourceChanged);
  CHECK(inspectSceneOverrides(applied.instance.scene, applied.instance.root)->total == 0);
  auto again = resolve();
  auto refreshed
      = prepareSceneLinkUpdate(applied.instance.scene, again.source, applied.instance.root,
                               applied.instance.scene.nextEntityValue()->value());
  REQUIRE_MESSAGE(refreshed, (refreshed ? "" : refreshed.error().message));
  CHECK(text(refreshed->scene) == text(applied.instance.scene));
}

TEST_CASE("scene_links.apply_rejects_invalid_combined_source_hierarchy") {
  auto source = Scene::create("Source").value();
  REQUIRE(source.createEntity("A"));
  REQUIRE(source.createEntity("B"));
  auto target = Scene::create("Target").value();
  auto linked = prepareLinkedScene(target, source, "assets/source.dotscene", 1).value();
  REQUIRE(linked.scene.setParent({linked.scene.id(), 2}, EntityId{linked.scene.id(), 3}));
  REQUIRE(source.setParent({source.id(), 2}, EntityId{source.id(), 1}));
  auto snapshot = captureSceneSource(source, "assets/source.dotscene").value();
  const auto before = text(linked.scene);
  CHECK_FALSE(prepareSceneFieldApply(linked.scene, snapshot, linked.root, {linked.scene.id(), 2},
                                     "parent", 4));
  CHECK(text(linked.scene) == before);
}

TEST_CASE("scene_links.retarget_preserves_overrides_identity_placement_and_other_instances") {
  Fixture f;
  auto second = prepareLinkedScene(f.target, f.source, "assets/panels.dotscene", 5).value();
  f.target = std::move(second.scene);
  const auto sibling = f.target.sceneLinks()->at(1);
  const auto local = f.target.createEntity("Local addition").value();
  REQUIRE(f.target.setParent(local, EntityId{f.target.id(), 2}));
  REQUIRE(f.target.renameEntity({f.target.id(), 3}, "Local panel"));
  REQUIRE(f.target.destroyEntity({f.target.id(), 4}));
  REQUIRE(f.source.renameEntity({f.source.id(), 1}, "Relocated panel"));
  auto mesh = f.source.entity({f.source.id(), 1})->mesh.value();
  mesh.visible = false;
  REQUIRE(f.source.setMeshRenderer({f.source.id(), 1}, mesh));
  REQUIRE(f.source.createEntity("New source node"));
  auto source = captureSceneSource(f.source, "assets/moved/panels.dotscene").value();
  const auto before = text(f.target);
  const auto sourceBefore = text(source.scene);
  auto updated = prepareSceneLinkRetarget(f.target, source, {f.target.id(), 2}, 9);
  REQUIRE_MESSAGE(updated, (updated ? "" : updated.error().message));
  CHECK(text(f.target) == before);
  CHECK(text(source.scene) == sourceBefore);
  CHECK(updated->created == std::vector<std::uint64_t>{9});
  CHECK(updated->root.value == 2);
  CHECK(updated->scene.transform(updated->root)->position.x == 10);
  CHECK(updated->scene.entity({updated->scene.id(), 3})->name == "Local panel");
  CHECK_FALSE(updated->scene.entity({updated->scene.id(), 3})->mesh->visible);
  CHECK_FALSE(updated->scene.findEntity(4));
  CHECK(updated->scene.findEntity(local.value).has_value());
  CHECK(updated->scene.sceneLinks()->at(1) == sibling);
  const auto link = updated->scene.sceneLinks()->front();
  CHECK(link.asset == source.asset);
  CHECK(link.namespaces.front().asset == source.asset);
  CHECK(link.mapping[0].target == 3);
  CHECK(link.mapping[1].target == 4);
  CHECK(link.mapping[2].target == 9);
  auto reopened = decodeScene(text(updated->scene)).value();
  auto repeated = prepareSceneLinkRetarget(reopened, source, {reopened.id(), 2}, 10).value();
  CHECK(text(repeated.scene) == text(reopened));
  CHECK(repeated.created.empty());
  auto reset = prepareSceneBaselineRevert(reopened, {reopened.id(), 2}).value();
  CHECK(reset.scene.entity({reset.scene.id(), 3})->name == "Relocated panel");
  CHECK(reset.scene.findEntity(4).has_value());
  CHECK_FALSE(reset.scene.findEntity(local.value));
  CHECK(reset.scene.transform(reset.root)->position.x == 10);
}

TEST_CASE("scene_links.retarget_keeps_source_reservations_and_rejects_retired_key_reuse") {
  Fixture f;
  REQUIRE(f.source.reserveEntityValuesBefore(10));
  f.target = std::move(f.update().scene);
  const auto before = text(f.target);
  auto replacement = Scene::create().value();
  REQUIRE(replacement.createEntity("Panel"));
  auto incoming = captureSceneSource(replacement, "assets/moved.dotscene").value();
  CHECK_FALSE(prepareSceneLinkRetarget(f.target, incoming, {f.target.id(), 2}, 5));
  REQUIRE(replacement.createEntityWithId("Reused retired identity", 5));
  REQUIRE(replacement.reserveEntityValuesBefore(10));
  incoming = captureSceneSource(replacement, "assets/moved.dotscene").value();
  CHECK_FALSE(prepareSceneLinkRetarget(f.target, incoming, {f.target.id(), 2}, 5));
  CHECK(text(f.target) == before);
  CHECK_FALSE(prepareSceneLinkRetarget(f.target, incoming, {f.target.id(), 3}, 5));
  CHECK_FALSE(prepareSceneLinkRetarget(f.target, incoming, {f.target.id() + 1, 2}, 5));
}

TEST_CASE("scene_links.retarget_conflicts_never_publish_partial_paths_or_node_changes") {
  for (const bool dangling : {false, true}) {
    Fixture f;

    if (dangling) {
      const auto local = f.target.createEntity("Local child").value();
      REQUIRE(f.target.setParent(local, EntityId{f.target.id(), 4}));
    } else {
      REQUIRE(f.target.renameEntity({f.target.id(), 4}, "Overridden child"));
    }

    REQUIRE(f.source.destroyEntity({f.source.id(), 2}));
    auto source = captureSceneSource(f.source, "assets/moved.dotscene").value();
    const auto before = text(f.target);
    const auto next = f.target.nextEntityValue()->value();
    CHECK_FALSE(prepareSceneLinkRetarget(f.target, source, {f.target.id(), 2}, next));
    CHECK(text(f.target) == before);
    CHECK(f.target.sceneLinks()->front().asset == "assets/panels.dotscene");
  }
}

TEST_CASE("scene_links.retarget_nested_keys_survive_new_materialization_and_keep_child_assets") {
  Fixture f;
  auto assembly = Scene::create("Assembly").value();
  assembly = std::move(prepareLinkedScene(assembly, f.source, "assets/leaf.dotscene", 1)->scene);
  auto original = captureSceneSource(assembly, "assets/assembly.dotscene").value();
  auto target = Scene::create("Level").value();
  auto linked = prepareLinkedScene(target, original, 1).value();
  const auto mapping = linked.scene.sceneLinks()->front().mapping;
  const auto value
      = std::ranges::find(mapping, SceneSourceKey{1, 1}, &SceneLinkMapping::key)->target;
  REQUIRE(linked.scene.renameEntity({linked.scene.id(), value}, "Nested local override"));
  REQUIRE(f.source.createEntity("New nested node"));
  const std::map<std::string, std::string> saved{{"assets/moved-assembly.dotscene", text(assembly)},
                                                 {"assets/leaf.dotscene", text(f.source)}};
  auto incoming = resolveSceneSource("assets/moved-assembly.dotscene", [&](std::string_view asset,
                                                                           std::size_t) {
                    return Dot::Core::Result<std::string>{saved.at(std::string(asset))};
                  }).value();
  auto updated = prepareSceneLinkRetarget(linked.scene, incoming.source, linked.root,
                                          linked.scene.nextEntityValue()->value());
  REQUIRE_MESSAGE(updated, (updated ? "" : updated.error().message));
  CHECK(updated->scene.entity({updated->scene.id(), value})->name == "Nested local override");
  CHECK(updated->created.size() == 1);
  CHECK(updated->scene.sceneLinks()->front().mapping[1].key == (SceneSourceKey{1, 1}));
  auto changed = incoming.source.namespaces;
  auto nested = std::ranges::find(changed, SceneSourceKey{1}, &SceneSourceNamespace::prefix);
  REQUIRE(nested != changed.end());
  nested->asset = "assets/unrelated-leaf.dotscene";
  incoming.source.namespaces = std::move(changed);
  CHECK_FALSE(prepareSceneLinkRetarget(linked.scene, incoming.source, linked.root,
                                       linked.scene.nextEntityValue()->value()));
}

TEST_CASE("scene_links.retarget_remaps_native_references_and_demotes_primary_components") {
  Fixture f;
  const auto mesh = EntityId{f.source.id(), 1};
  const auto joint = f.source.createEntity("Joint").value();
  REQUIRE(f.source.setSkin(mesh, SkinBinding{"assets/body.dotskin", {joint.value}}));
  AnimationPlayer animation;
  animation.clips = {"assets/idle.dotanimation"};
  animation.targets = {{0, joint.value}};
  REQUIRE(f.source.setAnimationPlayer(mesh, animation));
  REQUIRE(f.source.setCamera(joint, Camera{.primary = true}));
  REQUIRE(f.source.setAudioListener(joint, AudioListener{true}));
  auto source = captureSceneSource(f.source, "assets/moved.dotscene").value();
  auto updated = prepareSceneLinkRetarget(f.target, source, {f.target.id(), 2}, 5).value();
  auto native = updated.scene.entity({updated.scene.id(), 3}).value();
  CHECK(native.skin->joints == std::vector<std::uint64_t>{5});
  CHECK(native.animation->targets[0].entity == 5);
  CHECK_FALSE(updated.scene.entity({updated.scene.id(), 5})->camera->primary);
  CHECK_FALSE(updated.scene.entity({updated.scene.id(), 5})->audioListener->primary);
  source.asset = "../invalid.dotscene";
  CHECK_FALSE(prepareSceneLinkRetarget(f.target, source, {f.target.id(), 2}, 5));
}

TEST_CASE("scene_links.batch_matches_ordered_single_updates_and_preserves_input") {
  Fixture f;
  f.target = std::move(prepareLinkedScene(f.target, f.source, "assets/panels.dotscene", 5)->scene);
  REQUIRE(f.target.renameEntity({f.target.id(), 3}, "Local first"));
  REQUIRE(f.target.renameEntity({f.target.id(), 6}, "Local second"));
  REQUIRE(f.source.createEntity("Incoming node"));
  auto source = captureSceneSource(f.source, "assets/panels.dotscene").value();
  const auto before = text(f.target);
  auto sequential = prepareSceneLinkUpdate(f.target, source, {f.target.id(), 2}, 8).value();
  sequential
      = prepareSceneLinkUpdate(sequential.scene, source, {sequential.scene.id(), 5}, 9).value();
  auto batch = SceneLinkBatch::begin(f.target, 8).value();
  REQUIRE(batch.append(source, {f.target.id(), 2}));
  REQUIRE(batch.append(source, {f.target.id(), 5}));
  auto prepared = batch.finish().value();
  CHECK(text(prepared.scene) == text(sequential.scene));
  CHECK(prepared.created == (std::vector<std::uint64_t>{8, 9}));
  CHECK(prepared.root.value == 5);
  CHECK(text(f.target) == before);
}

TEST_CASE("scene_links.batch_owns_inputs_and_never_reuses_retired_staged_allocations") {
  std::optional<SceneLinkBatch> batch;

  {
    Fixture f;
    const EntityId root{f.target.id(), 2};
    batch.emplace(SceneLinkBatch::begin(f.target, 5).value());
    auto node = f.source.createEntity("Temporary source node").value();

    {
      auto source = captureSceneSource(f.source, "assets/moved.dotscene").value();
      REQUIRE(batch->append(source, root, SceneLinkAction::Retarget));
    }

    CHECK(batch->sourcePath(root).value() == "assets/moved.dotscene");
    REQUIRE(f.source.destroyEntity(node));

    {
      auto source = captureSceneSource(f.source, "assets/moved.dotscene").value();
      REQUIRE(batch->append(source, root));
    }

    REQUIRE(f.source.createEntity("Surviving new source node"));

    {
      auto source = captureSceneSource(f.source, "assets/moved.dotscene").value();
      REQUIRE(batch->append(source, root));
    }
  }

  auto prepared = batch->finish().value();
  CHECK(prepared.created == std::vector<std::uint64_t>{6});
  CHECK_FALSE(prepared.scene.findEntity(5));
  CHECK(prepared.scene.entity({prepared.scene.id(), 6})->name == "Surviving new source node");
  CHECK(prepared.scene.nextEntityValue().value() == 7);
}

TEST_CASE("scene_links.batch_append_failure_is_terminal_without_publishing_earlier_work") {
  Fixture f;
  f.target = std::move(prepareLinkedScene(f.target, f.source, "assets/panels.dotscene", 5)->scene);
  REQUIRE(f.target.renameEntity({f.target.id(), 7}, "Local deletion conflict"));
  auto removed = decodeScene(text(f.source)).value();
  REQUIRE(removed.destroyEntity({removed.id(), 2}));
  REQUIRE(f.source.createEntity("Incoming node"));
  auto first = captureSceneSource(f.source, "assets/panels.dotscene").value();
  auto second = captureSceneSource(removed, "assets/panels.dotscene").value();
  const auto before = text(f.target);
  auto batch = SceneLinkBatch::begin(f.target, 8).value();
  REQUIRE(batch.append(first, {f.target.id(), 2}));
  auto rejected = batch.append(second, {f.target.id(), 5});
  REQUIRE_FALSE(rejected);
  auto finished = batch.finish();
  REQUIRE_FALSE(finished);
  CHECK(finished.error().message == rejected.error().message);
  CHECK_FALSE(batch.append(first, {f.target.id(), 5}));
  CHECK(text(f.target) == before);
}

TEST_CASE("scene_links.batch_validates_final_native_references_after_explicit_dependent_restore") {
  Fixture f;
  f.target = std::move(prepareLinkedScene(f.target, f.source, "assets/panels.dotscene", 5)->scene);
  REQUIRE(f.target.setParent({f.target.id(), 6}, EntityId{f.target.id(), 4}));
  auto reduced = decodeScene(text(f.source)).value();
  REQUIRE(reduced.destroyEntity({reduced.id(), 2}));
  auto incoming = captureSceneSource(reduced, "assets/reduced.dotscene").value();
  auto original = captureSceneSource(f.source, "assets/panels.dotscene").value();
  CHECK_FALSE(prepareSceneLinkRetarget(f.target, incoming, {f.target.id(), 2}, 8));
  auto incomplete = SceneLinkBatch::begin(f.target, 8).value();
  REQUIRE(incomplete.append(incoming, {f.target.id(), 2}, SceneLinkAction::Retarget));
  CHECK_FALSE(incomplete.finish());
  auto batch = SceneLinkBatch::begin(f.target, 8).value();
  REQUIRE(batch.append(incoming, {f.target.id(), 2}, SceneLinkAction::Retarget));
  REQUIRE(batch.append(original, {f.target.id(), 5}, SceneLinkAction::RestoreSource));
  auto prepared = batch.finish().value();
  CHECK_FALSE(prepared.scene.findEntity(4));
  CHECK(prepared.scene.entity({prepared.scene.id(), 6})->parent->value == 5);
  CHECK(f.target.entity({f.target.id(), 6})->parent->value == 4);
}

TEST_CASE("scene_links.batch_owner_move_empty_and_consumed_lifetimes_are_explicit") {
  Fixture f;
  const EntityId root{f.target.id(), 2};
  auto source = captureSceneSource(f.source, "assets/panels.dotscene").value();
  CHECK_FALSE(SceneLinkBatch::begin(f.target, 4));
  auto empty = SceneLinkBatch::begin(f.target, 5).value();
  CHECK_FALSE(empty.finish());
  CHECK_FALSE(empty.append(source, root));
  auto batch = SceneLinkBatch::begin(f.target, 5).value();
  auto moved = std::move(batch);
  CHECK_FALSE(batch.sourcePath(root));
  bool rejected{};
  std::thread other([&] {
    rejected = !moved.sourcePath(root) && !moved.append(source, root) && !moved.finish();
  });
  other.join();
  CHECK(rejected);
  REQUIRE(moved.append(source, root));
  REQUIRE(moved.finish());
  CHECK_FALSE(moved.finish());
}

TEST_CASE("scene_links.batch_bounds_change_count_before_any_external_publication") {
  Fixture f;
  auto source = captureSceneSource(f.source, "assets/panels.dotscene").value();
  auto batch = SceneLinkBatch::begin(f.target, 5).value();

  for (int i = 0; i < 256; ++i) {
    REQUIRE(batch.append(source, {f.target.id(), 2}));
  }

  CHECK_FALSE(batch.append(source, {f.target.id(), 2}));
  CHECK_FALSE(batch.finish());
  auto invalid = SceneLinkBatch::begin(f.target, 5).value();
  CHECK_FALSE(invalid.append(source, {f.target.id(), 2}, static_cast<SceneLinkAction>(99)));
  CHECK_FALSE(invalid.finish());
}

TEST_CASE("scene_links.batch_bounds_working_nodes_while_input_remains_unchanged") {
  Fixture f;

  for (int i = 4; i < 9999; ++i) {
    REQUIRE(f.target.createEntity("Existing"));
  }

  const auto before = text(f.target);
  auto batch = SceneLinkBatch::begin(f.target, 10000).value();
  REQUIRE(f.source.createEntity("Last available node"));
  auto first = captureSceneSource(f.source, "assets/panels.dotscene").value();
  REQUIRE(batch.append(first, {f.target.id(), 2}));
  REQUIRE(f.source.createEntity("Overflow node"));
  auto second = captureSceneSource(f.source, "assets/panels.dotscene").value();
  CHECK_FALSE(batch.append(second, {f.target.id(), 2}));
  CHECK_FALSE(batch.finish());
  CHECK(text(f.target) == before);
}

TEST_CASE("scene_links.detached_transfer_preserves_provenance_and_reserved_values") {
  Fixture fixture;
  const auto original = text(fixture.target);
  const auto oldScope = fixture.target.id();
  const auto floor = fixture.target.nextEntityValue().value();
  auto detached = fixture.target.detach();
  REQUIRE(detached);
  auto received = Scene::adopt(*detached);
  REQUIRE(received);
  CHECK(received->id() != oldScope);
  CHECK(text(*received) == original);
  CHECK(received->nextEntityValue().value() == floor);
  CHECK_FALSE(received->entity({oldScope, 2}));
  CHECK(received->sceneLinks()->front().mapping.front().target == 3);
}

TEST_CASE("scene_links.apply_instance_promotes_additions_deletions_and_all_properties_atomically") {
  Fixture f;
  REQUIRE(f.target.renameEntity({f.target.id(), 3}, "Shared edited panel"));
  REQUIRE(f.target.destroyEntity({f.target.id(), 4}));
  const auto added = f.target.createEntity("Added bracket").value();
  REQUIRE(f.target.setParent(added, EntityId{f.target.id(), 3}));
  REQUIRE(f.target.setMeshRenderer(added, MeshRenderer{}));
  REQUIRE(f.target.setTransform(added, {{2, 3, 4}}));
  const auto before = text(f.target);
  const auto original = text(f.source);
  auto snapshot = captureSceneSource(f.source, "assets/panels.dotscene").value();
  auto applied = prepareSceneInstanceApply(f.target, snapshot, {f.target.id(), 2}, 6);
  REQUIRE_MESSAGE(applied, (applied ? "" : applied.error().message));
  CHECK(applied->sourceChanged);
  CHECK(applied->source.entityCount().value() == 2);
  CHECK_FALSE(applied->source.findEntity(2));
  CHECK(applied->source.entity({applied->source.id(), 1})->name == "Shared edited panel");
  CHECK(applied->source.entity({applied->source.id(), 3})->parent->value == 1);
  CHECK(applied->source.nextEntityValue()->value() == 4);
  const auto& result = applied->instance.scene;
  CHECK(result.findEntity(added.value));
  CHECK(result.entity({result.id(), added.value})->transform.position.x == 2);
  CHECK(applied->instance.created.empty());
  CHECK(inspectSceneOverrides(result, {result.id(), 2})->total == 0);
  CHECK(result.entity({result.id(), 2})->transform.position.x == 10);
  CHECK(text(f.target) == before);
  CHECK(text(f.source) == original);
  auto persisted = captureSceneSource(applied->source, "assets/panels.dotscene").value();
  auto repeated = prepareSceneInstanceApply(result, persisted, {result.id(), 2}, 6);
  REQUIRE(repeated);
  CHECK_FALSE(repeated->sourceChanged);
  CHECK(text(repeated->instance.scene) == text(result));
}

TEST_CASE("scene_links.apply_instance_merges_independent_source_leaves_and_rejects_conflicts") {
  Fixture f;
  auto local = f.target.entity({f.target.id(), 3})->mesh.value();
  local.tint = {0.2F, 0.3F, 0.4F, 1};
  REQUIRE(f.target.setMeshRenderer({f.target.id(), 3}, local));
  auto incoming = f.source.entity({f.source.id(), 1})->mesh.value();
  incoming.visible = false;
  REQUIRE(f.source.setMeshRenderer({f.source.id(), 1}, incoming));
  REQUIRE(f.source.createEntity("Independent incoming node"));
  auto snapshot = captureSceneSource(f.source, "assets/panels.dotscene").value();
  auto applied = prepareSceneInstanceApply(f.target, snapshot, {f.target.id(), 2}, 5);
  REQUIRE_MESSAGE(applied, (applied ? "" : applied.error().message));
  auto result = applied->source.entity({applied->source.id(), 1})->mesh.value();
  CHECK_FALSE(result.visible);
  CHECK(result.tint.x == doctest::Approx(0.2F));
  CHECK(applied->instance.created == std::vector<std::uint64_t>{5});
  CHECK(applied->instance.scene.entityCount().value() == 5);
  incoming.tint = {0.9F, 0.8F, 0.7F, 1};
  REQUIRE(f.source.setMeshRenderer({f.source.id(), 1}, incoming));
  const auto before = text(f.target);
  snapshot = captureSceneSource(f.source, "assets/panels.dotscene").value();
  CHECK_FALSE(prepareSceneInstanceApply(f.target, snapshot, {f.target.id(), 2}, 5));
  CHECK(text(f.target) == before);
  REQUIRE(f.target.destroyEntity({f.target.id(), 4}));
  REQUIRE(f.source.renameEntity({f.source.id(), 2}, "Changed deleted node"));
  snapshot = captureSceneSource(f.source, "assets/panels.dotscene").value();
  CHECK_FALSE(prepareSceneInstanceApply(f.target, snapshot, {f.target.id(), 2}, 5));
}

TEST_CASE("scene_links.apply_instance_no_overrides_is_not_an_unrelated_refresh") {
  Fixture f;
  REQUIRE(f.source.createEntity("External addition"));
  auto snapshot = captureSceneSource(f.source, "assets/panels.dotscene").value();
  const auto before = text(f.target);
  auto result = prepareSceneInstanceApply(f.target, snapshot, {f.target.id(), 2}, 5);
  REQUIRE(result);
  CHECK_FALSE(result->sourceChanged);
  CHECK(text(result->instance.scene) == before);
  REQUIRE(f.target.renameEntity({f.target.id(), 3}, "Already saved"));
  REQUIRE(f.source.renameEntity({f.source.id(), 1}, "Already saved"));
  snapshot = captureSceneSource(f.source, "assets/panels.dotscene").value();
  auto adopted = prepareSceneInstanceApply(f.target, snapshot, {f.target.id(), 2}, 5);
  REQUIRE(adopted);
  CHECK_FALSE(adopted->sourceChanged);
  CHECK(inspectSceneOverrides(adopted->instance.scene, {adopted->instance.scene.id(), 2})->total
        == 0);
  CHECK(adopted->instance.created == std::vector<std::uint64_t>{5});
}

TEST_CASE("scene_links.apply_instance_remaps_added_native_references_and_checks_boundaries") {
  Fixture f;
  const auto joint = f.target.createEntity("New joint").value();
  REQUIRE(f.target.setParent(joint, EntityId{f.target.id(), 3}));
  AnimationPlayer player;
  player.clips = {"assets/walk.dotanimation"};
  player.targets = {{0, 5}};
  REQUIRE(f.target.setAnimationPlayer({f.target.id(), 3}, player));
  auto encoded = Json::parse(text(f.target));
  auto& node
      = *std::ranges::find_if(encoded["entities"], [](const auto& e) { return e["id"] == "3"; });
  auto animated = decodeScene(encoded.dump());
  REQUIRE(animated);
  auto snapshot = captureSceneSource(f.source, "assets/panels.dotscene").value();
  auto result = prepareSceneInstanceApply(*animated, snapshot, {animated->id(), 2}, 6);
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->source.entity({result->source.id(), 1})->animation->targets[0].entity == 3);
  CHECK(
      result->instance.scene.entity({result->instance.scene.id(), 3})->animation->targets[0].entity
      == 5);
  node["animation"]["targets"][0]["entity"] = "1";
  auto external = decodeScene(encoded.dump());
  REQUIRE(external);
  CHECK_FALSE(prepareSceneInstanceApply(*external, snapshot, {external->id(), 2}, 6));
  REQUIRE(f.target.setParent({f.target.id(), 3}, {}));
  CHECK_FALSE(prepareSceneInstanceApply(f.target, snapshot, {f.target.id(), 2}, 6));
}

TEST_CASE("scene_links.apply_instance_copies_local_nested_links_and_reserved_deleted_values") {
  Fixture f;
  auto extra = Scene::create("Nested kit").value();
  REQUIRE(extra.createEntity("Nested kept"));
  REQUIRE(extra.createEntity("Nested deleted"));
  auto inserted = prepareLinkedScene(f.target, extra, "assets/nested.dotscene", 5, {},
                                     EntityId{f.target.id(), 2});
  REQUIRE(inserted);
  auto target = std::move(inserted->scene);
  REQUIRE(target.destroyEntity({target.id(), 7}));
  auto snapshot = captureSceneSource(f.source, "assets/panels.dotscene").value();
  auto result = prepareSceneInstanceApply(target, snapshot, {target.id(), 2}, 8);
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->source.sceneLinks()->size() == 1);
  CHECK(result->source.sceneLinks()->front().mapping.size() == 2);
  CHECK(result->instance.scene.sceneLinks()->size() == 1);
  CHECK(result->instance.scene.findEntity(5));
  CHECK(result->instance.scene.findEntity(6));
  CHECK_FALSE(result->instance.scene.findEntity(7));
  CHECK(inspectSceneOverrides(result->instance.scene, {result->instance.scene.id(), 2})->total
        == 0);
  CHECK(result->source.nextEntityValue()->value() == 6);
  CHECK(result->source.sceneLinks()->front().mapping[1].target == 5);
}

TEST_CASE("scene_links.apply_instance_respects_latest_source_allocation_floor_and_exhaustion") {
  Fixture f;
  const auto added = f.target.createEntity("Promoted").value();
  REQUIRE(f.target.setParent(added, EntityId{f.target.id(), 2}));
  auto source = captureSceneSource(f.source, "assets/panels.dotscene").value();
  source.namespaces[0].nextEntity = 1000;
  auto result = prepareSceneInstanceApply(f.target, source, {f.target.id(), 2}, 6);
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->source.findEntity(1000));
  CHECK(result->source.nextEntityValue()->value() == 1001);
  CHECK(result->instance.scene.findEntity(added.value));
  source.namespaces[0].nextEntity.reset();
  CHECK_FALSE(prepareSceneInstanceApply(f.target, source, {f.target.id(), 2}, 6));
  CHECK_FALSE(prepareSceneInstanceApply(f.target, source, {f.source.id(), 2}, 6));
  CHECK_FALSE(prepareSceneInstanceApply(f.target, source, {f.target.id(), 2}, 4));
}

TEST_CASE("scene_links.apply_instance_preserves_source_primary_and_rejects_local_primary") {
  Fixture f;
  REQUIRE(f.source.setCamera({f.source.id(), 1}, Camera{.primary = true}));
  f.target = std::move(f.update().scene);
  auto camera = f.target.entity({f.target.id(), 3})->camera.value();
  camera.verticalFovDegrees = 44;
  REQUIRE(f.target.setCamera({f.target.id(), 3}, camera));
  auto source = captureSceneSource(f.source, "assets/panels.dotscene").value();
  auto result = prepareSceneInstanceApply(f.target, source, {f.target.id(), 2}, 5);
  REQUIRE(result);
  CHECK(result->source.entity({result->source.id(), 1})->camera->primary);
  CHECK_FALSE(result->instance.scene.entity({result->instance.scene.id(), 3})->camera->primary);
  camera.primary = true;
  REQUIRE(f.target.setCamera({f.target.id(), 3}, camera));
  CHECK_FALSE(prepareSceneInstanceApply(f.target, source, {f.target.id(), 2}, 5));
}

TEST_CASE("scene_links.apply_instance_edits_nested_inheritance_without_writing_leaf_source") {
  Fixture f;
  auto assembly = Scene::create("Assembly").value();
  assembly = std::move(prepareLinkedScene(assembly, f.source, "assets/leaf.dotscene", 1)->scene);
  std::map<std::string, std::string> saved{{"assets/leaf.dotscene", text(f.source)},
                                           {"assets/assembly.dotscene", text(assembly)}};
  auto source
      = resolveSceneSource("assets/assembly.dotscene", [&](std::string_view asset, std::size_t) {
          return Dot::Core::Result<std::string>{saved.at(std::string(asset))};
        });
  REQUIRE(source);
  auto target = Scene::create("Level").value();
  auto linked = prepareLinkedScene(target, source->source, 1);
  REQUIRE(linked);
  auto& scene = linked->scene;
  const auto mapped = scene.sceneLinks()->front().mapping;
  const auto panel = std::ranges::find_if(mapped, [](const auto& m) {
                       return m.key == SceneSourceKey{1, 1};
                     })->target;
  REQUIRE(scene.renameEntity({scene.id(), panel}, "Nested shared name"));
  auto result = prepareSceneInstanceApply(scene, source->source, linked->root,
                                          scene.nextEntityValue()->value());
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->source.sceneLinks()->size() == 1);
  CHECK(inspectSceneOverrides(result->source, {result->source.id(), 1})->total == 1);
  CHECK(inspectSceneOverrides(result->instance.scene, result->instance.root)->total == 0);
  CHECK(text(f.source) == saved.at("assets/leaf.dotscene"));
}

TEST_CASE("scene_links.field_and_instance_apply_preserve_latest_physical_source_reservations") {
  Fixture f;
  REQUIRE(f.target.renameEntity({f.target.id(), 3}, "Applied name"));
  auto source = captureSceneSource(f.source, "assets/panels.dotscene").value();
  source.namespaces[0].nextEntity = 1000;
  auto field
      = prepareSceneFieldApply(f.target, source, {f.target.id(), 2}, {f.target.id(), 3}, "name", 5);
  auto whole = prepareSceneInstanceApply(f.target, source, {f.target.id(), 2}, 5);
  REQUIRE(field);
  REQUIRE(whole);
  CHECK(field->source.nextEntityValue()->value() == 1000);
  CHECK(whole->source.nextEntityValue()->value() == 1000);
  CHECK(field->instance.scene.sceneLinks()->front().namespaces[0].nextEntity.value() == 1000);
  CHECK(whole->instance.scene.sceneLinks()->front().namespaces[0].nextEntity.value() == 1000);
  source.namespaces[0].nextEntity.reset();
  auto sealed
      = prepareSceneFieldApply(f.target, source, {f.target.id(), 2}, {f.target.id(), 3}, "name", 5);
  REQUIRE(sealed);
  CHECK_FALSE(sealed->source.nextEntityValue()->has_value());
}

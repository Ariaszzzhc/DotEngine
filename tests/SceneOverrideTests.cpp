#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.World.SceneLinks;

namespace {
  using namespace Dot::World;
  using Json = nlohmann::json;

  struct Fixture {
    Scene source = Scene::create("Inherited").value();
    Scene target = Scene::create("Level").value();

    Fixture() {
      const auto parent = source.createEntity("Parent").value();
      const auto child = source.createEntity("Child").value();
      REQUIRE(source.setParent(child, parent));
      REQUIRE(source.setMeshRenderer(parent, MeshRenderer{}));
      REQUIRE(source.setScript(
          parent, ScriptComponent{"actor.ts", "Actor",
                                  R"({"a/b":{"~name":1},"arr":[1,2],"eraseMe":1,"null":null})"}));
      REQUIRE(target.createEntity("Unrelated"));
      auto linked
          = prepareLinkedScene(target, source, "assets/shared.dotscene", 2, {{10, 0, 0}}).value();
      target = std::move(linked.scene);
    }

    auto page(std::optional<std::uint64_t> node = {}, std::size_t offset = 0,
              std::size_t limit = 256) -> SceneOverridePage {
      auto result = inspectSceneOverrides(target, {target.id(), 2}, node, offset, limit);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }

    auto revert(std::span<const SceneOverrideKey> keys) -> SceneLinkUpdate {
      auto result = prepareSceneOverrideRevert(target, {target.id(), 2}, keys);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }
  };

  auto text(const Scene& scene) -> std::string {
    return encodeScene(scene).value();
  }

  auto property(std::string path, std::uint64_t entity = 3) -> SceneOverrideKey {
    return {SceneOverrideKind::Property, entity, std::move(path)};
  }

  auto change(const SceneOverridePage& page, const SceneOverrideKey& key) -> SceneOverride {
    auto found = std::ranges::find(page.changes, key, &SceneOverride::key);
    REQUIRE(found != page.changes.end());
    return *found;
  }
}  // namespace

TEST_CASE("scene_overrides.paged_inspection_is_stable_filtered_owned_and_read_only") {
  Fixture f;
  CHECK(f.page().total == 0);
  REQUIRE(f.target.renameEntity({f.target.id(), 3}, "Local name"));
  REQUIRE(f.target.setTransform({f.target.id(), 3}, {{7, 0, 0}, {}, {2, 3, 4}}));
  const auto before = text(f.target);
  const auto full = f.page();
  REQUIRE(full.changes.size() == 3);
  CHECK(full.changes[0].key == property("/name"));
  CHECK(full.changes[1].key == property("/transform/position"));
  CHECK(full.changes[2].key == property("/transform/scale"));
  CHECK(full.changes[0].source == 1);
  CHECK(full.changes[0].baseline == R"("Parent")");
  CHECK(full.changes[0].local == R"("Local name")");
  CHECK(full.fields == std::map<std::string, std::size_t>{{"name", 1}, {"transform", 2}});

  for (std::size_t offset = 0; offset < full.total; ++offset) {
    const auto page = f.page({}, offset, 1);
    CHECK(page.total == full.total);
    REQUIRE(page.changes.size() == 1);
    CHECK(page.changes[0].key == full.changes[offset].key);
    CHECK(page.fields == full.fields);
  }

  CHECK(f.page({}, full.total, 1).changes.empty());
  CHECK(f.page(3).total == 3);
  CHECK(f.page(2).total == 0);
  CHECK_FALSE(inspectSceneOverrides(f.target, {f.target.id(), 2}, 1));
  CHECK_FALSE(inspectSceneOverrides(f.target, {f.target.id(), 2}, {}, 4, 1));
  CHECK_FALSE(inspectSceneOverrides(f.target, {f.target.id(), 2}, {}, 0, 0));
  CHECK_FALSE(inspectSceneOverrides(f.target, {f.target.id(), 2}, {}, 0, 257));
  CHECK(text(f.target) == before);
}

TEST_CASE("scene_overrides.field_counts_and_restore_cover_changes_beyond_a_page") {
  Fixture f;
  auto script = *f.target.entity({f.target.id(), 3})->script;
  auto properties = Json::parse(script.properties);

  for (int index = 0; index < 300; ++index) {
    properties["property" + std::to_string(index)] = index;
  }

  script.properties = properties.dump();
  REQUIRE(f.target.setScript({f.target.id(), 3}, script));
  REQUIRE(f.target.renameEntity({f.target.id(), 3}, "Local name"));
  const auto before = text(f.target);
  const auto page = f.page(3, 301, 1);
  CHECK(page.total == 301);
  CHECK(page.changes.empty());
  CHECK(page.fields == std::map<std::string, std::size_t>{{"name", 1}, {"script", 300}});
  auto restored
      = prepareSceneFieldRevert(f.target, {f.target.id(), 2}, {f.target.id(), 3}, "script");
  REQUIRE_MESSAGE(restored, (restored ? "" : restored.error().message));
  CHECK(restored->created.empty());
  CHECK(restored->scene.nextEntityValue().value() == f.target.nextEntityValue().value());
  CHECK(restored->scene.entity({restored->scene.id(), 3})->script->properties
        == f.source.entity({f.source.id(), 1})->script->properties);
  CHECK(restored->scene.entity({restored->scene.id(), 3})->name == "Local name");
  CHECK(inspectSceneOverrides(restored->scene, restored->root)->fields
        == std::map<std::string, std::size_t>{{"name", 1}});
  CHECK(text(f.target) == before);
  f.target = std::move(restored->scene);
  CHECK(page.fields.at("script") == 300);
}

TEST_CASE("scene_overrides.field_restore_preserves_siblings_and_handles_whole_components") {
  Fixture f;
  REQUIRE(f.target.setTransform({f.target.id(), 3}, {{7, 8, 9}, {}, {2, 3, 4}}));
  REQUIRE(f.target.setMeshRenderer({f.target.id(), 3}, {}));
  REQUIRE(f.target.setMaterial({f.target.id(), 3}, MaterialBinding{"assets/local.dotmaterial"}));
  REQUIRE(f.target.renameEntity({f.target.id(), 3}, "Local"));
  CHECK(f.page(3).fields
        == std::map<std::string, std::size_t>{
            {"material", 1}, {"mesh", 1}, {"name", 1}, {"transform", 2}});

  for (const auto* field : {"mesh", "material", "transform"}) {
    auto result = prepareSceneFieldRevert(f.target, {f.target.id(), 2}, {f.target.id(), 3}, field);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    f.target = std::move(result->scene);
  }

  REQUIRE(f.target.entity({f.target.id(), 3})->mesh);
  CHECK(f.target.entity({f.target.id(), 3})->mesh->kind == MeshKind::Cube);
  CHECK_FALSE(f.target.entity({f.target.id(), 3})->material);
  CHECK(Json::parse(text(f.target))["entities"][2]["transform"]
        == Json::parse(text(f.source))["entities"][0]["transform"]);
  CHECK(f.page().fields == std::map<std::string, std::size_t>{{"name", 1}});
  const auto before = text(f.target);
  auto unchanged
      = prepareSceneFieldRevert(f.target, {f.target.id(), 2}, {f.target.id(), 3}, "transform");
  REQUIRE(unchanged);
  CHECK(text(unchanged->scene) == before);
}

TEST_CASE("scene_overrides.field_restore_rejects_invalid_ownership_fields_and_dependencies") {
  Fixture f;
  const auto added = f.target.createEntity("Local").value();
  REQUIRE(f.target.setParent(added, EntityId{f.target.id(), 2}));
  const auto before = text(f.target);

  for (const auto* field : {"", "id", "unknown", "/name", "script/properties"}) {
    CHECK_FALSE(prepareSceneFieldRevert(f.target, {f.target.id(), 2}, {f.target.id(), 3}, field));
    CHECK(text(f.target) == before);
  }

  for (const auto entity : {EntityId{f.target.id(), 1}, EntityId{f.target.id(), 2}, added,
                            EntityId{f.target.id(), 999}, EntityId{f.source.id(), 3}}) {
    CHECK_FALSE(prepareSceneFieldRevert(f.target, {f.target.id(), 2}, entity, "name"));
    CHECK(text(f.target) == before);
  }

  CHECK_FALSE(prepareSceneFieldRevert(f.target, {f.source.id(), 2}, {f.target.id(), 3}, "name"));
  CHECK_FALSE(prepareSceneFieldRevert(f.target, {f.target.id(), 1}, {f.target.id(), 3}, "name"));
  std::thread foreign([&] {
    CHECK_FALSE(prepareSceneFieldRevert(f.target, {f.target.id(), 2}, {f.target.id(), 3}, "name"));
  });
  foreign.join();
  REQUIRE(f.target.setParent({f.target.id(), 4}, EntityId{f.target.id(), 2}));
  REQUIRE(f.target.destroyEntity({f.target.id(), 3}));
  const auto broken = text(f.target);
  CHECK_FALSE(prepareSceneFieldRevert(f.target, {f.target.id(), 2}, {f.target.id(), 4}, "parent"));
  CHECK_FALSE(prepareSceneFieldRevert(f.target, {f.target.id(), 2}, {f.target.id(), 3}, "name"));
  CHECK(text(f.target) == broken);
  CHECK(f.page(added.value).fields.empty());
  CHECK(f.page(3).fields.empty());
}

TEST_CASE("scene_overrides.field_restore_remaps_native_bindings_and_demotes_primary_flags") {
  Fixture f;
  REQUIRE(f.source.setSkin({f.source.id(), 1}, SkinBinding{"assets/body.dotskin", {2}}));
  AnimationPlayer animation;
  animation.clips = {"assets/idle.dotanimation"};
  animation.targets = {{0, 2}};
  REQUIRE(f.source.setAnimationPlayer({f.source.id(), 1}, animation));
  REQUIRE(f.source.setCamera({f.source.id(), 2}, Camera{.primary = true}));
  REQUIRE(f.source.setAudioListener({f.source.id(), 2}, AudioListener{true}));
  auto linked = prepareSceneLinkUpdate(f.target, f.source, {f.target.id(), 2}, 5).value();
  f.target = std::move(linked.scene);
  REQUIRE(f.target.setSkin({f.target.id(), 3}, {}));
  REQUIRE(f.target.setAnimationPlayer({f.target.id(), 3}, {}));
  REQUIRE(f.target.setCamera({f.target.id(), 4}, {}));
  REQUIRE(f.target.setAudioListener({f.target.id(), 4}, {}));

  for (const auto* field : {"skin", "animation", "camera", "audioListener"}) {
    const auto id
        = std::string_view(field) == "skin" || std::string_view(field) == "animation" ? 3U : 4U;
    auto restored
        = prepareSceneFieldRevert(f.target, {f.target.id(), 2}, {f.target.id(), id}, field);
    REQUIRE_MESSAGE(restored, (restored ? "" : restored.error().message));
    f.target = std::move(restored->scene);
  }

  CHECK(f.target.entity({f.target.id(), 3})->skin->joints == std::vector<std::uint64_t>{4});
  CHECK(f.target.entity({f.target.id(), 3})->animation->targets[0].entity == 4);
  CHECK_FALSE(f.target.entity({f.target.id(), 4})->camera->primary);
  CHECK_FALSE(f.target.entity({f.target.id(), 4})->audioListener->primary);
  CHECK(f.page().total == 0);
}

TEST_CASE("scene_overrides.json_pointer_escaping_missing_null_and_atomic_arrays_revert_exactly") {
  Fixture f;
  auto script = f.target.entity({f.target.id(), 3})->script.value();
  auto data = Json::parse(script.properties);
  data["a/b"]["~name"] = 2;
  data["arr"][0] = 9;
  data.erase("eraseMe");
  data[""] = nullptr;
  data.erase("null");
  script.properties = data.dump();
  REQUIRE(f.target.setScript({f.target.id(), 3}, script));
  const auto page = f.page();
  CHECK(page.total == 5);
  CHECK_FALSE(change(page, property("/script/properties/")).baseline);
  CHECK(change(page, property("/script/properties/")).local == "null");
  CHECK(change(page, property("/script/properties/null")).baseline == "null");
  CHECK_FALSE(change(page, property("/script/properties/null")).local);
  CHECK(change(page, property("/script/properties/arr")).baseline == "[1,2]");
  auto one = f.revert(std::array{property("/script/properties/a~1b/~0name")});
  auto reverted = Json::parse(one.scene.entity({one.scene.id(), 3})->script->properties);
  CHECK(reverted["a/b"]["~name"] == 1);
  CHECK(reverted["arr"][0] == 9);
  f.target = std::move(one.scene);
  std::vector<SceneOverrideKey> remaining;

  for (const auto& entry : f.page().changes) {
    remaining.push_back(entry.key);
  }

  auto all = f.revert(remaining);
  CHECK(inspectSceneOverrides(all.scene, all.root)->total == 0);
  CHECK(all.scene.entity({all.scene.id(), 3})->script->properties
        == f.source.entity({f.source.id(), 1})->script->properties);
}

TEST_CASE("scene_overrides.deleted_dependencies_restore_together_without_allocating_new_values") {
  Fixture f;
  REQUIRE(f.target.destroyEntity({f.target.id(), 3}));
  const auto before = text(f.target);
  CHECK(f.page().total == 2);
  CHECK(f.page(4).changes[0].key.kind == SceneOverrideKind::DeletedNode);
  const auto child = SceneOverrideKey{SceneOverrideKind::DeletedNode, 4, {}};
  CHECK_FALSE(prepareSceneOverrideRevert(f.target, {f.target.id(), 2}, std::array{child}));
  CHECK(text(f.target) == before);
  auto restored
      = f.revert(std::array{child, SceneOverrideKey{SceneOverrideKind::DeletedNode, 3, {}}});
  CHECK(restored.root.scene == restored.scene.id());
  CHECK(restored.scene.entity({restored.scene.id(), 4})->parent->value == 3);
  CHECK(restored.scene.nextEntityValue().value() == 5);
  CHECK(restored.created == std::vector<std::uint64_t>{3, 4});
  CHECK(inspectSceneOverrides(restored.scene, restored.root)->total == 0);
}

TEST_CASE("scene_overrides.added_subtrees_remove_nested_links_and_direct_ownership_wins") {
  Fixture f;
  const auto local = f.target.createEntity("Local group").value();
  const auto child = f.target.createEntity("Local child").value();
  REQUIRE(f.target.setParent(local, EntityId{f.target.id(), 2}));
  REQUIRE(f.target.setParent(child, local));
  auto nested
      = prepareLinkedScene(f.target, f.source, "assets/nested.dotscene", 7, {}, local).value();
  f.target = std::move(nested.scene);
  CHECK(findSceneLinkRoot(f.target, {f.target.id(), 6}).value()->value == 2);
  CHECK(findSceneLinkRoot(f.target, {f.target.id(), 8}).value()->value == 7);
  CHECK(f.page().total == 5);
  auto reverted = f.revert(std::array{SceneOverrideKey{SceneOverrideKind::AddedNode, 5, {}}});
  CHECK(reverted.scene.sceneLinks()->size() == 1);
  CHECK(reverted.scene.entityCount().value() == 4);
  CHECK(reverted.scene.nextEntityValue().value() == 10);
  CHECK(inspectSceneOverrides(reverted.scene, reverted.root)->total == 0);
}

TEST_CASE("scene_overrides.parent_revert_and_local_group_removal_prepare_one_valid_transaction") {
  Fixture f;
  const auto local = f.target.createEntity("Local group").value();
  REQUIRE(f.target.setParent(local, EntityId{f.target.id(), 2}));
  REQUIRE(f.target.setParent({f.target.id(), 3}, local));
  const auto key = SceneOverrideKey{SceneOverrideKind::AddedNode, local.value, {}};
  const auto before = text(f.target);
  CHECK_FALSE(prepareSceneOverrideRevert(f.target, {f.target.id(), 2}, std::array{key}));
  CHECK(text(f.target) == before);
  auto restored = f.revert(std::array{key, property("/parent")});
  CHECK_FALSE(restored.scene.findEntity(local.value));
  CHECK(restored.scene.entity({restored.scene.id(), 3})->parent == restored.root);
  CHECK(restored.scene.findEntity(4));
  CHECK(inspectSceneOverrides(restored.scene, restored.root)->total == 0);
}

TEST_CASE(
    "scene_overrides.removing_referenced_additions_fails_until_reference_override_is_reverted") {
  Fixture f;
  const auto joint = f.target.createEntity("Added joint").value();
  REQUIRE(f.target.setParent(joint, EntityId{f.target.id(), 2}));
  REQUIRE(f.target.setSkin({f.target.id(), 3}, SkinBinding{"assets/rig.dotskin", {joint.value}}));
  const auto key = SceneOverrideKey{SceneOverrideKind::AddedNode, joint.value, {}};
  const auto before = text(f.target);
  CHECK_FALSE(prepareSceneOverrideRevert(f.target, {f.target.id(), 2}, std::array{key}));
  CHECK(text(f.target) == before);
  auto reverted = f.revert(std::array{key, property("/skin")});
  CHECK_FALSE(reverted.scene.entity({reverted.scene.id(), 3})->skin);
  CHECK_FALSE(reverted.scene.findEntity(joint.value));
}

TEST_CASE(
    "scene_overrides.baseline_revert_restores_inherited_nodes_removes_additions_and_keeps_"
    "placement") {
  Fixture f;
  const auto local = f.target.createEntity("Local group").value();
  REQUIRE(f.target.setParent(local, EntityId{f.target.id(), 2}));
  REQUIRE(f.target.setParent({f.target.id(), 3}, local));
  REQUIRE(f.target.destroyEntity({f.target.id(), 4}));
  REQUIRE(f.target.renameEntity({f.target.id(), 3}, "Overridden"));
  REQUIRE(f.target.setTransform({f.target.id(), 2}, {{20, 0, 0}}));
  auto restored = prepareSceneBaselineRevert(f.target, {f.target.id(), 2}).value();
  CHECK(restored.root.scene == restored.scene.id());
  CHECK(restored.scene.entity({restored.scene.id(), 3})->name == "Parent");
  CHECK(restored.scene.findEntity(4));
  CHECK_FALSE(restored.scene.findEntity(local.value));
  CHECK(restored.scene.transform(restored.root)->position.x == 20);
  CHECK(restored.scene.findEntity(1));
  CHECK(restored.scene.nextEntityValue().value() == 6);
  CHECK(restored.created == std::vector<std::uint64_t>{4});
  CHECK(inspectSceneOverrides(restored.scene, restored.root)->total == 0);
  auto again = prepareSceneBaselineRevert(restored.scene, restored.root).value();
  CHECK(text(again.scene) == text(restored.scene));
  CHECK(again.created.empty());
}

TEST_CASE("scene_overrides.invalid_duplicate_stale_or_nonatomic_array_keys_preserve_all_state") {
  Fixture f;
  REQUIRE(f.target.renameEntity({f.target.id(), 3}, "Overridden"));
  const auto before = text(f.target);
  CHECK_FALSE(prepareSceneOverrideRevert(f.target, {f.target.id(), 2}, {}));
  CHECK_FALSE(prepareSceneOverrideRevert(f.target, {f.target.id(), 2},
                                         std::array{property("/name"), property("/name")}));
  CHECK_FALSE(prepareSceneOverrideRevert(f.target, {f.target.id(), 2},
                                         std::vector<SceneOverrideKey>(257, property("/name"))));

  for (const auto& invalid : std::array{property("/id"), property("/transform/position/0"),
                                        SceneOverrideKey{static_cast<SceneOverrideKind>(99), 3, {}},
                                        SceneOverrideKey{SceneOverrideKind::DeletedNode, 3, {}}}) {
    CHECK_FALSE(prepareSceneOverrideRevert(f.target, {f.target.id(), 2},
                                           std::array{property("/name"), invalid}));
    CHECK(text(f.target) == before);
  }

  auto reverted = f.revert(std::array{property("/name")});
  CHECK_FALSE(
      prepareSceneOverrideRevert(reverted.scene, reverted.root, std::array{property("/name")}));
}

TEST_CASE(
    "scene_overrides.owner_resolution_survives_external_parenting_and_snapshots_outlive_owner") {
  Fixture f;
  REQUIRE(f.target.setParent({f.target.id(), 3}, EntityId{f.target.id(), 1}));
  const auto child = f.target.createEntity("Child of moved inherited node").value();
  REQUIRE(f.target.setParent(child, EntityId{f.target.id(), 3}));
  CHECK(findSceneLinkRoot(f.target, child).value()->value == 2);
  CHECK_FALSE(findSceneLinkRoot(f.target, {f.target.id(), 1}).value());
  const auto snapshot = f.page();
  const auto keys = std::array{property("/parent")};
  bool rejected{};
  std::thread worker([&] {
    rejected = !findSceneLinkRoot(f.target, child)
               && !inspectSceneOverrides(f.target, {f.target.id(), 2})
               && !prepareSceneOverrideRevert(f.target, {f.target.id(), 2}, keys)
               && !prepareSceneBaselineRevert(f.target, {f.target.id(), 2});
  });
  worker.join();
  CHECK(rejected);
  CHECK_FALSE(inspectSceneOverrides(f.target, {f.source.id(), 2}));
  auto moved = std::move(f.target);
  CHECK_FALSE(findSceneLinkRoot(f.target, child));
  CHECK_FALSE(prepareSceneBaselineRevert(f.target, {moved.id(), 2}));
  moved = Scene::create().value();
  CHECK(snapshot.total == 2);
  CHECK(change(snapshot, property("/parent")).baseline == R"("2")");
}

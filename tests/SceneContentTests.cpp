#include <doctest/doctest.h>

#include <array>
#include <future>
#include <limits>
#include <string>
#include <utility>
#include <vector>

import Dot.Core.Result;
import Dot.Math;
import Dot.World.Scene;
import Dot.World.SceneContent;
import Dot.World.SceneFiles;

namespace {
  using namespace Dot;
  using namespace Dot::World;

  auto snapshot(const Scene& scene) -> std::string {
    return encodeScene(scene).value();
  }

  auto near(const Math::Mat4& actual, const Math::Mat4& expected) -> void {
    for (std::size_t i = 0; i < actual.data.size(); ++i) {
      CHECK(actual.data[i] == doctest::Approx(expected.data[i]).epsilon(.00002).scale(1));
    }
  }
}  // namespace

TEST_CASE("scene_content.extraction_preserves_subtrees_world_layout_native_refs_and_opaque_data") {
  auto source = Scene::create("Source").value();
  auto parent = source.createEntity("Outside parent").value();
  auto mesh = source.createEntity("Mesh").value();
  auto joint = source.createEntity("Joint camera").value();
  auto other = source.createEntity("Other").value();
  REQUIRE(source.setTransform(
      parent, {{10, 2, 1}, Math::rotationEulerDegrees({0, 30, 0}).value(), {-2, 2, 2}}));
  REQUIRE(source.setTransform(mesh, {{1, 2, 0}}));
  REQUIRE(source.setTransform(joint, {{0, 1, 0}}));
  REQUIRE(source.setTransform(other, {{-3, 1, 2}}));
  REQUIRE(source.setParents(std::array{ParentUpdate{mesh, parent}, ParentUpdate{joint, mesh}}));
  REQUIRE(source.setMeshRenderer(mesh, MeshRenderer{}));
  REQUIRE(source.setSkin(mesh, SkinBinding{"assets/rig.dotskin", {joint.value}}));
  AnimationPlayer animation;
  animation.clips = {"assets/idle.dotanimation"};
  animation.targets = {{0, joint.value}};
  REQUIRE(source.setAnimationPlayer(mesh, animation));
  REQUIRE(source.setCamera(joint, Camera{.primary = true}));
  REQUIRE(source.setAudioListener(joint, AudioListener{true}));
  REQUIRE(source.setScript(mesh, ScriptComponent{"actor.ts", "Actor", R"({"literal":"3"})"}));
  REQUIRE(source.setEnvironment(Environment{"assets/studio.hdr"}));
  const auto before = snapshot(source);
  const auto selected = std::array{mesh, joint, other};
  auto prepared = extractSceneContent(source, selected, "Reusable 组合");
  REQUIRE_MESSAGE(prepared, (prepared ? "" : prepared.error().message));
  const auto& result = *prepared;
  REQUIRE(result.entities.size() == 3);
  CHECK(result.scene.entityCount().value() == 4);
  CHECK(result.root.value == 1);
  CHECK(result.scene.name().value() == "Reusable 组合");
  CHECK_FALSE(result.scene.environment()->has_value());
  CHECK_FALSE(result.scene.occlusion()->has_value());
  const auto pivot = source.worldTransform(other).value();

  for (const auto& mapping : result.entities) {
    CHECK(mapping.source.scene == source.id());
    CHECK(mapping.target.scene == result.scene.id());
    auto expected = source.worldTransform(mapping.source).value();
    expected.data[12] -= pivot.data[12];
    expected.data[13] -= pivot.data[13];
    expected.data[14] -= pivot.data[14];
    near(result.scene.worldTransform(mapping.target).value(), expected);
  }

  const auto copiedMesh = result.scene.entity(result.entities[0].target).value();
  CHECK(copiedMesh.parent == result.root);
  CHECK(copiedMesh.skin->joints == std::vector<std::uint64_t>{result.entities[1].target.value});
  CHECK(copiedMesh.animation->targets[0].entity == result.entities[1].target.value);
  CHECK(copiedMesh.script->properties == R"({"literal":"3"})");
  CHECK(result.scene.primaryCamera().value() == result.entities[1].target);
  CHECK(result.scene.primaryAudioListener().value() == result.entities[1].target);
  CHECK(snapshot(source) == before);
  auto restored = decodeScene(snapshot(result.scene)).value();
  CHECK(restored.entityCount().value() == 4);
}

TEST_CASE("scene_content.instance_candidate_preserves_target_and_remaps_native_owned_content") {
  auto target = Scene::create("Target").value();
  auto camera = target.createEntity("Existing camera").value();
  auto parent = target.createEntity("Placement parent").value();
  REQUIRE(target.setCamera(camera, Camera{.primary = true}));
  REQUIRE(target.setAudioListener(camera, AudioListener{true}));
  REQUIRE(target.setTransform(parent, {{10, 0, 0}, {}, {2, 3, 4}}));
  REQUIRE(target.setEnvironment(Environment{"assets/target.hdr"}));
  auto content = Scene::create("Content").value();
  const auto actor = content.createEntityWithId("Actor", 9007199254740994ULL).value();
  const auto child = content.createEntity("Child").value();
  REQUIRE(content.setParent(child, actor));
  REQUIRE(content.setTransform(actor, {{1, 2, 0}}));
  REQUIRE(content.setTransform(child, {{0, 1, 0}}));
  REQUIRE(content.setCamera(child, Camera{.primary = true}));
  REQUIRE(content.setAudioListener(child, AudioListener{true}));
  REQUIRE(content.setEnvironment(Environment{"assets/source.hdr"}));
  const auto oldTarget = snapshot(target), oldContent = snapshot(content);
  const Transform placement{{2, 0, 1}, Math::rotationEulerDegrees({0, 30, 0}).value(), {1, 1, 1}};
  auto result = prepareSceneInstance(target, content, 100, placement, parent);
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->root.value == 100);
  CHECK(result->scene.id() != target.id());
  CHECK(result->scene.entityCount().value() == 5);
  CHECK(result->scene.entity(result->root)->parent->value == parent.value);
  REQUIRE(result->entities.size() == 2);
  const auto basis
      = Math::multiply(
            target.worldTransform(parent).value(),
            Math::compose(placement.position, placement.rotation, placement.scale).value())
            .value();

  for (const auto& mapping : result->entities) {
    const auto expected
        = Math::multiply(basis, content.worldTransform(mapping.source).value()).value();
    near(result->scene.worldTransform(mapping.target).value(), expected);
  }

  CHECK(result->scene.primaryCamera()->value == camera.value);
  CHECK(result->scene.primaryAudioListener()->value == camera.value);
  CHECK_FALSE(result->scene.entity(result->entities[1].target)->camera->primary);
  CHECK_FALSE(result->scene.entity(result->entities[1].target)->audioListener->primary);
  CHECK(result->scene.environment()->value().asset == "assets/target.hdr");
  CHECK(snapshot(target) == oldTarget);
  CHECK(snapshot(content) == oldContent);
  CHECK(target.createEntity()->value == 3);
  CHECK_FALSE(result->scene.entity(camera));
}

TEST_CASE("scene_content.external_native_references_duplicate_and_foreign_selection_fail_cleanly") {
  auto source = Scene::create().value();
  auto mesh = source.createEntity("Mesh").value();
  auto joint = source.createEntity("External joint").value();
  REQUIRE(source.setMeshRenderer(mesh, MeshRenderer{}));
  REQUIRE(source.setSkin(mesh, SkinBinding{"assets/rig.dotskin", {joint.value}}));
  const auto before = snapshot(source);
  CHECK_FALSE(extractSceneContent(source, {}, "Empty"));
  CHECK_FALSE(extractSceneContent(source, std::array{mesh, mesh}, "Duplicate"));
  CHECK_FALSE(
      extractSceneContent(source, std::array{EntityId{source.id() + 1, mesh.value}}, "Foreign"));
  CHECK_FALSE(extractSceneContent(source, std::array{mesh}, "Partial skin"));
  CHECK(snapshot(source) == before);
  REQUIRE(extractSceneContent(source, std::array{mesh, joint}, "Whole rig"));
  REQUIRE(source.setSkin(mesh, std::nullopt));
  AnimationPlayer player;
  player.clips = {"assets/idle.dotanimation"};
  player.targets = {{0, joint.value}};
  REQUIRE(source.setAnimationPlayer(mesh, player));
  auto result = extractSceneContent(source, std::array{mesh}, "Partial animation");
  REQUIRE_FALSE(result);
  CHECK(result.error().code == Core::ErrorCode::InvalidArgument);
  CHECK_FALSE(extractSceneContent(source, std::array{mesh, joint}, ""));
}

TEST_CASE("scene_content.shear_placement_and_final_depth_rejections_leave_both_sources_unchanged") {
  auto source = Scene::create().value();
  auto parent = source.createEntity("Parent").value();
  auto child = source.createEntity("Child").value();
  REQUIRE(source.setTransform(parent, {{}, {}, {2, 1, 1}}));
  REQUIRE(source.setTransform(child, {{}, Math::rotationEulerDegrees({0, 0, 45}).value()}));
  REQUIRE(source.setParent(child, parent));
  CHECK_FALSE(extractSceneContent(source, std::array{child}, "Sheared"));
  REQUIRE(extractSceneContent(source, std::array{parent}, "Representable hierarchy"));
  auto target = Scene::create().value();
  std::vector<EntityId> nodes;
  std::vector<ParentUpdate> parents;

  for (std::size_t i = 0; i < 256; ++i) {
    nodes.push_back(target.createEntity().value());

    if (i > 0) {
      parents.push_back({nodes[i], nodes[i - 1]});
    }
  }

  REQUIRE(target.setParents(parents));
  const auto before = snapshot(target);
  CHECK_FALSE(prepareSceneInstance(target, source, 300, {}, nodes.back()));
  CHECK_FALSE(prepareSceneInstance(target, source, 300, {{}, {}, {0, 1, 1}}));
  CHECK_FALSE(prepareSceneInstance(target, source, 300, {}, parent));
  CHECK(snapshot(target) == before);
}

TEST_CASE("scene_content.identity_capacity_and_world_owner_boundaries_are_explicit") {
  auto target = Scene::create().value();
  const auto existing = target.createEntity().value();
  auto content = Scene::create().value();
  const auto node = content.createEntity().value();
  CHECK_FALSE(prepareSceneInstance(target, content, existing.value));
  CHECK_FALSE(prepareSceneInstance(target, content, 0));
  const auto maximum = std::numeric_limits<std::uint64_t>::max();
  CHECK_FALSE(prepareSceneInstance(target, content, maximum - 1));
  REQUIRE(prepareSceneInstance(target, content, maximum - 2));
  auto worker = std::async(std::launch::async, [&] {
    return !extractSceneContent(content, std::array{node}, "Wrong thread")
           && !prepareSceneInstance(target, content, 2);
  });
  CHECK(worker.get());
  auto moved = std::move(content);
  CHECK_FALSE(extractSceneContent(content, std::array{node}, "Moved"));
  CHECK_FALSE(prepareSceneInstance(target, content, 2));
  REQUIRE(extractSceneContent(moved, std::array{node}, "Owned"));
  auto full = Scene::create().value();

  for (std::size_t i = 0; i < 10000; ++i) {
    REQUIRE(full.createEntity());
  }

  const auto rejected = prepareSceneInstance(full, moved, 10001);
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().code == Core::ErrorCode::Unavailable);
  CHECK(full.entityCount().value() == 10000);
}

TEST_CASE("scene_content.instance_preparation_respects_reserved_and_exhausted_target_identity") {
  auto target = Scene::create().value();
  REQUIRE(target.createEntity());
  REQUIRE(target.reserveEntityValuesBefore(20));
  auto source = Scene::create().value();
  const auto node = source.createEntity().value();
  REQUIRE(source.reserveEntityValuesBefore(1000));
  CHECK_FALSE(prepareSceneInstance(target, source, 2));
  auto prepared = prepareSceneInstance(target, source, 20);
  REQUIRE(prepared);
  CHECK(prepared->root.value == 20);
  CHECK(prepared->scene.nextEntityValue().value() == 22);
  CHECK(target.nextEntityValue().value() == 20);
  auto extracted = extractSceneContent(source, std::array{node}, "Independent content");
  REQUIRE(extracted);
  CHECK(extracted->scene.nextEntityValue().value() == 3);
  REQUIRE(target.reserveEntityValuesBefore(std::nullopt));
  CHECK_FALSE(prepareSceneInstance(target, source, 1001));
  CHECK_FALSE(target.nextEntityValue().value());
}

TEST_CASE("scene_content.runtime_append_preserves_scope_handles_globals_and_native_references") {
  auto target = Scene::create("Running").value();
  auto player = target.createEntity("Player").value();
  REQUIRE(target.setTransform(player, {{3, 2, 1}}));
  REQUIRE(target.setCamera(player, Camera{.primary = true}));
  REQUIRE(target.setUi("assets/active.dotui"));
  auto source = Scene::create("Room").value();
  auto mesh = source.createEntity("Rig").value();
  auto joint = source.createEntity("Joint").value();
  REQUIRE(source.setMeshRenderer(mesh, MeshRenderer{}));
  REQUIRE(source.setSkin(mesh, SkinBinding{"assets/rig.dotskin", {joint.value}}));
  AnimationPlayer animation;
  animation.clips = {"assets/idle.dotanimation"};
  animation.targets = {{0, joint.value}};
  REQUIRE(source.setAnimationPlayer(mesh, animation));
  REQUIRE(source.setCamera(joint, Camera{.primary = true}));
  REQUIRE(source.setAudioListener(joint, AudioListener{true}));
  REQUIRE(source.setUi("assets/ignored.dotui"));
  const auto before = snapshot(source);
  const auto scope = target.id();
  auto added = target.appendContent(source, {{10, 0, 0}}, player);
  REQUIRE_MESSAGE(added, (added ? "" : added.error().message));
  REQUIRE(added->entities.size() == 3);
  CHECK(target.id() == scope);
  CHECK(target.entity(player)->name == "Player");
  CHECK(target.entity(player)->transform.position.x == 3);
  CHECK(target.entity(added->root)->parent == player);
  CHECK(target.ui().value() == "assets/active.dotui");
  CHECK(target.entity(added->entities[1])->skin->joints[0] == added->entities[2].value);
  CHECK(target.entity(added->entities[1])->animation->targets[0].entity
        == added->entities[2].value);
  CHECK_FALSE(target.entity(added->entities[2])->camera->primary);
  CHECK_FALSE(target.entity(added->entities[2])->audioListener->primary);
  CHECK(snapshot(source) == before);
  const auto floor = target.nextEntityValue().value();
  REQUIRE(target.removeContent(added->entities));
  CHECK(target.id() == scope);
  CHECK(target.entityCount().value() == 1);
  CHECK(target.entity(player));
  CHECK(target.nextEntityValue().value() == floor);
  REQUIRE(target.removeContent(added->entities));
  CHECK(target.createEntity()->value >= *floor);
}

TEST_CASE(
    "scene_content.runtime_removal_rejects_external_hierarchy_skin_and_animation_atomically") {
  auto target = Scene::create("Running").value();
  auto outside = target.createEntity("Outside").value();
  auto source = Scene::create("Content").value();
  REQUIRE(source.createEntity("Member"));
  auto added = target.appendContent(source).value();
  auto member = added.entities[1];
  REQUIRE(target.setParent(outside, member));
  auto before = snapshot(target);
  CHECK_FALSE(target.removeContent(added.entities));
  CHECK(snapshot(target) == before);
  REQUIRE(target.setParent(outside, std::nullopt));
  REQUIRE(target.setMeshRenderer(outside, MeshRenderer{}));
  REQUIRE(target.setSkin(outside, SkinBinding{"assets/rig.dotskin", {member.value}}));
  before = snapshot(target);
  CHECK_FALSE(target.removeContent(added.entities));
  CHECK(snapshot(target) == before);
  REQUIRE(target.setSkin(outside, std::nullopt));
  AnimationPlayer animation;
  animation.clips = {"assets/idle.dotanimation"};
  animation.targets = {{0, member.value}};
  REQUIRE(target.setAnimationPlayer(outside, animation));
  before = snapshot(target);
  CHECK_FALSE(target.removeContent(added.entities));
  CHECK(snapshot(target) == before);
  REQUIRE(target.setAnimationPlayer(outside, std::nullopt));
  REQUIRE(target.setParent(member, outside));
  REQUIRE(target.destroyEntity(added.root));
  REQUIRE(target.removeContent(added.entities));
  CHECK(target.entity(outside));
  CHECK_FALSE(target.entity(member));
}

TEST_CASE("scene_content.runtime_failures_preserve_world_and_allocation") {
  auto target = Scene::create("Running").value();
  auto outside = target.createEntity("Outside").value();
  auto empty = Scene::create("Empty").value();
  auto source = Scene::create("Content").value();
  REQUIRE(source.createEntity());
  const auto before = snapshot(target);
  CHECK_FALSE(target.appendContent(empty));
  CHECK_FALSE(target.appendContent(source, {}, EntityId{source.id(), 1}));
  CHECK_FALSE(target.removeContent({}));
  CHECK_FALSE(target.removeContent(std::array{outside, outside}));
  CHECK_FALSE(target.removeContent(std::array{EntityId{source.id(), 1}}));
  CHECK(snapshot(target) == before);
  CHECK(std::async(std::launch::async, [&] {
          return !target.appendContent(source) && !target.removeContent(std::array{outside});
        }).get());
  REQUIRE(target.reserveEntityValuesBefore(std::nullopt));
  CHECK_FALSE(target.appendContent(source));
  CHECK(target.entity(outside));
}

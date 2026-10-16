#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <numeric>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.World.SceneIdentity;
import Dot.World.SceneSources;
import Dot.World.SceneLinks;
import Dot.World.SceneContent;

namespace {
  using namespace Dot::World;
  using Json = nlohmann::json;
  constexpr auto assetA = "assets/assembly.dotscene";
  constexpr auto assetB = "assets/part.dotscene";

  auto spaces() -> std::vector<SceneSourceNamespace> {
    return {{{}, assetA, 11, {1, 10}},
            {{10}, assetB, 8, {1, 7}},
            {{10, 7}, "assets/leaf.dotscene", 5, {4}}};
  }

  auto origins() -> std::vector<SceneSourceOrigin> {
    return {{1, {1}}, {2, {10}}, {3, {10, 1}}, {4, {10, 7}}, {5, {10, 7, 4}}};
  }

  enum class Working { Initial, LocalCollision, ReplacedLeaf, HiddenLeaf, Reindexed };

  auto working(Working variant) -> SceneSourceSnapshot {
    const auto leaf = variant == Working::ReplacedLeaf ? 9ULL : 8ULL;
    auto part = Scene::create("Part").value();
    const auto mesh = part.createEntityWithId("Mesh", 7).value();
    REQUIRE(part.createEntityWithId("Leaf", leaf));
    REQUIRE(part.setMeshRenderer(mesh, MeshRenderer{}));
    REQUIRE(part.setSkin(mesh, SkinBinding{"assets/rig.dotskin", {leaf}}));
    REQUIRE(part.setScript(mesh, ScriptComponent{"actor.ts", "Actor", R"({"literal":"8"})"}));
    auto assembly = Scene::create("Assembly").value();
    REQUIRE(assembly.createEntity("Local"));
    const auto root = assembly.createEntity("Nested part").value();
    const auto copiedMesh = assembly.createEntity("Mesh").value();
    const bool collision = variant == Working::LocalCollision;
    REQUIRE(assembly.createEntity(collision ? "New local entity" : "Leaf"));

    const bool reindexed = variant == Working::Reindexed;

    if (collision || reindexed) {
      REQUIRE(assembly.createEntity("Leaf"));
    }

    if (reindexed) {
      REQUIRE(assembly.destroyEntity({assembly.id(), 4}));
    }

    const auto copiedLeaf = collision || reindexed ? 5ULL : 4ULL;
    REQUIRE(assembly.setParent(copiedMesh, root));
    REQUIRE(assembly.setParent({assembly.id(), copiedLeaf}, root));
    REQUIRE(assembly.setMeshRenderer(copiedMesh, MeshRenderer{}));
    REQUIRE(
        assembly.setScript(copiedMesh, ScriptComponent{"actor.ts", "Actor", R"({"literal":"8"})"}));

    if (variant == Working::HiddenLeaf) {
      REQUIRE(assembly.destroyEntity({assembly.id(), copiedLeaf}));
    } else {
      REQUIRE(assembly.setSkin(copiedMesh, SkinBinding{"assets/rig.dotskin", {copiedLeaf}}));
    }

    SceneLink link{2,
                   assetB,
                   encodeScene(part).value(),
                   {{7, 3, {7}}, {leaf, copiedLeaf, {leaf}}},
                   {{{}, assetB, part.nextEntityValue().value(), {7, leaf}}}};
    REQUIRE(assembly.setSceneLinks(std::array{link}));
    std::vector<SceneSourceOrigin> local{{1, {1}}, {2, {2}}};

    if (collision) {
      local.push_back({4, {4}});
    }

    SceneSourceNamespace original{
        {},
        assetA,
        collision ? 5ULL : 4ULL,
        collision ? std::vector<std::uint64_t>{1, 2, 3, 4} : std::vector<std::uint64_t>{1, 2, 3}};
    auto result
        = prepareSceneSourceSnapshot(std::move(assembly), assetA, local, std::move(original));
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return std::move(*result);
  }

  auto target(const SceneSourceSnapshot& source) -> SceneContentResult {
    auto scene = Scene::create().value();
    REQUIRE(scene.createEntity("Existing"));
    auto result = prepareLinkedScene(scene, source, 2);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return std::move(*result);
  }
}  // namespace

TEST_CASE(
    "scene_source.identity_accepts_closed_namespaces_and_independent_materialization_values") {
  auto namespaces = spaces();
  auto nodes = origins();
  REQUIRE(validateSceneSourceIdentity(assetA, nodes, namespaces));
  auto materialized = nodes;
  materialized[0].entity = 99;
  materialized[4].entity = 1;
  CHECK(validateSceneSourceUpdate(assetA, nodes, namespaces, materialized, namespaces));
  CHECK(nodes == origins());
  CHECK(namespaces == spaces());
  auto incoming = namespaces;
  incoming[0].nextEntity = 12;
  incoming[0].entities.push_back(11);
  materialized.push_back({100, {11}});
  CHECK(validateSceneSourceUpdate(assetA, nodes, namespaces, materialized, incoming));
}

TEST_CASE("scene_source.namespace_and_origin_boundaries_reject_invalid_or_ambiguous_identity") {
  const auto goodSpaces = spaces();
  const auto goodNodes = origins();

  for (int failure = 0; failure < 9; ++failure) {
    auto ns = goodSpaces;

    switch (failure) {
      case 0:
        ns.erase(ns.begin());
        break;
      case 1:
        ns.push_back(ns[0]);
        break;
      case 2:
        ns[0].asset = assetB;
        break;
      case 3:
        ns[1].prefix = {9};
        break;
      case 4:
        ns[0].nextEntity = 0;
        break;
      case 5:
        ns[0].nextEntity = std::numeric_limits<std::uint64_t>::max();
        break;
      case 6:
        ns[1].entities.push_back(1);
        break;
      case 7:
        ns[1].entities.push_back(8);
        break;
      case 8:
        ns[1].asset = "../part.dotscene";
        break;
    }

    CHECK_FALSE(validateSceneSourceIdentity(assetA, goodNodes, ns));
  }

  for (const auto& invalid :
       std::vector<SceneSourceOrigin>{{0, {1}},
                                      {1, {}},
                                      {1, {10, 8}},
                                      {1, SceneSourceKey(33, 1)},
                                      {1, {0}},
                                      {1, {std::numeric_limits<std::uint64_t>::max()}}}) {
    CHECK_FALSE(validateSceneSourceIdentity(assetA, std::array{invalid}, goodSpaces));
  }

  auto duplicate = goodNodes;
  duplicate.push_back({99, goodNodes[0].key});
  CHECK_FALSE(validateSceneSourceIdentity(assetA, duplicate, goodSpaces));
  duplicate = goodNodes;
  duplicate[1].entity = 1;
  CHECK_FALSE(validateSceneSourceIdentity(assetA, duplicate, goodSpaces));
}

TEST_CASE(
    "scene_source.update_tracks_real_retirement_and_allows_restoring_hidden_materialization") {
  const auto oldSpaces = spaces();
  const auto oldNodes = origins();
  auto hidden = oldNodes;
  hidden.erase(hidden.begin() + 2);
  CHECK(validateSceneSourceUpdate(assetA, oldNodes, oldSpaces, hidden, oldSpaces));
  CHECK(validateSceneSourceUpdate(assetA, hidden, oldSpaces, oldNodes, oldSpaces));
  auto deleted = oldSpaces;
  deleted[1].entities.erase(deleted[1].entities.begin());
  CHECK(validateSceneSourceUpdate(assetA, oldNodes, oldSpaces, hidden, deleted));
  CHECK_FALSE(validateSceneSourceUpdate(assetA, hidden, deleted, oldNodes, oldSpaces));
  auto regressed = oldSpaces;
  regressed[0].nextEntity = 10;
  regressed[0].entities = {1};
  CHECK_FALSE(validateSceneSourceUpdate(assetA, oldNodes, oldSpaces, {}, regressed));
  auto replaced = oldSpaces;
  replaced[1].asset = "assets/replaced.dotscene";
  CHECK_FALSE(validateSceneSourceUpdate(assetA, oldNodes, oldSpaces, oldNodes, replaced));
  auto added = oldSpaces;
  added.push_back({{1}, "assets/new.dotscene", 2, {1}});
  CHECK(validateSceneSourceUpdate(assetA, oldNodes, oldSpaces, oldNodes, added));
}

TEST_CASE("scene_source.key_depth_and_exhausted_namespace_contracts_are_explicit") {
  std::vector<SceneSourceNamespace> ns;

  for (std::size_t depth = 0; depth < 32; ++depth) {
    ns.push_back({SceneSourceKey(depth, 1), assetA, 2, {1}});
  }

  CHECK(validateSceneSourceIdentity(assetA, std::array{SceneSourceOrigin{1, SceneSourceKey(32, 1)}},
                                    ns));
  ns.push_back({SceneSourceKey(32, 1), assetA, 2, {1}});
  CHECK_FALSE(validateSceneSourceIdentity(assetA, {}, ns));
  ns = {{{}, assetA, std::nullopt, {1}}};
  const auto node = std::array{SceneSourceOrigin{1, {1}}};
  CHECK(validateSceneSourceIdentity(assetA, node, ns));
  auto unsealed = ns;
  unsealed[0].nextEntity = 2;
  CHECK_FALSE(validateSceneSourceUpdate(assetA, node, ns, node, unsealed));
  auto reused = ns;
  reused[0].entities.push_back(2);
  const auto newNode = std::array{SceneSourceOrigin{1, {1}}, SceneSourceOrigin{2, {2}}};
  CHECK_FALSE(validateSceneSourceUpdate(assetA, node, ns, newNode, reused));
  ns = {{{}, assetA, 100002, std::vector<std::uint64_t>(100001)}};
  std::iota(ns[0].entities.begin(), ns[0].entities.end(), 1ULL);
  CHECK_FALSE(validateSceneSourceIdentity(assetA, {}, ns));
}

TEST_CASE(
    "scene_source.working_capture_distinguishes_local_and_nested_ownership_without_file_reads") {
  auto source = working(Working::Initial);
  CHECK(source.namespaces[0].nextEntity == 4);
  CHECK(source.scene.nextEntityValue().value() == 5);
  CHECK(source.origins[3].key == SceneSourceKey{2, 8});
  CHECK(source.namespaces[1].prefix == SceneSourceKey{2});
  CHECK(source.namespaces[1].entities == std::vector<std::uint64_t>{7, 8});
  auto savedCapture = captureSceneSource(source.scene, assetA).value();
  CHECK(savedCapture.origins == source.origins);
  CHECK(savedCapture.namespaces[0].nextEntity == 5);
  REQUIRE(source.scene.renameEntity({source.scene.id(), 4}, "Changed after capture"));
  CHECK(savedCapture.scene.entity({savedCapture.scene.id(), 4})->name == "Leaf");
}

TEST_CASE(
    "scene_source.refresh_keeps_override_and_references_when_an_ephemeral_value_becomes_local") {
  auto initial = working(Working::Initial);
  auto linked = target(initial);
  REQUIRE(linked.scene.renameEntity({linked.scene.id(), 6}, "Outer override"));
  auto incoming = working(Working::LocalCollision);
  const auto before = encodeScene(linked.scene).value();
  auto refreshed = prepareSceneLinkUpdate(linked.scene, incoming, linked.root, 7);
  REQUIRE_MESSAGE(refreshed, (refreshed ? "" : refreshed.error().message));
  CHECK(encodeScene(linked.scene).value() == before);
  CHECK(refreshed->created == std::vector<std::uint64_t>{7});
  CHECK(refreshed->scene.entity({refreshed->scene.id(), 6})->name == "Outer override");
  CHECK(refreshed->scene.entity({refreshed->scene.id(), 7})->name == "New local entity");
  CHECK(refreshed->scene.entity({refreshed->scene.id(), 5})->skin->joints
        == std::vector<std::uint64_t>{6});
  CHECK(refreshed->scene.entity({refreshed->scene.id(), 5})->script->properties
        == R"({"literal":"8"})");
  const auto links = refreshed->scene.sceneLinks().value();
  const auto leaf
      = std::ranges::find(links[0].mapping, SceneSourceKey{2, 8}, &SceneLinkMapping::key);
  REQUIRE(leaf != links[0].mapping.end());
  CHECK(leaf->source == 4);
  CHECK(leaf->target == 6);
  auto restored = decodeScene(encodeScene(refreshed->scene).value()).value();
  auto page = inspectSceneOverrides(restored, {restored.id(), 2}).value();
  REQUIRE(page.changes.size() == 1);
  CHECK(page.changes[0].sourceKey == SceneSourceKey{2, 8});
  CHECK(page.changes[0].source == 4);
}

TEST_CASE("scene_source.reused_working_value_never_binds_a_different_nested_source_node") {
  auto initial = working(Working::Initial);
  auto linked = target(initial);
  auto replaced = working(Working::ReplacedLeaf);
  auto refreshed = prepareSceneLinkUpdate(linked.scene, replaced, linked.root, 7);
  REQUIRE_MESSAGE(refreshed, (refreshed ? "" : refreshed.error().message));
  CHECK_FALSE(refreshed->scene.findEntity(6));
  CHECK(refreshed->created == std::vector<std::uint64_t>{7});
  CHECK(refreshed->scene.entity({refreshed->scene.id(), 5})->skin->joints
        == std::vector<std::uint64_t>{7});
  REQUIRE(linked.scene.renameEntity({linked.scene.id(), 6}, "Must not transfer"));
  CHECK_FALSE(prepareSceneLinkUpdate(linked.scene, replaced, linked.root, 7));
}

TEST_CASE("scene_source.equivalent_reindexed_materializations_have_identical_persisted_baselines") {
  auto initial = working(Working::Initial);
  auto linked = target(initial);
  auto reindexed = working(Working::Reindexed);
  CHECK(initial.scene.nextEntityValue().value() == 5);
  CHECK(reindexed.scene.nextEntityValue().value() == 6);
  const auto before = encodeScene(linked.scene).value();
  auto refreshed = prepareSceneLinkUpdate(linked.scene, reindexed, linked.root, 7);
  REQUIRE_MESSAGE(refreshed, (refreshed ? "" : refreshed.error().message));
  CHECK(encodeScene(refreshed->scene).value() == before);
  CHECK(refreshed->created.empty());
}

TEST_CASE("scene_source.file_provenance_is_required_strict_and_canonical") {
  auto source = working(Working::Initial);
  auto linked = target(source);
  const auto valid = Json::parse(encodeScene(linked.scene).value());
  REQUIRE(decodeScene(valid.dump()));

  for (int failure = 0; failure < 7; ++failure) {
    auto invalid = valid;
    auto& link = invalid["links"][0];

    switch (failure) {
      case 0:
        link.erase("namespaces");
        break;
      case 1:
        link["mapping"][0].erase("key");
        break;
      case 2:
        link["mapping"][0]["key"] = Json::array({1});
        break;
      case 3:
        link["namespaces"][0].erase("entities");
        break;
      case 4:
        link["namespaces"][0]["entities"] = Json::array({"0"});
        break;
      case 5:
        link["mapping"][0]["key"] = link["mapping"][1]["key"];
        break;
      case 6:
        link["namespaces"][0]["unknown"] = false;
        break;
    }

    CHECK_FALSE(decodeScene(invalid.dump()));
  }

  auto links = linked.scene.sceneLinks().value();
  std::ranges::reverse(links[0].namespaces);

  for (auto& space : links[0].namespaces) {
    std::ranges::reverse(space.entities);
  }

  REQUIRE(linked.scene.setSceneLinks(links));
  CHECK(Json::parse(encodeScene(linked.scene).value()) == valid);
}

TEST_CASE("scene_source.hidden_nodes_can_reappear_without_reusing_retired_target_bindings") {
  auto initial = working(Working::Initial);
  auto linked = target(initial);
  auto hidden = working(Working::HiddenLeaf);
  auto hiddenTarget = prepareSceneLinkUpdate(linked.scene, hidden, linked.root, 7).value();
  CHECK_FALSE(hiddenTarget.scene.findEntity(6));
  auto shown = prepareSceneLinkUpdate(hiddenTarget.scene, initial, hiddenTarget.root, 7);
  REQUIRE_MESSAGE(shown, (shown ? "" : shown.error().message));
  CHECK(shown->created == std::vector<std::uint64_t>{7});
  CHECK_FALSE(shown->scene.findEntity(6));
  CHECK(shown->scene.entity({shown->scene.id(), 5})->skin->joints == std::vector<std::uint64_t>{7});
}

TEST_CASE(
    "scene_source.unlinking_a_saved_inner_instance_promotes_existing_allocations_to_local_"
    "origins") {
  auto workingSource = working(Working::Initial);
  auto saved = captureSceneSource(workingSource.scene, assetA).value();
  auto linked = target(saved);
  REQUIRE(workingSource.scene.setSceneLinks({}));
  auto detached = captureSceneSource(workingSource.scene, assetA).value();
  auto refreshed = prepareSceneLinkUpdate(linked.scene, detached, linked.root, 7);
  REQUIRE_MESSAGE(refreshed, (refreshed ? "" : refreshed.error().message));
  CHECK_FALSE(refreshed->scene.findEntity(5));
  CHECK_FALSE(refreshed->scene.findEntity(6));
  CHECK(refreshed->created == std::vector<std::uint64_t>{7, 8});
  CHECK(refreshed->scene.entity({refreshed->scene.id(), 7})->skin->joints
        == std::vector<std::uint64_t>{8});
}

TEST_CASE("scene_source.invalid_or_foreign_snapshots_reject_without_mutating_target_or_owner") {
  auto source = working(Working::Initial);
  auto linked = target(source);
  const auto before = encodeScene(linked.scene).value();
  source.origins[0].entity = 99;
  CHECK_FALSE(validateSceneSourceSnapshot(source));
  CHECK_FALSE(prepareSceneLinkUpdate(linked.scene, source, linked.root, 7));
  CHECK(encodeScene(linked.scene).value() == before);
  source = working(Working::Initial);
  bool rejected{};
  std::thread worker([&] {
    rejected = !validateSceneSourceSnapshot(source) && !captureSceneSource(source.scene, assetA);
  });
  worker.join();
  CHECK(rejected);
  auto copy = decodeScene(encodeScene(source.scene).value()).value();
  const auto locals
      = std::array{SceneSourceOrigin{1, {1}}, SceneSourceOrigin{2, {2}}, SceneSourceOrigin{3, {3}}};
  CHECK_FALSE(
      prepareSceneSourceSnapshot(std::move(copy), assetA, locals, {{}, assetA, 4, {1, 2, 3}}));
  auto moved = std::move(source);
  CHECK_FALSE(validateSceneSourceSnapshot(source));
  CHECK(validateSceneSourceSnapshot(moved));
}

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.World.SceneContent;
import Dot.World.SceneLinks;
import Dot.World.SceneSources;
import Dot.World.SceneResolver;

namespace {
  using namespace Dot::World;
  constexpr auto rootPath = "assets/root.dotscene";
  constexpr auto assemblyPath = "assets/assembly.dotscene";
  constexpr auto leafPath = "assets/leaf.dotscene";

  struct Fixture {
    Scene leaf = Scene::create("Leaf").value();
    Scene assembly = Scene::create("Assembly").value();
    Scene root = Scene::create("Root").value();
    std::map<std::string, std::string> files;
    std::map<std::string, std::size_t> reads;

    Fixture() {
      const auto panel = leaf.createEntity("Panel").value();
      const auto joint = leaf.createEntity("Joint").value();
      REQUIRE(leaf.setMeshRenderer(panel, MeshRenderer{}));
      REQUIRE(leaf.setSkin(panel, SkinBinding{"assets/rig.dotskin", {joint.value}}));
      REQUIRE(leaf.setScript(panel, ScriptComponent{"actor.ts", "Actor", R"({"literal":"2"})"}));
      REQUIRE(assembly.createEntity("Assembly local"));
      assembly = std::move(prepareLinkedScene(assembly, leaf, leafPath, 2)->scene);
      REQUIRE(assembly.renameEntity({assembly.id(), 3}, "Assembly override"));
      REQUIRE(root.createEntity("Root local"));
      root = std::move(prepareLinkedScene(root, assembly, assemblyPath, 2)->scene);
      REQUIRE(root.renameEntity({root.id(), 5}, "Root override"));
      save();
    }

    auto save() -> void {
      files[rootPath] = encodeScene(root).value();
      files[assemblyPath] = encodeScene(assembly).value();
      files[leafPath] = encodeScene(leaf).value();
    }

    auto loader() -> SceneSourceLoader {
      return [this](std::string_view asset, std::size_t maximum) -> Dot::Core::Result<std::string> {
        const auto path = std::string(asset);
        ++reads[path];
        const auto found = files.find(path);

        if (found == files.end()) {
          return Dot::Core::fail(Dot::Core::ErrorCode::NotFound, "Missing " + path);
        }

        if (found->second.size() > maximum) {
          return Dot::Core::fail(Dot::Core::ErrorCode::Unavailable, "Input quota: " + path);
        }

        return found->second;
      };
    }

    auto resolve(const SceneResolveLimits& limits = {}) -> ResolvedSceneSource {
      auto result = resolveSceneSource(rootPath, loader(), limits);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }
  };

  auto entityForKey(const SceneSourceSnapshot& source, const SceneSourceKey& key) -> EntityId {
    const auto found = std::ranges::find(source.origins, key, &SceneSourceOrigin::key);
    REQUIRE(found != source.origins.end());
    return {source.scene.id(), found->entity};
  }

  auto targetForKey(const Scene& scene, const SceneSourceKey& key) -> EntityId {
    const auto link = scene.sceneLinks()->front();
    const auto found = std::ranges::find(link.mapping, key, &SceneLinkMapping::key);
    REQUIRE(found != link.mapping.end());
    return {scene.id(), found->target};
  }
}  // namespace

TEST_CASE("scene_resolver.bottom_up_refresh_preserves_layered_overrides_and_native_references") {
  Fixture f;
  REQUIRE(f.leaf.renameEntity({f.leaf.id(), 1}, "Latest leaf name"));
  MeshRenderer mesh;
  mesh.visible = false;
  REQUIRE(f.leaf.setMeshRenderer({f.leaf.id(), 1}, mesh));
  const auto added = f.leaf.createEntity("New joint").value();
  REQUIRE(f.leaf.setSkin({f.leaf.id(), 1}, SkinBinding{"assets/rig.dotskin", {2, added.value}}));
  f.save();
  const auto before = f.files;
  auto resolved = f.resolve();
  CHECK(f.files == before);
  CHECK(resolved.stats.sources == 3);
  CHECK(resolved.stats.links == 2);
  CHECK(f.reads
        == std::map<std::string, std::size_t>{{rootPath, 1}, {assemblyPath, 1}, {leafPath, 1}});
  const auto panel = entityForKey(resolved.source, {2, 2, 1});
  const auto joint = entityForKey(resolved.source, {2, 2, 2});
  const auto newJoint = entityForKey(resolved.source, {2, 2, 3});
  const auto entity = resolved.source.scene.entity(panel).value();
  CHECK(entity.name == "Root override");
  CHECK_FALSE(entity.mesh->visible);
  CHECK(entity.skin->joints == std::vector<std::uint64_t>{joint.value, newJoint.value});
  CHECK(entity.script->properties == R"({"literal":"2"})");
  CHECK(resolved.source.namespaces[0].nextEntity == 7);
  CHECK(resolved.source.namespaces[0].entities == std::vector<std::uint64_t>{1, 2, 3, 4, 5, 6});
  CHECK(resolved.source.scene.nextEntityValue().value() == 8);
  auto repeated = f.resolve();
  CHECK(encodeScene(repeated.source.scene).value() == encodeScene(resolved.source.scene).value());
  CHECK(repeated.source.origins == resolved.source.origins);
  CHECK(repeated.source.namespaces == resolved.source.namespaces);
}

TEST_CASE("scene_resolver.working_values_can_collide_with_later_authored_local_allocations") {
  Fixture f;
  REQUIRE(f.leaf.createEntity("New leaf"));
  f.save();
  auto initial = f.resolve();
  auto target = Scene::create().value();
  auto linked = prepareLinkedScene(target, initial.source, 1).value();
  const auto inherited = targetForKey(linked.scene, {2, 2, 3});
  REQUIRE(linked.scene.renameEntity(inherited, "Must stay inherited"));
  REQUIRE(f.root.createEntity("New local root node"));
  f.save();
  auto incoming = f.resolve();
  CHECK(entityForKey(initial.source, {2, 2, 3}).value == 7);
  CHECK(entityForKey(incoming.source, {7}).value == 7);
  CHECK(entityForKey(incoming.source, {2, 2, 3}).value == 8);
  auto refreshed = prepareSceneLinkUpdate(linked.scene, incoming.source, linked.root,
                                          linked.scene.nextEntityValue().value().value());
  REQUIRE_MESSAGE(refreshed, (refreshed ? "" : refreshed.error().message));
  CHECK(targetForKey(refreshed->scene, {2, 2, 3}).value == inherited.value);
  CHECK(refreshed->scene.entity({refreshed->scene.id(), inherited.value})->name
        == "Must stay inherited");
  CHECK(refreshed->created.size() == 1);
  CHECK(refreshed->scene.entity(targetForKey(refreshed->scene, {7}))->name
        == "New local root node");
}

TEST_CASE("scene_resolver.shared_dependencies_read_once_and_materialize_independent_placements") {
  Fixture f;
  f.root = std::move(prepareLinkedScene(f.root, f.assembly, assemblyPath, 7)->scene);
  REQUIRE(f.leaf.createEntity("Latest child"));
  f.save();
  auto resolved = f.resolve();
  CHECK(resolved.stats.sources == 3);
  CHECK(resolved.stats.links == 3);
  CHECK(f.reads[assemblyPath] == 1);
  CHECK(f.reads[leafPath] == 1);
  const auto first = entityForKey(resolved.source, {2, 2, 3});
  const auto second = entityForKey(resolved.source, {7, 2, 3});
  CHECK(first.value != second.value);
  REQUIRE(resolved.source.scene.renameEntity(first, "First only"));
  CHECK(resolved.source.scene.entity(second)->name == "Latest child");
}

TEST_CASE("scene_resolver.shared_cached_height_cannot_bypass_a_deeper_placement_limit") {
  Fixture f;
  auto wrapper = Scene::create("Wrapper").value();
  wrapper = std::move(prepareLinkedScene(wrapper, f.assembly, assemblyPath, 1)->scene);
  f.root = std::move(prepareLinkedScene(f.root, wrapper, "assets/wrapper.dotscene", 7)->scene);
  f.save();
  f.files["assets/wrapper.dotscene"] = encodeScene(wrapper).value();
  SceneResolveLimits limits;
  limits.depth = 3;
  auto rejected = resolveSceneSource(rootPath, f.loader(), limits);
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().message.find("Cached scene source exceeds placement depth")
        != std::string::npos);
  CHECK(f.reads[assemblyPath] == 1);
  CHECK(f.reads[leafPath] == 1);
  limits.depth = 4;
  auto accepted = f.resolve(limits);
  CHECK(accepted.stats.sources == 4);
}

TEST_CASE("scene_resolver.cycles_and_active_document_references_never_use_stored_fallbacks") {
  Fixture f;
  auto failed = resolveSceneSource(rootPath, f.loader(), {}, leafPath);
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find(rootPath) != std::string::npos);
  CHECK(failed.error().message.find(assemblyPath) != std::string::npos);
  CHECK(failed.error().message.find(leafPath) != std::string::npos);
  CHECK(f.reads[leafPath] == 0);
  f.leaf = std::move(prepareLinkedScene(f.leaf, f.assembly, assemblyPath, 3)->scene);
  f.save();
  const auto before = f.files;
  failed = resolveSceneSource(rootPath, f.loader());
  REQUIRE_FALSE(failed);
  CHECK(failed.error().code == Dot::Core::ErrorCode::InvalidState);
  CHECK(failed.error().message.find("cycle") != std::string::npos);
  CHECK(f.files == before);
  auto self = Scene::create().value();
  auto empty = Scene::create().value();
  REQUIRE(empty.createEntity("Cycle baseline"));
  auto selfLink = prepareLinkedScene(self, empty, rootPath, 1);
  REQUIRE_MESSAGE(selfLink, (selfLink ? "" : selfLink.error().message));
  self = std::move(selfLink->scene);
  f.files[rootPath] = encodeScene(self).value();
  CHECK_FALSE(resolveSceneSource(rootPath, f.loader()));
}

TEST_CASE("scene_resolver.empty_saved_source_retains_its_namespace_without_materialization") {
  auto empty = Scene::create().value();
  const auto text = encodeScene(empty).value();
  auto source = resolveSceneSource(rootPath, [&](std::string_view, std::size_t) { return text; });
  REQUIRE(source);
  CHECK(source->source.scene.entityCount().value() == 0);
  CHECK(source->stats.sources == 1);
  CHECK(source->stats.links == 0);
  CHECK(source->stats.entities == 0);
  CHECK(source->source.origins.empty());
  REQUIRE(source->source.namespaces.size() == 1);
  CHECK(source->source.namespaces[0].nextEntity == 1);
}

TEST_CASE("scene_resolver.graph_budgets_have_exact_success_and_failure_boundaries") {
  Fixture f;
  auto initial = f.resolve();
  SceneResolveLimits exact{3,
                           initial.stats.sources,
                           initial.stats.links,
                           initial.stats.inputBytes,
                           initial.stats.resolvedBytes,
                           initial.stats.entities};
  CHECK(resolveSceneSource(rootPath, f.loader(), exact));
  const auto fields
      = std::array{&SceneResolveLimits::depth,         &SceneResolveLimits::sources,
                   &SceneResolveLimits::links,         &SceneResolveLimits::inputBytes,
                   &SceneResolveLimits::resolvedBytes, &SceneResolveLimits::entities};

  for (auto field : fields) {
    auto insufficient = exact;
    --(insufficient.*field);
    CHECK_FALSE(resolveSceneSource(rootPath, f.loader(), insufficient));
    auto invalid = exact;
    invalid.*field = 0;
    f.reads.clear();
    CHECK_FALSE(resolveSceneSource(rootPath, f.loader(), invalid));
    CHECK(f.reads.empty());
    invalid.*field = SceneResolveLimits{}.*field + 1;
    CHECK_FALSE(resolveSceneSource(rootPath, f.loader(), invalid));
    CHECK(f.reads.empty());
  }
}

TEST_CASE("scene_resolver.loader_errors_invalid_inputs_and_exceptions_keep_context_and_ownership") {
  Fixture f;
  const auto original = f.files;
  f.files.erase(leafPath);
  auto missing = resolveSceneSource(rootPath, f.loader());
  REQUIRE_FALSE(missing);
  CHECK(missing.error().code == Dot::Core::ErrorCode::NotFound);
  CHECK(missing.error().message.find(assemblyPath) != std::string::npos);
  CHECK(missing.error().message.find(leafPath) != std::string::npos);
  f.files[leafPath] = "{}";
  CHECK_FALSE(resolveSceneSource(rootPath, f.loader()));
  f.files = original;
  auto throwing = [](std::string_view, std::size_t) -> Dot::Core::Result<std::string> {
    throw std::runtime_error("Reader unavailable");
  };
  auto failure = resolveSceneSource(rootPath, throwing);
  REQUIRE_FALSE(failure);
  CHECK(failure.error().code == Dot::Core::ErrorCode::External);
  CHECK(failure.error().message.find("Reader unavailable") != std::string::npos);
  CHECK_FALSE(resolveSceneSource(rootPath, {}));

  for (const auto* path :
       {"../root.dotscene", "scenes/.dot-stage-x/root.dotscene", "assets/a.json"}) {
    f.reads.clear();
    CHECK_FALSE(resolveSceneSource(path, f.loader()));
    CHECK(f.reads.empty());
  }

  CHECK(f.files == original);
}

TEST_CASE("scene_resolver.loader_cannot_ignore_remaining_input_quota") {
  SceneResolveLimits limits;
  limits.inputBytes = 32;
  std::size_t calls{};
  auto oversized = [&](std::string_view, std::size_t maximum) -> Dot::Core::Result<std::string> {
    ++calls;
    CHECK(maximum == 32);
    return std::string(maximum + 1, ' ');
  };
  auto rejected = resolveSceneSource(rootPath, oversized, limits);
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().message.find("loader exceeded") != std::string::npos);
  CHECK(calls == 1);
}

TEST_CASE("scene_resolver.intermediate_override_conflicts_reject_without_writing_any_source") {
  Fixture f;
  REQUIRE(f.leaf.destroyEntity({f.leaf.id(), 1}));
  f.save();
  const auto before = f.files;
  auto rejected = resolveSceneSource(rootPath, f.loader());
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().message.find("overridden node") != std::string::npos);
  CHECK(rejected.error().message.find(assemblyPath) != std::string::npos);
  CHECK(rejected.error().message.find(leafPath) != std::string::npos);
  CHECK(f.files == before);
}

TEST_CASE("scene_resolver.exhausted_source_allocation_allows_noop_but_rejects_new_nested_nodes") {
  Fixture f;
  REQUIRE(f.assembly.reserveEntityValuesBefore(std::nullopt));
  f.save();
  CHECK(resolveSceneSource(rootPath, f.loader()));
  REQUIRE(f.leaf.createEntity("Cannot materialize in sealed assembly"));
  f.save();
  const auto before = f.files;
  auto rejected = resolveSceneSource(rootPath, f.loader());
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().message.find("exhausted") != std::string::npos);
  CHECK(f.files == before);
}

TEST_CASE("scene_resolver.link_order_does_not_change_materialization_or_target_serialization") {
  Fixture f;
  f.root = std::move(prepareLinkedScene(f.root, f.assembly, assemblyPath, 7)->scene);
  REQUIRE(f.leaf.createEntity("New shared leaf"));
  f.save();
  auto first = f.resolve();
  auto links = f.root.sceneLinks().value();
  std::ranges::reverse(links);
  REQUIRE(f.root.setSceneLinks(links));
  f.save();
  auto second = f.resolve();
  auto target = Scene::create().value();
  auto a = prepareLinkedScene(target, first.source, 1).value();
  auto b = prepareLinkedScene(target, second.source, 1).value();
  CHECK(encodeScene(a.scene).value() == encodeScene(b.scene).value());
}

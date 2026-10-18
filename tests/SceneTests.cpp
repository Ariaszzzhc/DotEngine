#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <expected>
#include <future>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.World.Scene;
import Dot.Core.Result;
import Dot.Math;

using namespace Dot::World;

namespace {
  auto position(Scene& scene, EntityId entity) -> Dot::Math::Vec3 {
    auto matrix = scene.worldTransform(entity);
    REQUIRE(matrix);
    auto point = Dot::Math::transformPoint(*matrix, {});
    REQUIRE(point);
    return *point;
  }
}  // namespace

TEST_CASE("scene.entities_snapshots_and_deterministic_order") {
  auto scene = Scene::create("中文场景");
  REQUIRE(scene);
  CHECK(scene->name().value() == "中文场景");
  CHECK(scene->entityCount().value() == 0);
  auto first = scene->createEntity("First");
  auto second = scene->createEntity("Second");
  REQUIRE(first);
  REQUIRE(second);
  CHECK(first->scene == scene->id());
  CHECK(first->value < second->value);
  CHECK(scene->findEntity(first->value).value() == *first);
  auto entries = scene->entities();
  REQUIRE(entries);
  REQUIRE(entries->size() == 2);
  CHECK((*entries)[0].id == *first);
  CHECK((*entries)[1].id == *second);
  (*entries)[0].name = "Edited snapshot";
  CHECK(scene->entity(*first)->name == "First");
  REQUIRE(scene->renameEntity(*first, "中文🙂"));
  CHECK(scene->entity(*first)->name == "中文🙂");
  REQUIRE(scene->rename("New scene"));
  CHECK(scene->name().value() == "New scene");
}

TEST_CASE("scene.name_boundaries_reject_invalid_utf8_and_nul") {
  for (const auto& name :
       {std::string{}, std::string(257, 'x'), std::string{"A\0B", 3}, std::string{"\xC0\x80"}}) {
    CHECK_FALSE(Scene::create(name));
  }

  auto scene = Scene::create(std::string(256, 'x'));
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  CHECK_FALSE(scene->createEntity(""));
  CHECK_FALSE(scene->rename(""));
  CHECK_FALSE(scene->renameEntity(*entity, "\xFF"));
  CHECK(scene->entityCount().value() == 1);
  CHECK(scene->entity(*entity)->name == "Entity");
  CHECK(scene->name()->size() == 256);
}

TEST_CASE("scene.default_transform_and_normalized_rotation") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  CHECK(scene->worldTransform(*entity).value() == Dot::Math::Mat4{});
  Transform transform{{1, 2, 3}, {0, 0, 0, 2}, {2, 3, 4}};
  REQUIRE(scene->setTransform(*entity, transform));
  CHECK(scene->entity(*entity)->transform.rotation == Dot::Math::Quat{});
  CHECK(position(*scene, *entity) == Dot::Math::Vec3{1, 2, 3});
  auto matrix = scene->worldTransform(*entity);
  REQUIRE(matrix);
  CHECK(matrix->data[0] == 2);
  CHECK(matrix->data[5] == 3);
  CHECK(matrix->data[10] == 4);
}

TEST_CASE("scene.rotation_storage_preserves_float_units_and_validates_other_inputs") {
  auto scene = Scene::create().value();
  const auto entity = scene.createEntity().value();
  const Dot::Math::Quat unit{0.0942542776465416F, 0.7153381705284119F, 0.054892364889383316F,
                             0.690213143825531F};
  REQUIRE(scene.setTransform(entity, {{}, unit}));
  CHECK(scene.transform(entity)->rotation == unit);
  const auto matrix = scene.worldTransform(entity).value();
  REQUIRE(scene.setTransform(entity, {{}, unit}));
  CHECK(scene.transform(entity)->rotation == unit);
  CHECK(scene.worldTransform(entity).value() == matrix);

  const auto maximum = std::numeric_limits<float>::max();
  const auto minimum = std::numeric_limits<float>::denorm_min();

  for (const auto rotation :
       std::array{Dot::Math::Quat{0, 0, 0, 1.001F}, Dot::Math::Quat{0, 0, 0, maximum},
                  Dot::Math::Quat{0, 0, 0, minimum}}) {
    REQUIRE(scene.setTransform(entity, {{}, rotation}));
    CHECK(scene.transform(entity)->rotation == Dot::Math::Quat{});
  }

  const auto before = scene.transform(entity).value();

  for (const auto rotation :
       std::array{Dot::Math::Quat{0, 0, 0, 0},
                  Dot::Math::Quat{0, std::numeric_limits<float>::infinity(), 0, 1},
                  Dot::Math::Quat{0, 0, std::numeric_limits<float>::quiet_NaN(), 1}}) {
    auto rejected = scene.setTransform(entity, {{}, rotation});
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Dot::Core::ErrorCode::InvalidArgument);
    const auto after = scene.transform(entity).value();
    CHECK(after.position == before.position);
    CHECK(after.rotation == before.rotation);
    CHECK(after.scale == before.scale);
  }
}

TEST_CASE("scene.hierarchy_composition_cache_invalidation_and_detach") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto parent = scene->createEntity("Parent");
  auto child = scene->createEntity("Child");
  auto grandchild = scene->createEntity("Grandchild");
  REQUIRE(parent);
  REQUIRE(child);
  REQUIRE(grandchild);
  REQUIRE(scene->setTransform(*parent, {{10, 0, 0}, {}, {2, 2, 2}}));
  REQUIRE(scene->setTransform(*child, {{3, 0, 0}}));
  REQUIRE(scene->setTransform(*grandchild, {{1, 0, 0}}));
  REQUIRE(scene->setParent(*child, *parent));
  REQUIRE(scene->setParent(*grandchild, *child));
  CHECK(position(*scene, *grandchild) == Dot::Math::Vec3{18, 0, 0});
  CHECK(position(*scene, *grandchild) == Dot::Math::Vec3{18, 0, 0});
  REQUIRE(scene->setTransform(*parent, {{20, 0, 0}, {}, {2, 2, 2}}));
  CHECK(position(*scene, *grandchild) == Dot::Math::Vec3{28, 0, 0});
  REQUIRE(scene->setParent(*child, std::nullopt));
  CHECK_FALSE(scene->entity(*child)->parent);
  CHECK(position(*scene, *grandchild) == Dot::Math::Vec3{4, 0, 0});
  CHECK(scene->entity(*child)->transform.position == Dot::Math::Vec3{3, 0, 0});
}

TEST_CASE("scene.cycle_rejection_preserves_existing_hierarchy") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto parent = scene->createEntity();
  auto child = scene->createEntity();
  REQUIRE(parent);
  REQUIRE(child);
  REQUIRE(scene->setParent(*child, *parent));
  CHECK_FALSE(scene->setParent(*parent, *child));
  CHECK_FALSE(scene->setParent(*parent, *parent));
  CHECK_FALSE(scene->entity(*parent)->parent);
  CHECK(scene->entity(*child)->parent.value() == *parent);
  CHECK(scene->worldTransform(*child));
}

TEST_CASE("scene.failed_transforms_are_transactional") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  REQUIRE(scene->setTransform(*entity, {{1, 2, 3}}));
  Transform invalid;
  invalid.scale.x = 0;
  CHECK_FALSE(scene->setTransform(*entity, invalid));
  invalid = {};
  invalid.rotation = {0, 0, 0, 0};
  CHECK_FALSE(scene->setTransform(*entity, invalid));
  invalid = {};
  invalid.position.y = std::numeric_limits<float>::infinity();
  CHECK_FALSE(scene->setTransform(*entity, invalid));
  CHECK(position(*scene, *entity) == Dot::Math::Vec3{1, 2, 3});
}

TEST_CASE("scene.world_overflow_rolls_back_transform_and_parent_changes") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto parent = scene->createEntity();
  auto child = scene->createEntity();
  REQUIRE(parent);
  REQUIRE(child);
  const auto maximum = std::numeric_limits<float>::max();
  REQUIRE(scene->setTransform(*parent, {{}, {}, {maximum, 1, 1}}));
  REQUIRE(scene->setParent(*child, *parent));
  CHECK_FALSE(scene->setTransform(*child, {{}, {}, {2, 1, 1}}));
  CHECK(scene->entity(*child)->transform.scale == Dot::Math::Vec3{1, 1, 1});
  CHECK(scene->worldTransform(*child)->data[0] == maximum);
  REQUIRE(scene->setParent(*child, std::nullopt));
  REQUIRE(scene->setTransform(*child, {{}, {}, {2, 1, 1}}));
  CHECK_FALSE(scene->setParent(*child, *parent));
  CHECK_FALSE(scene->entity(*child)->parent);
  CHECK(scene->worldTransform(*child)->data[0] == 2);
  REQUIRE(scene->setTransform(*parent, {}));
  REQUIRE(scene->setParent(*child, *parent));
  CHECK_FALSE(scene->setTransform(*parent, {{}, {}, {maximum, 1, 1}}));
  CHECK(scene->entity(*parent)->transform.scale == Dot::Math::Vec3{1, 1, 1});
  CHECK(scene->worldTransform(*child)->data[0] == 2);
}

TEST_CASE("scene.foreign_and_stale_handles_are_rejected") {
  auto scene = Scene::create();
  auto foreign = Scene::create();
  REQUIRE(scene);
  REQUIRE(foreign);
  auto entity = scene->createEntity();
  auto other = foreign->createEntity();
  REQUIRE(entity);
  REQUIRE(other);
  CHECK(entity->value == other->value);
  CHECK_FALSE(scene->entity(*other));
  CHECK_FALSE(scene->destroyEntity(*other));
  CHECK_FALSE(scene->setTransform(*other, {}));
  CHECK_FALSE(scene->setParent(*entity, *other));
  REQUIRE(scene->destroyEntity(*entity));
  auto stale = scene->entity(*entity);
  REQUIRE_FALSE(stale);
  CHECK(stale.error().code == Dot::Core::ErrorCode::NotFound);
  CHECK_FALSE(scene->renameEntity(*entity, "Stale"));
  CHECK_FALSE(scene->worldTransform(*entity));
  CHECK_FALSE(scene->primaryCamera());
  CHECK_FALSE(scene->setCamera(*entity, Camera{}));
  CHECK_FALSE(scene->viewProjection(*entity, 1));
  CHECK_FALSE(scene->setMeshRenderer(*entity, MeshRenderer{}));
  CHECK_FALSE(scene->findEntity(entity->value));
  auto replacement = scene->createEntity();
  REQUIRE(replacement);
  CHECK(replacement->value > entity->value);
  CHECK_FALSE(scene->entity(*entity));
}

TEST_CASE("scene.destroy_removes_only_the_requested_subtree") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto root = scene->createEntity();
  auto child = scene->createEntity();
  auto grandchild = scene->createEntity();
  auto sibling = scene->createEntity();
  auto unrelated = scene->createEntity();
  REQUIRE(root);
  REQUIRE(child);
  REQUIRE(grandchild);
  REQUIRE(sibling);
  REQUIRE(unrelated);
  REQUIRE(scene->setParent(*child, *root));
  REQUIRE(scene->setParent(*grandchild, *child));
  REQUIRE(scene->setParent(*sibling, *root));
  REQUIRE(scene->destroyEntity(*child));
  CHECK_FALSE(scene->entity(*child));
  CHECK_FALSE(scene->entity(*grandchild));
  CHECK(scene->entityCount().value() == 3);
  CHECK(scene->entity(*sibling)->parent.value() == *root);
  REQUIRE(scene->destroyEntity(*root));
  CHECK_FALSE(scene->entity(*sibling));
  CHECK(scene->entityCount().value() == 1);
  CHECK(scene->entity(*unrelated));
}

TEST_CASE("scene.hierarchy_depth_boundary_remains_transactional") {
  auto scene = Scene::create();
  REQUIRE(scene);
  std::vector<EntityId> entities;

  for (int index = 0; index < 257; ++index) {
    auto entity = scene->createEntity();
    REQUIRE(entity);
    entities.push_back(*entity);

    if (index > 0 && index < 256) {
      REQUIRE(scene->setParent(entities.back(), entities[static_cast<std::size_t>(index - 1)]));
    }
  }

  REQUIRE(scene->worldTransform(entities[255]));
  CHECK_FALSE(scene->setParent(entities[256], entities[255]));
  CHECK_FALSE(scene->entity(entities[256])->parent);
  CHECK_FALSE(scene->setParent(entities.front(), entities.back()));
  CHECK_FALSE(scene->entity(entities.front())->parent);
  REQUIRE(scene->destroyEntity(entities.front()));
  CHECK(scene->entityCount().value() == 1);
}

TEST_CASE("scene.entity_capacity_and_id_nonreuse") {
  auto scene = Scene::create();
  REQUIRE(scene);
  EntityId first;
  EntityId last;

  for (int index = 0; index < 10000; ++index) {
    auto entity = scene->createEntity();
    REQUIRE(entity);
    last = *entity;

    if (index == 0) {
      first = *entity;
    }
  }

  CHECK_FALSE(scene->createEntity());
  CHECK(scene->entityCount().value() == 10000);
  REQUIRE(scene->destroyEntity(first));
  auto replacement = scene->createEntity();
  REQUIRE(replacement);
  CHECK(replacement->value > last.value);
  CHECK(scene->entityCount().value() == 10000);
}

TEST_CASE("scene.moves_preserve_handles_and_empty_owners_fail") {
  auto scene = Scene::create();
  auto destination = Scene::create();
  REQUIRE(scene);
  REQUIRE(destination);
  auto entity = scene->createEntity();
  auto previous = destination->createEntity();
  REQUIRE(entity);
  REQUIRE(previous);
  auto moved = std::move(*scene);
  CHECK(moved.entity(*entity));
  CHECK(scene->id() == 0);
  CHECK_FALSE(scene->name());
  CHECK_FALSE(scene->rename("Moved"));
  CHECK_FALSE(scene->createEntity());
  CHECK_FALSE(scene->entity(*entity));
  CHECK_FALSE(scene->entities());
  CHECK_FALSE(scene->entityCount());
  CHECK_FALSE(scene->destroyEntity(*entity));
  CHECK_FALSE(scene->findEntity(entity->value));
  CHECK_FALSE(scene->renameEntity(*entity, "Moved"));
  CHECK_FALSE(scene->setParent(*entity, std::nullopt));
  CHECK_FALSE(scene->setTransform(*entity, {}));
  CHECK_FALSE(scene->worldTransform(*entity));
  *destination = std::move(moved);
  CHECK(destination->entity(*entity));
  CHECK_FALSE(destination->entity(*previous));
  CHECK(moved.id() == 0);
}

TEST_CASE("scene.worker_operations_fail_and_snapshots_outlive_owner") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity("Persistent snapshot");
  REQUIRE(entity);
  auto snapshot = scene->entity(*entity);
  REQUIRE(snapshot);
  bool rejected = false;
  std::thread worker([&] {
    rejected = !scene->name() && !scene->rename("Worker") && !scene->entityCount()
               && !scene->createEntity() && !scene->entity(*entity) && !scene->entities()
               && !scene->findEntity(entity->value) && !scene->setTransform(*entity, {})
               && !scene->setParent(*entity, std::nullopt) && !scene->worldTransform(*entity)
               && !scene->destroyEntity(*entity) && !scene->renameEntity(*entity, "Worker");
    rejected = rejected && !scene->primaryCamera() && !scene->setCamera(*entity, Camera{})
               && !scene->viewProjection(*entity, 1)
               && !scene->setMeshRenderer(*entity, MeshRenderer{});
  });
  worker.join();
  CHECK(rejected);
  REQUIRE(scene->destroyEntity(*entity));
  CHECK(snapshot->name == "Persistent snapshot");
  CHECK(scene->entityCount().value() == 0);
  std::optional<EntityInfo> detached;

  {
    auto owner = Scene::create();
    REQUIRE(owner);
    auto owned = owner->createEntity("After destruction");
    REQUIRE(owned);
    detached = owner->entity(*owned).value();
    std::thread destroyer([state = std::move(*owner)] { static_cast<void>(state.id()); });
    destroyer.join();
  }

  CHECK(detached->name == "After destruction");
}

TEST_CASE("scene.parent_rotation_affects_child_world_position") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto parent = scene->createEntity();
  auto child = scene->createEntity();
  auto rotation = Dot::Math::rotationEulerDegrees({0, 0, 90});
  REQUIRE(parent);
  REQUIRE(child);
  REQUIRE(rotation);
  REQUIRE(scene->setTransform(*parent, {{2, 3, 4}, *rotation, {2, 2, 2}}));
  REQUIRE(scene->setTransform(*child, {{1, 0, 0}}));
  REQUIRE(scene->setParent(*child, *parent));
  auto world = position(*scene, *child);
  CHECK(world.x == doctest::Approx(2));
  CHECK(world.y == doctest::Approx(5));
  CHECK(world.z == doctest::Approx(4));
}

TEST_CASE("scene.repeated_registry_lifecycle") {
  for (int iteration = 0; iteration < 100; ++iteration) {
    auto scene = Scene::create();
    REQUIRE(scene);
    auto root = scene->createEntity();
    auto child = scene->createEntity();
    REQUIRE(root);
    REQUIRE(child);
    REQUIRE(scene->setParent(*child, *root));
    REQUIRE(scene->destroyEntity(*root));
    CHECK(scene->entityCount().value() == 0);
  }
}

TEST_CASE("scene.independent_scenes_can_be_created_concurrently") {
  std::atomic<int> failures{};
  std::vector<std::uint64_t> identities(8);
  std::vector<std::thread> workers;

  for (std::size_t index = 0; index < identities.size(); ++index) {
    workers.emplace_back([&, index] {
      auto scene = Scene::create();

      if (!scene) {
        ++failures;
        return;
      }

      identities[index] = scene->id();

      for (int entityIndex = 0; entityIndex < 128; ++entityIndex) {
        auto entity = scene->createEntity();

        if (!entity || !scene->setTransform(*entity, {{1, 2, 3}})
            || !scene->worldTransform(*entity)) {
          ++failures;
          return;
        }
      }
    });
  }

  for (auto& worker : workers) {
    worker.join();
  }

  CHECK(failures.load() == 0);
  std::ranges::sort(identities);
  CHECK(identities.front() != 0);
  CHECK(std::adjacent_find(identities.begin(), identities.end()) == identities.end());
}

TEST_CASE("scene.environment_cpu_validation_is_transactional_thread_and_lifetime_bound") {
  auto scene = Scene::create().value();
  CHECK_FALSE(scene.environment().value());
  Environment valid{"assets/studio.hdr", 2, 90, false, {16, 4, 16, 16, 32}};
  REQUIRE(scene.setEnvironment(valid));
  CHECK(scene.environment().value() == valid);
  auto invalid = valid;

  for (const auto* path :
       {"", "../studio.hdr", "/studio.hdr", "assets/studio.png", "assets\\studio.hdr"}) {
    invalid.asset = path;
    CHECK_FALSE(scene.setEnvironment(invalid));
    CHECK(scene.environment().value() == valid);
  }

  invalid = valid;
  invalid.intensity = std::numeric_limits<float>::infinity();
  CHECK_FALSE(scene.setEnvironment(invalid));
  invalid.intensity = -1;
  CHECK_FALSE(scene.setEnvironment(invalid));
  invalid.intensity = 65;
  CHECK_FALSE(scene.setEnvironment(invalid));
  invalid = valid;
  invalid.rotationDegrees = 361;
  CHECK_FALSE(scene.setEnvironment(invalid));
  invalid.rotationDegrees = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(scene.setEnvironment(invalid));
  invalid = valid;
  invalid.processing.diffuseSize = 3;
  CHECK_FALSE(scene.setEnvironment(invalid));
  invalid.processing = {16, 4, 16, 16, 1025};
  CHECK_FALSE(scene.setEnvironment(invalid));
  bool rejected{};
  std::thread worker(
      [&] { rejected = !scene.environment() && !scene.setEnvironment(std::nullopt); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(scene);
  CHECK_FALSE(scene.environment());
  CHECK_FALSE(scene.setEnvironment(valid));
  CHECK(moved.environment().value() == valid);
  REQUIRE(moved.setEnvironment(std::nullopt));
  CHECK_FALSE(moved.environment().value());
}

TEST_CASE("scene.occlusion_configuration_validates_retains_and_obeys_thread_move_lifetime") {
  auto scene = Scene::create("AO scene").value();
  CHECK_FALSE(scene.occlusion().value());
  Dot::Graphics::OcclusionSettings expected{0.75F, 0.025F, 2, 16, 8, 4};
  REQUIRE(scene.setOcclusion(expected));
  auto invalid = expected;
  invalid.radius = std::numeric_limits<float>::infinity();
  CHECK_FALSE(scene.setOcclusion(invalid));
  CHECK(scene.occlusion()->value() == expected);
  bool rejected{};
  std::thread worker([&] { rejected = !scene.occlusion() && !scene.setOcclusion(std::nullopt); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(scene);
  CHECK_FALSE(scene.occlusion());
  CHECK_FALSE(scene.setOcclusion(std::nullopt));
  CHECK(moved.occlusion()->value() == expected);
  REQUIRE(moved.setOcclusion(std::nullopt));
  CHECK_FALSE(moved.occlusion().value());
}

TEST_CASE("scene.local_transform_read_is_owned_scoped_and_rejects_removed_or_moved_state") {
  using namespace Dot;
  auto scene = World::Scene::create().value();
  auto entity = scene.createEntity().value();
  REQUIRE(scene.setTransform(entity, {{1, 2, 3}, {}, {2, 3, 4}}));
  auto pose = scene.transform(entity).value();
  CHECK(pose.position == Math::Vec3{1, 2, 3});
  pose.position.x = 8;
  CHECK(scene.transform(entity)->position.x == 1);
  CHECK_FALSE(scene.transform({entity.scene + 1, entity.value}));
  auto moved = std::move(scene);
  CHECK_FALSE(scene.transform(entity));
  CHECK(moved.transform(entity)->scale == Math::Vec3{2, 3, 4});
  REQUIRE(moved.destroyEntity(entity));
  CHECK_FALSE(moved.transform(entity));
}

TEST_CASE("scene.entity_allocator_reservations_are_monotonic_owned_and_can_be_sealed") {
  auto scene = Scene::create().value();
  CHECK(scene.nextEntityValue().value() == 1);
  const auto first = scene.createEntity().value();
  REQUIRE(scene.reserveEntityValuesBefore(50));
  CHECK(scene.nextEntityValue().value() == 50);
  CHECK_FALSE(scene.reserveEntityValuesBefore(49));
  CHECK_FALSE(scene.reserveEntityValuesBefore(0));
  CHECK_FALSE(scene.reserveEntityValuesBefore(std::numeric_limits<std::uint64_t>::max()));
  REQUIRE(scene.reserveEntityValuesBefore(50));
  REQUIRE(scene.destroyEntity(first));
  CHECK(scene.nextEntityValue().value() == 50);
  const auto reserved = scene.createEntity().value();
  CHECK(reserved.value == 50);
  bool rejected{};
  std::thread worker(
      [&] { rejected = !scene.nextEntityValue() && !scene.reserveEntityValuesBefore(60); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(scene);
  CHECK_FALSE(scene.nextEntityValue());
  CHECK_FALSE(scene.reserveEntityValuesBefore(60));
  CHECK(moved.nextEntityValue().value() == 51);
  REQUIRE(moved.reserveEntityValuesBefore(std::nullopt));
  CHECK_FALSE(moved.nextEntityValue().value());
  CHECK_FALSE(moved.createEntity());
  CHECK_FALSE(moved.reserveEntityValuesBefore(51));
  REQUIRE(moved.destroyEntity(reserved));
  CHECK_FALSE(moved.nextEntityValue().value());
}

TEST_CASE("scene.named_lookup_is_exact_owned_sorted_and_tracks_lifetime_changes") {
  auto scene = Scene::create().value();
  auto first = scene.createEntity("维护员🙂").value();
  auto other = scene.createEntity("维护员").value();
  auto second = scene.createEntity("维护员🙂").value();
  REQUIRE(scene.setParent(second, other));
  auto matches = scene.findEntities("维护员🙂");
  REQUIRE(matches);
  CHECK(*matches == std::vector<EntityId>{first, second});
  CHECK(scene.findEntities("missing")->empty());
  auto upper = scene.createEntity("Surveyor").value();
  CHECK(scene.findEntities("surveyor")->empty());
  CHECK(scene.findEntities("Surveyor").value() == std::vector<EntityId>{upper});
  REQUIRE(scene.renameEntity(first, "Changed"));
  CHECK(scene.findEntities("维护员🙂").value() == std::vector<EntityId>{second});
  REQUIRE(scene.destroyEntity(second));
  CHECK(scene.findEntities("维护员🙂")->empty());
  CHECK(*matches == std::vector<EntityId>{first, second});
  CHECK_FALSE(scene.entity((*matches)[1]));
  const std::string boundary(256, 'a');
  auto last = scene.createEntity(boundary).value();
  CHECK(scene.findEntities(boundary).value() == std::vector<EntityId>{last});
}

TEST_CASE("scene.named_lookup_validates_names_owner_and_moved_lifetime") {
  auto scene = Scene::create().value();

  for (const auto& name :
       {std::string{}, std::string(257, 'a'), std::string("A\0B", 3), std::string("\xff")}) {
    auto result = scene.findEntities(name);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Dot::Core::ErrorCode::InvalidArgument);
  }

  std::atomic<bool> rejected{};
  std::jthread foreign([&] {
    auto result = scene.findEntities("Entity");
    rejected = !result && result.error().code == Dot::Core::ErrorCode::InvalidState;
  });
  foreign.join();
  CHECK(rejected);
  auto moved = std::move(scene);
  CHECK_FALSE(scene.findEntities("Entity"));
  CHECK(moved.findEntities("Entity")->empty());
}

TEST_CASE("scene.detached_envelopes_consume_once_and_invalidate_all_old_scopes") {
  auto scene = Scene::create("Sealed scene").value();
  const auto entity = scene.createEntity("Owned entity").value();
  REQUIRE(scene.setMeshRenderer(entity, MeshRenderer{}));
  std::jthread foreign([&] { CHECK_FALSE(scene.detach()); });
  foreign.join();
  CHECK(scene.entity(entity));
  auto detached = scene.detach();
  REQUIRE(detached);
  CHECK(scene.id() == 0);
  CHECK_FALSE(scene.entity(entity));
  CHECK_FALSE(scene.detach());
  auto moved = std::move(*detached);
  CHECK_FALSE(Scene::adopt(*detached));
  auto adopted = Scene::adopt(moved);
  REQUIRE(adopted);
  CHECK_FALSE(Scene::adopt(moved));
  CHECK(adopted->id() != entity.scene);
  CHECK_FALSE(adopted->entity(entity));
  const auto current = adopted->findEntity(entity.value).value();
  CHECK(adopted->entity(current)->mesh.has_value());
  CHECK(adopted->entity(current)->name == "Owned entity");
  std::jthread wrongReceiver([&] {
    CHECK_FALSE(adopted->name());
    CHECK_FALSE(adopted->detach());
  });
  wrongReceiver.join();
  CHECK(adopted->entity(current));
}

TEST_CASE("scene.detached_cpu_ownership_crosses_threads_with_hierarchy_and_allocator_intact") {
  auto scene = Scene::create("Transfer state").value();
  const auto parent = scene.createEntity("Parent").value();
  const auto child = scene.createEntity("Child").value();
  REQUIRE(scene.setParent(child, parent));
  REQUIRE(scene.setTransform(parent, {{5, 0, 0}}));
  REQUIRE(scene.setTransform(child, {{2, 0, 0}}));
  REQUIRE(scene.reserveEntityValuesBefore(100));
  CHECK(position(scene, child).x == doctest::Approx(7));
  auto envelope = scene.detach();
  REQUIRE(envelope);
  auto future = std::async(
      std::launch::async,
      [owned = std::move(*envelope), parent, child]() mutable -> Dot::Core::Result<DetachedScene> {
        auto received = Scene::adopt(owned);

        if (!received) {
          return std::unexpected(received.error());
        }

        CHECK_FALSE(received->entity(child));
        const EntityId currentChild{received->id(), child.value};
        const EntityId currentParent{received->id(), parent.value};
        CHECK(received->entity(currentChild)->parent == currentParent);
        CHECK(position(*received, currentChild).x == doctest::Approx(7));
        auto changed = received->setTransform(currentChild, {{3, 0, 0}});

        if (!changed) {
          return std::unexpected(changed.error());
        }

        CHECK(position(*received, currentChild).x == doctest::Approx(8));
        CHECK(received->createEntity("After handoff")->value == 100);
        return received->detach();
      });
  auto returned = future.get();
  REQUIRE(returned);
  auto adopted = Scene::adopt(*returned);
  REQUIRE(adopted);
  CHECK_FALSE(adopted->entity(child));
  CHECK(adopted->nextEntityValue()->value() == 101);
  CHECK(position(*adopted, {adopted->id(), child.value}).x == doctest::Approx(8));
  CHECK(adopted->entityCount().value() == 3);
}

TEST_CASE("scene.detached_empty_sealed_allocators_and_discard_are_backend_free") {
  auto empty = Scene::create("Empty sealed allocator").value();
  REQUIRE(empty.reserveEntityValuesBefore(std::nullopt));
  auto transfer = empty.detach();
  REQUIRE(transfer);
  auto adopted = Scene::adopt(*transfer);
  REQUIRE(adopted);
  CHECK(adopted->entityCount().value() == 0);
  CHECK_FALSE(adopted->nextEntityValue()->has_value());
  CHECK_FALSE(adopted->createEntity("Exhausted"));
  auto discarded = adopted->detach();
  REQUIRE(discarded);
  auto destruction = std::async(std::launch::async, [owned = std::move(*discarded)]() mutable {
    auto moved = std::move(owned);
    CHECK_FALSE(Scene::adopt(owned));
  });
  destruction.get();
}

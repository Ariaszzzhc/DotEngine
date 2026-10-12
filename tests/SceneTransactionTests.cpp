#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <optional>
#include <thread>
#include <vector>

import Dot.World.Scene;
import Dot.Core.Result;
import Dot.Math;

using namespace Dot::World;

TEST_CASE("scene.imported_ids_are_monotonic_and_do_not_wrap") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto first = scene->createEntityWithId("Imported", 50);
  REQUIRE(first);
  CHECK(first->value == 50);
  auto next = scene->createEntity();
  REQUIRE(next);
  CHECK(next->value == 51);
  CHECK_FALSE(scene->createEntityWithId("Old", 50));
  CHECK_FALSE(scene->createEntityWithId("Skipped", 1));
  CHECK_FALSE(scene->createEntityWithId("Zero", 0));
  CHECK_FALSE(scene->createEntityWithId("Overflow", std::numeric_limits<std::uint64_t>::max()));
  const auto maximum = std::numeric_limits<std::uint64_t>::max() - 1;
  auto last = scene->createEntityWithId("Last", maximum);
  REQUIRE(last);
  CHECK(last->value == maximum);
  CHECK_FALSE(scene->createEntity());
  REQUIRE(scene->destroyEntity(*last));
  CHECK_FALSE(scene->createEntityWithId("Consumed", maximum));
  CHECK(scene->entityCount().value() == 2);
}

TEST_CASE("scene.batch_parent_changes_validate_final_state_only") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto first = scene->createEntity();
  auto second = scene->createEntity();
  REQUIRE(first);
  REQUIRE(second);
  REQUIRE(scene->setParent(*second, *first));
  CHECK_FALSE(scene->setParent(*first, *second));
  const std::array changes{ParentUpdate{*first, *second}, ParentUpdate{*second, std::nullopt}};
  REQUIRE(scene->setParents(changes));
  CHECK(scene->entity(*first)->parent.value() == *second);
  CHECK_FALSE(scene->entity(*second)->parent);
  const std::array cycle{ParentUpdate{*first, *second}, ParentUpdate{*second, *first}};
  CHECK_FALSE(scene->setParents(cycle));
  CHECK(scene->entity(*first)->parent.value() == *second);
  CHECK_FALSE(scene->entity(*second)->parent);
}

TEST_CASE("scene.batch_transforms_commit_all_or_rollback_all") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto parent = scene->createEntity();
  auto child = scene->createEntity();
  REQUIRE(parent);
  REQUIRE(child);
  REQUIRE(scene->setParent(*child, *parent));
  const std::array changes{TransformUpdate{*parent, {{10, 0, 0}, {}, {2, 2, 2}}},
                           TransformUpdate{*child, {{3, 0, 0}}}};
  REQUIRE(scene->setTransforms(changes));
  auto matrix = scene->worldTransform(*child);
  REQUIRE(matrix);
  CHECK(matrix->data[12] == 16);
  const auto maximum = std::numeric_limits<float>::max();
  const std::array overflow{TransformUpdate{*parent, {{20, 0, 0}, {}, {maximum, 1, 1}}},
                            TransformUpdate{*child, {{}, {}, {2, 1, 1}}}};
  CHECK_FALSE(scene->setTransforms(overflow));
  CHECK(scene->entity(*parent)->transform.position.x == 10);
  CHECK(scene->entity(*child)->transform.position.x == 3);
  CHECK(scene->worldTransform(*child)->data[12] == 16);
  auto invalid = changes;
  invalid[1].transform.scale.x = 0;
  invalid[0].transform.position.x = 30;
  CHECK_FALSE(scene->setTransforms(invalid));
  CHECK(scene->entity(*parent)->transform.position.x == 10);
}

TEST_CASE("scene.batch_rejects_duplicate_foreign_and_stale_handles") {
  auto scene = Scene::create();
  auto other = Scene::create();
  REQUIRE(scene);
  REQUIRE(other);
  auto first = scene->createEntity();
  auto second = scene->createEntity();
  auto foreign = other->createEntity();
  REQUIRE(first);
  REQUIRE(second);
  REQUIRE(foreign);
  const std::array duplicateTransforms{TransformUpdate{*first, {}}, TransformUpdate{*first, {}}};
  const std::array duplicateParents{ParentUpdate{*first, std::nullopt},
                                    ParentUpdate{*first, *second}};
  CHECK_FALSE(scene->setTransforms(duplicateTransforms));
  CHECK_FALSE(scene->setParents(duplicateParents));
  const std::array foreignTransforms{TransformUpdate{*first, {{1, 0, 0}}},
                                     TransformUpdate{*foreign, {}}};
  const std::array foreignParents{ParentUpdate{*first, *foreign}};
  CHECK_FALSE(scene->setTransforms(foreignTransforms));
  CHECK_FALSE(scene->setParents(foreignParents));
  REQUIRE(scene->destroyEntity(*second));
  const std::array staleTransforms{TransformUpdate{*first, {{1, 0, 0}}},
                                   TransformUpdate{*second, {}}};
  const std::array staleParents{ParentUpdate{*first, *second}};
  CHECK_FALSE(scene->setTransforms(staleTransforms));
  CHECK_FALSE(scene->setParents(staleParents));
  CHECK(scene->entity(*first)->transform.position.x == 0);
  CHECK_FALSE(scene->entity(*first)->parent);
}

TEST_CASE("scene.batch_limits_and_empty_transactions") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  CHECK(scene->setTransforms({}));
  CHECK(scene->setParents({}));
  std::vector<TransformUpdate> transforms(10001, {*entity, {}});
  std::vector<ParentUpdate> parents(10001, {*entity, std::nullopt});
  CHECK_FALSE(scene->setTransforms(transforms));
  CHECK_FALSE(scene->setParents(parents));
  bool rejected = false;
  std::thread worker([&] {
    rejected = !scene->setTransforms({}) && !scene->setParents({})
               && !scene->createEntityWithId("Worker", 100);
  });
  worker.join();
  CHECK(rejected);
}

TEST_CASE("scene.batch_rejects_camera_pose_without_partial_changes") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto camera = scene->createEntity();
  auto object = scene->createEntity();
  REQUIRE(camera);
  REQUIRE(object);
  REQUIRE(scene->setCamera(*camera, Camera{}));
  const auto tiny = std::numeric_limits<float>::denorm_min();
  const std::array changes{TransformUpdate{*object, {{1, 2, 3}}},
                           TransformUpdate{*camera, {{}, {}, {tiny, 1, 1}}}};
  CHECK_FALSE(scene->setTransforms(changes));
  CHECK(scene->entity(*object)->transform.position == Dot::Math::Vec3{});
  CHECK(scene->entity(*camera)->transform.scale.x == 1);
}

TEST_CASE("scene.large_batches_preserve_hierarchy_and_cached_world_values") {
  auto scene = Scene::create();
  REQUIRE(scene);
  std::vector<EntityId> entities;
  std::vector<TransformUpdate> transforms;
  std::vector<ParentUpdate> parents;

  for (int index = 0; index < 1000; ++index) {
    auto entity = scene->createEntity();
    REQUIRE(entity);
    entities.push_back(*entity);
    const float x = index == 0 ? 1.0F : static_cast<float>(index);
    transforms.push_back({*entity, {{x, 0, 0}}});

    if (index != 0) {
      parents.push_back({*entity, entities.front()});
    }
  }

  REQUIRE(scene->setTransforms(transforms));
  REQUIRE(scene->setParents(parents));
  CHECK(scene->worldTransform(entities.back())->data[12] == 1000);
  CHECK(scene->entity(entities.back())->parent.value() == entities.front());
}

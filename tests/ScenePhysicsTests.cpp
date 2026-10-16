#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <optional>
#include <thread>
#include <utility>

import Dot.Physics.SceneSimulation;
import Dot.Physics.World;
import Dot.Physics.Body;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Core.Result;
import Dot.Math;

using namespace Dot::World;
using Dot::Physics::Motion;
using Dot::Physics::SceneSimulation;
using Dot::Physics::Shape;

namespace {
  auto addBody(Scene& scene, Transform transform = {}, RigidBody description = {}) -> EntityId {
    auto entity = scene.createEntity("Body");
    REQUIRE(entity);
    REQUIRE(scene.setTransform(*entity, transform));
    REQUIRE(scene.setRigidBody(*entity, description));
    return *entity;
  }

  auto box(Motion motion = Motion::Dynamic) -> RigidBody {
    RigidBody description;
    description.shape = Shape::Box;
    description.motion = motion;
    return description;
  }

  auto worldPosition(const Scene& scene, EntityId entity) -> Dot::Math::Vec3 {
    auto matrix = scene.worldTransform(entity);
    REQUIRE(matrix);
    return {matrix->data[12], matrix->data[13], matrix->data[14]};
  }
}  // namespace

TEST_CASE("scene_physics.falling_box_writes_back_and_ray_identifies_entity") {
  auto scene = Scene::create();
  REQUIRE(scene);
  const auto floor = addBody(*scene, {{0, -0.25F, 0}, {}, {10, 0.5F, 10}}, box(Motion::Static));
  const auto body = addBody(*scene, {{0, 3, 0}}, box());
  auto simulation = SceneSimulation::create(*scene);
  REQUIRE_MESSAGE(simulation, (simulation ? "" : simulation.error().message));

  for (int i = 0; i < 300; ++i) {
    REQUIRE_MESSAGE(simulation->step(*scene, 1.0 / 60), "Falling box step failed");
  }

  const auto position = worldPosition(*scene, body);
  CHECK(position.y == doctest::Approx(0.5).epsilon(0.05));
  CHECK(position.y == doctest::Approx(simulation->body(body)->position.y));
  CHECK_FALSE(simulation->body(body)->active);
  CHECK(worldPosition(*scene, floor) == Dot::Math::Vec3{0, -0.25F, 0});
  auto ray = simulation->rayCast({0, 2, 0}, {0, -4, 0});
  REQUIRE(ray);
  REQUIRE(ray->has_value());
  CHECK((*ray)->entity == body);
  REQUIRE((*ray)->normal);
  CHECK(simulation->stats()->bodies == 2);
  CHECK_FALSE(simulation->stats()->faulted);
}

TEST_CASE("scene_physics.uniform_parent_and_reflected_local_scale") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto parent = scene->createEntity("Parent");
  REQUIRE(parent);
  auto rotation = Dot::Math::rotationEulerDegrees({0, 0, 30});
  REQUIRE(rotation);
  REQUIRE(scene->setTransform(*parent, {{3, 2, 0}, *rotation, {2, 2, 2}}));
  auto description = box();
  description.velocity = {0, 1, 0};
  const auto body = addBody(*scene, {{1, 0, 0}, {}, {1, -2, 3}}, description);
  REQUIRE(scene->setParent(body, *parent));
  const auto before = worldPosition(*scene, body);
  auto simulation = SceneSimulation::create(*scene, {8, {}, 0});
  REQUIRE(simulation);
  REQUIRE(simulation->setAngularVelocity(body, {0, 0.5F, 0}));

  for (int i = 0; i < 30; ++i) {
    auto step = simulation->step(*scene, 1.0 / 60);
    REQUIRE_MESSAGE(step, (step ? "" : step.error().message));
  }

  const auto after = worldPosition(*scene, body);
  CHECK(after.x == doctest::Approx(before.x));
  CHECK(after.y == doctest::Approx(before.y + 0.5F));
  CHECK(scene->entity(body)->parent == *parent);
  CHECK(scene->entity(body)->transform.scale == Dot::Math::Vec3{1, -2, 3});
  CHECK(simulation->body(body)->angularVelocity.y == doctest::Approx(0.5));
}

TEST_CASE("scene_physics.moving_physics_parent_and_child_keep_independent_world_poses") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto parentDescription = box();
  parentDescription.velocity = {1, 0, 0};
  const auto parent = addBody(*scene, {}, parentDescription);
  const auto child = addBody(*scene, {{0, 3, 0}}, box(Motion::Static));
  REQUIRE(scene->setParent(child, parent));
  auto simulation = SceneSimulation::create(*scene, {8, {}, 0});
  REQUIRE(simulation);
  REQUIRE(simulation->step(*scene, 0.25));
  CHECK(worldPosition(*scene, parent).x == doctest::Approx(0.25));
  CHECK(worldPosition(*scene, child).x == doctest::Approx(0));
  CHECK(scene->entity(child)->transform.position.x == doctest::Approx(-0.25));
  CHECK(worldPosition(*scene, child).y == doctest::Approx(3));
}

TEST_CASE("scene_physics.incremental_components_preserve_unmodified_motion_and_sleep") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto description = box();
  description.velocity = {1, 0, 0};
  const auto moving = addBody(*scene, {}, description);
  const auto sleeper = addBody(*scene, {{0, 5, 0}}, box());
  auto simulation = SceneSimulation::create(*scene, {3, {}, 0});
  REQUIRE(simulation);
  REQUIRE(simulation->setAngularVelocity(moving, {0, 1, 0}));

  for (int i = 0; i < 90; ++i) {
    REQUIRE(simulation->step(*scene, 1.0 / 60));
  }

  CHECK_FALSE(simulation->body(sleeper)->active);
  const auto added = addBody(*scene, {{0, 10, 0}}, box());
  REQUIRE(simulation->synchronize(*scene));
  CHECK(simulation->stats()->bodies == 3);
  CHECK_FALSE(simulation->body(sleeper)->active);
  CHECK(simulation->body(moving)->velocity.x == doctest::Approx(1));
  CHECK(simulation->body(moving)->angularVelocity.y == doctest::Approx(1));
  REQUIRE(scene->setRigidBody(added, std::nullopt));
  REQUIRE(scene->destroyEntity(sleeper));
  REQUIRE(simulation->synchronize(*scene));
  CHECK(simulation->stats()->bodies == 1);
  CHECK_FALSE(simulation->body(added));
  CHECK_FALSE(simulation->body(sleeper));
  REQUIRE(simulation->step(*scene, 0.1));
  CHECK(simulation->body(moving)->angularVelocity.y == doctest::Approx(1));
}

TEST_CASE("scene_physics.full_capacity_reconfigure_preserves_runtime_velocity") {
  auto scene = Scene::create();
  REQUIRE(scene);
  const auto entity = addBody(*scene, {}, box());
  auto simulation = SceneSimulation::create(*scene, {1, {}, 0});
  REQUIRE(simulation);
  REQUIRE(simulation->setVelocity(entity, {2, 0, 0}));
  REQUIRE(simulation->setAngularVelocity(entity, {0, 1, 0}));
  auto changed = *scene->entity(entity)->rigidBody;
  changed.mass = 2;
  changed.halfExtents = {1, 1, 1};
  REQUIRE(scene->setRigidBody(entity, changed));
  REQUIRE(simulation->synchronize(*scene));
  CHECK(simulation->body(entity)->velocity.x == doctest::Approx(2));
  CHECK(simulation->body(entity)->angularVelocity.y == doctest::Approx(1));
  REQUIRE(simulation->addImpulse(entity, {2, 0, 0}));
  CHECK(simulation->body(entity)->velocity.x == doctest::Approx(3));
  changed.velocity = {5, 0, 0};
  REQUIRE(scene->setRigidBody(entity, changed));
  REQUIRE(simulation->synchronize(*scene));
  CHECK(simulation->body(entity)->velocity.x == doctest::Approx(5));
  changed.motion = Motion::Static;
  changed.velocity = {};
  REQUIRE(scene->setRigidBody(entity, changed));
  REQUIRE(simulation->synchronize(*scene));
  CHECK(simulation->body(entity)->angularVelocity == Dot::Math::Vec3{});
  CHECK_FALSE(simulation->addImpulse(entity, {}));
}

TEST_CASE("scene_physics.authoring_pose_teleports_without_losing_velocity") {
  auto scene = Scene::create();
  REQUIRE(scene);
  const auto entity = addBody(*scene, {}, box());
  auto simulation = SceneSimulation::create(*scene, {1, {}, 0});
  REQUIRE(simulation);
  REQUIRE(simulation->setVelocity(entity, {1, 0, 0}));
  REQUIRE(scene->setTransform(entity, {{3, 4, 5}, {}, {2, 3, 4}}));
  REQUIRE(simulation->synchronize(*scene));
  CHECK(simulation->body(entity)->position == Dot::Math::Vec3{3, 4, 5});
  CHECK(simulation->body(entity)->velocity == Dot::Math::Vec3{1, 0, 0});
  auto ray = simulation->rayCast({3, 8, 5}, {0, -8, 0});
  REQUIRE(ray);
  REQUIRE(ray->has_value());
  CHECK((*ray)->point.y == doctest::Approx(5.5));
}

TEST_CASE("scene_physics.invalid_scaled_geometry_and_shear_preserve_bindings") {
  auto scene = Scene::create();
  REQUIRE(scene);
  const auto entity = addBody(*scene);
  auto simulation = SceneSimulation::create(*scene, {2, {}, 0});
  REQUIRE(simulation);
  REQUIRE(scene->setTransform(entity, {{}, {}, {2, 1, 1}}));
  CHECK_FALSE(simulation->synchronize(*scene));
  CHECK(simulation->body(entity)->position == Dot::Math::Vec3{});
  CHECK_FALSE(simulation->stats()->faulted);
  REQUIRE(scene->setTransform(entity, {{}, {}, {1, 1, 1}}));
  auto parent = scene->createEntity();
  REQUIRE(parent);
  REQUIRE(scene->setTransform(*parent, {{}, {}, {2, 1, 1}}));
  auto rotation = Dot::Math::rotationEulerDegrees({0, 0, 45});
  REQUIRE(rotation);
  REQUIRE(scene->setTransform(entity, {{}, *rotation}));
  REQUIRE(scene->setParent(entity, *parent));
  CHECK_FALSE(simulation->synchronize(*scene));
  CHECK_FALSE(simulation->stats()->faulted);
  REQUIRE(scene->setParent(entity, std::nullopt));
  REQUIRE(scene->setTransform(entity, {{}, {}, {0.0001F, 0.0001F, 0.0001F}}));
  CHECK_FALSE(simulation->synchronize(*scene));
  REQUIRE(scene->setTransform(entity, {{}, {}, {100000, 100000, 100000}}));
  CHECK_FALSE(simulation->synchronize(*scene));
  REQUIRE(scene->setTransform(entity, {{1'000'001, 0, 0}}));
  CHECK_FALSE(simulation->synchronize(*scene));
  CHECK(simulation->stats()->bodies == 1);
}

TEST_CASE("scene_physics.capacity_and_invalid_duration_do_not_apply_scene_edits") {
  auto scene = Scene::create();
  REQUIRE(scene);
  const auto first = addBody(*scene, {}, box());
  auto simulation = SceneSimulation::create(*scene, {1, {}, 0});
  REQUIRE(simulation);
  const auto second = addBody(*scene, {{0, 3, 0}}, box());
  CHECK_FALSE(simulation->synchronize(*scene));
  CHECK(simulation->stats()->bodies == 1);
  CHECK_FALSE(simulation->body(second));
  REQUIRE(scene->setTransform(first, {{1, 0, 0}}));

  for (const double seconds : {0.0, 0.250001, 1e-9, std::numeric_limits<double>::quiet_NaN()}) {
    CHECK_FALSE(simulation->step(*scene, seconds));
    CHECK(simulation->body(first)->position == Dot::Math::Vec3{});
  }

  REQUIRE(scene->setRigidBody(first, std::nullopt));
  REQUIRE(simulation->synchronize(*scene));
  CHECK(simulation->stats()->bodies == 1);
  CHECK_FALSE(simulation->body(first));
  REQUIRE(simulation->body(second));
}

TEST_CASE("scene_physics.kinematic_world_target_updates_local_pose") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto parent = scene->createEntity();
  REQUIRE(parent);
  REQUIRE(scene->setTransform(*parent, {{2, 0, 0}, {}, {2, 2, 2}}));
  const auto entity = addBody(*scene, {}, box(Motion::Kinematic));
  REQUIRE(scene->setParent(entity, *parent));
  auto simulation = SceneSimulation::create(*scene, {1, {}, 0});
  REQUIRE(simulation);
  REQUIRE(simulation->moveKinematic(entity, {3, 0, 0}, {}, 0.25));
  REQUIRE(simulation->step(*scene, 0.25));
  CHECK(worldPosition(*scene, entity).x == doctest::Approx(3));
  CHECK(scene->entity(entity)->transform.position.x == doctest::Approx(0.5));
  REQUIRE(simulation->moveKinematic(entity, {3, 0, 0}, {}, 0.25));
  REQUIRE(simulation->step(*scene, 0.25));
  CHECK(worldPosition(*scene, entity).x == doctest::Approx(3));
}

TEST_CASE("scene_physics.post_advance_unrepresentable_local_pose_faults_and_reset_recovers") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto parent = scene->createEntity();
  REQUIRE(parent);
  REQUIRE(scene->setTransform(*parent, {{}, {}, {2, 1, 1}}));
  const auto entity = addBody(*scene, {}, box());
  REQUIRE(scene->setParent(entity, *parent));
  auto simulation = SceneSimulation::create(*scene, {1, {}, 0});
  REQUIRE(simulation);
  REQUIRE(simulation->setAngularVelocity(entity, {0, 0, 1}));
  const auto before = scene->entity(entity)->transform;
  CHECK_FALSE(simulation->step(*scene, 0.1));
  CHECK(simulation->stats()->faulted);
  CHECK(scene->entity(entity)->transform.rotation == before.rotation);
  CHECK(simulation->body(entity)->rotation.z > 0.04F);
  CHECK_FALSE(simulation->step(*scene, 0.1));
  CHECK_FALSE(simulation->setVelocity(entity, {}));
  REQUIRE(scene->setTransform(*parent, {}));
  REQUIRE(simulation->reset(*scene));
  CHECK_FALSE(simulation->stats()->faulted);
  CHECK(simulation->body(entity)->angularVelocity == Dot::Math::Vec3{});
  REQUIRE(simulation->step(*scene, 0.1));
}

TEST_CASE("scene_physics.reset_failure_retains_motion_and_rebind_changes_scope") {
  auto scene = Scene::create();
  auto other = Scene::create();
  REQUIRE(scene);
  REQUIRE(other);
  const auto entity = addBody(*scene, {}, box());
  auto simulation = SceneSimulation::create(*scene, {1, {}, 0});
  REQUIRE(simulation);
  REQUIRE(simulation->setVelocity(entity, {3, 0, 0}));
  const auto invalid = addBody(*other, {{}, {}, {2, 1, 1}});
  CHECK_FALSE(simulation->reset(*other));
  CHECK(simulation->body(entity)->velocity.x == doctest::Approx(3));
  REQUIRE(other->setTransform(invalid, {}));
  REQUIRE(simulation->reset(*other));
  CHECK_FALSE(simulation->body(entity));
  REQUIRE(simulation->body(invalid));
  CHECK_FALSE(simulation->step(*scene, 0.1));
}

TEST_CASE("scene_physics.persistence_move_and_service_lifetime") {
  auto scene = Scene::create();
  REQUIRE(scene);
  const auto entity = addBody(*scene, {{0, 2, 0}}, box());
  auto encoded = encodeScene(*scene);
  REQUIRE(encoded);
  auto loaded = decodeScene(*encoded);
  REQUIRE(loaded);
  auto restored = loaded->findEntity(entity.value);
  REQUIRE(restored);
  auto simulation = SceneSimulation::create(*loaded, {1, {}, 0});
  REQUIRE(simulation);
  CHECK_FALSE(simulation->body(entity));
  Scene moved = std::move(*loaded);
  SceneSimulation service = std::move(*simulation);
  REQUIRE(service.step(moved, 0.1));
  CHECK_FALSE(simulation->stats());
  CHECK_FALSE(simulation->synchronize(moved));
  CHECK_FALSE(simulation->reset(moved));
  CHECK_FALSE(simulation->body(*restored));
  CHECK_FALSE(simulation->setVelocity(*restored, {}));
  CHECK_FALSE(simulation->setAngularVelocity(*restored, {}));
  CHECK_FALSE(simulation->addImpulse(*restored, {}));
  CHECK_FALSE(simulation->moveKinematic(*restored, {}, {}, 0.1));
  CHECK_FALSE(simulation->rayCast({}, {1, 0, 0}));
}

TEST_CASE("scene_physics.owner_thread_and_empty_scene") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto simulation = SceneSimulation::create(*scene);
  REQUIRE(simulation);
  REQUIRE(simulation->step(*scene, 0.1));
  CHECK(simulation->stats()->bodies == 0);
  int rejected = 0;
  std::thread worker([&] {
    rejected += !simulation->synchronize(*scene);
    rejected += !simulation->step(*scene, 0.1);
    rejected += !simulation->reset(*scene);
    rejected += !simulation->stats();
    rejected += !simulation->body({scene->id(), 1});
    rejected += !simulation->rayCast({}, {1, 0, 0});
    rejected += !simulation->setVelocity({scene->id(), 1}, {});
    rejected += !simulation->setAngularVelocity({scene->id(), 1}, {});
    rejected += !simulation->addImpulse({scene->id(), 1}, {});
    rejected += !simulation->moveKinematic({scene->id(), 1}, {}, {}, 0.1);
    rejected += !SceneSimulation::create(*scene);
  });
  worker.join();
  CHECK(rejected == 11);
}

TEST_CASE("scene_physics.imported_child_before_parent_has_ordered_writeback") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto child = scene->createEntityWithId("Child", 1);
  auto parent = scene->createEntityWithId("Parent", 10);
  REQUIRE(child);
  REQUIRE(parent);
  REQUIRE(scene->setTransform(*child, {{0, 3, 0}}));
  REQUIRE(scene->setParent(*child, *parent));
  auto moving = box();
  moving.velocity = {1, 0, 0};
  REQUIRE(scene->setRigidBody(*parent, moving));
  REQUIRE(scene->setRigidBody(*child, box(Motion::Static)));
  auto simulation = SceneSimulation::create(*scene, {2, {}, 0});
  REQUIRE(simulation);
  REQUIRE(simulation->step(*scene, 0.25));
  CHECK(worldPosition(*scene, *parent).x == doctest::Approx(0.25));
  CHECK(worldPosition(*scene, *child).x == doctest::Approx(0));
}

TEST_CASE("scene_physics.service_outlives_scene_without_borrowed_storage") {
  std::optional<SceneSimulation> simulation;
  EntityId entity;

  {
    auto scene = Scene::create();
    REQUIRE(scene);
    entity = addBody(*scene, {}, box());
    auto created = SceneSimulation::create(*scene);
    REQUIRE(created);
    simulation.emplace(std::move(*created));
  }

  REQUIRE(simulation->body(entity));
  CHECK(simulation->stats()->bodies == 1);
  auto replacement = Scene::create();
  REQUIRE(replacement);
  REQUIRE(simulation->reset(*replacement));
  CHECK(simulation->stats()->bodies == 0);
  CHECK_FALSE(simulation->body(entity));
}

TEST_CASE("scene_physics.backend_contact_capacity_fault_preserves_authoring_and_cleanup") {
  auto scene = Scene::create();
  REQUIRE(scene);
  EntityId first;

  for (int n = 0; n < 256; ++n) {
    auto entity = addBody(
        *scene, {{static_cast<float>(n % 16) * 0.005F, static_cast<float>(n / 16) * 0.005F, 0}});

    if (n == 0) {
      first = entity;
    }
  }

  auto simulation = SceneSimulation::create(*scene, {256, {}, 0});
  REQUIRE(simulation);
  const auto original = worldPosition(*scene, first);
  auto step = simulation->step(*scene, 1.0 / 60);
  REQUIRE_FALSE(step);
  CHECK(step.error().code == Dot::Core::ErrorCode::External);
  CHECK(simulation->stats()->faulted);
  CHECK(worldPosition(*scene, first) == original);
  CHECK_FALSE(simulation->synchronize(*scene));
  REQUIRE(simulation->body(first));

  for (const auto& entity : scene->entities().value()) {
    REQUIRE(scene->destroyEntity(entity.id));
  }

  REQUIRE(simulation->reset(*scene));
  CHECK(simulation->stats()->bodies == 0);
  CHECK_FALSE(simulation->stats()->faulted);
}

TEST_CASE("scene_physics.miss_invalid_ray_and_foreign_or_missing_entities") {
  auto scene = Scene::create();
  auto other = Scene::create();
  REQUIRE(scene);
  REQUIRE(other);
  const auto entity = addBody(*scene, {}, box());
  const auto foreign = addBody(*other, {}, box());
  auto simulation = SceneSimulation::create(*scene, {1, {}, 0});
  REQUIRE(simulation);
  auto miss = simulation->rayCast({5, 0, 0}, {1, 0, 0});
  REQUIRE(miss);
  CHECK_FALSE(miss->has_value());
  CHECK_FALSE(simulation->rayCast({}, {}));
  CHECK_FALSE(simulation->body(foreign));
  CHECK_FALSE(simulation->setVelocity(foreign, {}));
  CHECK_FALSE(simulation->setAngularVelocity(foreign, {}));
  CHECK_FALSE(simulation->addImpulse(foreign, {}));
  CHECK_FALSE(simulation->moveKinematic(foreign, {}, {}, 0.1));
  REQUIRE(scene->destroyEntity(entity));
  REQUIRE(simulation->synchronize(*scene));
  CHECK_FALSE(simulation->body(entity));
  CHECK_FALSE(simulation->addImpulse(entity, {}));
  CHECK_FALSE(simulation->setVelocity(entity, {}));
}

TEST_CASE("scene_physics.solver_limit_motion_survives_description_edits") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto description = box();
  description.velocity = {500, 0, 0};
  const auto entity = addBody(*scene, {}, description);
  auto simulation = SceneSimulation::create(*scene, {1, {0, -1000, 0}, 0});
  REQUIRE(simulation);
  REQUIRE(simulation->step(*scene, 0.1));
  const auto velocity = simulation->body(entity)->velocity;
  description.mass = 2;
  REQUIRE(scene->setRigidBody(entity, description));
  REQUIRE(simulation->synchronize(*scene));
  CHECK(simulation->body(entity)->velocity.x == doctest::Approx(velocity.x));
  CHECK(simulation->body(entity)->velocity.y == doctest::Approx(velocity.y));
}

TEST_CASE("scene_physics.nonphysical_camera_child_follows_simulated_parent") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto description = box();
  description.velocity = {1, 0, 0};
  const auto parent = addBody(*scene, {}, description);
  auto camera = scene->createEntity("Camera child");
  REQUIRE(camera);
  Camera component;
  component.primary = true;
  REQUIRE(scene->setCamera(*camera, component));
  REQUIRE(scene->setTransform(*camera, {{0, 0, 5}}));
  REQUIRE(scene->setParent(*camera, parent));
  auto simulation = SceneSimulation::create(*scene, {1, {}, 0});
  REQUIRE(simulation);
  REQUIRE(simulation->step(*scene, 0.25));
  CHECK(worldPosition(*scene, *camera).x == doctest::Approx(0.25));
  CHECK(worldPosition(*scene, *camera).z == doctest::Approx(5));
  CHECK(scene->entity(*camera)->transform.position == Dot::Math::Vec3{0, 0, 5});
  CHECK(scene->primaryCamera().value() == *camera);
  REQUIRE(scene->viewProjection(*camera, 1));
  CHECK(simulation->stats()->bodies == 1);
}

TEST_CASE("scene_physics.scaled_sphere_and_capsule_use_actual_geometry") {
  for (const auto shape : {Shape::Sphere, Shape::Capsule}) {
    auto scene = Scene::create();
    REQUIRE(scene);
    RigidBody description;
    description.shape = shape;
    const auto entity = addBody(*scene, {{}, {}, {-2, 2, -2}}, description);
    auto simulation = SceneSimulation::create(*scene, {1, {}, 0});
    REQUIRE(simulation);
    auto ray = simulation->rayCast({0, 3, 0}, {0, -6, 0});
    REQUIRE(ray);
    REQUIRE(ray->has_value());
    CHECK((*ray)->entity == entity);
    CHECK((*ray)->point.y == doctest::Approx(shape == Shape::Sphere ? 1 : 2));
  }
}

TEST_CASE("scene_physics_continuous_reconfiguration_preserves_motion_and_native_wall_collision") {
  auto scene = Scene::create().value();
  auto obstacle = box(Motion::Static);
  obstacle.halfExtents = {.01F, 2, 2};
  addBody(scene, {}, obstacle);
  RigidBody description;
  description.radius = .1F;
  const auto actor = addBody(scene, {{-2, 0, 0}}, description);
  auto simulation
      = SceneSimulation::create(scene, {.maxBodies = 2, .gravity = {}, .maxCharacters = 0}).value();
  REQUIRE(simulation.setVelocity(actor, {240, 0, 0}));
  REQUIRE(simulation.setAngularVelocity(actor, {0, 2, 0}));
  description.collisionDetection = Dot::Physics::CollisionDetection::Continuous;
  REQUIRE(scene.setRigidBody(actor, description));
  REQUIRE(simulation.synchronize(scene));
  CHECK(simulation.body(actor)->collisionDetection == Dot::Physics::CollisionDetection::Continuous);
  CHECK(simulation.body(actor)->velocity.x == 240);
  CHECK(simulation.body(actor)->angularVelocity.y == 2);
  REQUIRE(simulation.step(scene, 1.0 / 60));
  CHECK(scene.transform(actor)->position.x < -.09F);
  CHECK(scene.transform(actor)->position.x > -.13F);
  auto restored = decodeScene(encodeScene(scene).value()).value();
  const auto id = restored.findEntity(actor.value).value();
  REQUIRE(simulation.reset(restored));
  CHECK(simulation.body(id)->collisionDetection == Dot::Physics::CollisionDetection::Continuous);
  CHECK(simulation.body(id)->velocity == Dot::Math::Vec3{});
  CHECK_FALSE(simulation.stats()->faulted);
}

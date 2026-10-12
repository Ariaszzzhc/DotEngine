#include <doctest/doctest.h>

#include <algorithm>
#include <expected>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

import Dot.Script.PhysicsSource;
import Dot.Core.Result;
import Dot.World.Scene;
import Dot.Physics.Body;

namespace {
  using namespace Dot::Script;
  using Json = nlohmann::json;
  using EntityId = Dot::World::EntityId;

  auto scene() -> std::shared_ptr<Dot::World::Scene> {
    auto result = Dot::World::Scene::create("Physics Host");
    REQUIRE(result);
    return std::make_shared<Dot::World::Scene>(std::move(*result));
  }

  auto add(const std::shared_ptr<Dot::World::Scene>& scene,
           Dot::Physics::Motion motion = Dot::Physics::Motion::Dynamic, float y = 0) -> EntityId {
    auto entity = scene->createEntity().value();
    Dot::World::RigidBody body;
    body.motion = motion;
    body.shape = Dot::Physics::Shape::Box;
    REQUIRE(scene->setRigidBody(entity, body));
    REQUIRE(scene->setTransform(entity, {{0, y, 0}}));
    return entity;
  }

  auto source(const std::shared_ptr<Dot::World::Scene>& scene, std::size_t bytes = 1024 * 1024)
      -> PhysicsSource {
    PhysicsSourceConfig config;
    config.simulation.gravity = {};
    config.contactJsonBytes = bytes;
    auto result = PhysicsSource::create(scene, config);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return std::move(*result);
  }

  auto id(EntityId entity) -> Json {
    return {{"scene", std::to_string(entity.scene)}, {"value", std::to_string(entity.value)}};
  }

  auto xyz(float x = 0, float y = 0, float z = 0) -> Json {
    return {{"x", x}, {"y", y}, {"z", z}};
  }

  auto invoke(const HostBindings& services, std::string_view name, const Json& args = Json::array())
      -> Dot::Core::Result<std::optional<std::string>> {
    auto found = std::ranges::find(services, name, &HostFunction::name);
    REQUIRE(found != services.end());
    return found->callback(args.dump());
  }

  auto read(const HostBindings& services, std::string_view name, const Json& args = Json::array())
      -> Json {
    auto result = invoke(services, name, args);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    REQUIRE(result->has_value());
    return Json::parse(**result);
  }
}  // namespace

TEST_CASE("physics_host_real_velocity_impulse_angular_motion_and_owned_state_write_back") {
  auto native = scene();
  auto entity = add(native);
  auto physics = source(native);
  auto services = physics.bindings().value();
  CHECK(services.size() == 8);
  auto old = read(services, "physics.body", Json::array({id(entity)}));
  CHECK(old["motion"] == "dynamic");
  REQUIRE(invoke(services, "physics.setVelocity", Json::array({id(entity), xyz(1)})));
  REQUIRE(invoke(services, "physics.addImpulse", Json::array({id(entity), xyz(1)})));
  REQUIRE(invoke(services, "physics.setAngularVelocity", Json::array({id(entity), xyz(0, 1)})));
  auto state = read(services, "physics.body", Json::array({id(entity)}));
  CHECK(state["velocity"]["x"].get<float>() == doctest::Approx(2));
  CHECK(state["angularVelocity"]["y"].get<float>() == doctest::Approx(1));
  CHECK(old["velocity"]["x"] == 0);
  state["position"]["x"] = 999;
  REQUIRE(physics.step(0.1));
  CHECK(native->entity(entity)->transform.position.x == doctest::Approx(0.2));
  CHECK(read(services, "physics.body", Json::array({id(entity)}))["position"]["x"].get<float>()
        == doctest::Approx(0.2));
  CHECK(read(services, "physics.stats")["bodies"] == 1);
  CHECK_FALSE(read(services, "physics.stats")["faulted"].get<bool>());
}

TEST_CASE("physics_host_kinematic_targets_rays_inside_normals_and_misses_use_real_shapes") {
  auto native = scene();
  auto entity = add(native, Dot::Physics::Motion::Kinematic);
  auto physics = source(native);
  auto services = physics.bindings().value();
  auto ray = read(services, "physics.rayCast", Json::array({xyz(0, 2), xyz(0, -4)}));
  CHECK(ray["entity"] == id(entity));
  CHECK(ray["fraction"].get<float>() == doctest::Approx(0.375));
  CHECK(ray["normal"]["y"].get<float>() == doctest::Approx(1));
  auto inside = read(services, "physics.rayCast", Json::array({xyz(), xyz(0, 2)}));
  CHECK(inside["fraction"] == 0);
  CHECK(inside["normal"].is_null());
  CHECK(read(services, "physics.rayCast", Json::array({xyz(4), xyz(0, 2)})).is_null());
  auto rotation = Json{{"x", 0}, {"y", 0}, {"z", 0}, {"w", 1}};
  REQUIRE(invoke(services, "physics.moveKinematic",
                 Json::array({id(entity), xyz(0.1F), rotation, 0.1})));
  REQUIRE(physics.step(0.1));
  CHECK(native->entity(entity)->transform.position.x == doctest::Approx(0.1));
  CHECK_FALSE(invoke(services, "physics.addImpulse", Json::array({id(entity), xyz(1)})));
}

TEST_CASE("physics_host_invalid_shapes_limits_and_transport_preserve_runtime_motion") {
  auto native = scene();
  auto entity = add(native);
  auto physics = source(native);
  auto services = physics.bindings().value();
  const auto old = read(services, "physics.body", Json::array({id(entity)}));

  for (const auto& value : std::vector<Json>{nullptr, 0, xyz(501), Json{{"x", 0}, {"y", 0}},
                                             Json{{"x", 0}, {"y", 0}, {"z", 0}, {"w", 0}},
                                             Json{{"x", 1e100}, {"y", 0}, {"z", 0}},
                                             Json{{"x", true}, {"y", 0}, {"z", 0}}}) {
    auto result = invoke(services, "physics.setVelocity", Json::array({id(entity), value}));
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Dot::Core::ErrorCode::InvalidArgument);
    CHECK(read(services, "physics.body", Json::array({id(entity)})) == old);
  }

  CHECK_FALSE(invoke(services, "physics.setAngularVelocity", Json::array({id(entity), xyz(100)})));
  CHECK_FALSE(invoke(services, "physics.addImpulse", Json::array({id(entity), xyz(501)})));
  CHECK_FALSE(invoke(services, "physics.rayCast", Json::array({xyz(), xyz()})));
  CHECK_FALSE(invoke(services, "physics.stats", Json::array({0})));
  CHECK_FALSE(invoke(services, "physics.body", Json::array({id(entity), 0})));
  auto method = std::ranges::find(services, "physics.setVelocity", &HostFunction::name);
  REQUIRE(method != services.end());

  for (const auto& bytes : std::vector<std::string>{"{}", "[", std::string("[]\0", 3),
                                                    std::string(65537, ' '), "[1e309]"}) {
    CHECK_FALSE(method->callback(bytes));
  }

  CHECK_FALSE(method->callback("[" + id(entity).dump() + ",{\"x\":1,\"x\":2,\"y\":0,\"z\":0}]"));
  CHECK(read(services, "physics.body", Json::array({id(entity)})) == old);
  CHECK_FALSE(physics.step(0));
}

TEST_CASE("physics_host_missing_foreign_removed_owners_and_pending_ray_hits_fail_explicitly") {
  auto native = scene();
  auto entity = add(native);
  auto physics = source(native);
  auto services = physics.bindings().value();
  auto other = scene();
  auto foreign = add(other);
  CHECK_FALSE(invoke(services, "physics.body", Json::array({id(foreign)})));
  auto pending = add(native, Dot::Physics::Motion::Dynamic, 4);
  CHECK_FALSE(invoke(services, "physics.body", Json::array({id(pending)})));
  REQUIRE(physics.synchronize());
  CHECK(read(services, "physics.stats")["bodies"] == 2);
  REQUIRE(native->destroyEntity(entity));
  CHECK_FALSE(invoke(services, "physics.body", Json::array({id(entity)})));
  CHECK_FALSE(invoke(services, "physics.setVelocity", Json::array({id(entity), xyz(1)})));
  auto staleRay = invoke(services, "physics.rayCast", Json::array({xyz(0, 2), xyz(0, -4)}));
  REQUIRE_FALSE(staleRay);
  CHECK(staleRay.error().code == Dot::Core::ErrorCode::InvalidState);
  REQUIRE(physics.synchronize());
  CHECK(read(services, "physics.rayCast", Json::array({xyz(0, 2), xyz(0, -4)})).is_null());
}

TEST_CASE("physics_host_contacts_are_owned_non_consuming_and_preserve_removed_entity_ids") {
  auto native = scene();
  auto floor = add(native, Dot::Physics::Motion::Static);
  auto moving = add(native, Dot::Physics::Motion::Dynamic, 0.9F);
  auto physics = source(native);
  auto services = physics.bindings().value();
  REQUIRE(physics.step(1.0 / 60));
  auto batch = read(services, "physics.contacts");
  REQUIRE_FALSE(batch["events"].empty());
  CHECK(batch["generation"] == "1");
  CHECK(batch["events"][0]["phase"] == "begin");
  CHECK(batch["events"][0]["first"] == id(floor));
  CHECK(batch["events"][0]["second"] == id(moving));
  CHECK(batch["events"][0]["endReason"].is_null());
  CHECK_FALSE(batch["events"][0]["geometry"].is_null());
  CHECK(read(services, "physics.contacts") == batch);
  batch["events"].clear();
  CHECK_FALSE(read(services, "physics.contacts")["events"].empty());
  REQUIRE(native->destroyEntity(moving));
  REQUIRE(physics.synchronize());
  auto end = read(services, "physics.contacts");
  REQUIRE(end["events"].size() == 1);
  CHECK(end["events"][0]["phase"] == "end");
  CHECK(end["events"][0]["endReason"] == "destroyed");
  CHECK(end["events"][0]["second"] == id(moving));
  CHECK(end["events"][0]["geometry"].is_null());
  CHECK_FALSE(native->entity(moving));
}

TEST_CASE("physics_host_publication_budget_fault_is_explicit_and_reset_retains_callbacks") {
  CHECK_FALSE(PhysicsSource::create({}));
  auto native = scene();
  auto floor = add(native, Dot::Physics::Motion::Static);
  auto moving = add(native, Dot::Physics::Motion::Dynamic, 0.9F);
  PhysicsSourceConfig bad;
  bad.contactJsonBytes = 1;
  CHECK_FALSE(PhysicsSource::create(native, bad));
  bad.contactJsonBytes = 16 * 1024 * 1024 + 1;
  CHECK_FALSE(PhysicsSource::create(native, bad));
  auto physics = source(native, 128);
  auto services = physics.bindings().value();
  auto result = physics.step(1.0 / 60);
  REQUIRE_FALSE(result);
  CHECK(result.error().code == Dot::Core::ErrorCode::Unavailable);
  CHECK(result.error().message.find("after mutation") != std::string::npos);
  CHECK(read(services, "physics.stats")["faulted"] == true);
  CHECK_FALSE(invoke(services, "physics.contacts"));
  CHECK_FALSE(invoke(services, "physics.setVelocity", Json::array({id(moving), xyz(1)})));
  CHECK_FALSE(physics.step(0.1));
  CHECK(read(services, "physics.body", Json::array({id(moving)}))["motion"] == "dynamic");
  REQUIRE(native->destroyEntity(floor));
  REQUIRE(physics.reset());
  CHECK(read(services, "physics.stats")["generation"] == "2");
  CHECK_FALSE(read(services, "physics.stats")["faulted"].get<bool>());
  CHECK(read(services, "physics.contacts")["events"].empty());
  REQUIRE(physics.step(0.1));
}

TEST_CASE("physics_host_source_moves_lifetime_foreign_threads_and_moved_scene_are_checked") {
  HostBindings services;
  auto native = scene();
  auto entity = add(native);
  std::weak_ptr<Dot::World::Scene> weak = native;

  {
    auto physics = source(native);
    services = physics.bindings().value();
    auto moved = std::move(physics);
    CHECK_FALSE(physics.bindings());
    CHECK_FALSE(physics.synchronize());
    CHECK_FALSE(physics.step(0.1));
    CHECK_FALSE(physics.reset());
    bool rejected = false;
    std::thread worker([&] {
      rejected = !moved.bindings() && !moved.step(0.1)
                 && !invoke(services, "physics.body", Json::array({id(entity)}));
    });
    worker.join();
    CHECK(rejected);
    REQUIRE(moved.step(0.1));
  }

  native.reset();
  CHECK_FALSE(weak.expired());
  CHECK(read(services, "physics.stats")["bodies"] == 1);
  auto retained = weak.lock();
  auto movedScene = std::move(*retained);
  CHECK_FALSE(invoke(services, "physics.stats"));
  retained.reset();
  services.clear();
  CHECK(weak.expired());
  CHECK(movedScene.entityCount().value() == 1);
}

TEST_CASE("physics_host_failed_authoring_sync_and_reset_retain_motion_generation_and_batch") {
  auto native = scene();
  auto entity = add(native);
  auto physics = source(native);
  auto services = physics.bindings().value();
  REQUIRE(invoke(services, "physics.setVelocity", Json::array({id(entity), xyz(2)})));
  REQUIRE(physics.step(0.1));
  auto oldBody = read(services, "physics.body", Json::array({id(entity)}));
  auto oldBatch = read(services, "physics.contacts");
  auto description = native->entity(entity)->rigidBody.value();
  description.shape = Dot::Physics::Shape::Sphere;
  REQUIRE(native->setRigidBody(entity, description));
  auto transform = native->entity(entity)->transform;
  transform.scale = {1, 2, 1};
  REQUIRE(native->setTransform(entity, transform));
  CHECK_FALSE(physics.synchronize());
  CHECK_FALSE(physics.reset());
  CHECK(read(services, "physics.body", Json::array({id(entity)})) == oldBody);
  CHECK(read(services, "physics.contacts") == oldBatch);
  CHECK(read(services, "physics.stats")["generation"] == "1");
  CHECK_FALSE(read(services, "physics.stats")["faulted"].get<bool>());
  transform.scale = {1, 1, 1};
  REQUIRE(native->setTransform(entity, transform));
  REQUIRE(physics.synchronize());
  REQUIRE(physics.step(0.1));
  CHECK(native->entity(entity)->transform.position.x == doctest::Approx(0.4));
}

TEST_CASE("physics_host_advance_preserves_contacts_across_slices_in_one_batch") {
  auto native = scene();
  auto sensor = add(native, Dot::Physics::Motion::Static);
  auto description = native->entity(sensor)->rigidBody.value();
  description.sensor = true;
  REQUIRE(native->setRigidBody(sensor, description));
  auto moving = add(native);
  REQUIRE(native->setTransform(moving, {{-2, 0, 0}}));
  auto physics = source(native);
  auto services = physics.bindings().value();
  REQUIRE(invoke(services, "physics.setVelocity", Json::array({id(moving), xyz(8)})));
  REQUIRE(physics.advance(1));
  CHECK(native->entity(moving)->transform.position.x == doctest::Approx(6));
  auto batch = read(services, "physics.contacts");
  CHECK(batch["index"] == "2");
  REQUIRE(batch["events"].size() >= 2);
  CHECK(batch["events"][0]["phase"] == "begin");
  CHECK(batch["events"].back()["phase"] == "end");
  CHECK(batch["events"][0]["step"] != batch["events"].back()["step"]);
  CHECK(read(services, "physics.contacts") == batch);
  REQUIRE(physics.advance(0.000001));
  CHECK(read(services, "physics.contacts")["index"] == "3");
}

TEST_CASE("physics_host_advance_invalid_duration_preserves_scene_motion_and_batch") {
  auto native = scene();
  auto moving = add(native);
  auto physics = source(native);
  auto services = physics.bindings().value();
  REQUIRE(invoke(services, "physics.setVelocity", Json::array({id(moving), xyz(2)})));
  const auto body = read(services, "physics.body", Json::array({id(moving)}));
  const auto batch = read(services, "physics.contacts");

  for (auto seconds : {-1., 0., 0.0000001, 1.000001, std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
    CHECK_FALSE(physics.advance(seconds));
    CHECK(read(services, "physics.body", Json::array({id(moving)})) == body);
    CHECK(read(services, "physics.contacts") == batch);
  }

  REQUIRE(physics.advance(0.250001));
  CHECK(native->entity(moving)->transform.position.x == doctest::Approx(0.500002));
}

TEST_CASE("physics_host_advance_budget_fault_rejects_stale_batch_until_reset") {
  auto native = scene();
  auto floor = add(native, Dot::Physics::Motion::Static);
  add(native, Dot::Physics::Motion::Dynamic, 0.9F);
  auto physics = source(native, 128);
  auto services = physics.bindings().value();
  CHECK_FALSE(physics.advance(1));
  CHECK(read(services, "physics.stats")["faulted"] == true);
  CHECK_FALSE(invoke(services, "physics.contacts"));
  CHECK_FALSE(physics.advance(0.1));
  REQUIRE(native->destroyEntity(floor));
  REQUIRE(physics.reset());
  REQUIRE(physics.advance(1));
  auto moved = std::move(physics);
  CHECK_FALSE(physics.advance(0.1));
  bool rejected = false;
  std::thread worker([&] { rejected = !moved.advance(0.1); });
  worker.join();
  CHECK(rejected);
  CHECK(read(services, "physics.contacts")["generation"] == "2");
}

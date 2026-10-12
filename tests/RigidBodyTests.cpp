#include <doctest/doctest.h>

#include <atomic>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Physics.Body;
import Dot.Core.Result;
import Dot.Math;

using namespace Dot::World;
using Dot::Physics::Motion;
using Dot::Physics::Shape;

namespace {
  using Json = nlohmann::json;

  auto document() -> Json {
    auto scene = Scene::create("Physics scene");
    REQUIRE(scene);
    auto entity = scene->createEntity("Body");
    REQUIRE(entity);
    REQUIRE(scene->setRigidBody(*entity, RigidBody{}));
    auto encoded = encodeScene(*scene);
    REQUIRE(encoded);
    return Json::parse(*encoded);
  }
}  // namespace

TEST_CASE("rigid_body.description_owned_snapshots_and_removal") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  CHECK_FALSE(scene->entity(*entity)->rigidBody);
  RigidBody description;
  description.shape = Shape::Box;
  description.mass = 5;
  description.velocity = {1, 2, 3};
  description.sensor = true;
  REQUIRE(scene->setRigidBody(*entity, description));
  auto snapshot = scene->entity(*entity);
  REQUIRE(snapshot);
  REQUIRE(snapshot->rigidBody);
  CHECK(*snapshot->rigidBody == description);
  snapshot->rigidBody->mass = 10;
  CHECK(scene->entity(*entity)->rigidBody->mass == 5);
  auto entities = scene->entities();
  REQUIRE(entities);
  CHECK(entities->front().rigidBody == description);
  description.motion = Motion::Static;
  description.velocity = {};
  REQUIRE(scene->setRigidBody(*entity, description));
  CHECK(scene->entity(*entity)->rigidBody == description);
  REQUIRE(scene->setRigidBody(*entity, std::nullopt));
  CHECK_FALSE(scene->entity(*entity)->rigidBody);
  REQUIRE(scene->setRigidBody(*entity, std::nullopt));
  CHECK(scene->entityCount().value() == 1);
}

TEST_CASE("rigid_body.invalid_replacement_preserves_previous_properties") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  RigidBody previous;
  previous.mass = 3;
  REQUIRE(scene->setRigidBody(*entity, previous));
  std::vector<RigidBody> invalid;
  auto append = [&](auto edit) {
    auto description = previous;
    edit(description);
    invalid.push_back(description);
  };
  append([](auto& body) { body.motion = static_cast<Motion>(99); });
  append([](auto& body) { body.shape = static_cast<Shape>(99); });
  append([](auto& body) { body.mass = 0; });
  append([](auto& body) { body.radius = 0; });
  append([](auto& body) { body.halfHeight = 0; });
  append([](auto& body) { body.halfExtents.x = 0; });
  append([](auto& body) { body.friction = -1; });
  append([](auto& body) { body.restitution = 2; });
  append([](auto& body) { body.gravityFactor = 11; });
  append([](auto& body) { body.velocity = {501, 0, 0}; });
  append([](auto& body) { body.mass = std::numeric_limits<float>::quiet_NaN(); });
  append([](auto& body) {
    body.motion = Motion::Static;
    body.velocity = {0, 1, 0};
  });

  for (const auto& description : invalid) {
    auto changed = scene->setRigidBody(*entity, description);
    REQUIRE_FALSE(changed);
    CHECK(changed.error().code == Dot::Core::ErrorCode::InvalidArgument);
    CHECK(scene->entity(*entity)->rigidBody == previous);
  }
}

TEST_CASE("rigid_body.transform_hierarchy_and_render_components_are_independent") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto parent = scene->createEntity();
  auto child = scene->createEntity();
  REQUIRE(parent);
  REQUIRE(child);
  REQUIRE(scene->setParent(*child, *parent));
  REQUIRE(scene->setTransform(*parent, {{3, 4, 5}, {}, {2, 2, 2}}));
  REQUIRE(scene->setTransform(*child, {{1, 0, 0}}));
  REQUIRE(scene->setRigidBody(*child, RigidBody{}));
  REQUIRE(scene->setMeshRenderer(*child, MeshRenderer{}));
  Camera camera;
  camera.primary = true;
  REQUIRE(scene->setCamera(*child, camera));
  auto info = scene->entity(*child);
  REQUIRE(info);
  CHECK(info->parent == *parent);
  CHECK(info->transform.position == Dot::Math::Vec3{1, 0, 0});
  CHECK(scene->worldTransform(*child)->data[12] == 5);
  REQUIRE(scene->setRigidBody(*child, std::nullopt));
  CHECK(scene->entity(*child)->camera->primary);
  CHECK(scene->entity(*child)->mesh->kind == MeshKind::Cube);
  REQUIRE(scene->setRigidBody(*child, RigidBody{}));
  REQUIRE(scene->destroyEntity(*parent));
  CHECK_FALSE(scene->setRigidBody(*child, std::nullopt));
  CHECK(scene->entityCount().value() == 0);
  REQUIRE(scene->createEntity());
  CHECK_FALSE(scene->entities()->front().rigidBody);
}

TEST_CASE("rigid_body.handle_scope_move_and_owner_thread") {
  auto scene = Scene::create();
  auto other = Scene::create();
  REQUIRE(scene);
  REQUIRE(other);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  CHECK_FALSE(other->setRigidBody(*entity, RigidBody{}));
  REQUIRE(scene->setRigidBody(*entity, RigidBody{}));
  Scene moved = std::move(*scene);
  REQUIRE(moved.entity(*entity)->rigidBody);
  CHECK_FALSE(scene->setRigidBody(*entity, RigidBody{}));
  std::atomic<bool> rejected{};
  std::thread worker([&] { rejected = !moved.setRigidBody(*entity, std::nullopt); });
  worker.join();
  CHECK(rejected);
  REQUIRE(moved.entity(*entity)->rigidBody);
  REQUIRE(moved.destroyEntity(*entity));
  CHECK_FALSE(moved.setRigidBody(*entity, RigidBody{}));
}

TEST_CASE("rigid_body.files_round_trip_all_motion_and_shape_variants") {
  auto scene = Scene::create();
  REQUIRE(scene);

  for (const auto motion : {Motion::Static, Motion::Kinematic, Motion::Dynamic}) {
    for (const auto shape : {Shape::Box, Shape::Sphere, Shape::Capsule}) {
      auto entity = scene->createEntity();
      REQUIRE(entity);
      RigidBody body;
      body.motion = motion;
      body.shape = shape;
      body.halfExtents = {1, 2, 3};
      body.radius = 0.7F;
      body.halfHeight = 0.9F;
      body.mass = 3;
      body.friction = 0.3F;
      body.restitution = 0.5F;
      body.gravityFactor = 2;
      body.sensor = true;

      if (motion != Motion::Static) {
        body.velocity = {1, 2, 3};
      }

      REQUIRE(scene->setRigidBody(*entity, body));
    }
  }

  auto encoded = encodeScene(*scene);
  REQUIRE(encoded);
  auto decoded = decodeScene(*encoded);
  REQUIRE_MESSAGE(decoded, (decoded ? "" : decoded.error().message));
  CHECK(decoded->entityCount().value() == 9);
  auto before = scene->entities().value();
  auto after = decoded->entities().value();

  for (std::size_t i = 0; i < before.size(); ++i) {
    CHECK(before[i].rigidBody == after[i].rigidBody);
  }

  CHECK(encodeScene(*decoded).value() == *encoded);
}

TEST_CASE("rigid_body.files_absent_null_and_old_documents") {
  auto value = document();
  value["entities"][0].erase("rigidBody");
  auto decoded = decodeScene(value.dump());
  REQUIRE(decoded);
  CHECK_FALSE(decoded->entities()->front().rigidBody);
  value["entities"][0]["rigidBody"] = nullptr;
  decoded = decodeScene(value.dump());
  REQUIRE(decoded);
  CHECK_FALSE(decoded->entities()->front().rigidBody);
}

TEST_CASE("rigid_body.files_strict_fields_types_and_shape_names") {
  const auto value = document();
  const auto component = value["entities"][0]["rigidBody"];

  for (const auto& [key, field] : component.items()) {
    (void)field;
    auto invalid = value;
    invalid["entities"][0]["rigidBody"].erase(key);
    CHECK_FALSE(decodeScene(invalid.dump()));
  }

  auto invalid = value;
  invalid["entities"][0]["rigidBody"]["position"] = {0, 0, 0};
  CHECK_FALSE(decodeScene(invalid.dump()));

  for (const auto& name : {"", "triangle", "BOX", "body"}) {
    invalid = value;
    invalid["entities"][0]["rigidBody"]["shape"] = name;
    CHECK_FALSE(decodeScene(invalid.dump()));
    invalid = value;
    invalid["entities"][0]["rigidBody"]["motion"] = name;
    CHECK_FALSE(decodeScene(invalid.dump()));
  }

  for (const auto& wrong : {Json(1), Json("true"), Json::array(), Json(nullptr)}) {
    invalid = value;
    invalid["entities"][0]["rigidBody"]["sensor"] = wrong;
    CHECK_FALSE(decodeScene(invalid.dump()));
  }

  for (const auto& wrong : {Json(1), Json::array(), Json("body"), Json(true)}) {
    invalid = value;
    invalid["entities"][0]["rigidBody"] = wrong;
    CHECK_FALSE(decodeScene(invalid.dump()));
  }
}

TEST_CASE("rigid_body.files_numeric_ranges_and_vectors") {
  const auto value = document();

  for (const auto& [key, wrong] :
       std::vector<std::pair<const char*, Json>>{{"mass", 0},
                                                 {"mass", 1'000'001},
                                                 {"friction", -1},
                                                 {"restitution", 2},
                                                 {"gravityFactor", 11},
                                                 {"radius", 0},
                                                 {"halfHeight", 10001},
                                                 {"mass", "1"},
                                                 {"radius", true},
                                                 {"halfExtents", {1, 0, 1}},
                                                 {"halfExtents", {1, 2}},
                                                 {"velocity", {501, 0, 0}},
                                                 {"velocity", {1, 2, "3"}}}) {
    auto invalid = value;
    invalid["entities"][0]["rigidBody"][key] = wrong;
    CHECK_FALSE(decodeScene(invalid.dump()));
  }

  auto invalid = value;
  invalid["entities"][0]["rigidBody"]["motion"] = "static";
  invalid["entities"][0]["rigidBody"]["velocity"] = {0, 1, 0};
  CHECK_FALSE(decodeScene(invalid.dump()));
  invalid["entities"][0]["rigidBody"]["velocity"] = {0, 0, 0};
  REQUIRE(decodeScene(invalid.dump()));
}

TEST_CASE("rigid_body.failed_import_leaves_live_scene_independent") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  RigidBody body;
  body.mass = 5;
  REQUIRE(scene->setRigidBody(*entity, body));
  auto invalid = document();
  invalid["entities"][0]["rigidBody"]["mass"] = 0;

  for (int i = 0; i < 64; ++i) {
    CHECK_FALSE(decodeScene(invalid.dump()));
    CHECK(scene->entity(*entity)->rigidBody == body);
  }

  auto duplicated = document().dump();
  const auto marker = duplicated.find("\"mass\":1.0");
  REQUIRE(marker != std::string::npos);
  duplicated.replace(marker, 10, "\"mass\":1.0,\"mass\":2.0");
  CHECK_FALSE(decodeScene(duplicated));
}

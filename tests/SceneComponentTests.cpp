#include <doctest/doctest.h>

#include <limits>
#include <optional>
#include <string>

import Dot.World.Scene;
import Dot.Core.Result;
import Dot.Math;

using namespace Dot::World;

TEST_CASE("scene.camera_projection_and_primary_lifetime") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity("Camera");
  REQUIRE(entity);
  CHECK_FALSE(scene->primaryCamera());
  CHECK_FALSE(scene->viewProjection(*entity, 1));
  Camera camera;
  camera.nearPlane = 1;
  camera.farPlane = 10;
  camera.primary = true;
  REQUIRE(scene->setTransform(*entity, {{0, 0, 5}}));
  REQUIRE(scene->setCamera(*entity, camera));
  CHECK(scene->primaryCamera().value() == *entity);
  auto matrix = scene->viewProjection(*entity, 1);
  REQUIRE(matrix);
  auto nearPoint = Dot::Math::transformPoint(*matrix, {0, 0, 4});
  auto farPoint = Dot::Math::transformPoint(*matrix, {0, 0, -5});
  REQUIRE(nearPoint);
  REQUIRE(farPoint);
  CHECK(nearPoint->z == doctest::Approx(0).epsilon(0.00001));
  CHECK(farPoint->z == doctest::Approx(1).epsilon(0.00001));
  camera.projection = Projection::Orthographic;
  camera.orthographicHeight = 2;
  REQUIRE(scene->setCamera(*entity, camera));
  matrix = scene->viewProjection(*entity, 2);
  REQUIRE(matrix);
  auto edge = Dot::Math::transformPoint(*matrix, {2, 1, 4});
  REQUIRE(edge);
  CHECK(edge->x == doctest::Approx(1));
  CHECK(edge->y == doctest::Approx(1));
  CHECK(edge->z == doctest::Approx(0).epsilon(0.00001));
  REQUIRE(scene->setCamera(*entity, std::nullopt));
  CHECK_FALSE(scene->entity(*entity)->camera);
  CHECK_FALSE(scene->primaryCamera());
  CHECK_FALSE(scene->viewProjection(*entity, 1));
}

TEST_CASE("scene.primary_camera_uniqueness_and_owned_snapshot") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto first = scene->createEntity();
  auto second = scene->createEntity();
  REQUIRE(first);
  REQUIRE(second);
  Camera camera;
  camera.primary = true;
  REQUIRE(scene->setCamera(*first, camera));
  CHECK_FALSE(scene->setCamera(*second, camera));
  CHECK_FALSE(scene->entity(*second)->camera);
  auto snapshot = scene->entity(*first);
  REQUIRE(snapshot);
  snapshot->camera->primary = false;
  CHECK(scene->primaryCamera().value() == *first);
  REQUIRE(scene->destroyEntity(*first));
  CHECK_FALSE(scene->primaryCamera());
  REQUIRE(scene->setCamera(*second, camera));
  CHECK(scene->primaryCamera().value() == *second);
}

TEST_CASE("scene.camera_invalid_parameters_preserve_existing_component") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  REQUIRE(scene->setCamera(*entity, Camera{}));
  Camera invalid;
  invalid.verticalFovDegrees = 180;
  CHECK_FALSE(scene->setCamera(*entity, invalid));
  invalid = {};
  invalid.orthographicHeight = 0;
  CHECK_FALSE(scene->setCamera(*entity, invalid));
  invalid = {};
  invalid.nearPlane = 0;
  CHECK_FALSE(scene->setCamera(*entity, invalid));
  invalid = {};
  invalid.farPlane = invalid.nearPlane;
  CHECK_FALSE(scene->setCamera(*entity, invalid));
  invalid = {};
  invalid.projection = static_cast<Projection>(999);
  CHECK_FALSE(scene->setCamera(*entity, invalid));
  CHECK(scene->entity(*entity)->camera->verticalFovDegrees == 60);
  CHECK_FALSE(scene->viewProjection(*entity, 0));
  CHECK_FALSE(scene->viewProjection(*entity, std::numeric_limits<float>::infinity()));
  CHECK(scene->viewProjection(*entity, 1));
}

TEST_CASE("scene.camera_pose_and_ancestor_changes_are_transactional") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto parent = scene->createEntity();
  auto camera = scene->createEntity();
  auto tiny = scene->createEntity();
  REQUIRE(parent);
  REQUIRE(camera);
  REQUIRE(tiny);
  REQUIRE(scene->setParent(*camera, *parent));
  REQUIRE(scene->setCamera(*camera, Camera{}));
  const auto scale = std::numeric_limits<float>::denorm_min();
  CHECK_FALSE(scene->setTransform(*camera, {{}, {}, {scale, 1, 1}}));
  CHECK_FALSE(scene->setTransform(*parent, {{}, {}, {scale, 1, 1}}));
  REQUIRE(scene->setTransform(*tiny, {{}, {}, {scale, 1, 1}}));
  CHECK_FALSE(scene->setParent(*camera, *tiny));
  CHECK(scene->entity(*camera)->parent.value() == *parent);
  CHECK(scene->entity(*camera)->transform.scale.x == 1);
  CHECK(scene->entity(*parent)->transform.scale.x == 1);
  CHECK_FALSE(scene->setCamera(*tiny, Camera{}));
  CHECK_FALSE(scene->entity(*tiny)->camera);
  CHECK(scene->viewProjection(*camera, 1));
}

TEST_CASE("scene.mesh_components_snapshots_removal_and_asset_paths") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  REQUIRE(scene->setMeshRenderer(*entity, MeshRenderer{}));
  auto snapshot = scene->entity(*entity);
  REQUIRE(snapshot);
  REQUIRE(snapshot->mesh);
  CHECK(snapshot->mesh->kind == MeshKind::Cube);
  snapshot->mesh->visible = false;
  CHECK(scene->entity(*entity)->mesh->visible);
  MeshRenderer asset;
  asset.kind = MeshKind::Asset;
  asset.asset = "模型/geometry.OBJ";
  asset.tint = {0.2F, 0.4F, 0.6F, 1};
  REQUIRE(scene->setMeshRenderer(*entity, asset));
  CHECK(scene->entity(*entity)->mesh->asset == asset.asset);
  REQUIRE(scene->setMeshRenderer(*entity, std::nullopt));
  CHECK_FALSE(scene->entity(*entity)->mesh);
  REQUIRE(scene->setMeshRenderer(*entity, std::nullopt));
}

TEST_CASE("scene.mesh_invalid_descriptions_are_transactional") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  REQUIRE(scene->setMeshRenderer(*entity, MeshRenderer{}));
  MeshRenderer invalid;
  invalid.tint.x = -1;
  CHECK_FALSE(scene->setMeshRenderer(*entity, invalid));
  invalid = {};
  invalid.asset = "ignored.dotmesh";
  CHECK_FALSE(scene->setMeshRenderer(*entity, invalid));
  invalid = {};
  invalid.kind = static_cast<MeshKind>(999);
  CHECK_FALSE(scene->setMeshRenderer(*entity, invalid));
  invalid.kind = MeshKind::Asset;

  for (const auto& path :
       {std::string{}, std::string{"../outside.obj"}, std::string{"folder/../outside.obj"},
        std::string{"/absolute.obj"}, std::string{"C:/drive.obj"}, std::string{"folder\\cube.obj"},
        std::string{"cube.png"}, std::string{"bad\0.obj", 8}, std::string{"\xFF.obj"},
        std::string(4097, 'x')}) {
    invalid.asset = path;
    CHECK_FALSE(scene->setMeshRenderer(*entity, invalid));
  }

  CHECK(scene->entity(*entity)->mesh->kind == MeshKind::Cube);
  CHECK(scene->entity(*entity)->mesh->asset.empty());
}

TEST_CASE("scene_material_and_light_authoring_are_owned_validated_and_transactional") {
  auto scene = Scene::create().value();
  const auto entity = scene.createEntity().value();
  MaterialBinding binding{"assets/steel.dotmaterial"};
  REQUIRE(scene.setMaterial(entity, binding));
  binding.asset = "changed.dotmaterial";
  CHECK(scene.entity(entity)->material->asset == "assets/steel.dotmaterial");
  CHECK_FALSE(scene.setMaterial(entity, MaterialBinding{"../steel.dotmaterial"}));
  CHECK_FALSE(scene.setMaterial(entity, MaterialBinding{"assets/steel.json"}));
  CHECK(scene.entity(entity)->material->asset == "assets/steel.dotmaterial");
  Light source;
  source.kind = LightKind::Spot;
  source.intensity = 40;
  REQUIRE(scene.setLight(entity, source));
  source.intensity = -1;
  CHECK_FALSE(scene.setLight(entity, source));
  CHECK(scene.entity(entity)->light->intensity == 40);
  source.intensity = 40;
  source.outerConeDegrees = source.innerConeDegrees;
  CHECK_FALSE(scene.setLight(entity, source));
  REQUIRE(scene.setLight(entity, std::nullopt));
  REQUIRE(scene.setLight(entity, std::nullopt));
  REQUIRE(scene.setMaterial(entity, std::nullopt));
  CHECK_FALSE(scene.entity(entity)->light);
  CHECK_FALSE(scene.entity(entity)->material);
  auto other = Scene::create().value();
  CHECK_FALSE(other.setLight(entity, Light{}));
  CHECK_FALSE(other.setMaterial(entity, binding));
  REQUIRE(scene.destroyEntity(entity));
  CHECK_FALSE(scene.setLight(entity, Light{}));
}

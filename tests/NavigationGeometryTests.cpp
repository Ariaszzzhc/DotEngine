#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

import Dot.Navigation.Geometry;
import Dot.Navigation.Files;
import Dot.Editor.NavigationBake;
import Dot.World.Scene;
import Dot.Physics.Body;
import Dot.Math;

namespace {
  using namespace Dot::Navigation;
  using namespace Dot::World;
  using namespace Dot::Physics;
  using Dot::Math::Vec3;

  auto collider(Scene& scene, Shape shape, Transform pose = {}, Motion motion = Motion::Static,
                bool sensor = false) -> EntityId {
    const auto id = scene.createEntity("Collider");
    REQUIRE(id);
    REQUIRE(scene.setTransform(*id, pose));
    RigidBody body;
    body.shape = shape;
    body.motion = motion;
    body.sensor = sensor;
    REQUIRE(scene.setRigidBody(*id, body));
    return *id;
  }

  auto room() -> Scene {
    auto scene = Scene::create();
    REQUIRE(scene);
    collider(*scene, Shape::Box, {{0, -.25F, 0}, {}, {12, .5F, 12}});
    collider(*scene, Shape::Box, {{0, 1.5F, 0}, {}, {1.5F, 3, 1.5F}});
    return std::move(*scene);
  }

  auto built() -> Surface {
    auto scene = room();
    const auto geometry = sceneGeometry(scene, {});
    REQUIRE(geometry);
    const auto result = Surface::build(*geometry, {});
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return *result;
  }
}  // namespace

TEST_CASE("navigation.scene_geometry_uses_native_static_colliders_and_world_transforms") {
  auto scene = room();
  auto geometry = sceneGeometry(scene, {});
  REQUIRE(geometry);
  CHECK(geometry->vertices.size() == 16);
  const auto identity = geometryIdentity(*geometry);
  REQUIRE(identity);
  const auto moving = collider(scene, Shape::Box, {{0, 2, 0}}, Motion::Dynamic);
  collider(scene, Shape::Box, {{0, 4, 0}}, Motion::Kinematic);
  collider(scene, Shape::Box, {{0, 6, 0}}, Motion::Static, true);
  const auto character = scene.createEntity("Character");
  REQUIRE(character);
  REQUIRE(scene.setCharacterController(*character, CharacterController{}));
  CHECK(geometryIdentity(sceneGeometry(scene, {}).value()).value() == *identity);
  REQUIRE(scene.setTransform(moving, {{200, 200, 200}}));
  CHECK(geometryIdentity(sceneGeometry(scene, {}).value()).value() == *identity);
  const auto surface = Surface::build(*geometry, {});
  REQUIRE(surface);
  REQUIRE(surface->route({{-4, 0, 0}, {4, 0, 0}}));
  const auto parent = scene.createEntity("Parent");
  REQUIRE(parent);
  REQUIRE(scene.setTransform(*parent, {{20, 0, 0}}));
  const auto nested = collider(scene, Shape::Box);
  REQUIRE(scene.setParent(nested, *parent));
  geometry = sceneGeometry(scene, {});
  REQUIRE(geometry);
  CHECK(geometry->vertices.back().x == doctest::Approx(19.5F));
  std::stop_source stop;
  stop.request_stop();
  CHECK_FALSE(sceneGeometry(scene, {}, {}, stop.get_token()));
  auto empty = Scene::create();
  REQUIRE(empty);
  CHECK_FALSE(sceneGeometry(*empty, {}));
}

TEST_CASE("navigation.round_primitives_are_conservative_with_bounded_error_and_scaling") {
  for (const auto shape : {Shape::Sphere, Shape::Capsule}) {
    auto scene = Scene::create();
    REQUIRE(scene);
    const auto id = collider(*scene, shape);
    const auto geometry = sceneGeometry(*scene, {});
    REQUIRE_MESSAGE(geometry, (geometry ? "" : geometry.error().message));
    const float halfHeight = shape == Shape::Capsule ? .5F : 0;

    for (const auto point : geometry->vertices) {
      const auto center = point.y >= 0 ? halfHeight : -halfHeight;
      const auto distance = Dot::Math::length(point - Vec3{0, center, 0});
      CHECK(distance >= .5 - 1e-6);
      CHECK(distance <= .5 + .005 + 1e-6);
    }

    for (std::size_t index = 0; index < geometry->indices.size(); index += 3) {
      const auto a = geometry->vertices[geometry->indices[index]];
      const auto b = geometry->vertices[geometry->indices[index + 1]];
      const auto c = geometry->vertices[geometry->indices[index + 2]];
      const auto u = b - a, v = c - a;
      const auto normal = Dot::Math::normalized(
          Vec3{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x});
      REQUIRE(normal);
      CHECK(Dot::Math::dot(*normal, a) >= .5 + halfHeight * std::abs(normal->y) - 1e-6);
    }

    REQUIRE(scene->setTransform(id, {{}, {}, {1, 2, 1}}));
    CHECK_FALSE(sceneGeometry(*scene, {}));
    REQUIRE(scene->setTransform(id, {{}, {}, {-2, 2, 2}}));
    REQUIRE(sceneGeometry(*scene, {}));
    REQUIRE(scene->setTransform(id, {{}, {}, {10000, 10000, 10000}}));
    CHECK_FALSE(sceneGeometry(*scene, {}));
  }

  BodyProperties body;
  Dot::Math::TransformParts pose;
  pose.scale.y = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(scaledBodyConfig(body, pose));
}

TEST_CASE("navigation.mesh_geometry_preserves_reflected_front_faces_without_substitution") {
  auto scene = Scene::create();
  REQUIRE(scene);
  const auto id = scene->createEntity("Mesh floor");
  REQUIRE(id);
  RigidBody body;
  body.motion = Motion::Static;
  body.shape = Shape::Mesh;
  body.meshAsset = "assets/floor.dotmesh";
  REQUIRE(scene->setRigidBody(*id, body));
  REQUIRE(scene->setTransform(*id, {{3, 0, 0}, {}, {-2, 1, 1}}));
  const auto mesh = TriangleMesh::create(
      {{{-3, 0, -3}, {3, 0, -3}, {3, 0, 3}, {-3, 0, 3}}, {0, 2, 1, 0, 3, 2}});
  REQUIRE(mesh);
  std::vector<MeshAsset> assets{{*body.meshAsset, *mesh}};
  CHECK_FALSE(sceneGeometry(*scene, {}));
  auto geometry = sceneGeometry(*scene, {}, assets);
  REQUIRE(geometry);
  CHECK(geometry->vertices.size() == 4);
  CHECK(geometry->vertices.front() == Vec3{9, 0, -3});
  CHECK(geometry->indices == std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
  const auto surface = Surface::build(*geometry, {});
  REQUIRE(surface);
  REQUIRE(surface->route({{0, 0, 0}, {6, 0, 0}}));
  assets.push_back(assets.front());
  CHECK_FALSE(sceneGeometry(*scene, {}, assets));
  assets.pop_back();
  assets[0].value = {};
  CHECK_FALSE(sceneGeometry(*scene, {}, assets));
}

TEST_CASE("navigation.binary_resource_round_trips_owned_surface_and_rejects_malformed_bytes") {
  const auto original = built();
  const auto encoded = encodeSurface(original);
  REQUIRE(encoded);
  CHECK((*encoded)[0] == std::byte{'D'});
  CHECK((*encoded)[7] == std::byte{1});
  const auto loaded = decodeSurface(*encoded);
  REQUIRE_MESSAGE(loaded, (loaded ? "" : loaded.error().message));
  CHECK(loaded->description().value()->source == original.description().value()->source);
  CHECK(loaded->stats()->polygons == original.stats()->polygons);
  const auto route = loaded->route({{-4, 0, 0}, {4, 0, 0}});
  REQUIRE(route);
  CHECK(route->points == original.route({{-4, 0, 0}, {4, 0, 0}})->points);
  CHECK(encodeSurface(*loaded).value() == *encoded);

  for (const auto length : {0U, 7U, 8U, 35U, 48U, 79U, 84U, 120U}) {
    REQUIRE(encoded->size() > length);
    CHECK_FALSE(decodeSurface(std::span(*encoded).first(length)));
  }

  CHECK_FALSE(decodeSurface(std::span(*encoded).first(encoded->size() - 1)));
  auto bad = *encoded;
  bad[7] = std::byte{2};
  CHECK_FALSE(decodeSurface(bad));
  bad = *encoded;
  bad.push_back(std::byte{0});
  CHECK_FALSE(decodeSurface(bad));
  bad = *encoded;

  for (const auto offset : {80U, 81U, 82U, 83U}) {
    bad[offset] = std::byte{255};
  }

  CHECK_FALSE(decodeSurface(bad));
  CHECK_FALSE(encodeSurface(Surface{}));
  REQUIRE(original.route({{-4, 0, 0}, {4, 0, 0}}));
}

TEST_CASE("navigation.strict_authoring_settings_reject_extra_duplicate_and_wrong_type_fields") {
  const auto text = Dot::Editor::encodeNavigationSettings({});
  REQUIRE(text);
  REQUIRE(Dot::Editor::decodeNavigationSettings(*text));
  CHECK_FALSE(Dot::Editor::decodeNavigationSettings("{}"));
  CHECK_FALSE(Dot::Editor::decodeNavigationSettings("{\"cellSize\":1,\"cellSize\":1}"));
  auto extra = *text;
  extra.insert(1, "\"extra\":1,");
  CHECK_FALSE(Dot::Editor::decodeNavigationSettings(extra));
  auto fractional = *text;
  const auto index = fractional.find("\"tileCells\":128");
  REQUIRE(index != std::string::npos);
  fractional.replace(index, 15, "\"tileCells\":32.5");
  CHECK_FALSE(Dot::Editor::decodeNavigationSettings(fractional));
}

TEST_CASE("navigation.geometry_state_tracks_only_static_shape_resource_and_world_pose_changes") {
  auto scene = room();
  const auto entity = scene.findEntity(2).value();
  auto state = GeometryState::capture(scene);
  REQUIRE(state);
  REQUIRE(state->unchanged(scene).value());
  auto properties = scene.entity(entity)->rigidBody.value();
  properties.friction = .7F;
  properties.radius = 4;
  REQUIRE(scene.setRigidBody(entity, properties));
  REQUIRE(state->unchanged(scene).value());
  properties.halfExtents.x = .75F;
  REQUIRE(scene.setRigidBody(entity, properties));
  CHECK_FALSE(state->unchanged(scene).value());
  properties.halfExtents.x = .5F;
  REQUIRE(scene.setRigidBody(entity, properties));
  REQUIRE(state->unchanged(scene).value());
  const auto parent = scene.createEntity("Parent").value();
  REQUIRE(scene.setParent(entity, parent));
  REQUIRE(state->unchanged(scene).value());
  REQUIRE(scene.setTransform(parent, {{1, 0, 0}}));
  CHECK_FALSE(state->unchanged(scene).value());
  REQUIRE(scene.setTransform(parent, {}));
  REQUIRE(state->unchanged(scene).value());
  properties.sensor = true;
  REQUIRE(scene.setRigidBody(entity, properties));
  CHECK_FALSE(state->unchanged(scene).value());
  properties.sensor = false;
  REQUIRE(scene.setRigidBody(entity, properties));
  REQUIRE(state->unchanged(scene).value());
  properties.shape = Shape::Sphere;
  REQUIRE(scene.setRigidBody(entity, properties));
  CHECK_FALSE(state->unchanged(scene).value());
  state = GeometryState::capture(scene);
  REQUIRE(state);
  properties.radius = 3;
  REQUIRE(scene.setRigidBody(entity, properties));
  CHECK_FALSE(state->unchanged(scene).value());
  properties.shape = Shape::Capsule;
  REQUIRE(scene.setRigidBody(entity, properties));
  state = GeometryState::capture(scene);
  REQUIRE(state);
  properties.halfHeight = .7F;
  REQUIRE(scene.setRigidBody(entity, properties));
  CHECK_FALSE(state->unchanged(scene).value());
  properties.motion = Motion::Dynamic;
  REQUIRE(scene.setRigidBody(entity, properties));
  state = GeometryState::capture(scene);
  REQUIRE(state);
  REQUIRE(scene.setTransform(entity, {{3, 3, 3}}));
  REQUIRE(state->unchanged(scene).value());
  auto moved = std::move(*state);
  CHECK_FALSE(state->unchanged(scene));
  REQUIRE(moved.unchanged(scene).value());
  CHECK_FALSE(GeometryState{}.unchanged(scene));
}

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Physics.Mesh;
import Dot.Physics.MeshFiles;
import Dot.Physics.World;
import Dot.Physics.SceneSimulation;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Project.ScenePreparation;
import Dot.Assets.Mesh;
import Dot.Graphics.Geometry;

namespace {
  using namespace Dot;

  auto splitFloor() -> Physics::TriangleMesh {
    return Physics::TriangleMesh::create({{{-3, 0, -3},
                                           {-3, 0, 3},
                                           {-1, 0, 3},
                                           {-1, 0, -3},
                                           {1, 0, -3},
                                           {1, 0, 3},
                                           {3, 0, 3},
                                           {3, 0, -3}},
                                          {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7}})
        .value();
  }

  struct MeshFixture {
    Core::StagedDirectory storage
        = Core::StagedDirectory::create(
              Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Core::FileSystem files = storage.files().value();
    World::Scene scene = World::Scene::create("Collision resources").value();
    World::EntityId owner = scene.createEntity("Static mesh").value();

    MeshFixture() {
      REQUIRE(files.createDirectories("assets"));
      REQUIRE(files.createDirectories("scenes"));
      auto mesh = Graphics::cube().value();
      REQUIRE(Assets::saveMesh(files, "assets/solid.dotmesh", mesh));
      World::RigidBody body;
      body.shape = Physics::Shape::Mesh;
      body.motion = Physics::Motion::Static;
      body.meshAsset = "assets/solid.dotmesh";
      REQUIRE(scene.setRigidBody(owner, body));
    }
  };
}  // namespace

TEST_CASE("collision_mesh.native_holes_reflections_queries_rigid_contacts_and_character_support") {
  using namespace Dot;
  using namespace Physics;

  for (const float sign : {1.F, -1.F}) {
    auto world = Dot::Physics::World::create().value();
    BodyConfig floor;
    floor.motion = Motion::Static;
    floor.shape = Shape::Mesh;
    floor.mesh = splitFloor();
    floor.meshScale = {sign * 2, 1, .5F};
    const auto id = world.createBody(floor);
    REQUIRE_MESSAGE(id, (id ? "" : id.error().message));
    auto opening = world.rayCast({0, 2, 0}, {0, -4, 0}).value();
    CHECK_FALSE(opening);
    auto front = world.rayCast({4, 2, 0}, {0, -4, 0}).value();
    auto back = world.rayCast({4, -2, 0}, {0, 4, 0}).value();
    REQUIRE(front);
    REQUIRE(back);
    CHECK(front->collider == ColliderId{*id});
    CHECK(front->fraction == doctest::Approx(.5));
    REQUIRE(front->normal);
    CHECK(front->normal->y == doctest::Approx(1));
    CHECK_FALSE(world.sphereCast({0, 2, 0}, .25F, {0, -4, 0}).value());
    auto sweep = world.sphereCast({4, 2, 0}, .25F, {0, -4, 0}).value();
    REQUIRE(sweep);
    CHECK(sweep->collider == ColliderId{*id});
    CHECK(sweep->fraction == doctest::Approx(.4375).epsilon(.001));
    REQUIRE(world.sphereCast({4, -2, 0}, .25F, {0, 4, 0}).value());

    BodyConfig sphere;
    sphere.position = {4, 2, -.5F};
    auto supported = world.createBody(sphere).value();
    sphere.position = {0, 2, 0};
    auto falling = world.createBody(sphere).value();
    CharacterConfig character;
    character.position = {4, 1, .75F};
    auto walker = world.createCharacter(character).value();

    for (int i = 0; i < 180; ++i) {
      REQUIRE(world.step(1.0 / 60));
    }

    CHECK(world.body(supported)->position.y == doctest::Approx(.5).epsilon(.04));
    CHECK(world.body(falling)->position.y < -10);
    auto state = world.character(walker).value();
    CHECK(state.ground == GroundState::Grounded);
    CHECK(std::abs(state.position.y) < .04F);
    CHECK(state.groundCollider == std::optional{ColliderId{*id}});
    REQUIRE(world.destroyBody(*id));
    CHECK_FALSE(world.rayCast({5, 2, 0}, {0, -4, 0}).value());
  }
}

TEST_CASE("collision_mesh.batch_triangle_budget_failure_preserves_existing_world") {
  using namespace Dot::Physics;
  auto world = Dot::Physics::World::create().value();
  BodyConfig floor;
  floor.motion = Motion::Static;
  floor.shape = Shape::Mesh;
  floor.mesh = splitFloor();
  const auto old = world.createBody(floor).value();
  auto view = floor.mesh.view().value();
  MeshDescription large;
  large.vertices.assign(view.vertices.begin(), view.vertices.end());

  for (std::size_t i = 0; i < 262144; ++i) {
    large.indices.insert(large.indices.end(), {0, 1, 2});
  }

  floor.mesh = TriangleMesh::create(std::move(large)).value();
  const std::vector<BodyChange> excessive(5, BodyChange{std::nullopt, floor});
  auto rejected = world.applyChanges(excessive);
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().message.find("triangles") != std::string::npos);
  CHECK(world.stats()->bodies == 1);
  CHECK_FALSE(world.stats()->faulted);
  REQUIRE(world.body(old));
  REQUIRE(world.rayCast({2, 1, 0}, {0, -2, 0}).value());
}

TEST_CASE(
    "collision_mesh.files_scene_roundtrip_strict_variants_and_failed_sync_are_transactional") {
  using namespace Dot;
  MeshFixture f;
  auto loaded = Physics::loadCollisionMesh(f.files, "assets/solid.dotmesh");
  REQUIRE(loaded);
  CHECK(loaded->triangles() == 12);
  auto text = World::encodeScene(f.scene).value();
  auto value = nlohmann::json::parse(text);
  auto invalid = value;
  invalid["entities"][0]["rigidBody"].erase("meshAsset");
  CHECK_FALSE(World::decodeScene(invalid.dump()));
  invalid = value;
  invalid["entities"][0]["rigidBody"]["meshAsset"] = 1;
  CHECK_FALSE(World::decodeScene(invalid.dump()));
  invalid = value;
  invalid["entities"][0]["rigidBody"]["shape"] = "box";
  CHECK_FALSE(World::decodeScene(invalid.dump()));
  auto decoded = World::decodeScene(text).value();
  CHECK(decoded.entities()->front().rigidBody->meshAsset == "assets/solid.dotmesh");
  CHECK_FALSE(Physics::SceneSimulation::create(f.scene));
  auto simulation = Physics::SceneSimulation::create(f.scene, {}, f.files).value();
  REQUIRE(simulation.rayCast({0, 2, 0}, {0, -4, 0}).value());
  auto changed = *f.scene.entities()->front().rigidBody;
  changed.meshAsset = "assets/missing.dotmesh";
  REQUIRE(f.scene.setRigidBody(f.owner, changed));
  CHECK_FALSE(simulation.synchronize(f.scene));
  CHECK(simulation.stats()->bodies == 1);
  REQUIRE(simulation.rayCast({0, 2, 0}, {0, -4, 0}).value());
  changed.meshAsset = "assets/solid.dotmesh";
  REQUIRE(f.scene.setRigidBody(f.owner, changed));
  REQUIRE(simulation.synchronize(f.scene));
  REQUIRE(f.files.writeTextAtomic("assets/solid.dotmesh", "corrupted"));
  REQUIRE(simulation.synchronize(f.scene));
  CHECK_FALSE(simulation.reset(f.scene));
  REQUIRE(simulation.body(f.owner));
  changed.meshAsset.reset();
  CHECK_FALSE(f.scene.setRigidBody(f.owner, changed));
  changed.meshAsset = "assets/solid.dotmesh";
  changed.shape = Physics::Shape::Box;
  CHECK_FALSE(f.scene.setRigidBody(f.owner, changed));
  CHECK_FALSE(Physics::loadCollisionMesh(f.files, "../assets/solid.dotmesh"));
  CHECK_FALSE(Physics::loadCollisionMesh(f.files, "assets/solid.obj"));
}

TEST_CASE("collision_mesh.worker_owns_initial_resources_without_file_fallback") {
  using namespace Dot;
  MeshFixture f;
  REQUIRE(World::saveScene(f.files, "scenes/main.dotscene", f.scene));
  auto job = Project::ScenePreparation::begin(f.files, "scenes/main.dotscene").value();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};

  while (!job.ready().value()) {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }

  auto proof = job.take().value();
  auto meshes = proof.collisionMeshes().value();
  REQUIRE(meshes.size() == 1);
  std::vector<Physics::MeshAsset> owned(meshes.begin(), meshes.end());
  REQUIRE(f.files.writeTextAtomic("assets/solid.dotmesh", "changed after preparation"));
  auto local = proof.scene().value();
  auto simulation = Physics::SceneSimulation::create(local, {}, f.files, owned);
  REQUIRE(simulation);
  REQUIRE(simulation->rayCast({0, 2, 0}, {0, -4, 0}).value());
  auto none
      = Physics::SceneSimulation::create(local, {}, f.files, std::vector<Physics::MeshAsset>{});
  CHECK_FALSE(none);
  owned.push_back(owned.front());
  CHECK_FALSE(Physics::SceneSimulation::create(local, {}, f.files, owned));
  auto noFiles = Physics::SceneSimulation::create(local, {}, {},
                                                  std::vector<Physics::MeshAsset>{meshes.front()})
                     .value();
  REQUIRE(noFiles.reset(local));
  REQUIRE(noFiles.rayCast({0, 2, 0}, {0, -4, 0}).value());
  auto moved = std::move(proof);
  CHECK_FALSE(proof.collisionMeshes());
  CHECK(moved.collisionMeshes()->size() == 1);
}

TEST_CASE("collision_mesh.native_resource_faces_paths_and_cancellation_are_strict") {
  using namespace Dot;
  MeshFixture f;
  auto geometry = Graphics::quad().value();
  Graphics::MeshData unindexed;

  for (const auto index : geometry.indices) {
    unindexed.vertices.push_back(geometry.vertices[index]);
  }

  REQUIRE(Assets::saveMesh(f.files, "assets/unindexed.dotmesh", unindexed));
  CHECK(Physics::loadCollisionMesh(f.files, "assets/unindexed.dotmesh")->triangles() == 2);
  geometry.indices = {0, 0, 1};
  REQUIRE(Assets::saveMesh(f.files, "assets/degenerate.dotmesh", geometry));
  auto degenerate = Physics::loadCollisionMesh(f.files, "assets/degenerate.dotmesh");
  REQUIRE_FALSE(degenerate);
  CHECK(degenerate.error().message.find("triangle") != std::string::npos);
  CHECK_FALSE(Physics::loadCollisionMesh(f.files, "assets/SOLID.dotmesh"));
  std::stop_source stop;
  stop.request_stop();
  CHECK_FALSE(Physics::loadSceneCollisionMeshes(f.files, f.scene, stop.get_token()));
#ifndef _WIN32
  std::filesystem::create_symlink("solid.dotmesh", f.files.root() / "assets/link.dotmesh");
  CHECK_FALSE(Physics::loadCollisionMesh(f.files, "assets/link.dotmesh"));
#endif
  auto body = *f.scene.entities()->front().rigidBody;
  body.meshAsset = "assets/unindexed.dotmesh";
  REQUIRE(f.scene.setRigidBody(f.owner, body));
  REQUIRE(f.scene.setTransform(f.owner, {{}, {}, {.00001F, 1, 1}}));
  CHECK_FALSE(Physics::loadSceneCollisionMeshes(f.files, f.scene));
}

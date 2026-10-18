#include <doctest/doctest.h>

#include <array>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Result;
import Dot.Math;
import Dot.World.Scene;
import Dot.Physics.Body;

namespace {
  using namespace Dot;

  auto source(std::string path) -> World::AudioSource {
    World::AudioSource result;
    result.asset = std::move(path);
    return result;
  }
}  // namespace

TEST_CASE("scene_snapshots.sparse_sorted_components_and_full_snapshots_agree") {
  using namespace Dot;
  auto scene = World::Scene::create().value();
  const auto root = scene.createEntity("Root").value();
  const auto body = scene.createEntity("Body").value();
  const auto removed = scene.createEntity("Removed").value();
  const auto listener = scene.createEntity("Listener").value();
  const auto character = scene.createEntity("Character").value();
  REQUIRE(scene.destroyEntity(removed));
  REQUIRE(scene.setParent(body, root));
  REQUIRE(scene.setTransform(body, {{2, 3, 4}, {}, {-2, 3, 1}}));
  REQUIRE(scene.setRigidBody(body, World::RigidBody{}));
  REQUIRE(scene.setCharacterController(character, World::CharacterController{}));
  REQUIRE(scene.setAudioSource(character, source("assets/late.wav")));
  REQUIRE(scene.setAudioSource(body, source("assets/early.wav")));
  REQUIRE(scene.setAudioListener(listener, World::AudioListener{true}));
  REQUIRE(scene.setMeshRenderer(root, World::MeshRenderer{}));
  const auto all = scene.entities().value();
  const auto transforms = scene.transforms().value();
  const auto colliders = scene.colliders().value();
  const auto audio = scene.audioSources().value();
  REQUIRE(transforms.size() == all.size());

  for (std::size_t i = 0; i < all.size(); ++i) {
    CHECK(transforms[i].id == all[i].id);
    CHECK(transforms[i].transform.position == all[i].transform.position);
    CHECK(transforms[i].transform.scale == all[i].transform.scale);
    CHECK(transforms[i].parent == all[i].parent);

    if (i) {
      CHECK(all[i - 1].id.value < all[i].id.value);
    }
  }

  REQUIRE(colliders.size() == 2);
  CHECK(colliders[0].id == body);
  CHECK(colliders[0].rigidBody == scene.entity(body)->rigidBody);
  CHECK_FALSE(colliders[0].character);
  CHECK(colliders[1].id == character);
  CHECK_FALSE(colliders[1].rigidBody);
  CHECK(colliders[1].character == scene.entity(character)->character);
  REQUIRE(audio.size() == 2);
  CHECK(audio[0].id == body);
  CHECK(audio[1].id == character);
  CHECK(audio[0].properties == *scene.entity(body)->audioSource);
  CHECK(audio[1].properties == *scene.entity(character)->audioSource);
  CHECK(scene.primaryAudioListener().value() == listener);
}

TEST_CASE("scene_snapshots.owned_values_survive_edits_removal_and_scene_lifetime") {
  using namespace Dot;
  std::vector<World::ColliderInfo> physical;
  std::vector<World::AudioSourceInfo> audio;
  std::vector<World::TransformInfo> transforms;

  {
    auto scene = World::Scene::create().value();
    const auto root = scene.createEntity().value();
    const auto child = scene.createEntity().value();
    REQUIRE(scene.setParent(child, root));
    REQUIRE(scene.setTransform(child, {{1, 2, 3}}));
    World::RigidBody body;
    body.motion = Physics::Motion::Static;
    body.shape = Physics::Shape::Mesh;
    body.meshAsset = "assets/owned.dotmesh";
    REQUIRE(scene.setRigidBody(child, body));
    REQUIRE(scene.setAudioSource(child, source("assets/owned.wav")));
    physical = scene.colliders().value();
    audio = scene.audioSources().value();
    transforms = scene.transforms().value();
    auto edited = physical;
    edited.front().rigidBody->meshAsset = "assets/snapshot-only.dotmesh";
    CHECK(scene.entity(child)->rigidBody->meshAsset == "assets/owned.dotmesh");
    body.meshAsset = "assets/rebound.dotmesh";
    REQUIRE(scene.setRigidBody(child, body));
    REQUIRE(scene.setAudioSource(child, source("assets/rebound.wav")));
    REQUIRE(scene.destroyEntity(root));
    CHECK(scene.colliders()->empty());
    CHECK(scene.audioSources()->empty());
    CHECK(scene.transforms()->empty());
  }

  REQUIRE(physical.size() == 1);
  REQUIRE(audio.size() == 1);
  REQUIRE(transforms.size() == 2);
  CHECK(physical.front().rigidBody->meshAsset == "assets/owned.dotmesh");
  CHECK(audio.front().properties.asset == "assets/owned.wav");
  CHECK(transforms.back().transform.position == Math::Vec3{1, 2, 3});
  CHECK(transforms.back().parent == transforms.front().id);
  bool readable{};
  std::thread reader([&] {
    readable = physical.front().rigidBody->meshAsset == "assets/owned.dotmesh"
               && audio.front().properties.asset == "assets/owned.wav"
               && transforms.back().parent == transforms.front().id;
  });
  reader.join();
  CHECK(readable);
}

TEST_CASE("scene_snapshots.owner_thread_moves_and_primary_listener_removal_are_explicit") {
  using namespace Dot;
  auto scene = World::Scene::create().value();
  const auto entity = scene.createEntity().value();
  REQUIRE(scene.setAudioListener(entity, World::AudioListener{true}));
  bool rejected{};
  std::thread caller([&] {
    const auto poses = scene.transforms();
    const auto bodies = scene.colliders();
    const auto voices = scene.audioSources();
    const auto full = scene.entities();
    rejected = !poses && !bodies && !voices && !full
               && poses.error().code == Core::ErrorCode::InvalidState;
  });
  caller.join();
  CHECK(rejected);
  auto moved = std::move(scene);
  CHECK_FALSE(scene.transforms());
  CHECK_FALSE(scene.colliders());
  CHECK_FALSE(scene.audioSources());
  CHECK(moved.transforms()->front().id == entity);
  REQUIRE(moved.setAudioListener(entity, World::AudioListener{false}));
  auto absent = moved.primaryAudioListener();
  REQUIRE_FALSE(absent);
  CHECK(absent.error().code == Core::ErrorCode::NotFound);
  REQUIRE(moved.destroyEntity(entity));
  CHECK(moved.transforms()->empty());
}

TEST_CASE("scene_snapshots.maximum_population_keeps_sparse_membership_and_pose_identity") {
  using namespace Dot;
  auto scene = World::Scene::create().value();
  std::vector<World::EntityId> ids;
  ids.reserve(10000);

  for (int i = 0; i < 10000; ++i) {
    ids.push_back(scene.createEntity().value());
  }

  CHECK_FALSE(scene.createEntity());
  REQUIRE(scene.setRigidBody(ids[3], World::RigidBody{}));
  REQUIRE(scene.setCharacterController(ids.back(), World::CharacterController{}));
  REQUIRE(scene.setAudioSource(ids[17], source("assets/tone.wav")));
  const auto poses = scene.transforms().value();
  REQUIRE(poses.size() == 10000);
  CHECK(poses.front().id == ids.front());
  CHECK(poses.back().id == ids.back());
  REQUIRE(scene.colliders()->size() == 2);
  CHECK(scene.audioSources()->size() == 1);
  REQUIRE(scene.setRigidBody(ids[3], {}));
  REQUIRE(scene.setAudioSource(ids[17], {}));
  CHECK(scene.colliders()->front().id == ids.back());
  CHECK(scene.audioSources()->empty());
  CHECK(poses.back().id == ids.back());
}

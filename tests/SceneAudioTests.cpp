#include <doctest/doctest.h>

#include <atomic>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Audio.Source;
import Dot.Audio.Mixer;
import Dot.Audio.Output;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Core.Result;

namespace {
  using namespace Dot::World;
  using Json = nlohmann::json;

  auto source() -> AudioSource {
    AudioSource value;
    value.asset = "audio/音效🙂.WAV";
    value.voice = {0.5F, 0, 1.5F, true};
    value.spatial.enabled = true;
    value.spatial.attenuation = Dot::Audio::Attenuation::Exponential;
    value.spatial.minDistance = 2;
    value.spatial.maxDistance = 200;
    value.spatial.rolloff = 2;
    value.spatial.dopplerFactor = 0.5F;
    value.spatial.innerConeDegrees = 60;
    value.spatial.outerConeDegrees = 120;
    value.spatial.outerConeGain = 0.25F;
    value.playOnStart = false;
    return value;
  }

  auto document() -> Json {
    auto scene = Scene::create("Audio scene");
    REQUIRE(scene);
    auto entity = scene->createEntity();
    REQUIRE(entity);
    REQUIRE(scene->setAudioSource(*entity, source()));
    REQUIRE(scene->setAudioListener(*entity, AudioListener{true}));
    auto encoded = encodeScene(*scene);
    REQUIRE(encoded);
    return Json::parse(*encoded);
  }
}  // namespace

TEST_CASE("scene_audio_descriptions_and_snapshots_are_owned_without_backend_initialization") {
  const auto mixerBefore = Dot::Audio::Mixer::backendMemory();
  const auto outputBefore = Dot::Audio::Output::backendMemory();
  auto scene = Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  CHECK_FALSE(scene->entity(*entity)->audioSource);
  CHECK_FALSE(scene->entity(*entity)->audioListener);
  auto properties = source();
  REQUIRE(scene->setAudioSource(*entity, properties));
  REQUIRE(scene->setAudioListener(*entity, AudioListener{true}));
  properties.asset = "changed.wav";
  auto snapshot = scene->entity(*entity);
  REQUIRE(snapshot);
  REQUIRE(snapshot->audioSource);
  CHECK(*snapshot->audioSource == source());
  snapshot->audioSource->voice.volume = 0;
  CHECK(scene->entity(*entity)->audioSource->voice.volume == 0.5F);
  auto entities = scene->entities();
  REQUIRE(entities);
  CHECK(entities->front().audioSource == source());
  CHECK(scene->primaryAudioListener().value() == *entity);
  CHECK(Dot::Audio::Mixer::backendMemory().bytes == mixerBefore.bytes);
  CHECK(Dot::Audio::Output::backendMemory().bytes == outputBefore.bytes);
}

TEST_CASE("scene_audio_source_paths_require_canonical_relative_utf8_audio_assets") {
  auto value = source();

  for (const std::string& path :
       {std::string{}, std::string{"/audio/a.wav"}, std::string{"../a.wav"},
        std::string{"audio/../a.wav"}, std::string{"audio/./a.wav"}, std::string{"audio//a.wav"},
        std::string{"audio\\a.wav"}, std::string{"C:/a.wav"}, std::string{"audio/a.ogg"},
        std::string{"audio/a.wav/"}, std::string{"audio/a?.wav"}, std::string{"audio\n/a.wav"},
        std::string{"audio. /a.wav"}, std::string{"a\0.wav", 6}, std::string{"\xc0\x80.wav"},
        std::string(4097, 'a') + ".wav"}) {
    value.asset = path;
    CHECK_FALSE(Dot::Audio::validateSourceProperties(value));
  }

  for (const char* path : {"audio/valid.wav", "sounds/中文.flac", "music/MENU.MP3"}) {
    value.asset = path;
    CHECK(Dot::Audio::validateSourceProperties(value));
  }
}

TEST_CASE("scene_audio_invalid_replacement_preserves_source_and_other_components") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  REQUIRE(scene->setAudioSource(*entity, source()));
  REQUIRE(scene->setRigidBody(*entity, RigidBody{}));
  REQUIRE(scene->setMeshRenderer(*entity, MeshRenderer{}));
  std::vector<AudioSource> invalid;
  const auto add = [&](auto edit) {
    auto value = source();
    edit(value);
    invalid.push_back(value);
  };
  add([](auto& value) { value.asset = "missing.ext"; });
  add([](auto& value) { value.voice.volume = -1; });
  add([](auto& value) { value.voice.pan = 0.5F; });
  add([](auto& value) { value.voice.pitch = 0; });
  add([](auto& value) { value.spatial.minDistance = 0; });
  add([](auto& value) { value.spatial.maxDistance = value.spatial.minDistance; });
  add([](auto& value) { value.spatial.dopplerFactor = 2; });
  add([](auto& value) { value.spatial.outerConeGain = std::numeric_limits<float>::quiet_NaN(); });
  add([](auto& value) { value.spatial.attenuation = static_cast<Dot::Audio::Attenuation>(99); });

  for (const auto& value : invalid) {
    CHECK_FALSE(scene->setAudioSource(*entity, value));
    CHECK(scene->entity(*entity)->audioSource == source());
    CHECK(scene->entity(*entity)->rigidBody.has_value());
    CHECK(scene->entity(*entity)->mesh.has_value());
  }

  auto flat = source();
  flat.spatial.enabled = false;
  flat.voice.pan = 0.5F;
  REQUIRE(scene->setAudioSource(*entity, flat));
}

TEST_CASE("scene_audio_listener_primary_is_unique_independent_and_removable") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto first = scene->createEntity();
  auto second = scene->createEntity();
  REQUIRE(first);
  REQUIRE(second);
  CHECK_FALSE(scene->primaryAudioListener());
  Camera camera;
  camera.primary = true;
  REQUIRE(scene->setCamera(*second, camera));
  REQUIRE(scene->setAudioListener(*first, AudioListener{true}));
  REQUIRE(scene->setAudioListener(*second, AudioListener{false}));
  CHECK_FALSE(scene->setAudioListener(*second, AudioListener{true}));
  CHECK(scene->primaryAudioListener().value() == *first);
  CHECK(scene->primaryCamera().value() == *second);
  CHECK_FALSE(scene->entity(*second)->audioListener->primary);
  REQUIRE(scene->setAudioListener(*first, std::nullopt));
  REQUIRE(scene->setAudioListener(*first, std::nullopt));
  REQUIRE(scene->setAudioListener(*second, AudioListener{true}));
  CHECK(scene->primaryAudioListener().value() == *second);
}

TEST_CASE("scene_audio_removal_subtree_destruction_and_scope_checks") {
  auto scene = Scene::create();
  auto other = Scene::create();
  REQUIRE(scene);
  REQUIRE(other);
  auto parent = scene->createEntity();
  auto child = scene->createEntity();
  REQUIRE(parent);
  REQUIRE(child);
  REQUIRE(scene->setParent(*child, *parent));
  REQUIRE(scene->setAudioSource(*child, source()));
  REQUIRE(scene->setAudioListener(*child, AudioListener{true}));
  CHECK_FALSE(other->setAudioSource(*child, source()));
  CHECK_FALSE(other->setAudioListener(*child, AudioListener{true}));
  REQUIRE(scene->setAudioSource(*child, std::nullopt));
  REQUIRE(scene->setAudioSource(*child, std::nullopt));
  REQUIRE(scene->setAudioSource(*child, source()));
  REQUIRE(scene->destroyEntity(*parent));
  CHECK_FALSE(scene->primaryAudioListener());
  CHECK_FALSE(scene->setAudioSource(*child, source()));
  CHECK_FALSE(scene->setAudioListener(*child, AudioListener{}));
  CHECK(scene->entityCount().value() == 0);
}

TEST_CASE("scene_audio_move_preserves_handles_and_rejects_foreign_thread_operations") {
  auto scene = Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  REQUIRE(scene->setAudioSource(*entity, source()));
  REQUIRE(scene->setAudioListener(*entity, AudioListener{true}));
  std::atomic<bool> rejected{};
  std::thread worker([&] {
    rejected = !scene->setAudioSource(*entity, source())
               && !scene->setAudioListener(*entity, AudioListener{})
               && !scene->primaryAudioListener();
  });
  worker.join();
  CHECK(rejected.load());
  auto moved = std::move(*scene);
  CHECK_FALSE(scene->setAudioSource(*entity, source()));
  CHECK_FALSE(scene->setAudioListener(*entity, AudioListener{}));
  CHECK_FALSE(scene->primaryAudioListener());
  CHECK(moved.primaryAudioListener().value() == *entity);
  CHECK(moved.entity(*entity)->audioSource == source());
}

TEST_CASE("scene_audio_persistence_round_trip_preserves_all_properties_and_ids") {
  auto original = document();
  auto decoded = decodeScene(original.dump());
  REQUIRE_MESSAGE(decoded, (decoded ? "" : decoded.error().message));
  auto entities = decoded->entities();
  REQUIRE(entities);
  REQUIRE(entities->size() == 1);
  CHECK(entities->front().audioSource == source());
  CHECK(entities->front().audioListener == AudioListener{true});
  CHECK(decoded->primaryAudioListener()->value == 1);
  CHECK(entities->front().id.scene == decoded->id());
  auto encoded = encodeScene(*decoded);
  REQUIRE(encoded);
  CHECK(Json::parse(*encoded) == original);
}

TEST_CASE("scene_audio_old_and_null_optional_components_remain_compatible") {
  auto value = document();
  value["entities"][0].erase("audioSource");
  value["entities"][0].erase("audioListener");
  auto old = decodeScene(value.dump());
  REQUIRE(old);
  CHECK_FALSE(old->entities()->front().audioSource);
  CHECK_FALSE(old->entities()->front().audioListener);
  value["entities"][0]["audioSource"] = nullptr;
  value["entities"][0]["audioListener"] = nullptr;
  CHECK(decodeScene(value.dump()));
}

TEST_CASE("scene_audio_codec_rejects_missing_unknown_and_wrongly_typed_fields") {
  const auto original = document();
  const auto reject = [&](auto edit) {
    auto value = original;
    edit(value["entities"][0]);
    CHECK_FALSE(decodeScene(value.dump()));
  };
  reject([](auto& entity) { entity["audioSource"].erase("asset"); });
  reject([](auto& entity) { entity["audioSource"]["extra"] = 1; });
  reject([](auto& entity) { entity["audioSource"]["asset"] = true; });
  reject([](auto& entity) { entity["audioSource"]["asset"] = "../bad.wav"; });
  reject([](auto& entity) { entity["audioSource"]["playOnStart"] = 1; });
  reject([](auto& entity) { entity["audioSource"]["voice"]["volume"] = true; });
  reject([](auto& entity) { entity["audioSource"]["voice"]["looping"] = "true"; });
  reject([](auto& entity) { entity["audioSource"]["voice"].erase("pitch"); });
  reject([](auto& entity) { entity["audioSource"]["voice"]["volume"] = -1; });
  reject([](auto& entity) { entity["audioSource"]["spatial"]["enabled"] = 0; });
  reject([](auto& entity) { entity["audioSource"]["spatial"]["attenuation"] = "unknown"; });
  reject([](auto& entity) { entity["audioSource"]["spatial"]["minDistance"] = 0; });
  reject([](auto& entity) { entity["audioSource"]["spatial"]["position"] = {0, 0, 0}; });
  reject([](auto& entity) { entity["audioSource"]["spatial"].erase("dopplerFactor"); });
  reject([](auto& entity) { entity["audioListener"]["primary"] = "true"; });
  reject([](auto& entity) { entity["audioListener"]["extra"] = false; });
}

TEST_CASE("scene_audio_codec_rejects_multiple_primary_listeners_instead_of_selecting_one") {
  auto value = document();
  auto second = value["entities"][0];
  second["id"] = "2";
  value["entities"].push_back(second);
  auto rejected = decodeScene(value.dump());
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().code == Dot::Core::ErrorCode::InvalidArgument);
  value["entities"][1]["audioListener"]["primary"] = false;
  CHECK(decodeScene(value.dump()));
}

TEST_CASE("scene_audio_loading_failures_do_not_replace_existing_scene") {
  auto scene = Scene::create("Kept");
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  REQUIRE(scene->setAudioSource(*entity, source()));
  const auto identity = scene->id();
  auto invalid = document();
  invalid["entities"][0]["audioSource"]["voice"]["pitch"] = 0;
  auto candidate = decodeScene(invalid.dump());
  REQUIRE_FALSE(candidate);
  CHECK(scene->id() == identity);
  CHECK(scene->name().value() == "Kept");
  CHECK(scene->entity(*entity)->audioSource == source());
}

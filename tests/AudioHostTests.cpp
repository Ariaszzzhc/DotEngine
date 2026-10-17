#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "SceneAudioFixture.hpp"

import Dot.Script.AudioSession;
import Dot.World.Scene;
import Dot.Core.Result;
import Dot.Audio.Mixer;
import Dot.Audio.Output;

namespace {
  using namespace Dot::Script;
  using Json = nlohmann::json;

  auto scene() -> std::shared_ptr<Dot::World::Scene> {
    auto result = Dot::World::Scene::create("Audio Host");
    REQUIRE(result);
    return std::make_shared<Dot::World::Scene>(std::move(*result));
  }

  auto add(const std::shared_ptr<Dot::World::Scene>& scene, std::string asset = "tone.wav")
      -> Dot::World::EntityId {
    auto entity = scene->createEntity().value();
    Dot::World::AudioSource source;
    source.asset = std::move(asset);
    source.playOnStart = false;
    REQUIRE(scene->setAudioSource(entity, source));
    return entity;
  }

  auto id(Dot::World::EntityId value) -> Json {
    return {{"scene", std::to_string(value.scene)}, {"value", std::to_string(value.value)}};
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

TEST_CASE("audio_host_real_play_pause_seek_stop_volume_and_owned_voice_snapshots") {
  DotTest::Audio::Directory directory;
  auto files = directory.files();
  DotTest::Audio::writeWave(files, "tone.wav", std::vector<float>(4096, 0.5F));
  auto native = scene();
  auto entity = add(native);
  auto audio = AudioSession::create(native, files).value();
  auto services = audio.bindings().value();
  CHECK(services.size() == 9);
  auto initial = read(services, "audio.voice", Json::array({id(entity)}));
  CHECK(initial["sourceFrame"] == "0");
  CHECK(initial["clip"]["frames"] == "4096");
  CHECK(initial["playing"] == false);
  REQUIRE(invoke(services, "audio.seek", Json::array({id(entity), "100"})));
  CHECK(read(services, "audio.voice", Json::array({id(entity)}))["sourceFrame"] == "100");
  REQUIRE(invoke(services, "audio.setVolume", Json::array({0.5})));
  REQUIRE(invoke(services, "audio.play", Json::array({id(entity)})));
  std::array<float, 128> samples;
  REQUIRE(audio.render(samples));
  CHECK(std::ranges::all_of(samples, [](float value) { return value == doctest::Approx(0.25); }));
  REQUIRE(invoke(services, "audio.pause", Json::array({id(entity)})));
  auto paused = read(services, "audio.voice", Json::array({id(entity)}));
  REQUIRE(audio.render(samples));
  CHECK(std::ranges::all_of(samples, [](float value) { return value == 0; }));
  CHECK(read(services, "audio.voice", Json::array({id(entity)}))["sourceFrame"]
        == paused["sourceFrame"]);
  REQUIRE(invoke(services, "audio.stop", Json::array({id(entity)})));
  CHECK(read(services, "audio.voice", Json::array({id(entity)}))["sourceFrame"] == "0");
  CHECK(initial["playing"] == false);
  auto stats = read(services, "audio.stats");
  CHECK(stats["mode"] == "offline");
  CHECK(stats["volume"] == 0.5);
  CHECK(stats["assets"] == 1);
  CHECK(stats["voices"] == 1);
  CHECK(stats["playback"].is_null());
}

TEST_CASE("audio_host_bad_frames_arguments_and_volume_preserve_voice_state") {
  DotTest::Audio::Directory directory;
  auto files = directory.files();
  DotTest::Audio::writeWave(files, "tone.wav", std::vector<float>(4096, 0.5F));
  auto native = scene();
  auto entity = add(native);
  auto audio = AudioSession::create(native, files).value();
  auto services = audio.bindings().value();
  auto original = read(services, "audio.voice", Json::array({id(entity)}));

  for (const auto& frame : std::vector<Json>{0, nullptr, "", "00", "01", "+1", "-1", "1.0", "4096",
                                             "18446744073709551615", "18446744073709551616"}) {
    auto result = invoke(services, "audio.seek", Json::array({id(entity), frame}));
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Dot::Core::ErrorCode::InvalidArgument);
    CHECK(read(services, "audio.voice", Json::array({id(entity)})) == original);
  }

  for (const auto& value : std::vector<Json>{true, "0.5", -0.1, 1.1, 1e100}) {
    CHECK_FALSE(invoke(services, "audio.setVolume", Json::array({value})));
    CHECK(read(services, "audio.stats")["volume"] == 1);
  }

  CHECK_FALSE(invoke(services, "audio.voice", Json::array({id(entity), 0})));
  CHECK_FALSE(invoke(services, "audio.listener", Json::array({0})));
  auto callback = std::ranges::find(services, "audio.stop", &HostFunction::name)->callback;

  for (const auto& bytes : std::vector<std::string>{"{}", "[", std::string("[]\0", 3),
                                                    std::string(65537, ' '), "[1e309]"}) {
    CHECK_FALSE(callback(bytes));
  }

  CHECK_FALSE(callback("[{\"scene\":\"1\",\"value\":\"1\",\"value\":\"2\"}]"));
  CHECK(read(services, "audio.voice", Json::array({id(entity)})) == original);
}

TEST_CASE("audio_host_asset_failures_reset_generations_and_stale_or_foreign_owners_are_explicit") {
  DotTest::Audio::Directory directory;
  auto files = directory.files();
  DotTest::Audio::writeWave(files, "tone.wav", std::vector<float>(4096, 0.5F));
  auto native = scene();
  auto entity = add(native);
  auto audio = AudioSession::create(native, files).value();
  auto services = audio.bindings().value();
  REQUIRE(invoke(services, "audio.seek", Json::array({id(entity), "123"})));
  REQUIRE(invoke(services, "audio.setVolume", Json::array({0.5})));
  auto description = native->entity(entity)->audioSource.value();
  description.asset = "missing.wav";
  REQUIRE(native->setAudioSource(entity, description));
  CHECK_FALSE(audio.synchronize());
  CHECK_FALSE(audio.reset());
  CHECK(read(services, "audio.voice", Json::array({id(entity)}))["sourceFrame"] == "123");
  CHECK(read(services, "audio.stats")["generation"] == "1");
  CHECK(read(services, "audio.stats")["volume"] == 0.5);
  description.asset = "tone.wav";
  REQUIRE(native->setAudioSource(entity, description));
  REQUIRE(audio.reset());
  CHECK(read(services, "audio.stats")["generation"] == "2");
  CHECK(read(services, "audio.stats")["volume"] == 1);
  CHECK(read(services, "audio.voice", Json::array({id(entity)}))["sourceFrame"] == "0");
  auto other = scene();
  auto foreign = add(other);
  CHECK_FALSE(invoke(services, "audio.play", Json::array({id(foreign)})));
  auto pending = add(native);
  CHECK_FALSE(invoke(services, "audio.voice", Json::array({id(pending)})));
  REQUIRE(native->destroyEntity(entity));
  CHECK_FALSE(invoke(services, "audio.voice", Json::array({id(entity)})));
  CHECK_FALSE(invoke(services, "audio.stop", Json::array({id(entity)})));
  REQUIRE(audio.synchronize());
  CHECK(read(services, "audio.stats")["voices"] == 1);
}

TEST_CASE("audio_host_spatial_source_listener_and_derived_velocity_use_native_world_poses") {
  DotTest::Audio::Directory directory;
  auto files = directory.files();
  DotTest::Audio::writeWave(files, "tone.wav", std::vector<float>(4096, 0.5F));
  auto native = scene();
  auto entity = add(native);
  auto listener = native->createEntity("Listener").value();
  REQUIRE(native->setAudioListener(listener, Dot::World::AudioListener{true}));
  auto description = native->entity(entity)->audioSource.value();
  description.spatial.enabled = true;
  description.spatial.attenuation = Dot::Audio::Attenuation::Linear;
  description.spatial.dopplerFactor = 0.5F;
  REQUIRE(native->setAudioSource(entity, description));
  REQUIRE(native->setTransform(entity, {{2, 0, 0}}));
  auto audio = AudioSession::create(native, files).value();
  auto services = audio.bindings().value();
  CHECK(read(services, "audio.voice", Json::array({id(entity)}))["spatial"]["position"]["x"] == 2);
  CHECK(read(services, "audio.voice", Json::array({id(entity)}))["spatial"]["attenuation"]
        == "linear");
  REQUIRE(native->setTransform(entity, {{3, 0, 0}}));
  REQUIRE(native->setTransform(listener, {{0, 1, 0}}));
  REQUIRE(audio.synchronize(0.5));
  CHECK(read(services, "audio.voice", Json::array({id(entity)}))["spatial"]["velocity"]["x"] == 2);
  auto pose = read(services, "audio.listener");
  CHECK(pose["position"]["y"] == 1);
  CHECK(pose["velocity"]["y"] == 2);
}

TEST_CASE("audio_host_motion_reset_survives_failures_and_moves_without_restarting_voices") {
  DotTest::Audio::Directory directory;
  auto files = directory.files();
  DotTest::Audio::writeWave(files, "tone.wav", std::vector<float>(4096, .5F));
  auto native = scene();
  auto entity = add(native);
  auto description = native->entity(entity)->audioSource.value();
  description.spatial.enabled = true;
  REQUIRE(native->setAudioSource(entity, description));
  auto listener = native->createEntity("Listener").value();
  REQUIRE(native->setAudioListener(listener, Dot::World::AudioListener{true}));
  auto audio = AudioSession::create(native, files).value();
  const auto services = audio.bindings().value();
  const auto args = Json::array({id(entity)});
  REQUIRE(invoke(services, "audio.play", args));
  REQUIRE(invoke(services, "audio.seek", Json::array({id(entity), "100"})));
  REQUIRE(invoke(services, "audio.setVolume", Json::array({.75})));
  REQUIRE(native->setTransform(entity, {{10, 0, 0}}));
  REQUIRE(native->setTransform(listener, {{20, 0, 0}}));
  CHECK_FALSE(audio.synchronize(.000001));
  CHECK_FALSE(invoke(services, "audio.resetMotion", Json::array({0})));
  CHECK(read(services, "audio.stats")["motionResetPending"] == false);
  REQUIRE(invoke(services, "audio.resetMotion"));
  CHECK(read(services, "audio.stats")["motionResetPending"] == true);
  CHECK_FALSE(audio.synchronize(std::numeric_limits<double>::quiet_NaN()));
  CHECK_FALSE(audio.synchronize(-1));
  REQUIRE(native->setTransform(listener, {{20, 0, 0}, {}, {-1, 1, 1}}));
  CHECK_FALSE(audio.synchronize(.000001));
  CHECK(read(services, "audio.stats")["motionResetPending"] == true);
  REQUIRE(native->setTransform(listener, {{20, 0, 0}}));
  auto moved = std::move(audio);
  CHECK_FALSE(audio.synchronize(.000001));
  REQUIRE(moved.synchronize(.000001));
  const auto voice = read(services, "audio.voice", args);
  CHECK(voice["sourceFrame"] == "100");
  CHECK(voice["playing"] == true);
  CHECK(voice["spatial"]["velocity"]["x"] == 0);
  CHECK(read(services, "audio.listener")["velocity"]["x"] == 0);
  CHECK(read(services, "audio.stats")["motionResetPending"] == false);
  CHECK(read(services, "audio.stats")["generation"] == "1");
  CHECK(read(services, "audio.stats")["volume"] == .75);
  REQUIRE(native->setTransform(entity, {{11, 0, 0}}));
  REQUIRE(native->setTransform(listener, {{21, 0, 0}}));
  REQUIRE(moved.synchronize(.1));
  CHECK(read(services, "audio.voice", args)["spatial"]["velocity"]["x"] == 10);
  CHECK(read(services, "audio.listener")["velocity"]["x"] == 10);
  CHECK(std::ranges::find(services, "audio.resetMotion", &HostFunction::name)->access
        == HostAccess::ReadWrite);
  REQUIRE(invoke(services, "audio.resetMotion"));
  REQUIRE(moved.reset());
  CHECK(read(services, "audio.stats")["motionResetPending"] == false);
}

TEST_CASE("audio_host_moves_threads_shared_lifetime_and_failed_initialization_release_backend") {
  DotTest::Audio::Directory directory;
  auto files = directory.files();
  DotTest::Audio::writeWave(files, "tone.wav", std::vector<float>(4096, 0.5F));
  auto memory = Dot::Audio::Mixer::backendMemory();
  CHECK_FALSE(AudioSession::create({}, files));
  auto native = scene();
  auto entity = add(native, "missing.wav");
  CHECK_FALSE(AudioSession::create(native, files));
  CHECK(Dot::Audio::Mixer::backendMemory().bytes == memory.bytes);
  auto description = native->entity(entity)->audioSource.value();
  description.asset = "tone.wav";
  REQUIRE(native->setAudioSource(entity, description));
  std::weak_ptr<Dot::World::Scene> weak = native;
  HostBindings services;

  {
    auto audio = AudioSession::create(native, files).value();
    services = audio.bindings().value();
    auto moved = std::move(audio);
    CHECK_FALSE(audio.bindings());
    CHECK_FALSE(audio.synchronize());
    CHECK_FALSE(audio.reset());
    CHECK_FALSE(audio.requestPlaybackClose());
    CHECK_FALSE(audio.pollPlaybackClosed());
    CHECK_FALSE(audio.closePlayback());
    bool rejected = false;
    std::thread worker([&] {
      rejected = !moved.bindings() && !moved.synchronize() && !moved.requestPlaybackClose()
                 && !moved.pollPlaybackClosed() && !moved.closePlayback()
                 && !invoke(services, "audio.voice", Json::array({id(entity)}));
    });
    worker.join();
    CHECK(rejected);
  }

  native.reset();
  CHECK_FALSE(weak.expired());
  CHECK(read(services, "audio.stats")["voices"] == 1);
  auto retained = weak.lock();
  auto movedScene = std::move(*retained);
  CHECK_FALSE(invoke(services, "audio.stats"));
  retained.reset();
  services.clear();
  CHECK(weak.expired());

  {
    auto original = scene();
    add(original, "tone.wav");
    auto audio = AudioSession::create(original, files).value();
    auto invalidated = std::move(*original);
    CHECK_FALSE(audio.synchronize());
    REQUIRE(audio.requestPlaybackClose());
    CHECK(audio.pollPlaybackClosed().value());
    REQUIRE(audio.closePlayback());
    CHECK(invalidated.entityCount().value() == 1);
  }

  CHECK(Dot::Audio::Mixer::backendMemory().bytes == memory.bytes);
  CHECK(Dot::Audio::Mixer::backendMemory().allocations == memory.allocations);
}

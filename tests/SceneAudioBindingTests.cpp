#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <optional>
#include <thread>
#include <utility>

#include "SceneAudioFixture.hpp"

import Dot.Audio.Clip;
import Dot.Audio.SceneAudio;
import Dot.World.Scene;
import Dot.Math;
import Dot.Core.Result;

namespace {
  using namespace Dot::Audio;
  using namespace Dot::World;
  using DotTest::Audio::Directory;
  using DotTest::Audio::writeWave;

  auto makeScene(std::string asset = "tone.wav", bool spatial = false, bool playing = true)
      -> Scene {
    auto scene = Scene::create();
    REQUIRE(scene);
    auto entity = scene->createEntityWithId("Source", 1);
    REQUIRE(entity);
    AudioSource source;
    source.asset = std::move(asset);
    source.voice.looping = true;
    source.spatial.enabled = spatial;
    source.spatial.rolloff = 0;
    source.playOnStart = playing;
    REQUIRE(scene->setAudioSource(*entity, source));
    REQUIRE(scene->setTransform(*entity, {{-10, 0, 0}}));

    if (spatial) {
      auto listener = scene->createEntityWithId("Listener", 2);
      REQUIRE(listener);
      REQUIRE(scene->setAudioListener(*listener, AudioListener{true}));
    }

    return std::move(*scene);
  }

  auto allNear(std::span<const float> samples, float expected) -> bool {
    return std::ranges::all_of(samples,
                               [=](float sample) { return std::abs(sample - expected) < 1e-5F; });
  }
}  // namespace

TEST_CASE("audio.scene_owned_files_cache_shared_pcm_and_real_output") {
  Directory directory;
  auto files = directory.files();
  writeWave(files, "音效🙂.WAV", std::vector<float>(4096, 0.25F));
  auto scene = makeScene("音效🙂.WAV");
  auto first = scene.findEntity(1).value();
  auto second = scene.createEntity().value();
  REQUIRE(scene.setAudioSource(second, *scene.entity(first)->audioSource));
  auto audio = SceneAudio::create(scene, files, {1});
  REQUIRE_MESSAGE(audio, (audio ? "" : audio.error().message));
  CHECK(audio->stats()->assets == 1);
  CHECK(audio->stats()->sampleBytes == 4096 * sizeof(float));
  CHECK(audio->stats()->mixer.voices == 2);
  std::array<float, 32> output{};
  REQUIRE(audio->render(output));
  CHECK(allNear(output, 0.5F));
  REQUIRE(scene.destroyEntity(first));
  REQUIRE(audio->synchronize(scene));
  CHECK(audio->stats()->assets == 1);
  CHECK_FALSE(audio->voice(first));
  REQUIRE(scene.destroyEntity(second));
  REQUIRE(audio->synchronize(scene));
  CHECK(audio->stats()->assets == 0);
  CHECK(audio->stats()->sampleBytes == 0);
  CHECK(audio->stats()->mixer.voices == 0);
  REQUIRE(audio->render(output));
  CHECK(allNear(output, 0));
}

TEST_CASE("audio.scene_sync_preserves_cursor_manual_pause_and_end_state") {
  Directory directory;
  auto files = directory.files();
  const std::array<float, 8> ramp{0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F};
  writeWave(files, "tone.wav", ramp);
  auto scene = makeScene();
  auto entity = scene.findEntity(1).value();
  auto source = *scene.entity(entity)->audioSource;
  source.voice.looping = false;
  REQUIRE(scene.setAudioSource(entity, source));
  auto audio = SceneAudio::create(scene, files, {1});
  REQUIRE(audio);
  std::array<float, 2> output{};
  REQUIRE(audio->render(output));
  REQUIRE(audio->synchronize(scene));
  source.voice.volume = 0.5F;
  REQUIRE(scene.setAudioSource(entity, source));
  REQUIRE(audio->synchronize(scene));
  REQUIRE(audio->render(output));
  CHECK(output[0] == doctest::Approx(0.15));
  CHECK(output[1] == doctest::Approx(0.2));
  REQUIRE(audio->pause(entity));
  REQUIRE(audio->synchronize(scene));
  CHECK_FALSE(audio->voice(entity)->playing);
  REQUIRE(audio->render(output));
  CHECK(allNear(output, 0));
  REQUIRE(audio->play(entity));
  REQUIRE(audio->seek(entity, 6));
  REQUIRE(audio->render(output));
  CHECK(output[0] == doctest::Approx(0.35));
  std::array<float, 8> tail{};
  REQUIRE(audio->render(tail));
  CHECK(audio->voice(entity)->atEnd);
  REQUIRE(audio->synchronize(scene));
  CHECK_FALSE(audio->voice(entity)->playing);
  CHECK(audio->voice(entity)->atEnd);
  CHECK_FALSE(audio->seek(entity, 8));
}

TEST_CASE("audio.scene_missing_or_corrupt_asset_retains_old_pcm_and_bindings") {
  Directory directory;
  auto files = directory.files();
  writeWave(files, "tone.wav", std::vector<float>(4096, 0.25F));
  auto scene = makeScene();
  auto entity = scene.findEntity(1).value();
  auto audio = SceneAudio::create(scene, files, {1});
  REQUIRE(audio);
  auto changed = *scene.entity(entity)->audioSource;
  changed.asset = "missing.wav";
  REQUIRE(scene.setAudioSource(entity, changed));
  auto rejected = audio->synchronize(scene);
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().message.find("missing.wav") != std::string::npos);
  CHECK(rejected.error().message.find("entity 1") != std::string::npos);
  CHECK(audio->stats()->assets == 1);
  CHECK(audio->voice(entity)->playing);
  std::array<float, 32> output{};
  REQUIRE(audio->render(output));
  CHECK(allNear(output, 0.25F));
  REQUIRE(files.writeTextAtomic("missing.wav", "invalid"));
  CHECK_FALSE(audio->synchronize(scene));
  REQUIRE(audio->render(output));
  CHECK(allNear(output, 0.25F));
  CHECK_FALSE(audio->stats()->mixer.faulted);
}

TEST_CASE("audio.scene_cache_is_stable_until_reset_and_failed_reset_retains_live_state") {
  Directory directory;
  auto files = directory.files();
  writeWave(files, "tone.wav", std::vector<float>(4096, 0.25F));
  auto scene = makeScene();
  auto entity = scene.findEntity(1).value();
  auto audio = SceneAudio::create(scene, files, {1});
  REQUIRE(audio);
  writeWave(files, "tone.wav", std::vector<float>(4096, 0.75F));
  REQUIRE(audio->synchronize(scene));
  std::array<float, 32> output{};
  REQUIRE(audio->render(output));
  CHECK(allNear(output, 0.25F));
  REQUIRE(audio->reset(scene));
  REQUIRE(audio->render(output));
  CHECK(allNear(output, 0.75F));
  REQUIRE(files.writeTextAtomic("tone.wav", "broken"));
  CHECK_FALSE(audio->reset(scene));
  CHECK(audio->voice(entity)->playing);
  REQUIRE(audio->render(output));
  CHECK(allNear(output, 0.75F));
}

TEST_CASE("audio.scene_replacement_at_full_capacity_uses_new_asset_and_play_on_start") {
  Directory directory;
  auto files = directory.files();
  writeWave(files, "tone.wav", std::vector<float>(4096, 0.25F));
  writeWave(files, "new.wav", std::vector<float>(4096, 0.75F));
  auto scene = makeScene();
  auto entity = scene.findEntity(1).value();
  auto audio = SceneAudio::create(scene, files, {1, 48000, 1});
  REQUIRE(audio);
  auto source = *scene.entity(entity)->audioSource;
  source.asset = "new.wav";
  source.playOnStart = false;
  REQUIRE(scene.setAudioSource(entity, source));
  REQUIRE(audio->synchronize(scene));
  CHECK(audio->stats()->mixer.voices == 1);
  CHECK(audio->stats()->assets == 1);
  CHECK_FALSE(audio->voice(entity)->playing);
  std::array<float, 32> output{};
  REQUIRE(audio->render(output));
  CHECK(allNear(output, 0));
  REQUIRE(audio->play(entity));
  REQUIRE(audio->render(output));
  CHECK(allNear(output, 0.75F));
}

TEST_CASE("audio.scene_parent_pose_listener_and_actual_spatial_direction_are_synchronized") {
  Directory directory;
  auto files = directory.files();
  writeWave(files, "tone.wav", std::vector<float>(8192, 0.25F));
  auto scene = makeScene("tone.wav", true);
  auto entity = scene.findEntity(1).value();
  auto listener = scene.findEntity(2).value();
  auto parent = scene.createEntity().value();
  REQUIRE(scene.setParent(entity, parent));
  REQUIRE(scene.setTransform(parent, {{0, 0, 0}, {}, {2, 2, 2}}));
  auto audio = SceneAudio::create(scene, files);
  REQUIRE(audio);
  CHECK(audio->voice(entity)->spatial.position.x == -20);
  std::array<float, 8192> output{};
  const auto energy = [&](std::size_t channel) {
    double result = 0;

    for (std::size_t index = output.size() / 2 + channel; index < output.size(); index += 2) {
      result += static_cast<double>(output[index]) * output[index];
    }

    return result;
  };
  REQUIRE(audio->render(output));
  CHECK(energy(0) > energy(1) * 10);
  auto rotation = Dot::Math::rotationEulerDegrees({0, 180, 0});
  REQUIRE(rotation);
  REQUIRE(scene.setTransform(listener, {{0, 0, 0}, *rotation}));
  REQUIRE(audio->synchronize(scene));
  REQUIRE(audio->render(output));
  CHECK(energy(1) > energy(0) * 10);
  REQUIRE(scene.destroyEntity(parent));
  REQUIRE(audio->synchronize(scene));
  CHECK_FALSE(audio->voice(entity));
  CHECK(audio->stats()->mixer.voices == 0);
}

TEST_CASE("audio.scene_primary_listener_mono_and_world_pose_failures_preserve_state") {
  Directory directory;
  auto files = directory.files();
  writeWave(files, "tone.wav", std::vector<float>(4096, 0.25F));
  writeWave(files, "stereo.wav", std::vector<float>(8192, 0.25F), 2);
  auto scene = makeScene("tone.wav", true);
  auto entity = scene.findEntity(1).value();
  auto listener = scene.findEntity(2).value();
  auto audio = SceneAudio::create(scene, files);
  REQUIRE(audio);
  const auto original = *audio->voice(entity);
  REQUIRE(scene.setAudioListener(listener, std::nullopt));
  CHECK_FALSE(audio->synchronize(scene));
  CHECK(audio->voice(entity)->spatial == original.spatial);
  REQUIRE(scene.setAudioListener(listener, AudioListener{true}));
  auto source = *scene.entity(entity)->audioSource;
  source.asset = "stereo.wav";
  REQUIRE(scene.setAudioSource(entity, source));
  CHECK_FALSE(audio->synchronize(scene));
  CHECK(audio->stats()->assets == 1);
  source.asset = "tone.wav";
  REQUIRE(scene.setAudioSource(entity, source));
  REQUIRE(scene.setTransform(entity, {{-10, 0, 0}, {}, {-1, 1, 1}}));
  CHECK_FALSE(audio->synchronize(scene));
  CHECK(audio->voice(entity)->spatial == original.spatial);
  REQUIRE(scene.setTransform(entity, {{2000000, 0, 0}}));
  CHECK_FALSE(audio->synchronize(scene));
  CHECK_FALSE(audio->stats()->mixer.faulted);
}

TEST_CASE(
    "audio.scene_doppler_velocities_derive_only_from_successful_positions_and_teleport_clears") {
  Directory directory;
  auto files = directory.files();
  writeWave(files, "tone.wav", std::vector<float>(4096, 0.25F));
  auto scene = makeScene("tone.wav", true);
  auto entity = scene.findEntity(1).value();
  auto listener = scene.findEntity(2).value();
  auto audio = SceneAudio::create(scene, files);
  REQUIRE(audio);
  REQUIRE(scene.setTransform(entity, {{-9, 0, 0}}));
  REQUIRE(scene.setTransform(listener, {{1, 0, 0}}));
  REQUIRE(audio->synchronize(scene, 0.1));
  CHECK(audio->voice(entity)->spatial.velocity.x == doctest::Approx(10));
  CHECK(audio->listener()->velocity.x == doctest::Approx(10));
  REQUIRE(scene.setTransform(entity, {{100, 0, 0}}));
  CHECK_FALSE(audio->synchronize(scene, 0.1));
  CHECK(audio->voice(entity)->spatial.position.x == -9);
  REQUIRE(scene.setTransform(entity, {{-8, 0, 0}}));
  REQUIRE(audio->synchronize(scene, 0.1));
  CHECK(audio->voice(entity)->spatial.velocity.x == doctest::Approx(10));
  REQUIRE(audio->synchronize(scene));
  CHECK(audio->voice(entity)->spatial.velocity == Dot::Math::Vec3{});
  CHECK(audio->listener()->velocity == Dot::Math::Vec3{});
  REQUIRE(audio->synchronize(scene, 1e-300));
  CHECK_FALSE(audio->synchronize(scene, -1));
  CHECK_FALSE(audio->synchronize(scene, std::numeric_limits<double>::quiet_NaN()));
  CHECK_FALSE(audio->synchronize(scene, 2));
}

TEST_CASE("audio.scene_asset_pcm_voice_limits_are_atomic_and_unique_assets_count_once") {
  Directory directory;
  auto files = directory.files();
  writeWave(files, "tone.wav", std::vector<float>(16, 0.25F));
  writeWave(files, "new.wav", std::vector<float>(16, 0.75F));
  auto scene = makeScene();
  auto entity = scene.findEntity(1).value();
  SceneAudioLimits limits;
  limits.maxAssets = 1;
  limits.sampleBytes = 64;
  auto audio = SceneAudio::create(scene, files, {1, 48000, 2}, limits);
  REQUIRE(audio);
  auto second = scene.createEntity().value();
  REQUIRE(scene.setAudioSource(second, *scene.entity(entity)->audioSource));
  REQUIRE(audio->synchronize(scene));
  CHECK(audio->stats()->assets == 1);
  CHECK(audio->stats()->sampleBytes == 64);
  auto source = *scene.entity(second)->audioSource;
  source.asset = "new.wav";
  REQUIRE(scene.setAudioSource(second, source));
  CHECK_FALSE(audio->synchronize(scene));
  CHECK(audio->stats()->mixer.voices == 2);
  CHECK(audio->stats()->sampleBytes == 64);
  auto third = scene.createEntity().value();
  REQUIRE(scene.setAudioSource(third, source));
  CHECK_FALSE(audio->synchronize(scene));
  limits.sampleBytes = 63;
  CHECK_FALSE(SceneAudio::create(makeScene(), files, {1}, limits));
  limits.sampleBytes = 64;
  limits.clip.sourceBytes = 1;
  CHECK_FALSE(SceneAudio::create(makeScene(), files, {1}, limits));
  limits.maxAssets = 0;
  CHECK_FALSE(SceneAudio::create(scene, files, {1}, limits));
}

TEST_CASE("audio.scene_move_scope_thread_reset_and_independent_lifetime") {
  Directory directory;
  auto files = directory.files();
  writeWave(files, "tone.wav", std::vector<float>(4096, 0.25F));
  const auto baseline = Mixer::backendMemory();

  for (int attempt = 0; attempt < 8; ++attempt) {
    auto scene = makeScene();
    auto entity = scene.findEntity(1).value();
    auto audio = SceneAudio::create(scene, files);
    REQUIRE(audio);
    auto movedScene = std::move(scene);
    REQUIRE(audio->synchronize(movedScene));
    CHECK_FALSE(audio->synchronize(scene));
    auto moved = std::move(*audio);
    CHECK_FALSE(audio->stats());
    std::atomic<bool> rejected{};
    std::thread worker([&] {
      rejected = !moved.synchronize(movedScene) && !moved.voice(entity) && !moved.stats();
    });
    worker.join();
    CHECK(rejected.load());
    auto other = makeScene();
    CHECK_FALSE(moved.synchronize(other));
    CHECK_FALSE(moved.voice(other.findEntity(1).value()));
    REQUIRE(moved.reset(other));
    CHECK_FALSE(moved.voice(entity));
    CHECK(moved.voice(other.findEntity(1).value()));
  }

  CHECK(Mixer::backendMemory().bytes == baseline.bytes);
  CHECK(Mixer::backendMemory().allocations == baseline.allocations);
}

TEST_CASE(
    "audio.scene_backend_preparation_failure_and_shear_rejection_retain_cache_and_old_voice") {
  Directory directory;
  auto files = directory.files();
  writeWave(files, "tone.wav", std::vector<float>(4096, 0.25F));
  writeWave(files, "new.wav", std::vector<float>(4096, 0.75F));
  std::uint64_t budget = 0;

  {
    auto reference = SceneAudio::create(makeScene(), files);
    REQUIRE(reference);
    budget = reference->stats()->mixer.backend.bytes;
  }

  auto scene = makeScene();
  auto entity = scene.findEntity(1).value();
  auto audio = SceneAudio::create(scene, files, {2, 48000, 1, budget});
  REQUIRE(audio);
  const auto baseline = audio->stats()->mixer.backend;
  auto source = *scene.entity(entity)->audioSource;
  source.asset = "new.wav";
  REQUIRE(scene.setAudioSource(entity, source));
  CHECK_FALSE(audio->synchronize(scene));
  CHECK(audio->stats()->mixer.backend.bytes == baseline.bytes);
  CHECK(audio->stats()->assets == 1);
  CHECK(audio->voice(entity)->playing);
  std::array<float, 32> output{};
  REQUIRE(audio->render(output));
  CHECK(allNear(output, 0.25F));
  auto spatialScene = makeScene("tone.wav", true);
  auto spatialEntity = spatialScene.findEntity(1).value();
  auto spatialAudio = SceneAudio::create(spatialScene, files);
  REQUIRE(spatialAudio);
  auto parent = spatialScene.createEntity().value();
  REQUIRE(spatialScene.setParent(spatialEntity, parent));
  REQUIRE(spatialScene.setTransform(parent, {{0, 0, 0}, {}, {2, 1, 1}}));
  auto rotation = Dot::Math::rotationEulerDegrees({0, 0, 45});
  REQUIRE(rotation);
  REQUIRE(spatialScene.setTransform(spatialEntity, {{-10, 0, 0}, *rotation}));
  CHECK_FALSE(spatialAudio->synchronize(spatialScene));
  CHECK(spatialAudio->voice(spatialEntity)->spatial.position.x == -10);
}

TEST_CASE("audio.scene_owns_last_binding_after_scene_and_filesystem_lifetime_end") {
  Directory directory;
  std::optional<SceneAudio> audio;
  EntityId entity;

  {
    auto files = directory.files();
    writeWave(files, "tone.wav", std::vector<float>(4096, 0.25F));
    auto scene = makeScene();
    entity = scene.findEntity(1).value();
    auto created = SceneAudio::create(scene, files, {1});
    REQUIRE(created);
    audio.emplace(std::move(*created));
  }

  CHECK(audio->voice(entity)->playing);
  std::array<float, 32> output{};
  REQUIRE(audio->render(output));
  CHECK(allNear(output, 0.25F));
  REQUIRE(audio->pause(entity));
  CHECK_FALSE(audio->voice(entity)->playing);
}

TEST_CASE("audio.scene_total_pcm_budget_bounds_unique_assets_and_listener_switch_resets_velocity") {
  Directory directory;
  auto files = directory.files();
  writeWave(files, "tone.wav", std::vector<float>(16, 0.25F));
  writeWave(files, "new.wav", std::vector<float>(16, 0.75F));
  auto scene = makeScene("tone.wav", true);
  auto entity = scene.findEntity(1).value();
  auto listener = scene.findEntity(2).value();
  SceneAudioLimits limits;
  limits.sampleBytes = 64;
  limits.maxAssets = 2;
  auto audio = SceneAudio::create(scene, files, {}, limits);
  REQUIRE(audio);
  auto second = scene.createEntity().value();
  auto source = *scene.entity(entity)->audioSource;
  source.asset = "new.wav";
  REQUIRE(scene.setAudioSource(second, source));
  CHECK_FALSE(audio->synchronize(scene));
  CHECK(audio->stats()->assets == 1);
  CHECK(audio->stats()->sampleBytes == 64);
  REQUIRE(scene.setAudioSource(second, std::nullopt));
  REQUIRE(scene.setAudioListener(listener, AudioListener{false}));
  REQUIRE(scene.setAudioListener(second, AudioListener{true}));
  REQUIRE(scene.setTransform(second, {{1000, 0, 0}}));
  REQUIRE(audio->synchronize(scene, 0.01));
  CHECK(audio->listener()->velocity == Dot::Math::Vec3{});
  CHECK(audio->listener()->position.x == 1000);
}

TEST_CASE("audio.scene_prepared_initial_pcm_owns_data_and_refreshes_after_last_reference") {
  Directory directory;
  auto files = directory.files();
  writeWave(files, "tone.wav", std::vector<float>(64, .25F));
  auto scene = makeScene();
  auto prepared = loadSceneClips(files, scene).value();
  REQUIRE(prepared.size() == 1);
  REQUIRE(files.writeTextAtomic("tone.wav", "unavailable after preparation"));
  auto audio = SceneAudio::create(scene, files, {1}, {}, std::span<const ClipAsset>{prepared});
  REQUIRE_MESSAGE(audio, (audio ? "" : audio.error().message));
  prepared.clear();
  std::array<float, 8> output{};
  REQUIRE(audio->render(output));
  CHECK(allNear(output, .25F));
  const auto owner = scene.findEntity(1).value();
  REQUIRE(audio->pause(owner));
  REQUIRE(audio->seek(owner, 7));
  REQUIRE(audio->synchronizePrepared(scene, {}));
  CHECK(audio->voice(owner)->sourceFrame == 7);
  CHECK_FALSE(audio->voice(owner)->playing);
  const auto description = *scene.entity(owner)->audioSource;
  REQUIRE(scene.setAudioSource(owner, std::nullopt));
  REQUIRE(audio->synchronizePrepared(scene, {}));
  CHECK(audio->stats()->assets == 0);
  REQUIRE(scene.setAudioSource(owner, description));
  CHECK_FALSE(audio->synchronizePrepared(scene, {}));
  CHECK_FALSE(audio->synchronize(scene));
  writeWave(files, "tone.wav", std::vector<float>(64, .5F));
  REQUIRE(audio->synchronize(scene));
  REQUIRE(audio->render(output));
  CHECK(allNear(output, .5F));
}

TEST_CASE("audio.scene_prepared_update_preserves_cached_pcm_cursor_and_manual_state") {
  Directory directory;
  auto files = directory.files();
  writeWave(files, "tone.wav", std::vector<float>(64, .25F));
  auto scene = makeScene();
  auto audio = SceneAudio::create(scene, files, {1}).value();
  const auto owner = scene.findEntity(1).value();
  REQUIRE(audio.pause(owner));
  REQUIRE(audio.seek(owner, 7));
  const auto added = scene.createEntity("Added audio").value();
  auto description = *scene.entity(owner)->audioSource;
  description.asset = "new.wav";
  REQUIRE(scene.setAudioSource(added, description));
  auto clip = Clip::fromPcm(std::vector<float>(64, .125F), 1, 48000).value();
  auto changedCache = Clip::fromPcm(std::vector<float>(64, .75F), 1, 48000).value();
  std::vector<ClipAsset> prepared{{"new.wav", clip}, {"tone.wav", changedCache}};
  REQUIRE(files.writeTextAtomic("tone.wav", "changed while referenced"));
  REQUIRE(audio.synchronizePrepared(scene, prepared));
  prepared.clear();
  CHECK(audio.voice(owner)->sourceFrame == 7);
  CHECK_FALSE(audio.voice(owner)->playing);
  std::array<float, 8> output{};
  REQUIRE(audio.render(output));
  CHECK(allNear(output, .125F));
  CHECK(audio.voice(owner)->sourceFrame == 7);
  REQUIRE(audio.play(owner));
  REQUIRE(audio.render(output));
  CHECK(allNear(output, .375F));
  REQUIRE(audio.synchronizePrepared(scene, {}));
  CHECK(audio.stats()->assets == 2);
  CHECK_FALSE(audio.reset(scene));
  REQUIRE(audio.render(output));
  CHECK(allNear(output, .375F));
}

TEST_CASE("audio.scene_prepared_rejections_preserve_backend_and_combined_limits") {
  Directory directory;
  auto files = directory.files();
  writeWave(files, "tone.wav", std::vector<float>(64, .25F));
  auto scene = makeScene();
  SceneAudioLimits limits;
  limits.maxAssets = 2;
  limits.sampleBytes = 512;
  limits.clip.sampleBytes = 256;
  auto audio = SceneAudio::create(scene, files, {1}, limits).value();
  const auto owner = scene.findEntity(1).value();
  REQUIRE(audio.pause(owner));
  REQUIRE(audio.seek(owner, 7));
  const auto added = scene.createEntity("Prepared voice").value();
  auto description = *scene.entity(owner)->audioSource;
  description.asset = "new.wav";
  REQUIRE(scene.setAudioSource(added, description));
  auto clip = Clip::fromPcm(std::vector<float>(64, .125F), 1, 48000).value();
  std::vector<ClipAsset> prepared{{"new.wav", clip}};
  CHECK_FALSE(audio.synchronizePrepared(scene, {}));
  prepared.push_back(prepared.front());
  CHECK_FALSE(audio.synchronizePrepared(scene, prepared));
  prepared.back().asset = "unused.wav";
  CHECK_FALSE(audio.synchronizePrepared(scene, prepared));
  prepared.back().asset = "../invalid.wav";
  CHECK_FALSE(audio.synchronizePrepared(scene, prepared));
  prepared.pop_back();
  prepared.front().value = Clip::fromPcm(std::vector<float>(65, .1F), 1, 48000).value();
  CHECK_FALSE(audio.synchronizePrepared(scene, prepared));
  prepared.front().value = clip;
  description.spatial.enabled = true;
  REQUIRE(scene.setAudioSource(added, description));
  CHECK_FALSE(audio.synchronizePrepared(scene, prepared));
  CHECK(audio.stats()->mixer.voices == 1);
  CHECK(audio.stats()->sampleBytes == 256);
  CHECK(audio.voice(owner)->sourceFrame == 7);
  CHECK_FALSE(audio.voice(owner)->playing);
  description.spatial.enabled = false;
  REQUIRE(scene.setAudioSource(added, description));
  REQUIRE(audio.synchronizePrepared(scene, prepared));
  CHECK(audio.stats()->sampleBytes == 512);
  const auto overflow = scene.createEntity("Over capacity").value();
  description.asset = "overflow.wav";
  REQUIRE(scene.setAudioSource(overflow, description));
  prepared.front().asset = "overflow.wav";
  CHECK_FALSE(audio.synchronizePrepared(scene, prepared));
  CHECK(audio.stats()->mixer.voices == 2);
  CHECK_FALSE(audio.voice(overflow));
  CHECK(audio.voice(owner)->sourceFrame == 7);
}

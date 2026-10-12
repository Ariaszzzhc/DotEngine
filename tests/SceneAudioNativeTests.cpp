#include <array>
#include <chrono>
#include <optional>
#include <thread>

#include "SceneAudioFixture.hpp"

import Dot.Audio.SceneAudio;
import Dot.World.Scene;

TEST_CASE("audio.scene_native_continues_playback_and_retains_bindings_after_failed_asset_edit") {
  DotTest::Audio::Directory directory;
  auto files = directory.files();
  DotTest::Audio::writeWave(files, "tone.wav", std::vector<float>(4096, 0.25F));
  auto scene = Dot::World::Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  Dot::World::AudioSource source;
  source.asset = "tone.wav";
  source.voice.looping = true;
  REQUIRE(scene->setAudioSource(*entity, source));
  Dot::Audio::PlaybackConfig config;
  config.output.volume = 0;
  const auto mixerBefore = Dot::Audio::Mixer::backendMemory();
  const auto outputBefore = Dot::Audio::Output::backendMemory();

  {
    auto audio = Dot::Audio::SceneAudio::createPlayback(*scene, files, config);
    REQUIRE_MESSAGE(audio, (audio ? "" : audio.error().message));
    const auto before = audio->stats()->playback->output.consumedFrames;
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    CHECK(audio->stats()->playback->output.consumedFrames > before + 1024);
    source.asset = "missing.wav";
    REQUIRE(scene->setAudioSource(*entity, source));
    CHECK_FALSE(audio->synchronize(*scene));
    CHECK_FALSE(audio->reset(*scene));
    CHECK(audio->voice(*entity)->playing);
    CHECK(audio->stats()->playback->state == Dot::Audio::PlaybackState::Running);
    std::array<float, 8> output;
    output.fill(0.75F);
    CHECK_FALSE(audio->render(output));
    CHECK(output[0] == 0.75F);
    source.asset = "tone.wav";
    REQUIRE(scene->setAudioSource(*entity, source));
    REQUIRE(audio->reset(*scene));
    CHECK(audio->voice(*entity)->playing);
    CHECK(audio->stats()->playback->state == Dot::Audio::PlaybackState::Running);
    REQUIRE(scene->setAudioSource(*entity, std::nullopt));
    REQUIRE(audio->synchronize(*scene));
    CHECK(audio->stats()->mixer.voices == 0);
    CHECK(audio->stats()->assets == 0);
  }

  CHECK(Dot::Audio::Mixer::backendMemory().bytes == mixerBefore.bytes);
  CHECK(Dot::Audio::Output::backendMemory().bytes == outputBefore.bytes);
}

TEST_CASE("audio.scene_native_failed_asset_initialization_joins_worker_and_releases_backend") {
  DotTest::Audio::Directory directory;
  auto files = directory.files();
  auto scene = Dot::World::Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  Dot::World::AudioSource source;
  source.asset = "missing.wav";
  REQUIRE(scene->setAudioSource(*entity, source));
  Dot::Audio::PlaybackConfig config;
  config.output.volume = 0;
  const auto mixerBefore = Dot::Audio::Mixer::backendMemory();
  const auto outputBefore = Dot::Audio::Output::backendMemory();

  for (int attempt = 0; attempt < 3; ++attempt) {
    CHECK_FALSE(Dot::Audio::SceneAudio::createPlayback(*scene, files, config));
    CHECK(Dot::Audio::Mixer::backendMemory().bytes == mixerBefore.bytes);
    CHECK(Dot::Audio::Mixer::backendMemory().allocations == mixerBefore.allocations);
    CHECK(Dot::Audio::Output::backendMemory().bytes == outputBefore.bytes);
    CHECK(Dot::Audio::Output::backendMemory().allocations == outputBefore.allocations);
  }
}

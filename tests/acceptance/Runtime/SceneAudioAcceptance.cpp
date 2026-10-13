#include <array>
#include <cmath>
#include <expected>
#include <filesystem>
#include <optional>
#include <print>
#include <string>

import Dot.Core.Files;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Audio.SceneAudio;

namespace Dot::Runtime {
  auto testAudioClip(const std::filesystem::path& root) -> int;

  auto testSceneAudioPlayback(const std::filesystem::path& root) -> int {
    if (testAudioClip(root) != 0) {
      return 1;
    }

    auto files = Core::FileSystem::open(root);
    auto scene = World::Scene::create("Audio playback");

    if (!files || !scene || !files->createDirectories("scenes")) {
      return 1;
    }

    auto entity = scene->createEntityWithId("Source", 1);
    auto listener = scene->createEntityWithId("Listener", 2);
    World::AudioSource source;
    source.asset = "audio/acceptance.wav";
    source.voice.looping = true;
    source.spatial.enabled = true;
    source.spatial.rolloff = 0;

    if (!entity || !listener || !scene->setAudioSource(*entity, source)
        || !scene->setAudioListener(*listener, World::AudioListener{true})
        || !scene->setTransform(*entity, {{-10, 0, 0}})
        || !World::saveScene(*files, "scenes/audio-play.dotscene", *scene)) {
      std::println(stderr, "Scene audio playback acceptance authoring failed");
      return 1;
    }

    auto loaded = World::loadScene(*files, "scenes/audio-play.dotscene");

    if (!loaded) {
      std::println(stderr, "{}", loaded.error().message);
      return 1;
    }

    auto restored = loaded->findEntity(1);
    auto audio = Audio::SceneAudio::create(*loaded, *files, {2, 44100});

    if (!restored || !audio) {
      std::println(stderr, "Scene audio playback acceptance binding failed");
      return 1;
    }

    std::array<float, 8192> output{};
    const auto energy = [&](std::size_t channel) {
      double value = 0;

      for (std::size_t index = output.size() / 2 + channel; index < output.size(); index += 2) {
        value += static_cast<double>(output[index]) * output[index];
      }

      return value;
    };

    if (!audio->render(output) || energy(0) <= energy(1) * 10 || audio->stats()->assets != 1) {
      std::println(stderr, "Scene audio playback acceptance left PCM failed");
      return 1;
    }

    auto missing = source;
    missing.asset = "audio/noexist.wav";

    if (!loaded->setAudioSource(*restored, missing) || audio->synchronize(*loaded)
        || !audio->voice(*restored)->playing || audio->stats()->assets != 1
        || !loaded->setAudioSource(*restored, source)
        || !loaded->setTransform(*restored, {{10, 0, 0}}) || !audio->synchronize(*loaded)
        || !audio->render(output) || energy(1) <= energy(0) * 10) {
      std::println(stderr, "Scene audio playback acceptance failed-load/pose PCM failed");
      return 1;
    }

    if (!loaded->destroyEntity(*restored) || !audio->synchronize(*loaded)
        || audio->stats()->mixer.voices != 0 || audio->stats()->assets != 0) {
      std::println(stderr, "Scene audio playback acceptance cleanup failed");
      return 1;
    }

    std::println(
        "Scene audio playback acceptance passed: saved scene, real PCM, failure isolation");
    return 0;
  }

  auto testSceneAudio(const std::filesystem::path& root) -> int {
    auto files = Core::FileSystem::open(root);
    auto scene = World::Scene::create("Audio authoring");

    if (!files || !scene) {
      std::println(stderr, "Scene audio acceptance setup failed");
      return 1;
    }

    auto directory = files->createDirectories("scenes");
    auto parent = scene->createEntityWithId("Listener", 20);
    auto child = scene->createEntityWithId("Source", 50);

    if (!directory || !parent || !child) {
      std::println(stderr, "Scene audio acceptance entity/directory creation failed");
      return 1;
    }

    World::AudioSource source;
    source.asset = "audio/音效🙂.wav";
    source.voice = {0.5F, 0, 1.25F, true};
    source.spatial.enabled = true;
    source.spatial.attenuation = Audio::Attenuation::Linear;
    source.spatial.minDistance = 2;
    source.spatial.maxDistance = 200;
    source.spatial.innerConeDegrees = 60;
    source.spatial.outerConeDegrees = 120;
    source.spatial.outerConeGain = 0.25F;
    source.playOnStart = false;
    auto attached = scene->setParent(*child, *parent);
    auto transformed = scene->setTransform(*parent, {{2, 3, 4}});
    auto sourced = scene->setAudioSource(*child, source);
    auto listened = scene->setAudioListener(*parent, World::AudioListener{true});

    if (!attached || !transformed || !sourced || !listened
        || scene->setAudioListener(*child, World::AudioListener{true})) {
      std::println(stderr, "Scene audio acceptance component/primary validation failed");
      return 1;
    }

    auto saved = World::saveScene(*files, "scenes/audio.dotscene", *scene);

    if (!saved) {
      std::println(stderr, "{}", saved.error().message);
      return 1;
    }

    auto loaded = World::loadScene(*files, "scenes/audio.dotscene");

    if (!loaded) {
      std::println(stderr, "{}", loaded.error().message);
      return 1;
    }

    auto restored = loaded->findEntity(50);
    auto listener = loaded->primaryAudioListener();

    if (!restored || !listener || listener->value != 20 || loaded->id() == scene->id()) {
      std::println(stderr, "Scene audio acceptance identity restoration failed");
      return 1;
    }

    auto snapshot = loaded->entity(*restored);
    auto matrix = loaded->worldTransform(*restored);

    if (!snapshot || snapshot->audioSource != source || snapshot->parent != *listener || !matrix) {
      std::println(stderr, "Scene audio acceptance component/hierarchy restoration failed");
      return 1;
    }

    auto position = Math::transformPoint(*matrix, {});
    auto malformed = files->writeTextAtomic("scenes/audio-invalid.dotscene", "{broken json");

    if (!position || *position != Math::Vec3{2, 3, 4} || !malformed
        || World::loadScene(*files, "scenes/audio-invalid.dotscene")
        || scene->entity(*child)->audioSource != source
        || loaded->entity(*restored)->audioSource != source) {
      std::println(stderr, "Scene audio acceptance transform/failure isolation failed");
      return 1;
    }

    auto removed = loaded->destroyEntity(*listener);

    if (!removed || loaded->entity(*restored) || loaded->primaryAudioListener()
        || scene->entity(*child)->audioSource != source) {
      std::println(stderr, "Scene audio acceptance independent subtree lifetime failed");
      return 1;
    }

    std::println("Scene audio acceptance passed: descriptions, file round trip, scoped lifetime");
    return 0;
  }
}  // namespace Dot::Runtime

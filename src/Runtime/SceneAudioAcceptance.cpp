#include <expected>
#include <filesystem>
#include <optional>
#include <print>
#include <string>

import Dot.Core.Files;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace Dot::Runtime {
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

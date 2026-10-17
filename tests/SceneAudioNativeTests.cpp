#include <array>
#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>

#include "SceneAudioFixture.hpp"
#include "ScenePreparation.hpp"

import Dot.Audio.SceneAudio;
import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Project;
import Dot.Platform.Input;
import Dot.Project.GameSession;
import Dot.Project.PlayerControl;
import Dot.Script.Compiler;
import Dot.Script.Host;

namespace {
  using Json = nlohmann::json;

  auto compiled(const Dot::Core::FileSystem& files, const std::string& body)
      -> Dot::Script::CompiledProgram {
    REQUIRE(files.createDirectories("program/modules"));
    const auto source
        = "import {ScriptComponent,audio,scenes} from 'dot:engine';import {invoke} from 'dot:host';"
          "export class Player extends ScriptComponent {prepared=audio.stats();"
          + body + "}";
    REQUIRE(files.writeTextAtomic("program/modules/player.js", source));
    const auto manifest = Json{{"version", 1},
                               {"format", "dot-esm"},
                               {"sourceFiles", 1},
                               {"sourceBytes", source.size()},
                               {"outputBytes", source.size()},
                               {"modules", {{{"path", "player.js"}, {"bytes", source.size()}}}}};
    REQUIRE(files.writeTextAtomic("program/program.json", manifest.dump()));
    auto root = Dot::Core::FileSystem::open(files.root() / "program").value();
    auto result = Dot::Script::CompiledProgram::loadPublished(root);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return std::move(*result);
  }
}  // namespace

TEST_CASE("audio.game_session_activates_native_audio_after_start_and_releases_prepared_failures") {
  DotTest::Audio::Directory directory;
  auto files = directory.files();
  DotTest::Audio::writeWave(files, "tone.wav", std::vector<float>(48000, 0.25F));
  auto scene = Dot::World::Scene::create().value();
  auto entity = scene.createEntity("Player").value();
  Dot::World::AudioSource source;
  source.asset = "tone.wav";
  source.voice.looping = true;
  REQUIRE(scene.setAudioSource(entity, source));
  REQUIRE(scene.setScript(entity, Dot::World::ScriptComponent{"player.ts", "Player"}));
  Dot::Project::Description description;
  description.applicationId = "audio-test";
  description.hotReload = false;
  Dot::Project::GameSessionConfig config;
  config.audioMode = Dot::Project::SessionAudio::Playback;
  config.audio.output.volume = 0;
  std::vector<Json> observations;
  config.scripts.services.push_back(
      {"observe.sample",
       [&](std::string_view args) -> Dot::Core::Result<std::optional<std::string>> {
         observations.push_back(Json::parse(args).at(0));
         return std::nullopt;
       }});
  const auto mixerBefore = Dot::Audio::Mixer::backendMemory();
  const auto outputBefore = Dot::Audio::Output::backendMemory();
  auto program = compiled(files,
                          "onStart(){invoke('observe.sample',[{before:this.prepared,"
                          "atStart:audio.stats(),cursor:this.self.audioVoice.sourceFrame}]);}"
                          "onUpdate(){invoke('observe.sample',[audio.stats()]);}");
  auto session
      = Dot::Project::GameSession::createCompiled(files, description, scene, program, config);
  REQUIRE_MESSAGE(session, (session ? "" : session.error().message));
  std::this_thread::sleep_for(std::chrono::milliseconds{40});
  auto activeScene = session->scene().value();
  auto activeEntity = activeScene->entities().value().front().id;
  CHECK(activeScene->entity(activeEntity)->name == "Player");
  auto started = session->start();
  REQUIRE(started);
  REQUIRE_MESSAGE(started->failures.empty(),
                  (started->failures.empty() ? "" : started->failures.front().error.message));
  CHECK_FALSE(started->realmFaulted);
  REQUIRE(observations.size() == 1);
  const auto startState = observations.front();

  for (const auto* field : {"before", "atStart"}) {
    CHECK(startState[field]["playback"]["state"] == "prepared");
    CHECK(startState[field]["renderedFrames"] == "0");
    CHECK(startState[field]["playback"]["output"]["running"] == false);
    CHECK(startState[field]["playback"]["output"]["callbacks"] == "0");
  }

  CHECK(startState["cursor"] == "0");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};

  while (true) {
    auto updated = session->update(0);
    REQUIRE(updated);
    REQUIRE(updated->frame.failures.empty());
    REQUIRE(observations.size() >= 2);

    if (std::stoull(observations.back()["playback"]["output"]["consumedFrames"].get<std::string>())
        > 0) {
      break;
    }

    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }

  const auto playing = observations.back();
  CHECK(playing["playback"]["state"] == "running");
  CHECK(std::stoull(playing["playback"]["output"]["consumedFrames"].get<std::string>()) > 0);
  REQUIRE(session->close());
  CHECK(Dot::Audio::Mixer::backendMemory().bytes == mixerBefore.bytes);
  CHECK(Dot::Audio::Output::backendMemory().bytes == outputBefore.bytes);

  program = compiled(
      files, "constructor(context){super(context);throw new Error('preparation rejected');}");
  auto failedPreparation
      = Dot::Project::GameSession::createCompiled(files, description, scene, program, config);
  REQUIRE_FALSE(failedPreparation);
  CHECK(failedPreparation.error().message.find("preparation rejected") != std::string::npos);
  CHECK(Dot::Audio::Mixer::backendMemory().bytes == mixerBefore.bytes);
  CHECK(Dot::Audio::Output::backendMemory().bytes == outputBefore.bytes);

  program = compiled(
      files, "onStart(){this.self.audioSource={...this.self.audioSource,asset:'missing.wav'};}");
  auto rejected
      = Dot::Project::GameSession::createCompiled(files, description, scene, program, config);
  REQUIRE(rejected);
  CHECK_FALSE(rejected->start());
  CHECK(rejected->stats()->state == Dot::Project::SessionState::Faulted);
  REQUIRE(rejected->close());
  CHECK(Dot::Audio::Mixer::backendMemory().bytes == mixerBefore.bytes);
  CHECK(Dot::Audio::Output::backendMemory().bytes == outputBefore.bytes);
}

TEST_CASE("audio.scene_native_prepares_autoplay_silently_and_preserves_activation_on_reset") {
  DotTest::Audio::Directory directory;
  auto files = directory.files();
  DotTest::Audio::writeWave(files, "tone.wav", std::vector<float>(48000, 0.25F));
  auto scene = Dot::World::Scene::create().value();
  auto entity = scene.createEntity().value();
  Dot::World::AudioSource source;
  source.asset = "tone.wav";
  source.voice.looping = true;
  REQUIRE(scene.setAudioSource(entity, source));
  Dot::Audio::PlaybackConfig config;
  config.output.volume = 0;
  config.deferredStart = true;
  auto audio = Dot::Audio::SceneAudio::createPlayback(scene, files, config);
  REQUIRE_MESSAGE(audio, (audio ? "" : audio.error().message));
  CHECK(audio->voice(entity)->playing);
  std::this_thread::sleep_for(std::chrono::milliseconds{40});
  CHECK(audio->voice(entity)->sourceFrame == 0);
  CHECK(audio->stats()->playback->state == Dot::Audio::PlaybackState::Prepared);
  CHECK(audio->stats()->playback->output.callbacks == 0);
  REQUIRE(audio->reset(scene));
  CHECK(audio->stats()->playback->state == Dot::Audio::PlaybackState::Prepared);
  CHECK(audio->voice(entity)->sourceFrame == 0);
  auto moved = std::move(*audio);
  CHECK_FALSE(audio->startPlayback());
  REQUIRE(moved.startPlayback());
  REQUIRE(moved.startPlayback());
  std::this_thread::sleep_for(std::chrono::milliseconds{60});
  CHECK(moved.voice(entity)->sourceFrame > 0);
  CHECK(moved.stats()->playback->state == Dot::Audio::PlaybackState::Running);
  REQUIRE(moved.reset(scene));
  CHECK(moved.stats()->playback->state == Dot::Audio::PlaybackState::Running);
  auto offline = Dot::Audio::SceneAudio::create(scene, files);
  REQUIRE(offline);
  REQUIRE(offline->startPlayback());
  CHECK(offline->voice(entity)->sourceFrame == 0);
}

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

TEST_CASE("audio.scene_transition_prepares_new_endpoint_silently_and_releases_retired_playback") {
  DotTest::Audio::Directory directory;
  auto files = directory.files();
  REQUIRE(files.createDirectories("scenes"));
  DotTest::Audio::writeWave(files, "tone.wav", std::vector<float>(48000, 0.25F));
  auto initial = Dot::World::Scene::create("Start").value();
  auto actor = initial.createEntity("Player").value();
  Dot::World::AudioSource source;
  source.asset = "tone.wav";
  source.voice.looping = true;
  REQUIRE(initial.setAudioSource(actor, source));
  REQUIRE(initial.setScript(actor, Dot::World::ScriptComponent{"player.ts", "Player"}));
  auto target = Dot::World::decodeScene(Dot::World::encodeScene(initial).value()).value();
  REQUIRE(target.rename("Target"));
  REQUIRE(Dot::World::saveScene(files, "scenes/next.dotscene", target));
  auto program
      = compiled(files,
                 "onStart(){invoke('observe.sample',[{phase:'start',where:scenes.current(),"
                 "audio:audio.stats(),cursor:this.self.audioVoice.sourceFrame}]);}"
                 "onUpdate(){invoke('observe.sample',[{phase:'update',audio:audio.stats()}]);}");
  std::vector<Json> observations;
  Dot::Project::GameSessionConfig config;
  config.audioMode = Dot::Project::SessionAudio::Playback;
  config.audio.output.volume = 0;
  config.scripts.services.push_back(
      {"observe.sample",
       [&](std::string_view arguments) -> Dot::Core::Result<std::optional<std::string>> {
         observations.push_back(Json::parse(arguments).at(0));
         return std::nullopt;
       }});
  Dot::Project::Description description;
  description.applicationId = "audio-test";
  description.hotReload = false;
  const auto mixerBefore = Dot::Audio::Mixer::backendMemory();
  const auto outputBefore = Dot::Audio::Output::backendMemory();
  auto session
      = Dot::Project::GameSession::createCompiled(files, description, initial, program, config)
            .value();
  REQUIRE(session.start());
  auto old = session.scene().value();
  const auto id = session.requestScene("scenes/next.dotscene").value();
  auto switched = session.update(0);
  REQUIRE(switched);
  REQUIRE(switched->frame.failures.empty());
  auto completed = Dot::Tests::awaitScene(session, id);
  REQUIRE(completed);
  REQUIRE(completed->failures.empty());
  CHECK(session.sceneRequest(id)->committed);
  CHECK(session.stats()->retiringAudio);
  CHECK(session.scene().value()->id() != old->id());
  REQUIRE(observations.size() == 3);
  const auto& started = observations.back();
  CHECK(started["where"]["path"] == "scenes/next.dotscene");
  CHECK(started["audio"]["playback"]["state"] == "prepared");
  CHECK(started["audio"]["playback"]["output"]["callbacks"] == "0");
  CHECK(started["cursor"] == "0");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};

  while (true) {
    REQUIRE(session.update(0));
    const auto& stats = observations.back()["audio"];
    CHECK(stats["playback"]["state"] == "running");

    if (std::stoull(stats["playback"]["output"]["consumedFrames"].get<std::string>()) > 0) {
      break;
    }

    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }

  REQUIRE(session.close());
  CHECK(Dot::Audio::Mixer::backendMemory().bytes == mixerBefore.bytes);
  CHECK(Dot::Audio::Mixer::backendMemory().allocations == mixerBefore.allocations);
  CHECK(Dot::Audio::Output::backendMemory().bytes == outputBefore.bytes);
  CHECK(Dot::Audio::Output::backendMemory().allocations == outputBefore.allocations);
}

TEST_CASE("audio.scene_adopts_only_matching_unstarted_deferred_playback") {
  DotTest::Audio::Directory directory;
  auto scene = Dot::World::Scene::create().value();
  Dot::Audio::PlaybackConfig config;
  config.deferredStart = true;
  config.output.volume = 0;
  auto playback = Dot::Audio::Playback::create(config).value();
  auto mismatch = config;
  ++mismatch.mixer.maxVoices;
  CHECK_FALSE(
      Dot::Audio::SceneAudio::createPlayback(scene, directory.files(), mismatch, {}, &playback));
  CHECK(playback.pollReady().value());
  CHECK_FALSE(playback.pollStarted().value());
  auto audio
      = Dot::Audio::SceneAudio::createPlayback(scene, directory.files(), config, {}, &playback);
  REQUIRE(audio);
  CHECK_FALSE(playback.pollReady());
  CHECK(audio->stats()->playback->state == Dot::Audio::PlaybackState::Prepared);
  CHECK(audio->stats()->playback->output.callbacks == 0);
  REQUIRE(audio->beginPlayback());
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};

  while (true) {
    auto ready = audio->pollPlayback();
    REQUIRE(ready);

    if (*ready) {
      break;
    }

    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }

  CHECK(audio->stats()->playback->state == Dot::Audio::PlaybackState::Running);
  REQUIRE(audio->requestPlaybackClose());

  while (!audio->pollPlaybackClosed().value()) {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }

  CHECK(audio->stats()->playback->state == Dot::Audio::PlaybackState::Closed);
  REQUIRE(audio->closePlayback());
  REQUIRE(audio->closePlayback());
  auto active = Dot::Audio::Playback::create(config).value();
  REQUIRE(active.start());
  CHECK_FALSE(
      Dot::Audio::SceneAudio::createPlayback(scene, directory.files(), config, {}, &active));
}

TEST_CASE("audio.scene_retirement_is_bounded_observable_and_owned_through_moves_and_close") {
  using namespace Dot;
  DotTest::Audio::Directory directory;
  const auto files = directory.files();
  REQUIRE(files.createDirectories("scenes"));
  auto initial = World::Scene::create("Retirement").value();
  const auto actor = initial.createEntity("Player").value();
  REQUIRE(initial.setScript(actor, World::ScriptComponent{"player.ts", "Player"}));
  REQUIRE(World::saveScene(files, "scenes/next.dotscene", initial));
  const auto program = compiled(files,
                                "onStart(){this.self.name='started';}"
                                "onStop(){this.self.name='stopped';}");
  Project::Description description;
  description.applicationId = "audio-retirement-test";
  description.hotReload = false;
  Project::GameSessionConfig config;
  config.audioMode = Project::SessionAudio::Playback;
  config.audio.output.volume = 0;
  const auto mixerBefore = Audio::Mixer::backendMemory();
  const auto outputBefore = Audio::Output::backendMemory();
  auto session
      = Project::GameSession::createCompiled(files, description, initial, program, config).value();
  REQUIRE(session.start());
  const auto mixerOne = Audio::Mixer::backendMemory().bytes - mixerBefore.bytes;
  const auto outputOne = Audio::Output::backendMemory().bytes - outputBefore.bytes;
  CHECK_FALSE(session.stats()->retiringAudio);
  auto request = session.requestScene("scenes/next.dotscene").value();
  std::weak_ptr<World::Scene> retired;

  for (int iteration = 0; iteration < 3; ++iteration) {
    retired = session.scene().value();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};

    while (session.sceneRequest(request)->state != Script::SceneRequestState::Succeeded) {
      auto frame = session.update(0);
      REQUIRE_MESSAGE(frame, (frame ? "" : frame.error().message));
      CHECK(frame->frame.failures.empty());
      CHECK(Audio::Mixer::backendMemory().bytes <= mixerBefore.bytes + 2 * mixerOne);
      CHECK(Audio::Output::backendMemory().bytes <= outputBefore.bytes + 2 * outputOne);
      REQUIRE(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    CHECK(session.stats()->retiringAudio);
    CHECK_FALSE(retired.expired());
    auto player = Project::PlayerControl::create(session).value();
    auto snapshot = player.perform(R"({"command":"inspect"})");
    REQUIRE(snapshot);
    CHECK(Json::parse(*snapshot)["player"]["retiringAudio"] == true);
    request = session.requestScene("scenes/next.dotscene").value();

    if (iteration != 2) {
      while (session.stats()->retiringAudio) {
        REQUIRE(session.pollScene());
        CHECK(session.sceneRequest(request)->state == Script::SceneRequestState::Pending);
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
      }

      CHECK(retired.expired());
      CHECK(
          Json::parse(player.perform(R"({"command":"inspect"})").value())["player"]["retiringAudio"]
          == false);
    }
  }

  auto moved = std::move(session);
  CHECK_FALSE(session.stats());
  CHECK(moved.stats()->retiringAudio);

  SUBCASE("explicit_close_joins_both_endpoints_and_cancels_the_queued_cut") {
    REQUIRE(moved.close());
    CHECK(moved.sceneRequest(request)->state == Script::SceneRequestState::Cancelled);
    CHECK_FALSE(moved.stats()->retiringAudio);
    CHECK(moved.scene().value()->entities()->front().name == "stopped");
  }

  SUBCASE("quiescent_foreign_destruction_joins_without_running_stop_hooks") {
    auto active = moved.scene().value();
    std::thread release([owned = std::move(moved)] {});
    release.join();
    CHECK(active->entities()->front().name == "started");
  }

  SUBCASE("failed_current_audio_sync_still_releases_both_endpoints") {
    auto active = moved.scene().value();
    World::AudioSource missing;
    missing.asset = "missing.wav";
    REQUIRE(active->setAudioSource(active->entities()->front().id, missing));
    CHECK_FALSE(moved.update(0));
    CHECK(moved.stats()->state == Project::SessionState::Faulted);
    CHECK(moved.stats()->retiringAudio);
    REQUIRE(moved.close());
    CHECK(moved.sceneRequest(request)->state == Script::SceneRequestState::Cancelled);
    CHECK_FALSE(moved.stats()->retiringAudio);
  }

  CHECK(retired.expired());
  CHECK(Audio::Mixer::backendMemory().bytes == mixerBefore.bytes);
  CHECK(Audio::Mixer::backendMemory().allocations == mixerBefore.allocations);
  CHECK(Audio::Output::backendMemory().bytes == outputBefore.bytes);
  CHECK(Audio::Output::backendMemory().allocations == outputBefore.allocations);
}

TEST_CASE("audio.game_async_activation_requires_completion_before_gameplay") {
  DotTest::Audio::Directory directory;
  auto scene = Dot::World::Scene::create().value();
  const auto entity = scene.createEntity("Player").value();
  REQUIRE(scene.setScript(entity, Dot::World::ScriptComponent{"player.ts", "Player"}));
  auto program = compiled(directory.files(), "onStart(){this.self.name='started';}");
  Dot::Project::Description description;
  description.applicationId = "audio-async-test";
  description.hotReload = false;
  Dot::Project::GameSessionConfig config;
  config.audioMode = Dot::Project::SessionAudio::Playback;
  config.audio.output.volume = 0;
  auto session = Dot::Project::GameSession::createCompiled(directory.files(), description, scene,
                                                           program, config)
                     .value();
  REQUIRE(session.beginStart());
  CHECK(session.stats()->state == Dot::Project::SessionState::Starting);
  REQUIRE(session.beginStart());
  CHECK_FALSE(session.update(0));
  CHECK_FALSE(session.fixedUpdate());
  CHECK_FALSE(session.publish(Dot::Platform::InputState{}));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};

  while (true) {
    auto ready = session.pollStart();
    REQUIRE(ready);

    if (*ready) {
      break;
    }

    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }

  CHECK(session.stats()->state == Dot::Project::SessionState::Running);
  REQUIRE(session.update(0));
  REQUIRE(session.close());
  CHECK_FALSE(session.pollPreparationClosed());
  CHECK_FALSE(session.requestPreparationClose());
  const auto mixerBefore = Dot::Audio::Mixer::backendMemory();
  const auto outputBefore = Dot::Audio::Output::backendMemory();
  std::shared_ptr<Dot::World::Scene> cancelledScene;
  program = compiled(directory.files(),
                     "onStart(){this.self.name='started';}onStop(){this.self.name='stopped';}");

  {
    auto cancelled = Dot::Project::GameSession::createCompiled(directory.files(), description,
                                                               scene, program, config)
                         .value();
    REQUIRE(cancelled.beginStart());
    cancelledScene = cancelled.scene().value();
    REQUIRE(cancelled.requestPreparationClose());
    CHECK(cancelled.stats()->state == Dot::Project::SessionState::Cancelling);
    CHECK_FALSE(cancelled.beginStart());
    CHECK_FALSE(cancelled.update(0));
    CHECK_FALSE(cancelled.publish(Dot::Platform::InputState{}));
    const auto closeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};

    while (!cancelled.pollPreparationClosed().value()) {
      REQUIRE(std::chrono::steady_clock::now() < closeDeadline);
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }

    REQUIRE(cancelled.close());
  }

  CHECK(cancelledScene->entities()->front().name == "started");
  CHECK(Dot::Audio::Mixer::backendMemory().bytes == mixerBefore.bytes);
  CHECK(Dot::Audio::Output::backendMemory().bytes == outputBefore.bytes);
}

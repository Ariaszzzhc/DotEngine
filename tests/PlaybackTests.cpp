#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <future>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

import Dot.Audio.Playback;
import Dot.Core.Result;

namespace {
  using namespace Dot::Audio;

  auto config() -> PlaybackConfig {
    PlaybackConfig result;
    result.output.volume = 0;
    return result;
  }

  auto clip(float value = 0.25F, std::size_t frames = 1024) -> Dot::Core::Result<Clip> {
    return Clip::fromPcm(std::vector<float>(frames, value), 1, 48000);
  }

  template <typename Predicate>
  auto waitFor(Predicate predicate) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};

    while (std::chrono::steady_clock::now() < deadline) {
      if (predicate()) {
        return true;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }

    return predicate();
  }
}  // namespace

TEST_CASE("audio.playback_deferred_start_preserves_voices_until_explicit_activation") {
  const auto mixerBefore = Mixer::backendMemory();
  const auto outputBefore = Output::backendMemory();
  auto properties = config();
  properties.deferredStart = true;
  auto playback = Playback::create(properties);
  REQUIRE_MESSAGE(playback, (playback ? "" : playback.error().message));
  auto source = clip(0.25F, 48000);
  REQUIRE(source);
  auto id = playback->createVoice(*source);
  REQUIRE(id);
  REQUIRE(playback->seek(*id, 100));
  REQUIRE(playback->play(*id));
  std::this_thread::sleep_for(std::chrono::milliseconds{40});
  auto prepared = playback->stats();
  REQUIRE(prepared);
  CHECK(prepared->state == PlaybackState::Prepared);
  CHECK_FALSE(prepared->output.running);
  CHECK(prepared->mixer.renderedFrames == 0);
  CHECK(prepared->output.submittedFrames == 0);
  CHECK(prepared->output.callbacks == 0);
  CHECK(prepared->output.queuedFrames == 0);
  CHECK(playback->voice(*id)->sourceFrame == 100);
  CHECK(playback->voice(*id)->playing);
  auto foreign = std::async(std::launch::async, [&] { return playback->start(); });
  CHECK_FALSE(foreign.get());
  CHECK(playback->stats()->state == PlaybackState::Prepared);
  auto moved = std::move(*playback);
  CHECK_FALSE(playback->start());
  REQUIRE(moved.start());
  REQUIRE(moved.start());
  REQUIRE(waitFor([&] { return moved.stats()->output.consumedFrames > 1024; }));
  CHECK(moved.stats()->state == PlaybackState::Running);
  CHECK(moved.voice(*id)->sourceFrame > 100);
  REQUIRE(moved.close());
  CHECK_FALSE(moved.start());
  CHECK(moved.stats()->state == PlaybackState::Closed);
  CHECK(Mixer::backendMemory().bytes == mixerBefore.bytes);
  CHECK(Output::backendMemory().bytes == outputBefore.bytes);
}

TEST_CASE("audio.playback_prepared_close_never_mixes_and_releases_every_resource") {
  const auto mixerBefore = Mixer::backendMemory();
  const auto outputBefore = Output::backendMemory();
  auto properties = config();
  properties.deferredStart = true;

  for (int iteration = 0; iteration < 3; ++iteration) {
    auto playback = Playback::create(properties);
    REQUIRE(playback);
    auto source = clip();
    REQUIRE(source);
    auto id = playback->createVoice(*source, {1, 0, 1, true});
    REQUIRE(id);
    REQUIRE(playback->play(*id));
    REQUIRE(playback->close());
    REQUIRE(playback->close());
    auto closed = playback->stats();
    REQUIRE(closed);
    CHECK(closed->state == PlaybackState::Closed);
    CHECK(closed->mixer.renderedFrames == 0);
    CHECK(closed->mixer.voices == 0);
    CHECK(closed->output.callbacks == 0);
    CHECK(closed->output.submittedFrames == 0);
    CHECK_FALSE(playback->start());
    CHECK(Mixer::backendMemory().bytes == mixerBefore.bytes);
    CHECK(Mixer::backendMemory().allocations == mixerBefore.allocations);
    CHECK(Output::backendMemory().bytes == outputBefore.bytes);
    CHECK(Output::backendMemory().allocations == outputBefore.allocations);
  }

  properties.output.backendBytes = 1;
  CHECK_FALSE(Playback::create(properties));
  CHECK(Mixer::backendMemory().bytes == mixerBefore.bytes);
  CHECK(Output::backendMemory().bytes == outputBefore.bytes);
}

TEST_CASE("audio.playback_continues_real_mixing_while_client_thread_is_idle") {
  auto playback = Playback::create(config());
  auto source = clip(0.75F);
  REQUIRE_MESSAGE(playback, (playback ? "" : playback.error().message));
  REQUIRE(source);
  auto first = playback->createVoice(*source, {1, 0, 1, true});
  auto second = playback->createVoice(*source, {1, 0, 1, true});
  REQUIRE(first);
  REQUIRE(second);
  REQUIRE(playback->play(*first));
  REQUIRE(playback->play(*second));
  const auto before = playback->stats();
  REQUIRE(before);
  std::this_thread::sleep_for(std::chrono::milliseconds{250});
  const auto after = playback->stats();
  REQUIRE(after);
  CHECK(after->state == PlaybackState::Running);
  CHECK(after->mixer.renderedFrames > before->mixer.renderedFrames + 4096);
  CHECK(after->output.consumedFrames > before->output.consumedFrames + 4096);
  CHECK(after->output.clippedSamples > 0);
  CHECK(after->mixer.voices == 2);
  REQUIRE(playback->close());
}

TEST_CASE("audio.playback_returns_executed_voice_controls_and_retains_clip_ownership") {
  auto playback = Playback::create(config());
  REQUIRE(playback);
  VoiceId id;

  {
    auto source = clip();
    REQUIRE(source);
    auto created = playback->createVoice(*source);
    REQUIRE(created);
    id = *created;
  }

  REQUIRE(playback->play(id));
  REQUIRE(playback->pause(id));
  CHECK_FALSE(playback->voice(id)->playing);
  REQUIRE(playback->seek(id, 64));
  REQUIRE(playback->configureVoice(id, {0.5F, -1, 2, true}));
  CHECK(playback->voice(id)->settings == VoiceSettings{0.5F, -1, 2, true});
  REQUIRE(playback->setVolume(0.5F));
  REQUIRE(playback->setOutputVolume(0));
  REQUIRE(playback->play(id));
  CHECK(playback->voice(id)->playing);
  REQUIRE(playback->destroyVoice(id));
  CHECK_FALSE(playback->voice(id));
  CHECK_FALSE(playback->play(id));
  CHECK(playback->stats()->mixer.voices == 0);
}

TEST_CASE("audio.playback_invalid_controls_and_capacity_fail_without_faulting_worker") {
  auto properties = config();
  properties.mixer.maxVoices = 1;
  auto playback = Playback::create(properties);
  auto source = clip();
  REQUIRE(playback);
  REQUIRE(source);
  auto id = playback->createVoice(*source, {1, 0, 1, true});
  REQUIRE(id);
  CHECK_FALSE(playback->createVoice(*source));
  CHECK_FALSE(playback->seek(*id, 1024));
  CHECK_FALSE(playback->configureVoice(*id, {-0.1F}));
  CHECK_FALSE(playback->configureVoice(*id, {1, 2}));
  CHECK_FALSE(playback->configureVoice(*id, {1, 0, 0}));
  CHECK_FALSE(playback->setVolume(-1));
  CHECK_FALSE(playback->setOutputVolume(2));
  CHECK_FALSE(playback->setVolume(std::numeric_limits<float>::quiet_NaN()));
  CHECK(playback->voice(*id)->settings == VoiceSettings{1, 0, 1, true});
  REQUIRE(playback->play(*id));
  const auto before = playback->stats()->output.consumedFrames;
  REQUIRE(waitFor([&] { return playback->stats()->output.consumedFrames > before; }));
  CHECK(playback->stats()->state == PlaybackState::Running);
  REQUIRE(playback->destroyVoice(*id));
  auto next = playback->createVoice(*source);
  REQUIRE(next);
  CHECK(next->value > id->value);
}

TEST_CASE("audio.playback_finished_voices_are_retained_and_restartable") {
  auto playback = Playback::create(config());
  auto source = clip(0.25F, 16);
  REQUIRE(playback);
  REQUIRE(source);
  auto id = playback->createVoice(*source);
  REQUIRE(id);
  REQUIRE(playback->play(*id));
  REQUIRE(waitFor([&] { return playback->voice(*id)->atEnd; }));
  CHECK(playback->stats()->mixer.voices == 1);
  REQUIRE(playback->seek(*id, 0));
  CHECK_FALSE(playback->voice(*id)->atEnd);
  REQUIRE(playback->play(*id));
  REQUIRE(waitFor([&] { return playback->voice(*id)->atEnd; }));
  REQUIRE(playback->destroyVoice(*id));
}

TEST_CASE("audio.playback_shutdown_is_joined_idempotent_and_preserves_terminal_counters") {
  const auto mixerBefore = Mixer::backendMemory();
  const auto outputBefore = Output::backendMemory();
  auto playback = Playback::create(config());
  REQUIRE(playback);
  auto info = playback->info();
  REQUIRE(info);
  REQUIRE(playback->close());
  REQUIRE(playback->close());
  auto final = playback->stats();
  REQUIRE(final);
  CHECK(final->state == PlaybackState::Closed);
  CHECK_FALSE(final->error);
  CHECK_FALSE(final->output.running);
  CHECK(final->mixer.voices == 0);
  CHECK(final->mixer.backend.bytes == 0);
  CHECK(final->output.backend.bytes == 0);
  CHECK(final->output.submittedFrames
        == final->output.consumedFrames + final->output.discardedFrames);
  CHECK(playback->info()->deviceName == info->deviceName);
  const auto callbacks = final->output.callbacks;
  std::this_thread::sleep_for(std::chrono::milliseconds{20});
  CHECK(playback->stats()->output.callbacks == callbacks);
  CHECK_FALSE(playback->play({}));
  CHECK_FALSE(playback->setVolume(1));
  CHECK(Mixer::backendMemory().bytes == mixerBefore.bytes);
  CHECK(Mixer::backendMemory().allocations == mixerBefore.allocations);
  CHECK(Output::backendMemory().bytes == outputBefore.bytes);
  CHECK(Output::backendMemory().allocations == outputBefore.allocations);
}

TEST_CASE("audio.playback_moves_preserve_handles_and_release_previous_worker") {
  auto created = Playback::create(config());
  auto target = Playback::create(config());
  auto source = clip();
  REQUIRE(created);
  REQUIRE(target);
  REQUIRE(source);
  auto first = created->createVoice(*source, {1, 0, 1, true});
  auto old = target->createVoice(*source);
  REQUIRE(first);
  REQUIRE(old);
  CHECK_FALSE(created->play(*old));
  CHECK_FALSE(target->destroyVoice(*first));
  auto moved = std::move(*created);
  CHECK_FALSE(created->stats());
  CHECK_FALSE(created->info());
  CHECK_FALSE(created->createVoice(*source));
  CHECK_FALSE(created->destroyVoice(*first));
  CHECK_FALSE(created->play(*first));
  CHECK_FALSE(created->pause(*first));
  CHECK_FALSE(created->seek(*first, 0));
  CHECK_FALSE(created->configureVoice(*first, {}));
  CHECK_FALSE(created->voice(*first));
  CHECK_FALSE(created->setVolume(1));
  CHECK_FALSE(created->setOutputVolume(0));
  CHECK_FALSE(created->close());
  *target = std::move(moved);
  CHECK_FALSE(moved.stats());
  REQUIRE(target->play(*first));
  CHECK_FALSE(target->voice(*old));
}

TEST_CASE("audio.playback_rejects_foreign_client_calls_and_destroys_quiescent_worker_elsewhere") {
  const auto mixerBefore = Mixer::backendMemory();
  const auto outputBefore = Output::backendMemory();
  auto playback = Playback::create(config());
  auto source = clip();
  REQUIRE(playback);
  REQUIRE(source);
  auto id = playback->createVoice(*source, {1, 0, 1, true});
  REQUIRE(id);
  REQUIRE(playback->play(*id));
  auto rejected = std::async(std::launch::async, [&] {
    return !playback->createVoice(*source) && !playback->destroyVoice(*id) && !playback->play(*id)
           && !playback->pause(*id) && !playback->seek(*id, 0) && !playback->configureVoice(*id, {})
           && !playback->voice(*id) && !playback->stats() && !playback->info()
           && !playback->setVolume(1) && !playback->setOutputVolume(0) && !playback->close();
  });
  CHECK(rejected.get());
  CHECK(playback->stats()->state == PlaybackState::Running);
  auto cleanup
      = std::async(std::launch::async, [owned = std::move(*playback)] { return !owned.info(); });
  CHECK(cleanup.get());
  CHECK(Mixer::backendMemory().bytes == mixerBefore.bytes);
  CHECK(Output::backendMemory().bytes == outputBefore.bytes);
}

TEST_CASE("audio.playback_failed_mixer_and_device_initialization_release_worker_resources") {
  const auto mixerBefore = Mixer::backendMemory();
  const auto outputBefore = Output::backendMemory();
  auto properties = config();
  properties.mixer.backendBytes = 1;
  CHECK_FALSE(Playback::create(properties));
  properties = config();
  properties.output.backendBytes = 1;
  CHECK_FALSE(Playback::create(properties));
  properties = config();
#if defined(__APPLE__)
  properties.output.backend = AudioBackend::Wasapi;
#else
  properties.output.backend = AudioBackend::CoreAudio;
#endif
  CHECK_FALSE(Playback::create(properties));
  CHECK(Mixer::backendMemory().bytes == mixerBefore.bytes);
  CHECK(Mixer::backendMemory().allocations == mixerBefore.allocations);
  CHECK(Output::backendMemory().bytes == outputBefore.bytes);
  CHECK(Output::backendMemory().allocations == outputBefore.allocations);
}

TEST_CASE("audio.playback_configuration_boundaries_fail_before_native_startup") {
  std::vector<PlaybackConfig> invalid;
  auto add = [&](auto change) {
    auto value = config();
    change(value);
    invalid.push_back(value);
  };
  add([](auto& value) { value.mixer.channels = 1; });
  add([](auto& value) { value.mixer.sampleRate = 44100; });
  add([](auto& value) { value.blockFrames = 0; });
  add([](auto& value) { value.blockFrames = 65537; });
  add([](auto& value) { value.blockFrames = value.targetFrames + 1; });
  add([](auto& value) { value.targetFrames = value.output.queueFrames + 1; });
  add([](auto& value) { value.targetFrames = 511; });
  add([](auto& value) { value.pollMilliseconds = 0; });
  add([](auto& value) { value.pollMilliseconds = 21; });

  for (const auto& value : invalid) {
    auto result = Playback::create(value);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Dot::Core::ErrorCode::InvalidArgument);
  }
}

TEST_CASE("audio.playback_repeated_voice_controls_and_worker_lifetimes_release_every_resource") {
  const auto mixerBefore = Mixer::backendMemory();
  const auto outputBefore = Output::backendMemory();
  auto source = clip();
  REQUIRE(source);

  for (int iteration = 0; iteration < 4; ++iteration) {
    auto playback = Playback::create(config());
    REQUIRE(playback);

    for (int cycle = 0; cycle < 16; ++cycle) {
      auto id = playback->createVoice(*source, {1, 0, 1, true});
      REQUIRE(id);
      REQUIRE(playback->play(*id));
      REQUIRE(playback->pause(*id));
      REQUIRE(playback->seek(*id, 0));
      REQUIRE(playback->destroyVoice(*id));
    }

    REQUIRE(playback->close());
    CHECK(Mixer::backendMemory().bytes == mixerBefore.bytes);
    CHECK(Output::backendMemory().bytes == outputBefore.bytes);
  }
}

TEST_CASE("audio.playback_recoverable_voice_allocation_failure_keeps_worker_running") {
  std::uint64_t budget = 0;

  {
    auto mixer = Mixer::create();
    REQUIRE(mixer);
    budget = mixer->stats()->backend.bytes;
  }

  auto properties = config();
  properties.mixer.backendBytes = budget;
  auto playback = Playback::create(properties);
  auto source = clip();
  REQUIRE_MESSAGE(playback, (playback ? "" : playback.error().message));
  REQUIRE(source);
  CHECK_FALSE(playback->createVoice(*source));
  auto before = playback->stats();
  REQUIRE(before);
  REQUIRE(waitFor([&] {
    auto snapshot = playback->stats();
    return snapshot && snapshot->state == PlaybackState::Running
           && snapshot->output.consumedFrames > before->output.consumedFrames;
  }));
  auto after = playback->stats();
  REQUIRE(after);
  CHECK(after->state == PlaybackState::Running);
  CHECK(after->mixer.voices == 0);
  CHECK(after->output.consumedFrames > before->output.consumedFrames);
  CHECK(after->mixer.backend.bytes == before->mixer.backend.bytes);
}

TEST_CASE("audio.playback_adaptive_refill_supports_small_high_rate_buffers") {
  auto properties = config();
  properties.mixer.channels = 1;
  properties.mixer.sampleRate = 192000;
  properties.output.channels = 1;
  properties.output.sampleRate = 192000;
  properties.output.queueFrames = 256;
  properties.output.periodFrames = 64;
  properties.targetFrames = 128;
  properties.blockFrames = 64;
  properties.pollMilliseconds = 20;
  auto playback = Playback::create(properties);
  REQUIRE_MESSAGE(playback, (playback ? "" : playback.error().message));
  const auto before = playback->stats()->output.consumedFrames;
  std::this_thread::sleep_for(std::chrono::milliseconds{50});
  const auto after = playback->stats();
  REQUIRE(after);
  CHECK(after->state == PlaybackState::Running);
  CHECK(after->output.consumedFrames > before + 512);
  REQUIRE(playback->close());
}

TEST_CASE("audio.playback_spatial_and_listener_controls_execute_on_worker") {
  auto playback = Playback::create(config());
  auto source = clip();
  REQUIRE(playback);
  REQUIRE(source);
  auto id = playback->createVoice(*source, {1, 0, 1, true});
  REQUIRE(id);
  SpatialSettings spatial;
  spatial.enabled = true;
  spatial.position = {-10, 0, 0};
  spatial.rolloff = 0;
  REQUIRE(playback->setSpatial(*id, spatial));
  Listener listener;
  listener.position = {1, 2, 3};
  listener.rotation.w = 2;
  REQUIRE(playback->setListener(listener));
  CHECK(playback->listener()->position == listener.position);
  CHECK(playback->listener()->rotation.w == 1);
  CHECK(playback->voice(*id)->spatial == spatial);
  REQUIRE(playback->play(*id));
  auto invalid = spatial;
  invalid.velocity.x = 101;
  CHECK_FALSE(playback->setSpatial(*id, invalid));
  CHECK(playback->voice(*id)->spatial == spatial);
  CHECK_FALSE(playback->setListener({{}, {0, 0, 0, 0}}));
  CHECK(playback->listener()->position == listener.position);
  auto rejected = std::async(std::launch::async, [&] {
    return !playback->setSpatial(*id, spatial) && !playback->setListener({})
           && !playback->listener();
  });
  CHECK(rejected.get());
  auto moved = std::move(*playback);
  CHECK_FALSE(playback->setSpatial(*id, spatial));
  CHECK_FALSE(playback->setListener({}));
  CHECK_FALSE(playback->listener());
  REQUIRE(moved.close());
  CHECK_FALSE(moved.listener());
  CHECK_FALSE(moved.setSpatial(*id, spatial));
}

TEST_CASE("audio.playback_executes_owned_voice_batches_and_recovers_from_rejected_batch") {
  auto properties = config();
  properties.mixer.maxVoices = 2;
  auto playback = Playback::create(properties);
  REQUIRE(playback);
  std::vector<VoiceId> ids;

  {
    auto source = clip();
    REQUIRE(source);
    VoiceBatch batch;
    SpatialSettings spatial;
    spatial.enabled = true;
    spatial.position = {-10, 0, 0};
    spatial.rolloff = 0;
    batch.create.push_back({*source, {1, 0, 1, true}, spatial, true});
    batch.create.push_back({*source, {0.5F, 0, 1, true}, {}, false});
    batch.listener = Listener{{1, 2, 3}, {0, 0, 0, 2}};
    auto created = playback->applyChanges(batch);
    REQUIRE(created);
    ids = *created;
    batch.create.clear();
  }

  REQUIRE(ids.size() == 2);
  CHECK(playback->voice(ids[0])->playing);
  CHECK_FALSE(playback->voice(ids[1])->playing);
  CHECK(playback->listener()->position == Dot::Math::Vec3{1, 2, 3});
  CHECK(playback->listener()->rotation.w == 1);
  VoiceBatch change;
  change.update.push_back({ids[0], VoiceSettings{0.25F, 0, 1.5F, true}});
  change.remove.push_back(ids[1]);
  auto source = clip(0.75F);
  REQUIRE(source);
  change.create.push_back({*source, {1, 0, 1, true}, {}, true});
  auto replaced = playback->applyChanges(change);
  REQUIRE(replaced);
  CHECK(replaced->front().value > ids[1].value);
  CHECK_FALSE(playback->voice(ids[1]));
  CHECK(playback->voice(ids[0])->settings.pitch == 1.5F);
  CHECK(playback->voice(ids[0])->spatial.enabled);
  auto rejected = change;
  rejected.update.front().settings->volume = 0;
  CHECK_FALSE(playback->applyChanges(rejected));
  CHECK(playback->voice(ids[0])->settings.volume == 0.25F);
  CHECK(playback->voice(replaced->front())->playing);
  CHECK(playback->stats()->state == PlaybackState::Running);
  const auto before = playback->stats()->output.consumedFrames;
  REQUIRE(waitFor([&] { return playback->stats()->output.consumedFrames > before + 2048; }));
  VoiceBatch removal;
  removal.remove = {ids[0], replaced->front()};
  REQUIRE(playback->applyChanges(removal));
  CHECK(playback->stats()->mixer.voices == 0);
  REQUIRE(playback->close());
  CHECK_FALSE(playback->applyChanges({}));
}

TEST_CASE("audio.playback_batch_preparation_failure_preserves_running_worker_and_old_voice") {
  std::uint64_t budget = 0;

  {
    auto reference = Mixer::create();
    auto source = clip();
    REQUIRE(reference);
    REQUIRE(source);
    REQUIRE(reference->createVoice(*source));
    budget = reference->stats()->backend.bytes;
  }

  auto properties = config();
  properties.mixer.backendBytes = budget;
  properties.mixer.maxVoices = 1;
  auto playback = Playback::create(properties);
  auto source = clip();
  REQUIRE(playback);
  REQUIRE(source);
  auto id = playback->createVoice(*source, {1, 0, 1, true});
  REQUIRE(id);
  REQUIRE(playback->play(*id));
  const auto baseline = playback->stats()->mixer.backend;
  VoiceBatch batch;
  batch.remove.push_back(*id);
  batch.create.push_back({*source, {}, {}, true});
  batch.listener = Listener{{1, 0, 0}};
  auto failed = playback->applyChanges(batch);
  REQUIRE_FALSE(failed);
  CHECK(failed.error().code == Dot::Core::ErrorCode::External);
  CHECK(playback->voice(*id)->playing);
  CHECK(playback->listener()->position == Dot::Math::Vec3{});
  auto stats = playback->stats();
  REQUIRE(stats);
  CHECK(stats->state == PlaybackState::Running);
  CHECK(stats->mixer.voices == 1);
  CHECK(stats->mixer.backend.bytes == baseline.bytes);
  CHECK(stats->mixer.backend.allocations == baseline.allocations);
  const auto before = stats->output.consumedFrames;
  REQUIRE(waitFor([&] { return playback->stats()->output.consumedFrames > before + 2048; }));
  REQUIRE(playback->destroyVoice(*id));
  REQUIRE(playback->close());
}

TEST_CASE("audio_playback_stop_is_one_worker_command_and_rewinds_the_paused_source") {
  auto playback = Playback::create(config());
  REQUIRE_MESSAGE(playback, (playback ? "" : playback.error().message));
  auto source = clip(0.25F, 48000);
  REQUIRE(source);
  auto voice = playback->createVoice(*source);
  REQUIRE(voice);
  REQUIRE(playback->seek(*voice, 1000));
  CHECK(playback->voice(*voice)->sourceFrame == 1000);
  REQUIRE(playback->play(*voice));
  REQUIRE(waitFor([&] { return playback->voice(*voice)->sourceFrame > 1000; }));
  REQUIRE(playback->stop(*voice));
  CHECK_FALSE(playback->voice(*voice)->playing);
  CHECK(playback->voice(*voice)->sourceFrame == 0);
  std::this_thread::sleep_for(std::chrono::milliseconds{20});
  CHECK(playback->voice(*voice)->sourceFrame == 0);
  REQUIRE(playback->close());
}

TEST_CASE("audio.playback_async_preparation_activation_and_close_preserve_real_completion") {
  const auto mixerBefore = Mixer::backendMemory();
  const auto outputBefore = Output::backendMemory();
  auto properties = config();
  properties.deferredStart = true;
  auto playback = Playback::begin(properties);
  REQUIRE(playback);
  CHECK(playback->configuration().value() == properties);
  REQUIRE(waitFor([&] {
    const auto ready = playback->pollReady();
    return !ready || *ready;
  }));
  REQUIRE(playback->pollReady().value());
  CHECK_FALSE(playback->pollStarted().value());
  auto prepared = playback->stats();
  REQUIRE(prepared);
  CHECK(prepared->output.callbacks == 0);
  CHECK(prepared->output.submittedFrames == 0);
  CHECK(prepared->mixer.renderedFrames == 0);
  auto foreign = std::async(std::launch::async, [&] {
    return !playback->pollReady() && !playback->configuration() && !playback->beginStart()
           && !playback->pollStarted() && !playback->requestClose() && !playback->pollClosed();
  });
  CHECK(foreign.get());
  REQUIRE(playback->beginStart());
  REQUIRE(playback->beginStart());
  REQUIRE(waitFor([&] {
    auto started = playback->pollStarted();
    return !started || *started;
  }));
  REQUIRE(playback->pollStarted().value());
  CHECK(playback->stats()->state == PlaybackState::Running);
  REQUIRE(playback->requestClose());
  REQUIRE(playback->requestClose());
  CHECK_FALSE(playback->beginStart());
  REQUIRE(waitFor([&] { return playback->pollClosed().value(); }));
  REQUIRE(playback->close());
  CHECK(playback->stats()->state == PlaybackState::Closed);
  CHECK(Mixer::backendMemory().bytes == mixerBefore.bytes);
  CHECK(Mixer::backendMemory().allocations == mixerBefore.allocations);
  CHECK(Output::backendMemory().bytes == outputBefore.bytes);
  CHECK(Output::backendMemory().allocations == outputBefore.allocations);
}

TEST_CASE("audio.playback_async_cancel_and_failed_prepare_release_all_owned_resources") {
  const auto mixerBefore = Mixer::backendMemory();
  const auto outputBefore = Output::backendMemory();
  auto properties = config();
  properties.deferredStart = true;

  for (int iteration = 0; iteration < 3; ++iteration) {
    auto playback = Playback::begin(properties);
    REQUIRE(playback);
    REQUIRE(playback->requestClose());
    REQUIRE(waitFor([&] { return playback->pollClosed().value(); }));
    REQUIRE(playback->close());
    CHECK(playback->stats()->output.callbacks == 0);
    CHECK(playback->stats()->mixer.renderedFrames == 0);
    CHECK(Mixer::backendMemory().bytes == mixerBefore.bytes);
    CHECK(Output::backendMemory().bytes == outputBefore.bytes);
  }

  properties.output.backendBytes = 1;
  auto failed = Playback::begin(properties);
  REQUIRE(failed);
  REQUIRE(waitFor([&] { return !failed->pollReady(); }));
  CHECK_FALSE(failed->info());
  CHECK_FALSE(failed->beginStart());
  REQUIRE(waitFor([&] { return failed->pollClosed().value(); }));
  CHECK_FALSE(failed->close());
  CHECK(failed->stats()->state == PlaybackState::Faulted);
  auto moved = std::move(*failed);
  CHECK_FALSE(failed->pollReady());
  CHECK_FALSE(failed->configuration());
  CHECK_FALSE(failed->beginStart());
  CHECK_FALSE(failed->pollStarted());
  CHECK_FALSE(failed->requestClose());
  CHECK_FALSE(failed->pollClosed());
  CHECK(Mixer::backendMemory().bytes == mixerBefore.bytes);
  CHECK(Mixer::backendMemory().allocations == mixerBefore.allocations);
  CHECK(Output::backendMemory().bytes == outputBefore.bytes);
  CHECK(Output::backendMemory().allocations == outputBefore.allocations);
}

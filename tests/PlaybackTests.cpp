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
  std::this_thread::sleep_for(std::chrono::milliseconds{30});
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

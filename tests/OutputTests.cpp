#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <limits>
#include <span>
#include <thread>
#include <utility>
#include <vector>

import Dot.Audio.Output;
import Dot.Core.Result;

namespace {
  using namespace Dot::Audio;

  auto config() -> OutputConfig {
    OutputConfig result;
    result.volume = 0;
    return result;
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

TEST_CASE("audio.output_real_backend_consumes_owned_pcm_and_reports_underruns") {
  const auto before = Output::backendMemory();

  {
    auto output = Output::create(config());
    REQUIRE_MESSAGE(output, (output ? "" : output.error().message));
    auto info = output->info();
    REQUIRE(info);
    CHECK(info->backend != AudioBackend::Default);
    CHECK_FALSE(info->deviceName.empty());
    CHECK(info->channels == 2);
    CHECK(info->sampleRate == 48000);
    CHECK(info->deviceChannels > 0);
    CHECK(info->deviceSampleRate > 0);
    std::array<float, 2048> samples;
    samples.fill(0.00001F);
    REQUIRE(output->submit(samples));
    samples.fill(99);
    CHECK(output->stats()->queuedFrames == 1024);
    REQUIRE(output->start());
    REQUIRE(waitFor([&] { return output->stats()->consumedFrames == 1024; }));
    REQUIRE(waitFor([&] { return output->stats()->underrunFrames > 0; }));
    REQUIRE(output->stop());
    auto stats = output->stats();
    REQUIRE(stats);
    CHECK_FALSE(stats->running);
    CHECK(stats->fault == OutputFault::None);
    CHECK(stats->callbacks > 0);
    CHECK(stats->submittedFrames == 1024);
    CHECK(stats->callbackFrames == stats->consumedFrames + stats->underrunFrames);
    CHECK(stats->clippedSamples == 0);
    const auto calls = stats->callbacks;
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    CHECK(output->stats()->callbacks == calls);
    REQUIRE(output->start());
    REQUIRE(output->start());
    REQUIRE(output->stop());
    REQUIRE(output->stop());
  }

  CHECK(Output::backendMemory().allocations == before.allocations);
  CHECK(Output::backendMemory().bytes == before.bytes);
}

TEST_CASE("audio.output_clips_only_at_endpoint_before_muted_device_gain") {
  auto output = Output::create(config());
  REQUIRE_MESSAGE(output, (output ? "" : output.error().message));
  const std::vector<float> source(1024, 1.5F);
  REQUIRE(output->submit(source));
  REQUIRE(output->start());
  REQUIRE(waitFor([&] { return output->stats()->consumedFrames == 512; }));
  REQUIRE(output->stop());
  CHECK(output->stats()->clippedSamples == 1024);
}

TEST_CASE("audio.output_mixes_same_format_and_rejects_capacity_before_graph_advance") {
  auto output = Output::create(config());
  auto mixer = Mixer::create();
  auto mono = Mixer::create({1});
  auto rate = Mixer::create({2, 44100});
  auto clip = Clip::fromPcm(std::vector<float>(1024, 0.00001F), 1, 48000);
  REQUIRE(output);
  REQUIRE(mixer);
  REQUIRE(mono);
  REQUIRE(rate);
  REQUIRE(clip);
  auto voice = mixer->createVoice(*clip, {1, 0, 1, true});
  REQUIRE(voice);
  REQUIRE(mixer->play(*voice));
  CHECK_FALSE(output->mix(*mono, 64));
  CHECK_FALSE(output->mix(*rate, 64));
  CHECK_FALSE(output->mix(*mixer, 0));
  CHECK_FALSE(output->mix(*mixer, 65537));
  CHECK(mixer->stats()->renderedFrames == 0);
  REQUIRE(output->mix(*mixer, 4096));
  CHECK_FALSE(output->mix(*mixer, 1));
  CHECK(mixer->stats()->renderedFrames == 4096);
  REQUIRE(output->start());
  REQUIRE(waitFor([&] { return output->stats()->consumedFrames == 4096; }));
  REQUIRE(output->stop());
  CHECK(output->stats()->clippedSamples == 0);
}

TEST_CASE("audio.output_invalid_samples_clear_and_volume_preserve_queue") {
  auto output = Output::create(config());
  REQUIRE(output);
  CHECK_FALSE(output->submit({}));
  CHECK_FALSE(output->submit(std::array{0.0F}));
  CHECK_FALSE(output->submit(std::vector<float>(65537 * 2)));

  for (float value :
       {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
    CHECK_FALSE(output->submit(std::array{value, 0.0F}));
    CHECK_FALSE(output->setVolume(value));
  }

  CHECK_FALSE(output->setVolume(-0.01F));
  CHECK_FALSE(output->setVolume(1.01F));
  REQUIRE(output->setVolume(1));
  REQUIRE(output->setVolume(0));
  REQUIRE(output->submit(std::vector<float>(8192, 0.00001F)));
  CHECK_FALSE(output->submit(std::array{0.0F, 0.0F}));
  CHECK(output->stats()->submittedFrames == 4096);
  REQUIRE(output->clear());
  CHECK(output->stats()->queuedFrames == 0);
  CHECK(output->stats()->discardedFrames == 4096);
  CHECK(output->stats()->submittedFrames == 4096);
  REQUIRE(output->start());
  CHECK_FALSE(output->clear());
  REQUIRE(output->stop());
  REQUIRE(output->clear());
}

TEST_CASE("audio.output_failed_context_or_device_preparation_releases_native_allocations") {
  const auto before = Output::backendMemory();

  for (std::uint64_t budget : std::array<std::uint64_t, 5>{1, 16, 256, 512, 1024}) {
    auto limited = config();
    limited.backendBytes = budget;
    CHECK_FALSE(Output::create(limited));
    CHECK(Output::backendMemory().bytes == before.bytes);
    CHECK(Output::backendMemory().allocations == before.allocations);
  }

  auto unavailable = config();
#if defined(__APPLE__)
  unavailable.backend = AudioBackend::Wasapi;
#else
  unavailable.backend = AudioBackend::CoreAudio;
#endif
  auto failed = Output::create(unavailable);
  REQUIRE_FALSE(failed);
  CHECK(failed.error().code == Dot::Core::ErrorCode::Unavailable);
  CHECK(Output::backendMemory().bytes == before.bytes);
  CHECK(Output::backendMemory().allocations == before.allocations);
}

TEST_CASE("audio.output_moves_preserve_queue_and_thread_ownership") {
  const auto before = Output::backendMemory();

  {
    auto output = Output::create(config());
    auto target = Output::create(config());
    REQUIRE(output);
    REQUIRE(target);
    REQUIRE(output->submit(std::vector<float>(2048, 0.00001F)));
    auto moved = std::move(*output);
    CHECK_FALSE(output->info());
    CHECK_FALSE(output->stats());
    CHECK_FALSE(output->submit(std::array{0.0F, 0.0F}));
    CHECK_FALSE(output->start());
    CHECK_FALSE(output->stop());
    CHECK_FALSE(output->clear());
    CHECK_FALSE(output->setVolume(0));
    auto mixer = Mixer::create();
    REQUIRE(mixer);
    CHECK_FALSE(output->mix(*mixer, 1));
    *target = std::move(moved);
    CHECK_FALSE(moved.stats());
    CHECK(target->stats()->queuedFrames == 1024);
    auto rejected = std::async(std::launch::async, [&] {
      return !target->info() && !target->stats() && !target->submit(std::array{0.0F, 0.0F})
             && !target->start() && !target->stop() && !target->clear() && !target->setVolume(0)
             && !target->mix(*mixer, 1);
    });
    CHECK(rejected.get());
    REQUIRE(target->start());
    REQUIRE(waitFor([&] { return target->stats()->consumedFrames == 1024; }));
  }

  CHECK(Output::backendMemory().allocations == before.allocations);
  CHECK(Output::backendMemory().bytes == before.bytes);
}

TEST_CASE("audio.output_repeated_running_destruction_and_foreign_quiescent_cleanup") {
  const auto before = Output::backendMemory();

  for (int iteration = 0; iteration < 4; ++iteration) {
    auto output = Output::create(config());
    REQUIRE(output);
    REQUIRE(output->submit(std::vector<float>(2048, 0.00001F)));
    REQUIRE(output->start());
    REQUIRE(waitFor([&] { return output->stats()->consumedFrames > 0; }));
    auto cleanup
        = std::async(std::launch::async, [owned = std::move(*output)] { return !owned.stats(); });
    CHECK(cleanup.get());
    CHECK(Output::backendMemory().bytes == before.bytes);
    CHECK(Output::backendMemory().allocations == before.allocations);
  }
}

TEST_CASE("audio.output_invalid_configuration_fails_before_backend_creation") {
  const auto before = Output::backendMemory();
  std::vector<OutputConfig> invalid;
  auto add = [&](auto change) {
    auto value = config();
    change(value);
    invalid.push_back(value);
  };
  add([](auto& value) { value.channels = 0; });
  add([](auto& value) { value.channels = 3; });
  add([](auto& value) { value.sampleRate = 7999; });
  add([](auto& value) { value.sampleRate = 192001; });
  add([](auto& value) { value.queueFrames = 0; });
  add([](auto& value) { value.queueFrames = 32; });
  add([](auto& value) { value.queueFrames = 4095; });
  add([](auto& value) { value.queueFrames = 524288; });
  add([](auto& value) { value.periodFrames = 63; });
  add([](auto& value) { value.periodFrames = 8193; });
  add([](auto& value) { value.periodFrames = 8192; });
  add([](auto& value) { value.backendBytes = 0; });
  add([](auto& value) { value.backendBytes = 1024ULL * 1024 * 1024 + 1; });
  add([](auto& value) { value.backend = static_cast<AudioBackend>(99); });
  add([](auto& value) { value.volume = -0.01F; });
  add([](auto& value) { value.volume = 1.01F; });
  add([](auto& value) { value.volume = std::numeric_limits<float>::quiet_NaN(); });

  for (const auto& value : invalid) {
    auto result = Output::create(value);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Dot::Core::ErrorCode::InvalidArgument);
  }

  CHECK(Output::backendMemory().bytes == before.bytes);
  CHECK(Output::backendMemory().allocations == before.allocations);
}

TEST_CASE("audio.output_mono_and_client_rate_conversion_use_real_device") {
  for (auto rate : std::array<std::uint32_t, 3>{8000, 44100, 192000}) {
    auto properties = config();
    properties.channels = 1;
    properties.sampleRate = rate;
    properties.queueFrames = 64;
    properties.periodFrames = 64;
    auto output = Output::create(properties);
    REQUIRE_MESSAGE(output, (output ? "" : output.error().message));
    REQUIRE(output->info());
    CHECK(output->info()->sampleRate == rate);
    CHECK(output->info()->channels == 1);
    REQUIRE(output->submit(std::array<float, 64>{}));
    REQUIRE(output->start());
    REQUIRE(waitFor([&] { return output->stats()->consumedFrames == 64; }));
    REQUIRE(output->stop());
    CHECK(output->stats()->fault == OutputFault::None);
  }
}

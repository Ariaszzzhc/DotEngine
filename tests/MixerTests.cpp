#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <future>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

import Dot.Audio.Mixer;
import Dot.Core.Result;

namespace {
  using namespace Dot::Audio;

  auto constant(float value = 0.25F, std::size_t frames = 4096, std::uint32_t channels = 1,
                std::uint32_t rate = 48000) -> Dot::Core::Result<Clip> {
    return Clip::fromPcm(std::vector<float>(frames * channels, value), channels, rate);
  }

  auto allNear(std::span<const float> samples, float value) -> bool {
    return std::ranges::all_of(
        samples, [value](float sample) { return std::abs(sample - value) < 0.0001F; });
  }
}  // namespace

TEST_CASE("audio.mixer_empty_graph_renders_silence_and_tracks_frames") {
  const auto before = Mixer::backendMemory();

  {
    auto mixer = Mixer::create();
    REQUIRE_MESSAGE(mixer, (mixer ? "" : mixer.error().message));
    auto stats = mixer->stats();
    REQUIRE(stats);
    CHECK(stats->voices == 0);
    CHECK(stats->backend.allocations > 0);
    CHECK(stats->backend.bytes > 0);
    std::array<float, 20> output;
    output.fill(99);
    REQUIRE(mixer->render(output));
    CHECK(allNear(output, 0));
    CHECK(mixer->stats()->renderedFrames == 10);
    CHECK_FALSE(mixer->stats()->faulted);
  }

  CHECK(Mixer::backendMemory().allocations == before.allocations);
  CHECK(Mixer::backendMemory().bytes == before.bytes);
}

TEST_CASE("audio.mixer_mono_source_expands_and_applies_voice_and_master_volume") {
  auto mixer = Mixer::create();
  auto clip = constant(0.5F);
  REQUIRE(mixer);
  REQUIRE(clip);
  auto voice = mixer->createVoice(*clip, {0.5F});
  REQUIRE(voice);
  CHECK_FALSE(mixer->voice(*voice)->playing);
  REQUIRE(mixer->play(*voice));
  REQUIRE(mixer->setVolume(0.5F));
  std::array<float, 256> output{};
  REQUIRE(mixer->render(output));
  CHECK(allNear(output, 0.125F));
  CHECK(mixer->stats()->playing == 1);
  REQUIRE(mixer->setVolume(0));
  REQUIRE(mixer->render(output));
  CHECK(allNear(output, 0));
  REQUIRE(mixer->setVolume(1));
  REQUIRE(mixer->configureVoice(*voice, {0.25F}));
  REQUIRE(mixer->render(output));
  CHECK(allNear(output, 0.125F));
}

TEST_CASE("audio.mixer_sums_real_voices_without_hidden_clipping") {
  auto mixer = Mixer::create({1});
  auto clip = constant(0.75F);
  REQUIRE(mixer);
  REQUIRE(clip);
  auto first = mixer->createVoice(*clip);
  auto second = mixer->createVoice(*clip);
  REQUIRE(first);
  REQUIRE(second);
  REQUIRE(mixer->play(*first));
  REQUIRE(mixer->play(*second));
  std::array<float, 32> output{};
  REQUIRE(mixer->render(output));
  CHECK(allNear(output, 1.5F));
  REQUIRE(mixer->destroyVoice(*first));
  REQUIRE(mixer->render(output));
  CHECK(allNear(output, 0.75F));
  CHECK(mixer->stats()->voices == 1);
}

TEST_CASE("audio.mixer_stereo_preserves_channels_and_pans_to_each_side") {
  auto mixer = Mixer::create();
  std::vector<float> source(2048);

  for (std::size_t index = 0; index < source.size(); index += 2) {
    source[index] = 0.25F;
    source[index + 1] = -0.5F;
  }

  auto clip = Clip::fromPcm(source, 2, 48000);
  REQUIRE(mixer);
  REQUIRE(clip);
  auto voice = mixer->createVoice(*clip);
  REQUIRE(voice);
  REQUIRE(mixer->play(*voice));
  std::array<float, 64> output{};
  REQUIRE(mixer->render(output));
  CHECK(output[0] == doctest::Approx(0.25));
  CHECK(output[1] == doctest::Approx(-0.5));
  REQUIRE(mixer->configureVoice(*voice, {1, -1}));
  REQUIRE(mixer->render(output));
  CHECK(output[0] == doctest::Approx(-0.25));
  CHECK(output[1] == 0);
  REQUIRE(mixer->configureVoice(*voice, {1, 1}));
  REQUIRE(mixer->render(output));
  CHECK(output[0] == 0);
  CHECK(output[1] == doctest::Approx(-0.25));
}

TEST_CASE("audio.mixer_pausing_resumes_position_and_seek_changes_source_frame") {
  auto mixer = Mixer::create({1});
  const std::array<float, 8> source{0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F};
  auto clip = Clip::fromPcm(source, 1, 48000);
  REQUIRE(mixer);
  REQUIRE(clip);
  auto voice = mixer->createVoice(*clip);
  REQUIRE(voice);
  REQUIRE(mixer->play(*voice));
  std::array<float, 2> output{};
  REQUIRE(mixer->render(output));
  CHECK(output[0] == doctest::Approx(0.1));
  CHECK(output[1] == doctest::Approx(0.2));
  REQUIRE(mixer->pause(*voice));
  REQUIRE(mixer->render(output));
  CHECK(allNear(output, 0));
  CHECK_FALSE(mixer->voice(*voice)->playing);
  REQUIRE(mixer->play(*voice));
  REQUIRE(mixer->render(output));
  CHECK(output[0] == doctest::Approx(0.3));
  CHECK(output[1] == doctest::Approx(0.4));
  REQUIRE(mixer->seek(*voice, 6));
  REQUIRE(mixer->render(output));
  CHECK(output[0] == doctest::Approx(0.7));
  CHECK(output[1] == doctest::Approx(0.8));
  REQUIRE(mixer->pause(*voice));
  REQUIRE(mixer->seek(*voice, 1));
  REQUIRE(mixer->render(output));
  CHECK(allNear(output, 0));
  REQUIRE(mixer->play(*voice));
  REQUIRE(mixer->render(output));
  CHECK(output[0] == doctest::Approx(0.2));
}

TEST_CASE("audio.mixer_nonlooping_end_is_retained_and_play_restarts") {
  auto mixer = Mixer::create({1});
  auto clip = constant(0.25F, 4);
  REQUIRE(mixer);
  REQUIRE(clip);
  auto voice = mixer->createVoice(*clip);
  REQUIRE(voice);
  REQUIRE(mixer->play(*voice));
  std::array<float, 16> output{};
  REQUIRE(mixer->render(output));
  CHECK(allNear(std::span{output}.first(4), 0.25F));
  CHECK(allNear(std::span{output}.subspan(4), 0));
  REQUIRE(mixer->render(output));
  CHECK(allNear(output, 0));
  CHECK(mixer->voice(*voice)->atEnd);
  CHECK_FALSE(mixer->voice(*voice)->playing);
  CHECK(mixer->stats()->voices == 1);
  REQUIRE(mixer->play(*voice));
  REQUIRE(mixer->render(output));
  CHECK(allNear(std::span{output}.first(4), 0.25F));
}

TEST_CASE("audio.mixer_loop_repeats_exact_samples_and_can_be_disabled") {
  auto mixer = Mixer::create({1});
  const std::array<float, 2> source{0.25F, -0.25F};
  auto clip = Clip::fromPcm(source, 1, 48000);
  REQUIRE(mixer);
  REQUIRE(clip);
  auto voice = mixer->createVoice(*clip, {1, 0, 1, true});
  REQUIRE(voice);
  REQUIRE(mixer->play(*voice));
  std::array<float, 64> output{};
  REQUIRE(mixer->render(output));

  for (std::size_t index = 0; index < output.size(); ++index) {
    CHECK(output[index] == source[index % 2]);
  }

  CHECK_FALSE(mixer->voice(*voice)->atEnd);
  REQUIRE(mixer->configureVoice(*voice, {}));
  REQUIRE(mixer->render(output));
  REQUIRE(mixer->render(output));
  CHECK(mixer->voice(*voice)->atEnd);
}

TEST_CASE("audio.mixer_resamples_source_rate_and_pitch_to_real_output") {
  auto mixer = Mixer::create({1, 48000});
  std::vector<float> source(48000);

  for (std::size_t index = 0; index < source.size(); ++index) {
    source[index] = static_cast<float>(
        0.25 * std::sin(2 * 3.141592653589793 * static_cast<double>(index) / 48));
  }

  auto clip = Clip::fromPcm(source, 1, 24000);
  REQUIRE(mixer);
  REQUIRE(clip);
  auto voice = mixer->createVoice(*clip);
  REQUIRE(voice);
  REQUIRE(mixer->play(*voice));
  std::array<float, 4800> output{};
  REQUIRE(mixer->render(output));
  const auto crossings = [&] {
    int count = 0;

    for (std::size_t index = 100; index < output.size(); ++index) {
      count += output[index - 1] <= 0 && output[index] > 0 ? 1 : 0;
    }

    return count;
  };
  const auto normal = crossings();
  CHECK(normal >= 48);
  CHECK(normal <= 50);
  REQUIRE(mixer->configureVoice(*voice, {1, 0, 2}));
  REQUIRE(mixer->render(output));
  CHECK(crossings() >= 97);
  CHECK(crossings() <= 99);
}

TEST_CASE("audio.mixer_invalid_settings_and_buffers_leave_state_unchanged") {
  auto mixer = Mixer::create();
  auto clip = constant();
  REQUIRE(mixer);
  REQUIRE(clip);
  auto voice = mixer->createVoice(*clip);
  REQUIRE(voice);
  REQUIRE(mixer->play(*voice));

  for (float invalid : {-1.1F, 4.1F, std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
    CHECK_FALSE(mixer->configureVoice(*voice, {invalid}));
    CHECK_FALSE(mixer->configureVoice(*voice, {1, invalid}));
    CHECK_FALSE(mixer->configureVoice(*voice, {1, 0, invalid}));
    CHECK_FALSE(mixer->setVolume(invalid));
    CHECK_FALSE(mixer->createVoice(*clip, {invalid}));
    CHECK(mixer->voice(*voice)->settings == VoiceSettings{});
  }

  CHECK_FALSE(mixer->seek(*voice, clip->info()->frames));
  CHECK_FALSE(mixer->seek(*voice, std::numeric_limits<std::uint64_t>::max()));
  CHECK_FALSE(mixer->render({}));
  std::array<float, 3> odd{99, 99, 99};
  CHECK_FALSE(mixer->render(odd));
  CHECK(allNear(odd, 99));
  std::vector<float> large(65537 * 2, 99);
  CHECK_FALSE(mixer->render(large));
  CHECK(allNear(large, 99));
  CHECK(mixer->stats()->renderedFrames == 0);
  CHECK(mixer->stats()->voices == 1);
  CHECK_FALSE(mixer->stats()->faulted);
  REQUIRE(mixer->configureVoice(*voice, {0, -1, 0.25F, true}));
  REQUIRE(mixer->configureVoice(*voice, {1, 1, 4}));
}

TEST_CASE("audio.mixer_capacity_and_stale_foreign_handles_are_explicit") {
  auto mixer = Mixer::create({2, 48000, 1});
  auto other = Mixer::create();
  auto clip = constant();
  REQUIRE(mixer);
  REQUIRE(other);
  REQUIRE(clip);
  auto voice = mixer->createVoice(*clip);
  REQUIRE(voice);
  CHECK_FALSE(mixer->createVoice(*clip));
  CHECK_FALSE(other->play(*voice));
  CHECK_FALSE(other->pause(*voice));
  CHECK_FALSE(other->seek(*voice, 0));
  CHECK_FALSE(other->configureVoice(*voice, {}));
  CHECK_FALSE(other->destroyVoice(*voice));
  CHECK_FALSE(other->voice(*voice));
  CHECK_FALSE(mixer->play({}));
  REQUIRE(mixer->destroyVoice(*voice));
  CHECK_FALSE(mixer->play(*voice));
  CHECK_FALSE(mixer->destroyVoice(*voice));
  auto next = mixer->createVoice(*clip);
  REQUIRE(next);
  CHECK(next->value > voice->value);
  CHECK(next->mixer == voice->mixer);
  CHECK(mixer->stats()->voices == 1);
}

TEST_CASE("audio.mixer_retains_clip_and_releases_voice_backend_allocations") {
  auto mixer = Mixer::create();
  REQUIRE(mixer);
  const auto baseline = mixer->stats()->backend;
  std::optional<VoiceId> voice;

  {
    auto clip = constant();
    REQUIRE(clip);
    auto created = mixer->createVoice(*clip);
    REQUIRE(created);
    voice = *created;
  }

  CHECK(mixer->stats()->backend.bytes > baseline.bytes);
  REQUIRE(mixer->play(*voice));
  std::array<float, 128> output{};
  REQUIRE(mixer->render(output));
  CHECK(allNear(output, 0.25F));
  REQUIRE(mixer->destroyVoice(*voice));
  CHECK(mixer->stats()->backend.bytes == baseline.bytes);
  CHECK(mixer->stats()->backend.allocations == baseline.allocations);
  REQUIRE(mixer->render(output));
  CHECK(allNear(output, 0));
}

TEST_CASE("audio.mixer_invalid_configuration_and_failed_initialization_release_memory") {
  const auto baseline = Mixer::backendMemory();

  for (auto config :
       {MixerConfig{0}, MixerConfig{3}, MixerConfig{2, 7999}, MixerConfig{2, 192001},
        MixerConfig{2, 48000, 0}, MixerConfig{2, 48000, 1025}, MixerConfig{2, 48000, 1, 0},
        MixerConfig{2, 48000, 1, 1024ULL * 1024 * 1024 + 1}}) {
    CHECK_FALSE(Mixer::create(config));
  }

  for (auto budget : std::array<std::uint64_t, 4>{1, 16, 256, 4096}) {
    auto failed = Mixer::create({2, 48000, 64, budget});
    CHECK_FALSE(failed);
    CHECK(Mixer::backendMemory().bytes == baseline.bytes);
    CHECK(Mixer::backendMemory().allocations == baseline.allocations);
  }

  CHECK(Mixer::create({1, 8000, 1}));
  CHECK(Mixer::create({2, 192000, 1024}));
}

TEST_CASE("audio.mixer_move_retains_handles_and_assignment_releases_old_graph") {
  const auto baseline = Mixer::backendMemory();

  {
    auto created = Mixer::create();
    auto target = Mixer::create();
    auto clip = constant();
    REQUIRE(created);
    REQUIRE(target);
    REQUIRE(clip);
    auto voice = created->createVoice(*clip);
    auto old = target->createVoice(*clip);
    REQUIRE(voice);
    REQUIRE(old);
    auto moved = std::move(*created);
    CHECK_FALSE(created->stats());
    CHECK_FALSE(created->render({}));
    CHECK_FALSE(created->createVoice(*clip));
    CHECK_FALSE(created->destroyVoice(*voice));
    CHECK_FALSE(created->play(*voice));
    CHECK_FALSE(created->pause(*voice));
    CHECK_FALSE(created->seek(*voice, 0));
    CHECK_FALSE(created->configureVoice(*voice, {}));
    CHECK_FALSE(created->voice(*voice));
    CHECK_FALSE(created->setVolume(1));
    *target = std::move(moved);
    CHECK_FALSE(moved.stats());
    CHECK_FALSE(target->voice(*old));
    REQUIRE(target->play(*voice));
    std::array<float, 32> output{};
    REQUIRE(target->render(output));
    CHECK(allNear(output, 0.25F));
  }

  CHECK(Mixer::backendMemory().bytes == baseline.bytes);
  CHECK(Mixer::backendMemory().allocations == baseline.allocations);
}

TEST_CASE("audio.mixer_owner_thread_checks_and_quiescent_foreign_destruction") {
  const auto baseline = Mixer::backendMemory();
  auto mixer = Mixer::create();
  auto clip = constant();
  REQUIRE(mixer);
  REQUIRE(clip);
  auto voice = mixer->createVoice(*clip);
  REQUIRE(voice);
  auto checks = std::async(std::launch::async, [&] {
    std::array<float, 4> output{99, 99, 99, 99};
    return !mixer->stats() && !mixer->createVoice(*clip) && !mixer->destroyVoice(*voice)
           && !mixer->play(*voice) && !mixer->pause(*voice) && !mixer->seek(*voice, 0)
           && !mixer->configureVoice(*voice, {}) && !mixer->voice(*voice) && !mixer->setVolume(1)
           && !mixer->render(output) && allNear(output, 99);
  });
  CHECK(checks.get());
  auto released = std::async(std::launch::async,
                             [owned = std::move(*mixer)]() mutable { return !owned.stats(); });
  CHECK(released.get());
  CHECK(Mixer::backendMemory().bytes == baseline.bytes);
  CHECK(Mixer::backendMemory().allocations == baseline.allocations);
}

TEST_CASE("audio.mixer_repeated_voice_lifetimes_and_initialization_are_bounded") {
  const auto baseline = Mixer::backendMemory();
  auto clip = constant(0.25F, 16);
  REQUIRE(clip);

  for (int iteration = 0; iteration < 16; ++iteration) {
    auto mixer = Mixer::create({1, 48000, 1});
    REQUIRE(mixer);
    const auto initial = mixer->stats()->backend;

    for (int cycle = 0; cycle < 32; ++cycle) {
      auto voice = mixer->createVoice(*clip, {1, 0, 1, true});
      REQUIRE(voice);
      REQUIRE(mixer->play(*voice));
      std::array<float, 64> output{};
      REQUIRE(mixer->render(output));
      REQUIRE(mixer->destroyVoice(*voice));
      CHECK(mixer->stats()->backend.bytes == initial.bytes);
      CHECK(mixer->stats()->backend.allocations == initial.allocations);
    }
  }

  CHECK(Mixer::backendMemory().bytes == baseline.bytes);
  CHECK(Mixer::backendMemory().allocations == baseline.allocations);
}

TEST_CASE("audio.mixer_backend_voice_allocation_failure_preserves_graph_and_budget") {
  std::uint64_t bytes = 0;

  {
    auto reference = Mixer::create();
    REQUIRE(reference);
    bytes = reference->stats()->backend.bytes;
  }

  auto limited = Mixer::create({2, 48000, 64, bytes});
  auto clip = constant();
  REQUIRE_MESSAGE(limited, (limited ? "" : limited.error().message));
  REQUIRE(clip);
  const auto baseline = limited->stats()->backend;

  for (int attempt = 0; attempt < 8; ++attempt) {
    auto failed = limited->createVoice(*clip);
    CHECK_FALSE(failed);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == Dot::Core::ErrorCode::External);
    CHECK(limited->stats()->backend.bytes == baseline.bytes);
    CHECK(limited->stats()->backend.allocations == baseline.allocations);
    CHECK(limited->stats()->voices == 0);
    CHECK_FALSE(limited->stats()->faulted);
    std::array<float, 32> output{};
    REQUIRE(limited->render(output));
    CHECK(allNear(output, 0));
  }
}

TEST_CASE("audio.mixer_resampled_seek_matches_fresh_graph_without_old_samples") {
  auto mixer = Mixer::create();
  auto fresh = Mixer::create();
  std::vector<float> source(8192);

  for (std::size_t index = 0; index < source.size(); ++index) {
    source[index] = index < 4096 ? 0.5F : -0.5F;
  }

  auto clip = Clip::fromPcm(source, 1, 24000);
  REQUIRE(mixer);
  REQUIRE(fresh);
  REQUIRE(clip);
  auto first = mixer->createVoice(*clip, {1, 0, 1.5F});
  auto second = fresh->createVoice(*clip, {1, 0, 1.5F});
  REQUIRE(first);
  REQUIRE(second);
  REQUIRE(mixer->play(*first));
  std::array<float, 64> output{};
  REQUIRE(mixer->render(output));
  REQUIRE(mixer->seek(*first, 6000));
  REQUIRE(fresh->seek(*second, 6000));
  REQUIRE(fresh->play(*second));
  std::array<float, 64> expected{};
  REQUIRE(mixer->render(output));
  REQUIRE(fresh->render(expected));

  for (std::size_t index = 0; index < output.size(); ++index) {
    CHECK(output[index] == doctest::Approx(expected[index]));
  }

  CHECK(output.back() < -0.49F);
}

TEST_CASE("audio.mixer_loop_changes_retain_unconsumed_position_and_end_seek_can_restart") {
  auto mixer = Mixer::create({1});
  const std::array<float, 5> source{0.1F, 0.2F, 0.3F, 0.4F, 0.5F};
  auto clip = Clip::fromPcm(source, 1, 48000);
  REQUIRE(mixer);
  REQUIRE(clip);
  auto voice = mixer->createVoice(*clip, {1, 0, 1, true});
  REQUIRE(voice);
  REQUIRE(mixer->play(*voice));
  std::array<float, 2> first{};
  REQUIRE(mixer->render(first));
  CHECK(first[1] == doctest::Approx(0.2));
  REQUIRE(mixer->configureVoice(*voice, {}));
  std::array<float, 8> output{};
  REQUIRE(mixer->render(output));
  CHECK(output[0] == doctest::Approx(0.3));
  CHECK(output[1] == doctest::Approx(0.4));
  CHECK(output[2] == doctest::Approx(0.5));
  CHECK(allNear(std::span{output}.subspan(3), 0));
  REQUIRE(mixer->render(output));
  CHECK(mixer->voice(*voice)->atEnd);
  REQUIRE(mixer->seek(*voice, 3));
  CHECK_FALSE(mixer->voice(*voice)->atEnd);
  CHECK_FALSE(mixer->voice(*voice)->playing);
  REQUIRE(mixer->play(*voice));
  REQUIRE(mixer->render(output));
  CHECK(output[0] == doctest::Approx(0.4));
  CHECK(output[1] == doctest::Approx(0.5));
  REQUIRE(mixer->play(*voice));
  REQUIRE(mixer->seek(*voice, 0));
  REQUIRE(mixer->render(first));
  CHECK(first[1] == doctest::Approx(0.2));
  REQUIRE(mixer->configureVoice(*voice, {1, 0, 1, true}));
  REQUIRE(mixer->render(output));
  CHECK(output[0] == doctest::Approx(0.3));
  CHECK(output[3] == doctest::Approx(0.1));
}

TEST_CASE("audio.mixer_supports_maximum_frame_blocks_and_moved_clip_rejection") {
  auto mixer = Mixer::create({2, 48000, 1});
  auto clip = constant(0.25F, 128);
  REQUIRE(mixer);
  REQUIRE(clip);
  auto retained = std::move(*clip);
  CHECK_FALSE(mixer->createVoice(*clip));
  auto voice = mixer->createVoice(retained, {1, 0, 1, true});
  REQUIRE(voice);
  REQUIRE(mixer->play(*voice));
  std::vector<float> output(65536 * 2);
  REQUIRE(mixer->render(output));
  CHECK(allNear(output, 0.25F));
  CHECK(mixer->stats()->renderedFrames == 65536);
}

TEST_CASE("audio.mixer_independent_graphs_render_on_separate_owner_threads") {
  const auto baseline = Mixer::backendMemory();
  auto clip = constant();
  REQUIRE(clip);
  std::vector<std::future<bool>> workers;

  for (int index = 0; index < 4; ++index) {
    workers.push_back(std::async(std::launch::async, [source = *clip] {
      auto mixer = Mixer::create();

      if (!mixer) {
        return false;
      }

      auto voice = mixer->createVoice(source, {1, 0, 1, true});

      if (!voice || !mixer->play(*voice)) {
        return false;
      }

      for (int iteration = 0; iteration < 32; ++iteration) {
        std::array<float, 128> output{};

        if (!mixer->render(output) || !allNear(output, 0.25F)) {
          return false;
        }
      }

      return mixer->stats()->voices == 1;
    }));
  }

  for (auto& worker : workers) {
    CHECK(worker.get());
  }

  CHECK(Mixer::backendMemory().bytes == baseline.bytes);
  CHECK(Mixer::backendMemory().allocations == baseline.allocations);
}

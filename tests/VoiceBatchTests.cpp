#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

import Dot.Audio.Mixer;
import Dot.Core.Result;

namespace {
  using namespace Dot::Audio;

  auto clip(float sample = 0.25F, std::uint32_t channels = 1) -> Clip {
    auto result = Clip::fromPcm(std::vector<float>(4096 * channels, sample), channels, 48000);
    REQUIRE(result);
    return *result;
  }

  auto spatial(float x) -> SpatialSettings {
    SpatialSettings result;
    result.enabled = true;
    result.position = {x, 0, 0};
    result.rolloff = 0;
    return result;
  }

  auto allNear(std::span<const float> samples, float expected) -> bool {
    return std::ranges::all_of(samples,
                               [=](float sample) { return std::abs(sample - expected) < 1e-5F; });
  }
}  // namespace

TEST_CASE("audio.batch_creates_ordered_owned_voices_with_real_pcm_and_listener") {
  auto mixer = Mixer::create({1});
  REQUIRE(mixer);
  std::vector<VoiceId> ids;

  {
    VoiceBatch batch;
    batch.create.push_back({clip(0.25F), {1, 0, 1, true}, {}, true});
    batch.create.push_back({clip(0.5F), {0.5F, 0, 1, true}, {}, true});
    batch.listener = Listener{{1, 2, 3}, {0, 0, 0, 2}};
    auto created = mixer->applyChanges(batch);
    REQUIRE(created);
    ids = *created;
  }

  REQUIRE(ids.size() == 2);
  CHECK(ids[0].mixer == ids[1].mixer);
  CHECK(ids[1].value > ids[0].value);
  CHECK(mixer->voice(ids[0])->settings.volume == 1);
  CHECK(mixer->voice(ids[1])->settings.volume == 0.5F);
  CHECK(mixer->listener()->position == Dot::Math::Vec3{1, 2, 3});
  CHECK(mixer->listener()->rotation.w == 1);
  CHECK(mixer->stats()->renderedFrames == 0);
  CHECK(mixer->stats()->playing == 2);
  std::array<float, 32> output{};
  REQUIRE(mixer->render(output));
  CHECK(allNear(output, 0.5F));
}

TEST_CASE("audio.batch_updates_preserve_cursor_handle_and_omitted_properties") {
  auto mixer = Mixer::create({1});
  const std::array<float, 8> samples{0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F};
  auto source = Clip::fromPcm(samples, 1, 48000);
  REQUIRE(mixer);
  REQUIRE(source);
  auto id = mixer->createVoice(*source);
  REQUIRE(id);
  REQUIRE(mixer->play(*id));
  std::array<float, 2> output{};
  REQUIRE(mixer->render(output));
  VoiceBatch batch;
  batch.update.push_back({*id, VoiceSettings{0.5F}});
  batch.listener = Listener{{1, 0, 0}};
  REQUIRE(mixer->applyChanges(batch));
  CHECK(mixer->stats()->renderedFrames == 2);
  CHECK(mixer->voice(*id)->playing);
  REQUIRE(mixer->render(output));
  CHECK(output[0] == doctest::Approx(0.15));
  CHECK(output[1] == doctest::Approx(0.2));
  batch = {};
  batch.update.push_back({*id, std::nullopt, std::nullopt, false});
  REQUIRE(mixer->applyChanges(batch));
  REQUIRE(mixer->render(output));
  CHECK(allNear(output, 0));
  batch.update.front().playing = true;
  REQUIRE(mixer->applyChanges(batch));
  REQUIRE(mixer->render(output));
  CHECK(output[0] == doctest::Approx(0.25));
  CHECK(output[1] == doctest::Approx(0.3));
}

TEST_CASE("audio.batch_replaces_at_full_capacity_without_reusing_retired_handles") {
  auto mixer = Mixer::create({1, 48000, 1});
  REQUIRE(mixer);
  auto old = mixer->createVoice(clip());
  REQUIRE(old);
  REQUIRE(mixer->play(*old));
  VoiceBatch batch;
  batch.remove.push_back(*old);
  batch.create.push_back({clip(0.75F), {}, {}, true});
  auto created = mixer->applyChanges(batch);
  REQUIRE(created);
  REQUIRE(created->size() == 1);
  CHECK(created->front().value > old->value);
  CHECK_FALSE(mixer->voice(*old));
  CHECK(mixer->stats()->voices == 1);
  std::array<float, 32> output{};
  REQUIRE(mixer->render(output));
  CHECK(allNear(output, 0.75F));
  CHECK_FALSE(mixer->applyChanges(batch));
  CHECK(mixer->voice(created->front())->playing);
}

TEST_CASE("audio.batch_validates_combined_pan_spatial_transitions") {
  auto mixer = Mixer::create();
  REQUIRE(mixer);
  auto id = mixer->createVoice(clip(), {1, 0.5F, 1, true});
  REQUIRE(id);
  VoiceBatch batch;
  batch.update.push_back({*id, VoiceSettings{0.5F, 0, 1, true}, spatial(-10)});
  REQUIRE(mixer->applyChanges(batch));
  CHECK(mixer->voice(*id)->spatial.enabled);
  CHECK(mixer->voice(*id)->settings.pan == 0);
  SpatialSettings flat;
  batch.update.front() = {*id, VoiceSettings{0.5F, -0.5F, 1, true}, flat};
  REQUIRE(mixer->applyChanges(batch));
  CHECK_FALSE(mixer->voice(*id)->spatial.enabled);
  CHECK(mixer->voice(*id)->settings.pan == -0.5F);
  batch.update.front().spatial = spatial(10);
  CHECK_FALSE(mixer->applyChanges(batch));
  CHECK_FALSE(mixer->voice(*id)->spatial.enabled);
  CHECK(mixer->voice(*id)->settings.pan == -0.5F);
}

TEST_CASE("audio.batch_spatial_and_listener_commit_changes_actual_stereo_direction") {
  auto mixer = Mixer::create();
  REQUIRE(mixer);
  VoiceBatch batch;
  batch.create.push_back({clip(), {1, 0, 1, true}, spatial(-10), true});
  auto ids = mixer->applyChanges(batch);
  REQUIRE(ids);
  std::array<float, 8192> output{};
  const auto energy = [&](std::size_t channel) {
    double value = 0;

    for (std::size_t index = output.size() / 2 + channel; index < output.size(); index += 2) {
      value += static_cast<double>(output[index]) * output[index];
    }

    return value;
  };
  REQUIRE(mixer->render(output));
  CHECK(energy(0) > energy(1) * 10);
  batch = {};
  batch.update.push_back({ids->front(), std::nullopt, spatial(10)});
  batch.listener = Listener{{0, 0, 0}, {0, 0, 0, 2}};
  REQUIRE(mixer->applyChanges(batch));
  REQUIRE(mixer->render(output));
  CHECK(energy(1) > energy(0) * 10);
  CHECK(mixer->listener()->rotation.w == 1);
  CHECK(mixer->voice(ids->front())->spatial.position.x == 10);
}

TEST_CASE("audio.batch_late_invalid_inputs_leave_every_existing_component_and_cursor_intact") {
  auto mixer = Mixer::create({1});
  REQUIRE(mixer);
  const std::array<float, 8> samples{0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F};
  auto source = Clip::fromPcm(samples, 1, 48000);
  REQUIRE(source);
  auto id = mixer->createVoice(*source);
  auto removed = mixer->createVoice(clip());
  REQUIRE(id);
  REQUIRE(removed);
  REQUIRE(mixer->play(*id));
  std::array<float, 2> output{};
  REQUIRE(mixer->render(output));
  const auto baseline = mixer->stats()->backend;
  VoiceBatch batch;
  batch.update.push_back({*id, VoiceSettings{0.5F}, std::nullopt, false});
  batch.remove.push_back(*removed);
  batch.listener = Listener{{1, 2, 3}};
  batch.create.push_back({clip(0.75F), {}, {}, true});
  batch.create.push_back({clip(), VoiceSettings{-1}, {}, true});
  CHECK_FALSE(mixer->applyChanges(batch));
  CHECK(mixer->stats()->voices == 2);
  CHECK(mixer->stats()->backend.bytes == baseline.bytes);
  CHECK(mixer->voice(*id)->settings.volume == 1);
  CHECK(mixer->voice(*id)->playing);
  CHECK(mixer->voice(*removed));
  CHECK(mixer->listener()->position == Dot::Math::Vec3{});
  CHECK_FALSE(mixer->stats()->faulted);
  REQUIRE(mixer->render(output));
  CHECK(output[0] == doctest::Approx(0.3));
  CHECK(output[1] == doctest::Approx(0.4));
}

TEST_CASE("audio.batch_duplicate_overlap_foreign_and_stale_handles_are_rejected_before_edits") {
  auto mixer = Mixer::create();
  auto other = Mixer::create();
  REQUIRE(mixer);
  REQUIRE(other);
  auto first = mixer->createVoice(clip());
  auto second = mixer->createVoice(clip());
  auto foreign = other->createVoice(clip());
  REQUIRE(first);
  REQUIRE(second);
  REQUIRE(foreign);
  std::vector<VoiceBatch> invalid(5);
  invalid[0].update = {{*first, VoiceSettings{0.5F}}, {*first, VoiceSettings{0.25F}}};
  invalid[1].remove = {*first, *first};
  invalid[2].remove = {*first};
  invalid[2].update = {{*first, VoiceSettings{0.5F}}};
  invalid[3].update = {{*first, VoiceSettings{0.5F}}, {*foreign, VoiceSettings{0.25F}}};
  invalid[4].remove = {*first, {first->mixer, 999}};

  for (const auto& batch : invalid) {
    CHECK_FALSE(mixer->applyChanges(batch));
    CHECK(mixer->stats()->voices == 2);
    CHECK(mixer->voice(*first)->settings.volume == 1);
    CHECK(mixer->voice(*second));
    CHECK_FALSE(mixer->stats()->faulted);
  }
}

TEST_CASE("audio.batch_invalid_listener_and_spatial_clip_boundaries_preserve_graph") {
  auto mixer = Mixer::create();
  REQUIRE(mixer);
  auto id = mixer->createVoice(clip());
  REQUIRE(id);
  VoiceBatch valid;
  valid.remove.push_back(*id);
  valid.create.push_back({clip(), {}, {}, true});
  const auto reject = [&](VoiceBatch batch) {
    CHECK_FALSE(mixer->applyChanges(batch));
    CHECK(mixer->voice(*id));
    CHECK(mixer->stats()->voices == 1);
    CHECK_FALSE(mixer->stats()->faulted);
  };
  auto invalid = valid;
  invalid.listener = Listener{{std::numeric_limits<float>::infinity(), 0, 0}};
  reject(invalid);
  invalid = valid;
  invalid.create.front().clip = clip(0.25F, 2);
  invalid.create.front().spatial = spatial(10);
  reject(invalid);
  invalid = valid;
  invalid.create.front().settings.pitch = std::numeric_limits<float>::quiet_NaN();
  reject(invalid);
  invalid = valid;
  invalid.create.front().spatial.rotation = {0, 0, 0, 0};
  reject(invalid);
  invalid = valid;
  invalid.create.front().spatial.outerConeGain = 2;
  reject(invalid);
  invalid = valid;
  invalid.create.front().spatial = spatial(10);
  invalid.create.front().settings.pan = 0.5F;
  reject(invalid);
  auto retained = std::move(invalid.create.front().clip);
  reject(invalid);
  CHECK(retained.info());
}

TEST_CASE("audio.batch_final_capacity_is_preflighted_and_empty_batches_do_not_advance") {
  auto mixer = Mixer::create({1, 48000, 2});
  REQUIRE(mixer);
  VoiceBatch initial;
  initial.create = {{clip(), {}, {}, true}, {clip(), {}, {}, true}};
  auto ids = mixer->applyChanges(initial);
  REQUIRE(ids);
  const auto frames = mixer->stats()->renderedFrames;
  auto empty = mixer->applyChanges({});
  REQUIRE(empty);
  CHECK(empty->empty());
  CHECK(mixer->stats()->renderedFrames == frames);
  VoiceBatch invalid;
  invalid.remove.push_back(ids->front());
  invalid.create = initial.create;
  CHECK_FALSE(mixer->applyChanges(invalid));
  CHECK(mixer->stats()->voices == 2);
  CHECK(mixer->voice(ids->front()));
  VoiceBatch removal;
  removal.remove = *ids;
  REQUIRE(mixer->applyChanges(removal));
  CHECK(mixer->stats()->voices == 0);
  std::array<float, 32> output{};
  REQUIRE(mixer->render(output));
  CHECK(allNear(output, 0));
}

TEST_CASE("audio.batch_partial_preparation_allocation_failure_restores_old_graph_and_budget") {
  std::uint64_t twoVoiceBudget = 0;

  {
    auto reference = Mixer::create({1});
    REQUIRE(reference);
    REQUIRE(reference->createVoice(clip()));
    REQUIRE(reference->createVoice(clip()));
    twoVoiceBudget = reference->stats()->backend.bytes;
  }

  auto mixer = Mixer::create({1, 48000, 3, twoVoiceBudget});
  REQUIRE(mixer);
  std::vector<float> ramp(4096);

  for (std::size_t frame = 0; frame < ramp.size(); ++frame) {
    ramp[frame] = 0.1F + static_cast<float>(frame) * 0.0001F;
  }

  auto rampClip = Clip::fromPcm(ramp, 1, 48000);
  REQUIRE(rampClip);
  auto id = mixer->createVoice(*rampClip);
  REQUIRE(id);
  REQUIRE(mixer->play(*id));
  const auto baseline = mixer->stats()->backend;
  VoiceBatch probe;
  probe.create.push_back({clip(0.5F), {}, {}, false});
  auto probed = mixer->applyChanges(probe);
  REQUIRE(probed);
  const auto expectedNext = probed->front().value + 1;
  probe = {};
  probe.remove = *probed;
  REQUIRE(mixer->applyChanges(probe));
  CHECK(mixer->stats()->backend.bytes == baseline.bytes);
  VoiceBatch batch;
  batch.remove.push_back(*id);
  batch.create = {{clip(0.5F), {}, {}, true}, {clip(0.75F), {}, {}, true}};
  batch.listener = Listener{{1, 0, 0}};

  for (int attempt = 0; attempt < 8; ++attempt) {
    auto failed = mixer->applyChanges(batch);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == Dot::Core::ErrorCode::External);
    CHECK(mixer->stats()->backend.bytes == baseline.bytes);
    CHECK(mixer->stats()->backend.allocations == baseline.allocations);
    CHECK(mixer->stats()->voices == 1);
    CHECK(mixer->voice(*id)->playing);
    CHECK(mixer->listener()->position == Dot::Math::Vec3{});
    CHECK_FALSE(mixer->stats()->faulted);
    std::array<float, 32> output{};
    REQUIRE(mixer->render(output));
    for (std::size_t frame = 0; frame < output.size(); ++frame) {
      CHECK(output[frame]
            == doctest::Approx(ramp[static_cast<std::size_t>(attempt) * output.size() + frame]));
    }
  }

  batch.create.erase(batch.create.begin() + 1, batch.create.end());
  auto created = mixer->applyChanges(batch);
  REQUIRE(created);
  CHECK(created->front().value == expectedNext);
  CHECK_FALSE(mixer->voice(*id));
  CHECK(mixer->stats()->voices == 1);
}

TEST_CASE("audio.batch_playing_true_restarts_finished_voices_and_false_retains_end") {
  auto mixer = Mixer::create({1});
  REQUIRE(mixer);
  auto shortClip = Clip::fromPcm(std::array<float, 2>{0.25F, -0.25F}, 1, 48000);
  REQUIRE(shortClip);
  VoiceBatch batch;
  batch.create.push_back({*shortClip, {}, {}, true});
  auto ids = mixer->applyChanges(batch);
  REQUIRE(ids);
  std::array<float, 8> output{};
  REQUIRE(mixer->render(output));
  CHECK(mixer->voice(ids->front())->atEnd);
  batch = {};
  batch.update.push_back({ids->front(), std::nullopt, std::nullopt, false});
  REQUIRE(mixer->applyChanges(batch));
  CHECK(mixer->voice(ids->front())->atEnd);
  batch.update.front().playing = true;
  REQUIRE(mixer->applyChanges(batch));
  REQUIRE(mixer->render(output));
  CHECK(output[0] == doctest::Approx(0.25));
  CHECK(output[1] == doctest::Approx(-0.25));
}

TEST_CASE("audio.batch_move_thread_and_repeated_lifetime_contracts") {
  const auto baseline = Mixer::backendMemory();

  for (int attempt = 0; attempt < 12; ++attempt) {
    auto mixer = Mixer::create();
    REQUIRE(mixer);
    VoiceBatch batch;
    batch.create = {{clip(), {}, {}, true}, {clip(), {}, {}, false}};
    auto ids = mixer->applyChanges(batch);
    REQUIRE(ids);
    auto moved = std::move(*mixer);
    CHECK_FALSE(mixer->applyChanges({}));
    std::atomic<bool> rejected{};
    std::thread worker([&] { rejected = !moved.applyChanges(batch); });
    worker.join();
    CHECK(rejected.load());
    batch = {};
    batch.remove = *ids;
    REQUIRE(moved.applyChanges(batch));
    CHECK(moved.stats()->voices == 0);
  }

  CHECK(Mixer::backendMemory().bytes == baseline.bytes);
  CHECK(Mixer::backendMemory().allocations == baseline.allocations);
}

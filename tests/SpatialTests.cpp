#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <future>
#include <limits>
#include <span>
#include <utility>
#include <vector>

import Dot.Audio.Mixer;
import Dot.Audio.Spatial;
import Dot.Core.Result;
import Dot.Math;

namespace {
  using namespace Dot::Audio;

  auto source() -> Dot::Core::Result<Clip> {
    return Clip::fromPcm(std::vector<float>(4096, 0.25F), 1, 48000);
  }

  auto spatial(Dot::Math::Vec3 position = {0, 0, -1}) -> SpatialSettings {
    SpatialSettings result;
    result.enabled = true;
    result.position = position;
    return result;
  }

  auto energy(std::span<const float> samples, std::size_t channel = 0) -> double {
    double value = 0;

    for (std::size_t index = samples.size() / 2 + channel; index < samples.size(); index += 2) {
      value += static_cast<double>(samples[index]) * samples[index];
    }

    return std::sqrt(value / static_cast<double>(samples.size() / 4));
  }

  auto render(Mixer& mixer) -> std::array<float, 8192> {
    std::array<float, 8192> output{};
    REQUIRE(mixer.render(output));
    return output;
  }
}  // namespace

TEST_CASE("audio.spatial_validates_bounded_poses_and_normalizes_rotations") {
  Listener listener;
  listener.position = {1000000, -1000000, 0};
  listener.velocity = {100, 0, 0};
  listener.rotation = {0, 0, 0, 2};
  auto valid = validatedListener(listener);
  REQUIRE(valid);
  CHECK(valid->rotation.w == 1);
  auto settings = spatial();
  settings.rotation.w = 2;
  settings.minDistance = 0.001F;
  settings.maxDistance = 1000000;
  settings.rolloff = 10;
  settings.dopplerFactor = 1;
  settings.innerConeDegrees = 0;
  settings.outerConeGain = 1;
  auto checked = validatedSpatial(settings);
  REQUIRE(checked);
  CHECK(checked->rotation.w == 1);
}

TEST_CASE("audio.spatial_rejects_invalid_pose_velocity_and_zero_rotation") {
  for (float invalid : {1000001.0F, -1000001.0F, std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
    Listener listener;
    listener.position.x = invalid;
    CHECK_FALSE(validatedListener(listener));
    auto value = spatial();
    value.position.y = invalid;
    CHECK_FALSE(validatedSpatial(value));
  }

  Listener listener;
  listener.velocity = {80, 80, 0};
  CHECK_FALSE(validatedListener(listener));
  listener.velocity = {};
  listener.rotation = {0, 0, 0, 0};
  CHECK_FALSE(validatedListener(listener));
  auto value = spatial();
  value.velocity = {0, 0, 100.01F};
  CHECK_FALSE(validatedSpatial(value));
  value.velocity = {};
  value.rotation = {0, 0, 0, 0};
  CHECK_FALSE(validatedSpatial(value));
  value.rotation.x = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(validatedSpatial(value));
}

TEST_CASE("audio.spatial_checks_every_attenuation_and_cone_boundary_even_when_disabled") {
  std::vector<SpatialSettings> invalid;
  const auto add = [&](auto change) {
    SpatialSettings value;
    change(value);
    invalid.push_back(value);
  };
  add([](auto& value) { value.attenuation = static_cast<Attenuation>(99); });
  add([](auto& value) { value.minDistance = 0; });
  add([](auto& value) { value.maxDistance = 1000001; });
  add([](auto& value) { value.maxDistance = value.minDistance; });
  add([](auto& value) { value.rolloff = -0.01F; });
  add([](auto& value) { value.rolloff = 10.01F; });
  add([](auto& value) { value.dopplerFactor = -0.01F; });
  add([](auto& value) { value.dopplerFactor = 1.01F; });
  add([](auto& value) { value.innerConeDegrees = -1; });
  add([](auto& value) { value.outerConeDegrees = 361; });
  add([](auto& value) { value.outerConeDegrees = 90; });
  add([](auto& value) { value.outerConeGain = -0.1F; });
  add([](auto& value) { value.outerConeGain = 1.1F; });
  add([](auto& value) { value.rolloff = std::numeric_limits<float>::quiet_NaN(); });

  for (const auto& value : invalid) {
    CHECK_FALSE(validatedSpatial(value));
  }
}

TEST_CASE("audio.spatial_actual_pcm_tracks_source_side_and_listener_yaw") {
  auto mixer = Mixer::create();
  auto clip = source();
  REQUIRE(mixer);
  REQUIRE(clip);
  auto id = mixer->createVoice(*clip, {1, 0, 1, true});
  REQUIRE(id);
  auto value = spatial({-10, 0, 0});
  value.rolloff = 0;
  REQUIRE(mixer->setSpatial(*id, value));
  REQUIRE(mixer->play(*id));
  auto left = render(*mixer);
  CHECK(energy(left, 0) > energy(left, 1) * 2);
  value.position.x = 10;
  REQUIRE(mixer->setSpatial(*id, value));
  auto right = render(*mixer);
  CHECK(energy(right, 1) > energy(right, 0) * 2);
  auto rotated = Dot::Math::rotationEulerDegrees({0, 180, 0});
  REQUIRE(rotated);
  REQUIRE(mixer->setListener({{}, *rotated}));
  auto flipped = render(*mixer);
  CHECK(energy(flipped, 0) > energy(flipped, 1) * 2);
  CHECK(energy(right, 1) == doctest::Approx(energy(flipped, 0)).epsilon(0.001));
}

TEST_CASE("audio.spatial_listener_translation_and_roll_change_real_geometry") {
  auto mixer = Mixer::create();
  auto clip = source();
  REQUIRE(mixer);
  REQUIRE(clip);
  auto id = mixer->createVoice(*clip, {1, 0, 1, true});
  REQUIRE(id);
  auto value = spatial({0, 0, -1});
  REQUIRE(mixer->setSpatial(*id, value));
  REQUIRE(mixer->play(*id));
  const auto near = energy(render(*mixer));
  REQUIRE(mixer->setListener({{0, 0, 9}}));
  const auto far = energy(render(*mixer));
  CHECK(far / near == doctest::Approx(0.1).epsilon(0.002));
  value.position = {10, 0, 0};
  value.rolloff = 0;
  REQUIRE(mixer->setSpatial(*id, value));
  auto rotation = Dot::Math::rotationEulerDegrees({0, 0, 180});
  REQUIRE(rotation);
  REQUIRE(mixer->setListener({{}, *rotation}));
  auto rolled = render(*mixer);
  CHECK(energy(rolled, 0) > energy(rolled, 1) * 2);
}

TEST_CASE("audio.spatial_distance_models_and_clamped_ranges_produce_expected_ratios") {
  auto mixer = Mixer::create();
  auto clip = source();
  REQUIRE(mixer);
  REQUIRE(clip);
  auto id = mixer->createVoice(*clip, {1, 0, 1, true});
  REQUIRE(id);
  REQUIRE(mixer->play(*id));
  auto value = spatial();
  value.maxDistance = 11;
  REQUIRE(mixer->setSpatial(*id, value));
  const auto reference = energy(render(*mixer));
  REQUIRE(reference > 0);
  value.position.z = -2;
  REQUIRE(mixer->setSpatial(*id, value));
  CHECK(energy(render(*mixer)) / reference == doctest::Approx(0.5).epsilon(0.002));
  value.attenuation = Attenuation::Exponential;
  value.rolloff = 2;
  REQUIRE(mixer->setSpatial(*id, value));
  CHECK(energy(render(*mixer)) / reference == doctest::Approx(0.25).epsilon(0.002));
  value.attenuation = Attenuation::Linear;
  value.rolloff = 1;
  value.position.z = -6;
  REQUIRE(mixer->setSpatial(*id, value));
  CHECK(energy(render(*mixer)) / reference == doctest::Approx(0.5).epsilon(0.002));
  value.position.z = -20;
  REQUIRE(mixer->setSpatial(*id, value));
  CHECK(energy(render(*mixer)) == doctest::Approx(0).epsilon(0.001));
  value.attenuation = Attenuation::Inverse;
  REQUIRE(mixer->setSpatial(*id, value));
  CHECK(energy(render(*mixer)) / reference == doctest::Approx(1.0 / 11).epsilon(0.002));
  value.rolloff = 0;
  REQUIRE(mixer->setSpatial(*id, value));
  CHECK(energy(render(*mixer)) / reference == doctest::Approx(1).epsilon(0.002));
}

TEST_CASE("audio.spatial_directional_cone_uses_source_rotation_and_outer_gain") {
  auto mixer = Mixer::create();
  auto clip = source();
  REQUIRE(mixer);
  REQUIRE(clip);
  auto id = mixer->createVoice(*clip, {1, 0, 1, true});
  REQUIRE(id);
  REQUIRE(mixer->play(*id));
  auto value = spatial({0, 0, -10});
  value.rolloff = 0;
  value.innerConeDegrees = 60;
  value.outerConeDegrees = 120;
  value.outerConeGain = 0.1F;
  auto rotation = Dot::Math::rotationEulerDegrees({0, 180, 0});
  REQUIRE(rotation);
  value.rotation = *rotation;
  REQUIRE(mixer->setSpatial(*id, value));
  const auto facing = energy(render(*mixer));
  REQUIRE(facing > 0);
  value.rotation = {};
  REQUIRE(mixer->setSpatial(*id, value));
  CHECK(energy(render(*mixer)) / facing == doctest::Approx(0.1).epsilon(0.002));
}

TEST_CASE("audio.spatial_disabled_mode_restores_normal_pcm_without_resetting_voice_identity") {
  auto mixer = Mixer::create();
  auto clip = source();
  REQUIRE(mixer);
  REQUIRE(clip);
  auto id = mixer->createVoice(*clip, {1, 0, 1, true});
  REQUIRE(id);
  REQUIRE(mixer->setSpatial(*id, spatial({10, 0, 0})));
  REQUIRE(mixer->play(*id));
  CHECK(energy(render(*mixer)) > 0);
  REQUIRE(mixer->setSpatial(*id, {}));
  auto plain = render(*mixer);
  CHECK(std::ranges::all_of(plain, [](float value) { return value == 0.25F; }));
  CHECK_FALSE(mixer->voice(*id)->spatial.enabled);
  CHECK(mixer->voice(*id)->playing);
}

TEST_CASE("audio.spatial_rejects_stereo_pan_and_invalid_edits_before_state_changes") {
  auto mixer = Mixer::create();
  auto clip = source();
  auto stereo = Clip::fromPcm(std::vector<float>(1024, 0.25F), 2, 48000);
  REQUIRE(mixer);
  REQUIRE(clip);
  REQUIRE(stereo);
  auto id = mixer->createVoice(*clip, {1, 0.5F, 1, true});
  auto music = mixer->createVoice(*stereo);
  REQUIRE(id);
  REQUIRE(music);
  CHECK_FALSE(mixer->setSpatial(*music, spatial()));
  CHECK_FALSE(mixer->setSpatial(*id, spatial()));
  CHECK_FALSE(mixer->voice(*id)->spatial.enabled);
  REQUIRE(mixer->configureVoice(*id, {1, 0, 1, true}));
  REQUIRE(mixer->setSpatial(*id, spatial()));
  auto before = mixer->voice(*id);
  REQUIRE(before);
  auto invalid = spatial();
  invalid.maxDistance = invalid.minDistance;
  CHECK_FALSE(mixer->setSpatial(*id, invalid));
  CHECK_FALSE(mixer->configureVoice(*id, {1, -0.5F, 1, true}));
  CHECK(mixer->voice(*id)->spatial == before->spatial);
  CHECK(mixer->voice(*id)->settings == before->settings);
  CHECK_FALSE(mixer->setSpatial({}, spatial()));
  auto listener = mixer->listener();
  REQUIRE(listener);
  CHECK_FALSE(mixer->setListener({{}, {0, 0, 0, 0}}));
  CHECK(*mixer->listener() == *listener);
}

TEST_CASE("audio.spatial_pose_and_listener_snapshots_are_owned_and_normalized") {
  auto mixer = Mixer::create();
  auto clip = source();
  REQUIRE(mixer);
  REQUIRE(clip);
  auto id = mixer->createVoice(*clip);
  REQUIRE(id);
  auto value = spatial();
  value.rotation.w = 2;
  REQUIRE(mixer->setSpatial(*id, value));
  value.position.x = 999;
  CHECK(mixer->voice(*id)->spatial.position.x == 0);
  CHECK(mixer->voice(*id)->spatial.rotation.w == 1);
  Listener listener;
  listener.rotation.w = 2;
  REQUIRE(mixer->setListener(listener));
  listener.position.x = 999;
  CHECK(mixer->listener()->position.x == 0);
  CHECK(mixer->listener()->rotation.w == 1);
}

TEST_CASE("audio.spatial_doppler_changes_real_frequency_and_can_be_disabled") {
  auto mixer = Mixer::create();
  std::vector<float> samples(48000);

  for (std::size_t index = 0; index < samples.size(); ++index) {
    samples[index] = static_cast<float>(
        0.25 * std::sin(2 * 3.141592653589793 * 500 * static_cast<double>(index) / 48000));
  }

  auto clip = Clip::fromPcm(samples, 1, 48000);
  REQUIRE(mixer);
  REQUIRE(clip);
  auto id = mixer->createVoice(*clip, {1, 0, 1, true});
  REQUIRE(id);
  auto value = spatial({0, 0, -20});
  value.rolloff = 0;
  REQUIRE(mixer->setSpatial(*id, value));
  REQUIRE(mixer->play(*id));
  const auto frequency = [&] {
    auto output = render(*mixer);
    int count = 0;

    for (std::size_t index = 1026; index < output.size(); index += 2) {
      count += output[index - 2] <= 0 && output[index] > 0 ? 1 : 0;
    }

    return count;
  };
  const auto normal = frequency();
  REQUIRE(normal > 20);
  value.dopplerFactor = 1;
  value.velocity = {0, 0, 50};
  REQUIRE(mixer->setSpatial(*id, value));
  const auto approach = frequency();
  CHECK(static_cast<double>(approach) / normal > 1.1);
  CHECK(static_cast<double>(approach) / normal < 1.25);
  Listener listener;
  listener.velocity = {0, 0, -50};
  REQUIRE(mixer->setListener(listener));
  const auto both = frequency();
  CHECK(both > approach);
  value.dopplerFactor = 0;
  REQUIRE(mixer->setSpatial(*id, value));
  CHECK(std::abs(frequency() - normal) <= 1);
}

TEST_CASE("audio.spatial_move_scope_and_owner_thread_checks_preserve_state") {
  auto mixer = Mixer::create();
  auto other = Mixer::create();
  auto clip = source();
  REQUIRE(mixer);
  REQUIRE(other);
  REQUIRE(clip);
  auto id = mixer->createVoice(*clip);
  REQUIRE(id);
  REQUIRE(mixer->setSpatial(*id, spatial()));
  CHECK_FALSE(other->setSpatial(*id, spatial()));
  auto rejected = std::async(std::launch::async, [&] {
    return !mixer->setSpatial(*id, spatial()) && !mixer->setListener({}) && !mixer->listener();
  });
  CHECK(rejected.get());
  auto moved = std::move(*mixer);
  CHECK_FALSE(mixer->setSpatial(*id, spatial()));
  CHECK_FALSE(mixer->setListener({}));
  CHECK_FALSE(mixer->listener());
  CHECK(moved.voice(*id)->spatial.enabled);
}

TEST_CASE("audio.spatial_coincident_and_extreme_positions_keep_pcm_finite_with_doppler") {
  auto mixer = Mixer::create();
  auto clip = source();
  REQUIRE(mixer);
  REQUIRE(clip);
  auto id = mixer->createVoice(*clip, {1, 0, 4, true});
  REQUIRE(id);
  REQUIRE(mixer->play(*id));
  auto value = spatial({0, 0, 0});
  value.dopplerFactor = 1;
  value.velocity = {100, 0, 0};
  REQUIRE(mixer->setSpatial(*id, value));
  auto output = render(*mixer);
  CHECK(std::ranges::all_of(output, [](float sample) { return std::isfinite(sample); }));
  value.position = {1000000, -1000000, 1000000};
  REQUIRE(mixer->setSpatial(*id, value));
  REQUIRE(mixer->setListener({{-1000000, 1000000, -1000000}, {}, {-100, 0, 0}}));
  output = render(*mixer);
  CHECK(std::ranges::all_of(output, [](float sample) { return std::isfinite(sample); }));
  CHECK_FALSE(mixer->stats()->faulted);
}

TEST_CASE("audio.spatial_seek_and_pitch_controls_retain_spatial_state_and_lifetime") {
  const auto before = Mixer::backendMemory();

  {
    auto mixer = Mixer::create();
    auto clip = source();
    REQUIRE(mixer);
    REQUIRE(clip);
    auto id = mixer->createVoice(*clip, {1, 0, 1, true});
    REQUIRE(id);
    auto value = spatial({-10, 0, 0});
    value.rolloff = 0;
    REQUIRE(mixer->setSpatial(*id, value));
    REQUIRE(mixer->play(*id));
    REQUIRE(mixer->configureVoice(*id, {0.5F, 0, 2, true}));
    REQUIRE(mixer->seek(*id, 100));
    auto output = render(*mixer);
    CHECK(energy(output, 0) > energy(output, 1) * 2);
    CHECK(mixer->voice(*id)->spatial == value);
    REQUIRE(mixer->destroyVoice(*id));
    CHECK_FALSE(mixer->setSpatial(*id, value));
  }

  CHECK(Mixer::backendMemory().bytes == before.bytes);
  CHECK(Mixer::backendMemory().allocations == before.allocations);
}

TEST_CASE("audio.spatial_mono_output_keeps_distance_attenuation_and_independent_listeners") {
  auto first = Mixer::create({1});
  auto second = Mixer::create({1});
  auto clip = source();
  REQUIRE(first);
  REQUIRE(second);
  REQUIRE(clip);
  auto a = first->createVoice(*clip, {1, 0, 1, true});
  auto b = second->createVoice(*clip, {1, 0, 1, true});
  REQUIRE(a);
  REQUIRE(b);
  REQUIRE(first->setSpatial(*a, spatial()));
  REQUIRE(second->setSpatial(*b, spatial()));
  REQUIRE(first->play(*a));
  REQUIRE(second->play(*b));
  REQUIRE(second->setListener({{0, 0, 9}}));
  std::array<float, 4096> near{};
  std::array<float, 4096> far{};
  REQUIRE(first->render(near));
  REQUIRE(second->render(far));
  CHECK(near.back() == doctest::Approx(0.25).epsilon(0.002));
  CHECK(far.back() == doctest::Approx(0.025).epsilon(0.002));
  CHECK(first->listener()->position == Dot::Math::Vec3{});
  CHECK(second->listener()->position == Dot::Math::Vec3{0, 0, 9});
}

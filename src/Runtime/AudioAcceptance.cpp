#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <print>
#include <thread>
#include <utility>
#include <vector>

import Dot.Audio.Clip;
import Dot.Audio.Mixer;
import Dot.Audio.Output;
import Dot.Audio.Playback;
import Dot.Core.Files;
import Dot.Core.Result;

namespace Dot::Runtime {
  auto testAudioSpatial() -> int {
    auto mixer = Dot::Audio::Mixer::create();
    auto clip = Dot::Audio::Clip::fromPcm(std::vector<float>(4096, 0.25F), 1, 48000);

    if (!mixer || !clip) {
      std::println(stderr, "Spatial audio acceptance setup failed");
      return 1;
    }

    auto voice = mixer->createVoice(*clip, {1, 0, 1, true});
    Dot::Audio::SpatialSettings settings;
    settings.enabled = true;
    settings.position = {-10, 0, 0};
    settings.rolloff = 0;

    if (!voice || !mixer->setSpatial(*voice, settings) || !mixer->play(*voice)) {
      std::println(stderr, "Spatial audio acceptance voice setup failed");
      return 1;
    }

    std::array<float, 8192> samples{};
    const auto energy = [&](std::size_t channel) {
      double sum = 0;

      for (std::size_t index = samples.size() / 2 + channel; index < samples.size(); index += 2) {
        sum += static_cast<double>(samples[index]) * samples[index];
      }

      return sum;
    };

    if (!mixer->render(samples) || energy(0) <= energy(1) * 4) {
      std::println(stderr, "Spatial audio acceptance left-channel geometry failed");
      return 1;
    }

    settings.position.x = 10;
    auto rotation = Dot::Math::rotationEulerDegrees({0, 180, 0});

    if (!rotation || !mixer->setSpatial(*voice, settings) || !mixer->render(samples)
        || energy(1) <= energy(0) * 4 || !mixer->setListener({{}, *rotation})
        || !mixer->render(samples) || energy(0) <= energy(1) * 4 || !mixer->destroyVoice(*voice)
        || mixer->setSpatial(*voice, settings)) {
      std::println(stderr, "Spatial audio acceptance listener/move/lifetime failed");
      return 1;
    }

    std::println(
        "Spatial audio acceptance passed: actual left/right PCM, rotated listener, stale voice");
    return 0;
  }

  auto testAudioPlayback() -> int {
    const auto mixerBefore = Dot::Audio::Mixer::backendMemory();
    const auto outputBefore = Dot::Audio::Output::backendMemory();
    Dot::Audio::PlaybackConfig config;
    config.output.volume = 0;
    auto playback = Dot::Audio::Playback::create(config);
    auto clip = Dot::Audio::Clip::fromPcm(std::vector<float>(1024, 0.75F), 1, 48000);

    if (!playback || !clip) {
      std::println(stderr, "Continuous playback acceptance setup failed: {}",
                   !playback ? playback.error().message : clip.error().message);
      return 1;
    }

    auto first = playback->createVoice(*clip, {1, 0, 1, true});
    auto second = playback->createVoice(*clip, {1, 0, 1, true});

    if (!first || !second || !playback->play(*first) || !playback->play(*second)) {
      std::println(stderr, "Continuous playback acceptance voice setup failed");
      return 1;
    }

    auto before = playback->stats();
    std::this_thread::sleep_for(std::chrono::milliseconds{250});
    auto after = playback->stats();

    if (!before || !after || after->state != Dot::Audio::PlaybackState::Running
        || after->mixer.renderedFrames <= before->mixer.renderedFrames + 4096
        || after->output.consumedFrames <= before->output.consumedFrames + 4096
        || after->output.clippedSamples == 0) {
      std::println(stderr,
                   "Continuous playback did not mix/deliver actual PCM while client was idle");
      return 1;
    }

    auto closed = playback->close();
    auto final = playback->stats();

    if (!closed || !final || final->state != Dot::Audio::PlaybackState::Closed
        || final->mixer.voices != 0 || final->output.running
        || final->output.submittedFrames
               != final->output.consumedFrames + final->output.discardedFrames
        || Dot::Audio::Mixer::backendMemory().bytes != mixerBefore.bytes
        || Dot::Audio::Output::backendMemory().bytes != outputBefore.bytes) {
      std::println(stderr, "Continuous playback acceptance joined-cleanup verification failed");
      return 1;
    }

    std::println(
        "Continuous playback acceptance passed: {} device frames during client idle; "
        "executed controls and joined cleanup; endpoint muted",
        after->output.consumedFrames - before->output.consumedFrames);
    return 0;
  }

  auto testAudioOutput() -> int {
    const auto before = Dot::Audio::Output::backendMemory();

    {
      Dot::Audio::OutputConfig config;
      config.volume = 0;
      auto output = Dot::Audio::Output::create(config);
      auto mixer = Dot::Audio::Mixer::create();
      std::vector<float> samples(4800);

      for (std::size_t index = 0; index < samples.size(); ++index) {
        samples[index] = static_cast<float>(
            0.01 * std::sin(2 * 3.141592653589793 * 440 * static_cast<double>(index) / 48000));
      }

      auto clip = Dot::Audio::Clip::fromPcm(samples, 1, 48000);

      if (!output || !mixer || !clip) {
        std::println(stderr, "Native audio acceptance setup failed: {}",
                     !output  ? output.error().message
                     : !mixer ? mixer.error().message
                              : clip.error().message);
        return 1;
      }

      auto voice = mixer->createVoice(*clip, {1, 0, 1, true});

      if (!voice || !mixer->play(*voice) || !output->mix(*mixer, config.queueFrames)) {
        std::println(stderr, "Native audio acceptance voice/queue setup failed");
        return 1;
      }

      auto started = output->start();

      if (!started) {
        std::println(stderr, "{}", started.error().message);
        return 1;
      }

      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};

      while (std::chrono::steady_clock::now() < deadline) {
        auto stats = output->stats();

        if (!stats || stats->fault != Dot::Audio::OutputFault::None) {
          std::println(stderr, "Native audio acceptance device fault");
          return 1;
        }

        if (stats->consumedFrames >= config.queueFrames && stats->underrunFrames > 0) {
          break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{5});
      }

      auto stopped = output->stop();
      auto stats = output->stats();
      auto info = output->info();

      if (!stopped || !stats || !info || stats->running
          || stats->fault != Dot::Audio::OutputFault::None
          || stats->consumedFrames != config.queueFrames || stats->underrunFrames == 0
          || stats->callbackFrames != stats->consumedFrames + stats->underrunFrames
          || info->backend == Dot::Audio::AudioBackend::Default) {
        std::println(stderr, "Native audio acceptance callback/lifetime verification failed");
        return 1;
      }

      std::println(
          "Native audio acceptance passed: {}; {} PCM frames consumed, {} underrun frames; "
          "endpoint muted",
          info->deviceName, stats->consumedFrames, stats->underrunFrames);
    }

    const auto after = Dot::Audio::Output::backendMemory();

    if (after.bytes != before.bytes || after.allocations != before.allocations) {
      std::println(stderr, "Native audio acceptance backend allocation leak");
      return 1;
    }

    return 0;
  }

  auto testAudioMix() -> int {
    const auto before = Dot::Audio::Mixer::backendMemory();

    {
      auto mixer = Dot::Audio::Mixer::create({1, 48000, 2});
      std::array<float, 128> source{};
      source.fill(0.25F);
      auto clip = Dot::Audio::Clip::fromPcm(source, 1, 48000);

      if (!mixer || !clip) {
        std::println(stderr, "Audio mix acceptance setup failed");
        return 1;
      }

      auto first = mixer->createVoice(*clip, {1, 0, 1, true});
      auto second = mixer->createVoice(*clip, {0.5F, 0, 1, true});

      if (!first || !second || !mixer->play(*first) || !mixer->play(*second)) {
        std::println(stderr, "Audio mix acceptance voice startup failed");
        return 1;
      }

      std::array<float, 64> output{};
      const auto near = [&](float expected) {
        return std::ranges::all_of(
            output, [expected](float sample) { return std::abs(sample - expected) < 0.0001F; });
      };

      if (!mixer->render(output) || !near(0.375F) || !mixer->pause(*first) || !mixer->render(output)
          || !near(0.125F) || !mixer->seek(*second, 32) || !mixer->setVolume(0.5F)
          || !mixer->render(output) || !near(0.0625F) || !mixer->destroyVoice(*first)
          || mixer->play(*first) || !mixer->destroyVoice(*second) || !mixer->render(output)
          || !near(0) || mixer->stats()->voices != 0 || mixer->stats()->renderedFrames != 256) {
        std::println(stderr, "Audio mix acceptance PCM/control/lifetime verification failed");
        return 1;
      }
    }

    const auto after = Dot::Audio::Mixer::backendMemory();

    if (after.bytes != before.bytes || after.allocations != before.allocations) {
      std::println(stderr, "Audio mix acceptance backend allocation leak");
      return 1;
    }

    std::println("Audio mix acceptance passed: summed PCM, pause/seek/gain, and backend cleanup");
    return 0;
  }

  auto testAudioClip(const std::filesystem::path& root) -> int {
    constexpr std::array<unsigned char, 52> wave{
        'R', 'I', 'F', 'F', 44, 0, 0,    0,    'W', 'A',  'V',  'E',  'f', 'm', 't',  ' ', 16, 0,
        0,   0,   1,   0,   1,  0, 0x44, 0xac, 0,   0,    0x88, 0x58, 1,   0,   2,    0,   16, 0,
        'd', 'a', 't', 'a', 8,  0, 0,    0,    0,   0x80, 0,    0xc0, 0,   0,   0xff, 0x7f};
    std::array<std::byte, wave.size()> source{};

    for (std::size_t index = 0; index < wave.size(); ++index) {
      source[index] = static_cast<std::byte>(wave[index]);
    }

    auto files = Dot::Core::FileSystem::open(root);

    if (!files) {
      std::println(stderr, "{}", files.error().message);
      return 1;
    }

    auto directory = files->createDirectories("audio");

    if (!directory) {
      std::println(stderr, "{}", directory.error().message);
      return 1;
    }

    auto written = files->writeAtomic("audio/acceptance.wav", source);

    if (!written) {
      std::println(stderr, "{}", written.error().message);
      return 1;
    }

    auto clip = Dot::Audio::loadClip(*files, "audio/acceptance.wav");

    if (!clip) {
      std::println(stderr, "{}", clip.error().message);
      return 1;
    }

    auto copy = *clip;
    auto retained = std::move(*clip);
    const auto info = retained.info();
    const auto samples = copy.samples();

    if (!info || !samples || info->channels != 1 || info->sampleRate != 44100 || info->frames != 4
        || samples->size() != 4 || (*samples)[0] != -1 || (*samples)[1] != -0.5F
        || (*samples)[2] != 0 || (*samples)[3] != 32767.0F / 32768 || clip->info()
        || samples->data() != retained.samples()->data()
        || Dot::Audio::Clip::decode(source, {source.size(), 15})
        || Dot::Audio::loadClip(*files, "audio/missing.wav")) {
      std::println(stderr, "Audio clip acceptance ownership/data/failure verification failed");
      return 1;
    }

    std::println("Audio clip acceptance passed: {} frames, {} Hz; owned PCM and errors verified",
                 info->frames, info->sampleRate);
    return 0;
  }
}  // namespace Dot::Runtime

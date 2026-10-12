#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <print>
#include <utility>

import Dot.Audio.Clip;
import Dot.Audio.Mixer;
import Dot.Core.Files;
import Dot.Core.Result;

namespace Dot::Runtime {
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

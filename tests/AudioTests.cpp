#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <limits>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import Dot.Audio.Clip;
import Dot.Core.Files;
import Dot.Core.Result;

namespace {
  using namespace Dot::Audio;
  using Bytes = std::vector<std::byte>;

  auto put(Bytes& bytes, std::size_t offset, std::uint64_t value, std::size_t count) -> void {
    for (std::size_t index = 0; index < count; ++index) {
      bytes[offset + index] = static_cast<std::byte>((value >> (8 * index)) & 255);
    }
  }

  auto wave(std::uint16_t channels = 1, std::uint32_t rate = 44100, bool floating = false)
      -> Bytes {
    const std::array<std::uint16_t, 4> pcm{0x8000, 0xc000, 0, 0x7fff};
    const std::array<float, 4> values{-1, -0.5F, 0, 1};
    Bytes bytes(44 + (floating ? 16 : 8));
    const auto text = [&](std::size_t offset, std::string_view value) {
      for (auto character : value) {
        bytes[offset++] = static_cast<std::byte>(character);
      }
    };
    text(0, "RIFF");
    put(bytes, 4, bytes.size() - 8, 4);
    text(8, "WAVEfmt ");
    put(bytes, 16, 16, 4);
    put(bytes, 20, floating ? 3 : 1, 2);
    put(bytes, 22, channels, 2);
    put(bytes, 24, rate, 4);
    put(bytes, 28, rate * channels * (floating ? 4 : 2), 4);
    put(bytes, 32, channels * (floating ? 4 : 2), 2);
    put(bytes, 34, floating ? 32 : 16, 2);
    text(36, "data");
    put(bytes, 40, bytes.size() - 44, 4);

    for (std::size_t index = 0; index < pcm.size(); ++index) {
      put(bytes, 44 + index * (floating ? 4 : 2),
          floating ? std::bit_cast<std::uint32_t>(values[index]) : pcm[index], floating ? 4 : 2);
    }

    return bytes;
  }

  /** Generated constant-zero FLAC frame, per RFC 9639 sections 8.2 and 9.
   * 按 RFC 9639 第 8.2、9 节生成的常量零 FLAC 帧，无外部媒体素材。
   * https://www.rfc-editor.org/rfc/rfc9639.html */
  auto flac() -> Bytes {
    constexpr std::string_view hex
        = "664c614380000022001000100000000000000ac440f000000010"
          "00000000000000000000000000000000fff86908000f300000004680";
    Bytes bytes;
    const auto digit = [](char value) { return value <= '9' ? value - '0' : value - 'a' + 10; };

    for (std::size_t index = 0; index < hex.size(); index += 2) {
      bytes.push_back(static_cast<std::byte>(digit(hex[index]) * 16 + digit(hex[index + 1])));
    }

    return bytes;
  }

  /** Four generated MPEG-1 Layer III silent frames, stereo 44.1 kHz/128 kbps.
   * 四个生成的 MPEG-1 Layer III 静音帧，立体声 44.1 kHz/128 kbps；无外部媒体。 */
  auto mp3() -> Bytes {
    Bytes bytes(4 * 417);

    for (std::size_t offset = 0; offset < bytes.size(); offset += 417) {
      bytes[offset] = std::byte{0xff};
      bytes[offset + 1] = std::byte{0xfb};
      bytes[offset + 2] = std::byte{0x90};
    }

    return bytes;
  }

  struct Directory {
    std::filesystem::path root;

    Directory() {
      std::random_device random;

      for (int attempt = 0; attempt < 100; ++attempt) {
        root = std::filesystem::temp_directory_path() / ("dot-audio-" + std::to_string(random()));
        std::error_code error;

        if (std::filesystem::create_directory(root, error)) {
          return;
        }

        if (error) {
          throw std::runtime_error(error.message());
        }
      }

      throw std::runtime_error("Cannot create audio test directory");
    }

    ~Directory() {
      std::error_code ignored;
      std::filesystem::remove_all(root, ignored);
    }
  };
}  // namespace

TEST_CASE("audio.pcm_copies_input_and_shares_immutable_owned_samples") {
  std::array<float, 4> source{-1, -0.5F, 0, 1};
  auto clip = Clip::fromPcm(source, 2, 48000);
  REQUIRE(clip);
  auto copy = *clip;
  source.fill(0);
  auto samples = copy.samples();
  REQUIRE(samples);
  CHECK((*samples)[0] == -1);
  CHECK(samples->data() == clip->samples()->data());
  auto moved = std::move(*clip);
  CHECK_FALSE(clip->samples());
  CHECK_FALSE(clip->info());
  REQUIRE(moved.info());
  CHECK(moved.info()->channels == 2);
  CHECK(moved.info()->frames == 2);
  CHECK(moved.info()->durationSeconds == doctest::Approx(2.0 / 48000));
  moved = std::move(copy);
  CHECK_FALSE(copy.info());
  CHECK((*samples)[3] == 1);
  copy = moved;
  CHECK(copy.samples()->data() == samples->data());
}

TEST_CASE("audio.pcm_validates_format_sample_values_and_exact_byte_limits") {
  const std::array<float, 4> samples{-1, 0, 0.5F, 1};
  CHECK(Clip::fromPcm(samples, 1, 8000, 16));
  CHECK(Clip::fromPcm(samples, 4, 192000, 16));
  const std::array<float, 8> surround{};
  CHECK(Clip::fromPcm(surround, 8, 192000, 32));
  CHECK_FALSE(Clip::fromPcm(samples, 0, 44100));
  CHECK_FALSE(Clip::fromPcm(samples, 9, 44100));
  CHECK_FALSE(Clip::fromPcm(samples, 3, 44100));
  CHECK_FALSE(Clip::fromPcm(samples, 1, 7999));
  CHECK_FALSE(Clip::fromPcm(samples, 1, 192001));
  CHECK_FALSE(Clip::fromPcm({}, 1, 44100));
  CHECK_FALSE(Clip::fromPcm(samples, 1, 44100, 15));
  CHECK_FALSE(Clip::fromPcm(samples, 1, 44100, 0));
  CHECK_FALSE(Clip::fromPcm(samples, 1, 44100, 1024ULL * 1024 * 1024 + 1));

  for (float value : {1.01F, -1.01F, std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::quiet_NaN()}) {
    CHECK_FALSE(Clip::fromPcm(std::span{&value, 1}, 1, 44100));
  }
}

TEST_CASE("audio.wav_decodes_signed_pcm_and_preserves_interleaving") {
  for (auto channels : std::array<std::uint16_t, 2>{1, 2}) {
    auto encoded = wave(channels);
    auto clip = Clip::decode(encoded);
    REQUIRE_MESSAGE(clip, (clip ? "" : clip.error().message));
    encoded.clear();
    REQUIRE(clip->info());
    CHECK(clip->info()->channels == channels);
    CHECK(clip->info()->sampleRate == 44100);
    CHECK(clip->info()->frames == 4 / channels);
    auto samples = clip->samples();
    REQUIRE(samples);
    REQUIRE(samples->size() == 4);
    CHECK((*samples)[0] == -1);
    CHECK((*samples)[1] == -0.5F);
    CHECK((*samples)[2] == 0);
    CHECK((*samples)[3] == doctest::Approx(32767.0 / 32768));
  }
}

TEST_CASE("audio.wav_decodes_float_and_rejects_nonfinite_or_unbounded_output") {
  auto encoded = wave(2, 48000, true);
  auto clip = Clip::decode(encoded);
  REQUIRE(clip);
  CHECK(clip->info()->sampleRate == 48000);
  CHECK((*clip->samples())[3] == 1);

  for (float value : {1.1F, -1.1F, std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::quiet_NaN()}) {
    put(encoded, 44, std::bit_cast<std::uint32_t>(value), 4);
    CHECK_FALSE(Clip::decode(encoded));
  }
}

TEST_CASE("audio.wav_decodes_extensible_pcm_and_unsigned_odd_padded_frames") {
  auto bytes = wave(2);
  bytes.insert(bytes.begin() + 36, 24, std::byte{0});
  put(bytes, 4, bytes.size() - 8, 4);
  put(bytes, 16, 40, 4);
  put(bytes, 20, 65534, 2);
  put(bytes, 36, 22, 2);
  put(bytes, 38, 16, 2);
  put(bytes, 40, 3, 4);
  const std::array<unsigned char, 16> guid{1,   0, 0, 0,   0, 0,  16,  0,
                                           128, 0, 0, 170, 0, 56, 155, 113};

  for (std::size_t index = 0; index < guid.size(); ++index) {
    bytes[44 + index] = static_cast<std::byte>(guid[index]);
  }

  auto clip = Clip::decode(bytes);
  REQUIRE_MESSAGE(clip, (clip ? "" : clip.error().message));
  CHECK(clip->info()->channels == 2);
  CHECK(clip->info()->frames == 2);
  CHECK((*clip->samples())[0] == -1);
  bytes = wave();
  bytes.resize(46);
  put(bytes, 4, 38, 4);
  put(bytes, 28, 44100, 4);
  put(bytes, 32, 1, 2);
  put(bytes, 34, 8, 2);
  put(bytes, 40, 1, 4);
  bytes[44] = std::byte{128};
  bytes[45] = std::byte{0};
  clip = Clip::decode(bytes);
  REQUIRE(clip);
  CHECK(clip->info()->frames == 1);
  CHECK((*clip->samples())[0] == doctest::Approx(1.0 / 255).epsilon(0.0001));
}

TEST_CASE("audio.flac_decodes_generated_constant_frame") {
  auto clip = Clip::decode(flac());
  REQUIRE_MESSAGE(clip, (clip ? "" : clip.error().message));
  CHECK(clip->info()->channels == 1);
  CHECK(clip->info()->sampleRate == 44100);
  CHECK(clip->info()->frames == 16);
  auto samples = clip->samples();
  REQUIRE(samples);
  CHECK(std::ranges::all_of(*samples, [](float value) { return value == 0; }));
}

TEST_CASE("audio.mp3_decodes_generated_silent_frames") {
  auto clip = Clip::decode(mp3());
  REQUIRE_MESSAGE(clip, (clip ? "" : clip.error().message));
  CHECK(clip->info()->channels == 2);
  CHECK(clip->info()->sampleRate == 44100);
  CHECK(clip->info()->frames == 4 * 1152);
  auto samples = clip->samples();
  REQUIRE(samples);
  CHECK(std::ranges::all_of(*samples, [](float value) { return value == 0; }));
}

TEST_CASE("audio.decode_enforces_source_and_pcm_limits_for_every_format") {
  for (const auto& bytes : {wave(), flac(), mp3()}) {
    auto clip = Clip::decode(bytes);
    REQUIRE(clip);
    const auto size = clip->info()->frames * clip->info()->channels * sizeof(float);
    CHECK(Clip::decode(bytes, {bytes.size(), size}));
    CHECK_FALSE(Clip::decode(bytes, {bytes.size() - 1, size}));
    CHECK_FALSE(Clip::decode(bytes, {bytes.size(), size - 1}));
    CHECK_FALSE(Clip::decode(bytes, {0, size}));
    CHECK_FALSE(Clip::decode(bytes, {bytes.size(), 0}));
    CHECK_FALSE(Clip::decode(bytes, {1024ULL * 1024 * 1024 + 1, size}));
    CHECK_FALSE(Clip::decode(bytes, {bytes.size(), 1024ULL * 1024 * 1024 + 1}));
  }
}

TEST_CASE("audio.wav_rejects_truncated_container_chunks_and_extra_bytes") {
  const auto valid = wave();

  for (std::size_t length = 0; length < valid.size(); ++length) {
    CHECK_FALSE(Clip::decode(std::span{valid}.first(length)));
  }

  auto invalid = valid;
  invalid.push_back(std::byte{0});
  CHECK_FALSE(Clip::decode(invalid));
  put(invalid, 4, invalid.size() - 8, 4);
  CHECK_FALSE(Clip::decode(invalid));
  invalid = valid;
  put(invalid, 16, 1000000, 4);
  CHECK_FALSE(Clip::decode(invalid));
  invalid = valid;
  put(invalid, 40, 7, 4);
  CHECK_FALSE(Clip::decode(invalid));
}

TEST_CASE("audio.wav_checks_format_fields_and_requires_nonempty_complete_frames") {
  for (auto field : std::array<std::pair<std::size_t, std::uint64_t>, 8>{
           {{20, 65535}, {22, 0}, {22, 9}, {24, 7999}, {24, 192001}, {28, 1}, {32, 1}, {34, 7}}}) {
    auto bytes = wave();
    put(bytes, field.first, field.second, field.first == 24 || field.first == 28 ? 4 : 2);
    CHECK_FALSE(Clip::decode(bytes));
  }

  auto empty = wave();
  empty.resize(44);
  put(empty, 4, 36, 4);
  put(empty, 40, 0, 4);
  CHECK_FALSE(Clip::decode(empty));
}

TEST_CASE("audio.wav_accepts_padded_unknown_chunks_and_rejects_duplicates") {
  auto bytes = wave();
  const std::array junk{std::byte{'J'}, std::byte{'U'}, std::byte{'N'}, std::byte{'K'},
                        std::byte{1},   std::byte{0},   std::byte{0},   std::byte{0},
                        std::byte{42},  std::byte{0}};
  bytes.insert(bytes.end(), junk.begin(), junk.end());
  put(bytes, 4, bytes.size() - 8, 4);
  CHECK(Clip::decode(bytes));
  bytes.pop_back();
  put(bytes, 4, bytes.size() - 8, 4);
  CHECK_FALSE(Clip::decode(bytes));

  const auto valid = wave();

  for (auto range : {std::pair{12, 36}, std::pair{36, 52}}) {
    bytes = valid;
    bytes.insert(bytes.end(), valid.begin() + range.first, valid.begin() + range.second);
    put(bytes, 4, bytes.size() - 8, 4);
    CHECK_FALSE(Clip::decode(bytes));
  }

  bytes = valid;
  bytes[12] = std::byte{'x'};
  CHECK_FALSE(Clip::decode(bytes));
  bytes = valid;
  bytes[36] = std::byte{'x'};
  CHECK_FALSE(Clip::decode(bytes));
}

TEST_CASE("audio.compressed_decode_rejects_incomplete_and_corrupt_data") {
  auto bytes = flac();
  bytes.resize(42);
  CHECK_FALSE(Clip::decode(bytes));
  bytes = flac();
  bytes.back() ^= std::byte{1};
  CHECK_FALSE(Clip::decode(bytes));
  bytes = mp3();
  bytes.resize(10);
  CHECK_FALSE(Clip::decode(bytes));
  CHECK_FALSE(Clip::decode({}));
  CHECK_FALSE(Clip::decode(Bytes(64, std::byte{0})));
}

TEST_CASE("audio.decode_recovers_after_failure_and_runs_on_independent_threads") {
  auto shared = Clip::decode(wave());
  REQUIRE(shared);
  std::vector<std::future<bool>> workers;

  for (int worker = 0; worker < 4; ++worker) {
    workers.push_back(std::async(std::launch::async, [copy = *shared] {
      for (int iteration = 0; iteration < 32; ++iteration) {
        auto bad = wave(1, 44100, true);
        put(bad, 44, std::bit_cast<std::uint32_t>(2.0F), 4);

        if (Clip::decode(bad) || !Clip::decode(flac()) || !Clip::decode(mp3())
            || (*copy.samples())[0] != -1) {
          return false;
        }
      }

      return true;
    }));
  }

  for (auto& worker : workers) {
    CHECK(worker.get());
  }
}

TEST_CASE("audio.files_load_unicode_uppercase_and_enforce_root_and_container") {
  Directory directory;
  auto files = Dot::Core::FileSystem::open(directory.root);
  REQUIRE(files);
  const std::filesystem::path name = std::filesystem::path{u8"音效.WAV"};
  REQUIRE(files->writeAtomic(name, wave()));
  auto clip = loadClip(*files, name);
  REQUIRE(clip);
  REQUIRE(files->writeAtomic(name, wave(2)));
  CHECK(clip->info()->channels == 1);
  CHECK(loadClip(*files, name)->info()->channels == 2);
  REQUIRE(files->writeAtomic("silence.flac", flac()));
  CHECK(loadClip(*files, "silence.flac"));
  REQUIRE(files->writeAtomic("silence.mp3", mp3()));
  CHECK(loadClip(*files, "silence.mp3"));
  CHECK_FALSE(loadClip(*files, "missing.wav"));
  CHECK_FALSE(loadClip(*files, "../escape.wav"));
  CHECK_FALSE(loadClip(*files, directory.root / name));
  CHECK_FALSE(loadClip(*files, name, {8, 1024}));
  CHECK_FALSE(loadClip(*files, name, {1024, 0}));
  REQUIRE(files->writeAtomic("wrong.flac", wave()));
  CHECK_FALSE(loadClip(*files, "wrong.flac"));
  REQUIRE(files->writeAtomic("wrong.wav", flac()));
  CHECK_FALSE(loadClip(*files, "wrong.wav"));
  REQUIRE(files->writeAtomic("wrong.mp3", wave()));
  CHECK_FALSE(loadClip(*files, "wrong.mp3"));
  REQUIRE(files->writeAtomic("wrong.ogg", wave()));
  CHECK_FALSE(loadClip(*files, "wrong.ogg"));
}

module;

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

module Dot.Audio.Clip;

import Dot.Core.Result;
import Dot.Core.Files;

namespace Dot::Audio {
  namespace {
    constexpr std::uint64_t maxLimit = 1024 * 1024 * 1024;

    auto limitValid(std::uint64_t bytes) -> bool {
      return bytes != 0 && bytes <= maxLimit;
    }

    auto formatValid(std::uint32_t channels, std::uint32_t rate) -> bool {
      return channels >= 1 && channels <= 8 && rate >= 8000 && rate <= 192000;
    }

    auto sampleValid(float sample) -> bool {
      return std::isfinite(sample) && sample >= -1 && sample <= 1;
    }

    auto integer(std::span<const std::byte> data, std::size_t offset, std::size_t bytes)
        -> std::uint64_t {
      std::uint64_t result = 0;

      for (std::size_t index = 0; index < bytes; ++index) {
        result |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(data[offset + index]))
                  << (index * 8);
      }

      return result;
    }

    auto marker(std::span<const std::byte> data, std::size_t offset, std::string_view text)
        -> bool {
      if (offset > data.size() || text.size() > data.size() - offset) {
        return false;
      }

      for (std::size_t index = 0; index < text.size(); ++index) {
        if (std::to_integer<unsigned char>(data[offset + index])
            != static_cast<unsigned char>(text[index])) {
          return false;
        }
      }

      return true;
    }

    auto validateWave(std::span<const std::byte> data) -> Core::Result<void> {
      if (data.size() < 12 || !marker(data, 8, "WAVE") || integer(data, 4, 4) + 8 != data.size()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "WAV RIFF length/type mismatch");
      }

      std::size_t offset = 12;
      std::optional<std::size_t> format;
      std::optional<std::size_t> samples;
      std::uint64_t sampleBytes = 0;

      while (offset < data.size()) {
        if (data.size() - offset < 8) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Truncated WAV chunk header");
        }

        const auto bytes = integer(data, offset + 4, 4);

        if (bytes > data.size() - offset - 8) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Truncated WAV chunk payload");
        }

        if (marker(data, offset, "fmt ")) {
          if (format || bytes < 16) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Missing/duplicate WAV format chunk");
          }

          format = offset + 8;
        } else if (marker(data, offset, "data")) {
          if (samples) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Duplicate WAV sample chunk");
          }

          samples = offset + 8;
          sampleBytes = bytes;
        }

        const auto padded = bytes + (bytes % 2);

        if (padded > data.size() - offset - 8) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Missing WAV chunk padding");
        }

        offset += 8 + static_cast<std::size_t>(padded);
      }

      if (!format || !samples) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "WAV requires format and sample chunks");
      }

      const auto encoding = integer(data, *format, 2);
      const auto channels = integer(data, *format + 2, 2);
      const auto rate = integer(data, *format + 4, 4);

      if (!formatValid(static_cast<std::uint32_t>(channels), static_cast<std::uint32_t>(rate))) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Unsupported WAV channels/sample rate");
      }

      if (encoding == 1 || encoding == 3 || encoding == 65534) {
        const auto byteRate = integer(data, *format + 8, 4);
        const auto alignment = integer(data, *format + 12, 2);
        const auto bits = integer(data, *format + 14, 2);

        if (channels == 0 || bits == 0 || bits % 8 != 0 || alignment == 0
            || alignment != channels * (bits / 8) || byteRate != rate * alignment
            || sampleBytes % alignment != 0) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "WAV PCM frame alignment/rate mismatch");
        }
      }

      return {};
    }

    class Decoder {
    public:
      ma_decoder value{};
      bool initialized{};

      Decoder() = default;
      Decoder(const Decoder&) = delete;
      auto operator=(const Decoder&) -> Decoder& = delete;

      ~Decoder() {
        if (initialized) {
          ma_decoder_uninit(&value);
        }
      }
    };

    auto failure(ma_result result, std::string_view operation) -> std::unexpected<Core::Error> {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        std::format("{}: {}", operation, ma_result_description(result)));
    }
  }  // namespace

  struct Clip::Impl {
    ClipInfo info;
    std::vector<float> samples;
  };

  Clip::Clip(std::shared_ptr<const Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Clip::Clip(const Clip&) noexcept = default;

  auto Clip::operator=(const Clip&) noexcept -> Clip& = default;

  Clip::Clip(Clip&&) noexcept = default;

  auto Clip::operator=(Clip&&) noexcept -> Clip& = default;

  Clip::~Clip() = default;

  auto Clip::fromPcm(std::span<const float> samples, std::uint32_t channels,
                     std::uint32_t sampleRate, std::uint64_t maxSampleBytes) -> Core::Result<Clip> {
    if (!formatValid(channels, sampleRate) || !limitValid(maxSampleBytes) || samples.empty()
        || samples.size() % channels != 0 || samples.size() > maxSampleBytes / sizeof(float)
        || !std::ranges::all_of(samples, sampleValid)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid clip PCM format, samples, or byte limit");
    }

    try {
      auto impl = std::make_shared<Impl>();
      const auto frames = samples.size() / channels;
      impl->info = {channels, sampleRate, frames, static_cast<double>(frames) / sampleRate};
      impl->samples.assign(samples.begin(), samples.end());
      return Clip{std::move(impl)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        std::format("Allocate clip PCM: {}", error.what()));
    }
  }

  auto Clip::decode(std::span<const std::byte> encoded, const ClipLimits& limits)
      -> Core::Result<Clip> {
    if (!limitValid(limits.sourceBytes) || !limitValid(limits.sampleBytes) || encoded.empty()
        || encoded.size() > limits.sourceBytes) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid encoded clip size/limits");
    }

    try {
      auto config = ma_decoder_config_init(ma_format_f32, 0, 0);

      if (marker(encoded, 0, "RIFF")) {
        auto checked = validateWave(encoded);

        if (!checked) {
          return std::unexpected(checked.error());
        }

        config.encodingFormat = ma_encoding_format_wav;
      } else if (marker(encoded, 0, "fLaC")) {
        config.encodingFormat = ma_encoding_format_flac;
      } else if (marker(encoded, 0, "ID3")
                 || (encoded.size() >= 2 && integer(encoded, 0, 1) == 255
                     && (integer(encoded, 1, 1) & 224) == 224)) {
        config.encodingFormat = ma_encoding_format_mp3;
      } else {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Unsupported audio container signature");
      }

      Decoder decoder;
      auto status = ma_decoder_init_memory(encoded.data(), encoded.size(), &config, &decoder.value);

      if (status != MA_SUCCESS) {
        return failure(status, "Initialize audio decoder");
      }

      decoder.initialized = true;
      ma_format format{};
      ma_uint32 channels = 0;
      ma_uint32 rate = 0;
      status = ma_decoder_get_data_format(&decoder.value, &format, &channels, &rate, nullptr, 0);

      if (status != MA_SUCCESS) {
        return failure(status, "Read audio format");
      }

      if (format != ma_format_f32 || !formatValid(channels, rate)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Unsupported audio channels/sample rate");
      }

      ma_uint64 frames = 0;
      status = ma_decoder_get_length_in_pcm_frames(&decoder.value, &frames);

      if (status != MA_SUCCESS) {
        return failure(status, "Read audio frame length");
      }

      if (frames == 0 || frames > limits.sampleBytes / (channels * sizeof(float))) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Decoded clip is empty or exceeds PCM byte limit");
      }

      status = ma_decoder_seek_to_pcm_frame(&decoder.value, 0);

      if (status != MA_SUCCESS) {
        return failure(status, "Rewind audio decoder");
      }

      auto impl = std::make_shared<Impl>();
      impl->samples.resize(static_cast<std::size_t>(frames) * channels);
      ma_uint64 read = 0;
      status = ma_decoder_read_pcm_frames(&decoder.value, impl->samples.data(), frames, &read);

      if ((status != MA_SUCCESS && status != MA_AT_END) || read != frames) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Audio decoder could not read its complete declared frame count");
      }

      std::array<float, 8> extra{};
      read = 0;
      status = ma_decoder_read_pcm_frames(&decoder.value, extra.data(), 1, &read);

      if ((status != MA_SUCCESS && status != MA_AT_END) || read != 0
          || !std::ranges::all_of(impl->samples, sampleValid)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Audio length mismatch or non-finite/out-of-range samples");
      }

      impl->info = {channels, rate, frames, static_cast<double>(frames) / rate};
      return Clip{std::move(impl)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        std::format("Decode audio clip: {}", error.what()));
    }
  }

  auto Clip::info() const -> Core::Result<ClipInfo> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio clip has been moved");
    }

    return m_impl->info;
  }

  auto Clip::samples() const -> Core::Result<std::span<const float>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Audio clip has been moved");
    }

    return std::span<const float>{m_impl->samples};
  }

  auto loadClip(const Core::FileSystem& files, const std::filesystem::path& relative,
                const ClipLimits& limits) -> Core::Result<Clip> {
    if (!limitValid(limits.sourceBytes) || !limitValid(limits.sampleBytes)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid audio clip byte limits");
    }

    try {
      auto extension = relative.extension().generic_u8string();

      for (auto& character : extension) {
        if (character >= u8'A' && character <= u8'Z') {
          character = static_cast<char8_t>(character + (u8'a' - u8'A'));
        }
      }

      if (extension != u8".wav" && extension != u8".flac" && extension != u8".mp3") {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Audio extension must be .wav/.flac/.mp3");
      }

      auto source = files.readBytes(relative, static_cast<std::size_t>(limits.sourceBytes));

      if (!source) {
        return std::unexpected(source.error());
      }

      const bool wave = marker(*source, 0, "RIFF") && marker(*source, 8, "WAVE");
      const bool flac = marker(*source, 0, "fLaC");
      const bool mp3 = marker(*source, 0, "ID3")
                       || (source->size() >= 2 && integer(*source, 0, 1) == 255
                           && (integer(*source, 1, 1) & 224) == 224);

      if ((extension == u8".wav" && !wave) || (extension == u8".flac" && !flac)
          || (extension == u8".mp3" && !mp3)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Audio asset extension/container mismatch");
      }

      return Clip::decode(*source, limits);
    } catch (const std::filesystem::filesystem_error& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        std::format("Decode audio asset path: {}", error.what()));
    }
  }
}  // namespace Dot::Audio

#pragma once

#include <doctest/doctest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

import Dot.Core.Files;

namespace DotTest::Audio {
  struct Directory {
    std::filesystem::path root;

    Directory() {
      std::random_device random;

      for (int attempt = 0; attempt < 100; ++attempt) {
        root = std::filesystem::temp_directory_path()
               / ("dot-scene-audio-" + std::to_string(random()) + "-" + std::to_string(random()));
        std::error_code error;

        if (std::filesystem::create_directory(root, error)) {
          return;
        }

        if (error) {
          throw std::runtime_error(error.message());
        }
      }

      throw std::runtime_error("Cannot create scene audio test directory");
    }

    ~Directory() {
      std::error_code ignored;
      std::filesystem::remove_all(root, ignored);
    }

    auto files() const -> Dot::Core::FileSystem {
      auto result = Dot::Core::FileSystem::open(root);
      REQUIRE(result);
      return *result;
    }
  };

  inline auto writeWave(const Dot::Core::FileSystem& files, std::string_view path,
                        std::span<const float> samples, std::uint16_t channels = 1) -> void {
    std::vector<std::byte> bytes(44 + samples.size_bytes());
    const auto put = [&](std::size_t offset, std::uint64_t value, std::size_t count) {
      for (std::size_t index = 0; index < count; ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (8 * index)) & 255);
      }
    };
    const auto text = [&](std::size_t offset, std::string_view value) {
      for (auto character : value) {
        bytes[offset++] = static_cast<std::byte>(character);
      }
    };
    text(0, "RIFF");
    put(4, bytes.size() - 8, 4);
    text(8, "WAVEfmt ");
    put(16, 16, 4);
    put(20, 3, 2);
    put(22, channels, 2);
    put(24, 48000, 4);
    put(28, 48000 * channels * 4, 4);
    put(32, channels * 4, 2);
    put(34, 32, 2);
    text(36, "data");
    put(40, samples.size_bytes(), 4);

    for (std::size_t index = 0; index < samples.size(); ++index) {
      put(44 + index * 4, std::bit_cast<std::uint32_t>(samples[index]), 4);
    }

    REQUIRE(
        files.writeAtomic(std::filesystem::path{std::u8string(path.begin(), path.end())}, bytes));
  }
}  // namespace DotTest::Audio

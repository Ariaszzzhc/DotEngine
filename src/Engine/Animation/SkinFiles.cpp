module;

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Dot.Animation.SkinFiles;

import Dot.Core.Paths;

namespace Dot::Animation {
  namespace {
    constexpr std::size_t maxBytes = 64 * 1024 * 1024;
    constexpr std::array<std::byte, 8> magic{std::byte{'D'}, std::byte{'O'}, std::byte{'T'},
                                             std::byte{'S'}, std::byte{'K'}, std::byte{'I'},
                                             std::byte{'N'}, std::byte{0}};

    auto checkedPath(const Core::FileSystem& files, const std::filesystem::path& path, bool saving)
        -> Core::Result<void> {
      const auto bytes = path.generic_u8string();
      const std::string text(bytes.begin(), bytes.end());

      if (!Core::isPortableRelativePath(text) || !text.ends_with(".dotskin")) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Skin requires a portable .dotskin path");
      }

      auto current = files.root();

      for (const auto& part : path) {
        current /= part;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(current, error);
        const bool last = current == files.root() / path;

        if (saving && last && status.type() == std::filesystem::file_type::not_found) {
          return {};
        }

        if (error || std::filesystem::is_symlink(status)
            || (last ? !std::filesystem::is_regular_file(status)
                     : !std::filesystem::is_directory(status))) {
          return Core::fail(Core::ErrorCode::Io,
                            "Skin path is missing, inaccessible or a link/special file: " + text);
        }
      }

      return {};
    }
  }  // namespace

  auto decodeSkin(std::span<const std::byte> bytes) -> Core::Result<Skin> {
    if (bytes.size() < 16 || bytes.size() > maxBytes
        || !std::equal(magic.begin(), magic.end(), bytes.begin())) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid skin header/byte capacity");
    }

    std::size_t offset = 8;
    const auto word = [&]() {
      std::uint32_t value{};

      for (unsigned shift = 0; shift < 32; shift += 8) {
        value |= std::to_integer<std::uint32_t>(bytes[offset++]) << shift;
      }

      return value;
    };
    const auto joints = word();
    const auto vertices = word();
    const auto expected = 16ULL + 64ULL * joints + 64ULL * vertices;

    if (joints == 0 || joints > 1024 || vertices == 0 || vertices > 1'000'000
        || expected != bytes.size()) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Skin counts/length do not match");
    }

    try {
      SkinDescription description;
      description.inverseBindMatrices.resize(joints);
      description.vertices.resize(vertices);

      for (auto& matrix : description.inverseBindMatrices) {
        for (auto& value : matrix.data) {
          value = std::bit_cast<float>(word());
        }
      }

      for (auto& influence : description.vertices) {
        for (auto& joint : influence.joints) {
          joint = word();
        }

        for (auto& weight : influence.weights) {
          weight = std::bit_cast<float>(word());
        }
      }

      return Skin::create(std::move(description));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Decode skin: " + std::string(error.what()));
    }
  }

  auto encodeSkin(const Skin& skin) -> Core::Result<std::vector<std::byte>> {
    auto description = skin.description();

    if (!description) {
      return std::unexpected(description.error());
    }

    try {
      const auto& data = **description;
      std::vector<std::byte> bytes(magic.begin(), magic.end());
      bytes.reserve(16 + data.inverseBindMatrices.size() * 64 + data.vertices.size() * 64);
      const auto word = [&](std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
          bytes.push_back(static_cast<std::byte>((value >> shift) & 255U));
        }
      };
      word(static_cast<std::uint32_t>(data.inverseBindMatrices.size()));
      word(static_cast<std::uint32_t>(data.vertices.size()));

      for (const auto& matrix : data.inverseBindMatrices) {
        for (const auto value : matrix.data) {
          word(std::bit_cast<std::uint32_t>(value));
        }
      }

      for (const auto& influence : data.vertices) {
        for (const auto joint : influence.joints) {
          word(joint);
        }

        for (const auto weight : influence.weights) {
          word(std::bit_cast<std::uint32_t>(weight));
        }
      }

      return bytes;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Encode skin: " + std::string(error.what()));
    }
  }

  auto loadSkin(const Core::FileSystem& files, const std::filesystem::path& path)
      -> Core::Result<Skin> {
    auto valid = checkedPath(files, path, false);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto bytes = files.readBytes(path, maxBytes);
    auto result = bytes ? decodeSkin(*bytes) : Core::Result<Skin>{std::unexpected(bytes.error())};

    if (!result) {
      const auto utf = path.generic_u8string();
      return Core::fail(result.error().code, "Skin '" + std::string(utf.begin(), utf.end())
                                                 + "': " + result.error().message);
    }

    return result;
  }

  auto saveSkin(const Core::FileSystem& files, const std::filesystem::path& path, const Skin& skin)
      -> Core::Result<void> {
    auto valid = checkedPath(files, path, true);

    if (!valid) {
      return valid;
    }

    auto bytes = encodeSkin(skin);
    return bytes ? files.writeAtomic(path, *bytes) : std::unexpected(bytes.error());
  }
}  // namespace Dot::Animation

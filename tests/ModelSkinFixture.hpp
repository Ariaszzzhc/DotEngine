#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace DotTest::ModelSkin {
  using Json = nlohmann::json;

  inline auto base64(std::span<const std::byte> bytes) -> std::string {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;

    for (std::size_t i = 0; i < bytes.size(); i += 3) {
      const auto count = std::min<std::size_t>(3, bytes.size() - i);
      std::uint32_t value{};

      for (std::size_t j = 0; j < 3; ++j) {
        value = (value << 8) | (j < count ? std::to_integer<std::uint32_t>(bytes[i + j]) : 0U);
      }

      result += alphabet[(value >> 18) & 63];
      result += alphabet[(value >> 12) & 63];
      result += count > 1 ? alphabet[(value >> 6) & 63] : '=';
      result += count > 2 ? alphabet[value & 63] : '=';
    }

    return result;
  }

  /** Explicit little-endian test buffers; independent from importer/native codec.
   * 独立于导入器和原生编解码的显式小端测试缓冲。 */
  template <class T>
  auto accessor(Json& root, const std::vector<T>& values, std::uint32_t component, const char* type,
                std::size_t width, bool normalized = false) -> std::size_t {
    std::vector<std::byte> bytes;

    for (const auto value : values) {
      const std::uint32_t word = [&] {
        if constexpr (std::is_same_v<T, float>) {
          return std::bit_cast<std::uint32_t>(value);
        } else {
          return static_cast<std::uint32_t>(value);
        }
      }();

      for (std::size_t i = 0; i < sizeof(T); ++i) {
        bytes.push_back(static_cast<std::byte>((word >> (i * 8)) & 255U));
      }
    }

    const auto buffer = root["buffers"].size();
    root["buffers"].push_back({{"byteLength", bytes.size()},
                               {"uri", "data:application/octet-stream;base64," + base64(bytes)}});
    const auto view = root["bufferViews"].size();
    root["bufferViews"].push_back({{"buffer", buffer}, {"byteLength", bytes.size()}});
    const auto index = root["accessors"].size();
    root["accessors"].push_back({{"bufferView", view},
                                 {"componentType", component},
                                 {"count", values.size() / width},
                                 {"type", type},
                                 {"normalized", normalized}});
    return index;
  }
}  // namespace DotTest::ModelSkin

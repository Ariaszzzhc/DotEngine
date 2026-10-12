module;

#include <cstdint>
#include <string_view>

export module Dot.Core.Types;

export namespace Dot::Core {
  using u8 = std::uint8_t;
  using u32 = std::uint32_t;
  using u64 = std::uint64_t;
  using i32 = std::int32_t;
  using f32 = float;
  using f64 = double;

  enum class Platform : u8 { Windows, MacOS, Linux };

  [[nodiscard]] constexpr auto platform() noexcept -> Platform {
#if defined(_WIN32)
    return Platform::Windows;
#elif defined(__APPLE__)
    return Platform::MacOS;
#elif defined(__linux__)
    return Platform::Linux;
#else
#  error Unsupported platform
#endif
  }

  [[nodiscard]] constexpr auto platformName() noexcept -> std::string_view {
    switch (platform()) {
      case Platform::Windows:
        return "Windows";
      case Platform::MacOS:
        return "macOS";
      case Platform::Linux:
        return "Linux";
    }

    return "Unknown";
  }
}  // namespace Dot::Core

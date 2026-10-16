module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "../Serialization/Json.hpp"

module Dot.Editor.PlayInput;

import Dot.Core.Result;
import Dot.Core.Text;
import Dot.Platform.Events;
import Dot.Platform.Input;

namespace Dot::Editor {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;

    auto point(const Json& value, float bound) -> Platform::Point {
      const auto numbers = Codec::numbers<2>(value);

      if (std::abs(numbers[0]) > bound || std::abs(numbers[1]) > bound) {
        throw std::invalid_argument("Input point exceeds its coordinate bound");
      }

      return {numbers[0], numbers[1]};
    }
  }  // namespace

  auto PlayInput::submit(std::string_view value, const Platform::InputState& previous)
      -> Core::Result<void> {
    if (m_stats.active) {
      return Core::fail(Core::ErrorCode::InvalidState, "Agent input is still active or releasing");
    }

    if (value.empty() || value.size() > 64 * 1024 || value.find('\0') != std::string_view::npos
        || !Core::isValidUtf8(value)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Play input requires UTF-8 JSON without NUL within 64 KiB");
    }

    try {
      const auto input = Codec::parseJson(value, 8);
      Codec::requireFields(
          input, {"frames", "keys", "buttons", "pointer", "pointerDelta", "wheel", "text"});
      const auto& frames = input["frames"];

      if (!frames.is_number_integer() || frames < 1 || frames > 600) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Input requires 1..600 integer frames");
      }

      if (!input["keys"].is_array() || input["keys"].size() > Platform::keyNames().size()
          || !input["buttons"].is_array()
          || input["buttons"].size() > Platform::buttonNames().size()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Input requires bounded name arrays");
      }

      Platform::InputState baseline;

      for (const auto& [name, key] : Platform::keyNames()) {
        static_cast<void>(
            baseline.consume({0, 0, Platform::KeyChanged{key, previous.key(key).down, false}}));
      }

      for (const auto& [name, button] : Platform::buttonNames()) {
        static_cast<void>(baseline.consume(
            {0, 0, Platform::ButtonChanged{button, previous.button(button).down}}));
      }

      baseline.beginFrame();
      auto candidate = baseline;
      std::array<bool, static_cast<std::size_t>(Platform::Key::Count)> keys{};
      std::array<bool, static_cast<std::size_t>(Platform::MouseButton::Count)> buttons{};

      for (const auto& item : input["keys"]) {
        const auto name = item.get<std::string>();
        const auto names = Platform::keyNames();
        const auto found
            = std::ranges::find(names, std::string_view(name), &Platform::KeyName::first);

        if (found == names.end() || found->second == Platform::Key::Escape
            || found->second == Platform::Key::F5
            || keys[static_cast<std::size_t>(found->second)]) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Unknown, reserved or duplicate input key: " + name);
        }

        keys[static_cast<std::size_t>(found->second)] = true;
      }

      for (const auto& item : input["buttons"]) {
        const auto name = item.get<std::string>();
        const auto names = Platform::buttonNames();
        const auto found
            = std::ranges::find(names, std::string_view(name), &Platform::ButtonName::first);

        if (found == names.end() || buttons[static_cast<std::size_t>(found->second)]) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Unknown or duplicate input button: " + name);
        }

        buttons[static_cast<std::size_t>(found->second)] = true;
      }

      for (const auto& [name, key] : Platform::keyNames()) {
        static_cast<void>(candidate.consume(
            {0, 0, Platform::KeyChanged{key, keys[static_cast<std::size_t>(key)], false}}));
      }

      for (const auto& [name, button] : Platform::buttonNames()) {
        static_cast<void>(candidate.consume(
            {0, 0, Platform::ButtonChanged{button, buttons[static_cast<std::size_t>(button)]}}));
      }

      const auto position = point(input["pointer"], 1e6F);
      const auto delta = point(input["pointerDelta"], 1e6F);
      const auto wheel = point(input["wheel"], 1000.F);
      const auto text = input["text"].get<std::string>();

      if (text.size() > 16 * 1024 || text.find('\0') != std::string::npos
          || !Core::isValidUtf8(text)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Input text requires UTF-8 without NUL within 16 KiB");
      }

      static_cast<void>(candidate.consume(
          {0, 0, Platform::PointerMoved{position.x, position.y, delta.x, delta.y}}));
      static_cast<void>(candidate.consume({0, 0, Platform::Wheel{wheel.x, wheel.y}}));
      static_cast<void>(candidate.consume({0, 0, Platform::Text{text}}));
      m_state = std::move(candidate);
      m_previous = std::move(baseline);
      m_delta = delta;
      m_wheel = wheel;
      m_stats = {true, frames.get<std::uint32_t>(), 0};
      return {};
    } catch (const Json::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Play input JSON: " + std::string(error.what()));
    } catch (const std::invalid_argument& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Play input: " + std::string(error.what()));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare play input: " + std::string(error.what()));
    }
  }

  auto PlayInput::next() -> Core::Result<std::optional<Platform::InputState>> {
    if (!m_stats.active) {
      return std::optional<Platform::InputState>{};
    }

    try {
      auto output = m_stats.published == 0 && m_stats.remaining == 0 ? m_previous : m_state;
      auto stats = m_stats;

      if (stats.remaining == 0) {
        output.beginFrame();
        static_cast<void>(output.consume({0, 0, Platform::Focus{false}}));
        stats.active = false;
      } else {
        if (stats.published != 0) {
          output.beginFrame();
          const auto position = output.pointer();
          static_cast<void>(output.consume(
              {0, 0, Platform::PointerMoved{position.x, position.y, m_delta.x, m_delta.y}}));
          static_cast<void>(output.consume({0, 0, Platform::Wheel{m_wheel.x, m_wheel.y}}));
        }

        --stats.remaining;
      }

      ++stats.published;
      auto retained = output;
      m_state = std::move(retained);
      m_stats = stats;
      return std::optional<Platform::InputState>{std::move(output)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Publish play input: " + std::string(error.what()));
    }
  }

  auto PlayInput::cancel() noexcept -> void {
    if (m_stats.active) {
      m_stats.remaining = 0;
    }
  }

  auto PlayInput::reset() noexcept -> void {
    m_state = Platform::InputState{};
    m_previous = Platform::InputState{};
    m_delta = m_wheel = {};
    m_stats = {};
  }

  auto PlayInput::stats() const noexcept -> PlayInputStats {
    return m_stats;
  }
}  // namespace Dot::Editor

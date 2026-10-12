module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

#include "../Serialization/Json.hpp"

module Dot.Script.InputSource;

import Dot.Core.Result;
import Dot.Core.Text;
import Dot.Platform.Input;
import Dot.Platform.Events;

namespace Dot::Script {
  namespace {
    using Json = Serialization::Detail::Json;
    using Platform::Key;
    using Platform::MouseButton;
    using Reply = Core::Result<std::optional<std::string>>;

    constexpr std::array keyNames{std::pair{"A", Key::A},
                                  std::pair{"B", Key::B},
                                  std::pair{"C", Key::C},
                                  std::pair{"D", Key::D},
                                  std::pair{"E", Key::E},
                                  std::pair{"F", Key::F},
                                  std::pair{"G", Key::G},
                                  std::pair{"H", Key::H},
                                  std::pair{"I", Key::I},
                                  std::pair{"J", Key::J},
                                  std::pair{"K", Key::K},
                                  std::pair{"L", Key::L},
                                  std::pair{"M", Key::M},
                                  std::pair{"N", Key::N},
                                  std::pair{"O", Key::O},
                                  std::pair{"P", Key::P},
                                  std::pair{"Q", Key::Q},
                                  std::pair{"R", Key::R},
                                  std::pair{"S", Key::S},
                                  std::pair{"T", Key::T},
                                  std::pair{"U", Key::U},
                                  std::pair{"V", Key::V},
                                  std::pair{"W", Key::W},
                                  std::pair{"X", Key::X},
                                  std::pair{"Y", Key::Y},
                                  std::pair{"Z", Key::Z},
                                  std::pair{"Digit1", Key::Digit1},
                                  std::pair{"Digit2", Key::Digit2},
                                  std::pair{"Digit3", Key::Digit3},
                                  std::pair{"Digit4", Key::Digit4},
                                  std::pair{"Digit5", Key::Digit5},
                                  std::pair{"Digit6", Key::Digit6},
                                  std::pair{"Digit7", Key::Digit7},
                                  std::pair{"Digit8", Key::Digit8},
                                  std::pair{"Digit9", Key::Digit9},
                                  std::pair{"Digit0", Key::Digit0},
                                  std::pair{"Enter", Key::Enter},
                                  std::pair{"Escape", Key::Escape},
                                  std::pair{"Backspace", Key::Backspace},
                                  std::pair{"Tab", Key::Tab},
                                  std::pair{"Space", Key::Space},
                                  std::pair{"Left", Key::Left},
                                  std::pair{"Right", Key::Right},
                                  std::pair{"Up", Key::Up},
                                  std::pair{"Down", Key::Down},
                                  std::pair{"F1", Key::F1},
                                  std::pair{"F2", Key::F2},
                                  std::pair{"F3", Key::F3},
                                  std::pair{"F4", Key::F4},
                                  std::pair{"F5", Key::F5},
                                  std::pair{"F6", Key::F6},
                                  std::pair{"F7", Key::F7},
                                  std::pair{"F8", Key::F8},
                                  std::pair{"F9", Key::F9},
                                  std::pair{"F10", Key::F10},
                                  std::pair{"F11", Key::F11},
                                  std::pair{"F12", Key::F12},
                                  std::pair{"LeftControl", Key::LeftControl},
                                  std::pair{"RightControl", Key::RightControl},
                                  std::pair{"LeftShift", Key::LeftShift},
                                  std::pair{"RightShift", Key::RightShift},
                                  std::pair{"LeftAlt", Key::LeftAlt},
                                  std::pair{"RightAlt", Key::RightAlt},
                                  std::pair{"LeftSuper", Key::LeftSuper},
                                  std::pair{"RightSuper", Key::RightSuper}};

    constexpr std::array buttonNames{
        std::pair{"Left", MouseButton::Left}, std::pair{"Middle", MouseButton::Middle},
        std::pair{"Right", MouseButton::Right}, std::pair{"Back", MouseButton::Back},
        std::pair{"Forward", MouseButton::Forward}};
    constexpr std::array fields{"snapshot",     "key",   "button", "pointer",
                                "pointerDelta", "wheel", "text",   "index"};

    template <typename Enum, std::size_t N>
    constexpr auto complete(const std::array<std::pair<const char*, Enum>, N>& entries, Enum count)
        -> bool {
      if (N + 1 != static_cast<std::size_t>(count)) {
        return false;
      }

      std::array<bool, N + 1> seen{};

      for (const auto& [name, value] : entries) {
        for (const auto& previous : entries) {
          if (previous.second != value && std::string_view(previous.first) == name) {
            return false;
          }
        }

        const auto index = static_cast<std::size_t>(value);

        if (index == 0 || index >= seen.size() || seen[index]) {
          return false;
        }

        seen[index] = true;
      }

      return true;
    }

    static_assert(complete(keyNames, Key::Count));
    static_assert(complete(buttonNames, MouseButton::Count));

    auto point(Platform::Point value) -> Json {
      return {{"x", value.x}, {"y", value.y}};
    }

    auto button(Platform::ButtonState value) -> Json {
      return {{"down", value.down}, {"pressed", value.pressed}, {"released", value.released}};
    }

    auto finite(Platform::Point value) -> bool {
      return std::isfinite(value.x) && std::isfinite(value.y);
    }
  }  // namespace

  struct InputSource::Impl {
    InputSourceConfig m_config;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_index{};
    std::optional<Json> m_frame;

    explicit Impl(InputSourceConfig value) : m_config(value) {
    }

    auto check() const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Input source requires its owner thread");
      }

      return {};
    }

    auto read(std::string_view field, std::string_view arguments) const -> Reply {
      auto checked = check();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (arguments.empty() || arguments.size() > 4096 || !Core::isValidUtf8(arguments)
          || arguments.find('\0') != std::string_view::npos) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid input host argument bytes");
      }

      try {
        auto args = Serialization::Detail::parseJson(arguments, 8);
        const bool named = field == "key" || field == "button";

        if (!args.is_array() || args.size() != (named ? 1U : 0U)
            || (named && !args[0].is_string())) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid input query argument shape");
        }

        std::string name;

        if (named) {
          name = args[0].get<std::string>();
          const auto matches = [&name](const auto& entry) { return name == entry.first; };
          const bool known = field == "key" ? std::ranges::any_of(keyNames, matches)
                                            : std::ranges::any_of(buttonNames, matches);

          if (!known) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Unknown input " + std::string(field) + " name: " + name);
          }
        }

        if (!m_frame) {
          return Core::fail(Core::ErrorCode::Unavailable, "Input frame has not been published");
        }

        const auto& value = field == "snapshot" ? *m_frame
                            : named ? m_frame->at(field == "key" ? "keys" : "buttons").at(name)
                                    : m_frame->at(field);
        return std::optional{value.dump()};
      } catch (const std::invalid_argument& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Input JSON: " + std::string(error.what()));
      } catch (const Json::exception& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Input JSON: " + std::string(error.what()));
      } catch (const std::exception& error) {
        return Core::fail(Core::ErrorCode::External, "Input query: " + std::string(error.what()));
      }
    }
  };

  InputSource::InputSource(std::shared_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  InputSource::InputSource(InputSource&&) noexcept = default;

  auto InputSource::operator=(InputSource&&) noexcept -> InputSource& = default;

  InputSource::~InputSource() = default;

  auto InputSource::create(const InputSourceConfig& config) -> Core::Result<InputSource> {
    if (config.textBytes == 0 || config.textBytes > 1024 * 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid input text byte limit");
    }

    try {
      return InputSource(std::make_shared<Impl>(config));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create input source: " + std::string(error.what()));
    }
  }

  auto InputSource::publish(const Platform::InputState& state) -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Input source was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_index == std::numeric_limits<std::uint64_t>::max() - 1) {
      return Core::fail(Core::ErrorCode::Unavailable, "Input frame index exhausted");
    }

    if (!finite(state.pointer()) || !finite(state.pointerDelta()) || !finite(state.wheel())
        || state.text().size() > m_impl->m_config.textBytes || !Core::isValidUtf8(state.text())) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid published input coordinates/text");
    }

    try {
      Json frame{{"index", std::to_string(m_impl->m_index + 1)},
                 {"keys", Json::object()},
                 {"buttons", Json::object()},
                 {"pointer", point(state.pointer())},
                 {"pointerDelta", point(state.pointerDelta())},
                 {"wheel", point(state.wheel())},
                 {"text", std::string(state.text())}};

      for (const auto& [name, key] : keyNames) {
        frame["keys"][name] = button(state.key(key));
      }

      for (const auto& [name, value] : buttonNames) {
        frame["buttons"][name] = button(state.button(value));
      }

      /** JSON move is nonthrowing; all allocation/validation precedes replacement.
       * JSON 移动不抛异常，全部分配/校验发生在替换之前。 */
      static_assert(std::is_nothrow_move_constructible_v<Json>);
      m_impl->m_frame.emplace(std::move(frame));
      return ++m_impl->m_index;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Publish input frame: " + std::string(error.what()));
    }
  }

  auto InputSource::bindings() const -> Core::Result<HostBindings> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Input source was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      HostBindings result;
      result.reserve(fields.size());

      for (const auto* field : fields) {
        result.push_back({"input." + std::string(field),
                          [impl = m_impl, field](std::string_view args) -> Reply {
                            return impl->read(field, args);
                          },
                          HostAccess::ReadOnly});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Bind input source: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Script

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
    using Reply = Core::Result<std::optional<std::string>>;

    constexpr std::array fields{"snapshot",
                                "key",
                                "button",
                                "pointer",
                                "pointerDelta",
                                "wheel",
                                "text",
                                "index",
                                "pointerCaptured",
                                "capturePointer",
                                "requestTextInput",
                                "textInputRequested",
                                "textInputActive"};

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
    bool m_captureRequested{};
    bool m_textRequested{};

    explicit Impl(InputSourceConfig value) : m_config(value) {
    }

    auto check() const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Input source requires its owner thread");
      }

      return {};
    }

    auto invoke(std::string_view field, std::string_view arguments) -> Reply {
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
        const bool capture = field == "capturePointer" || field == "requestTextInput";

        if (!args.is_array() || args.size() != (named || capture ? 1U : 0U)
            || (named && !args[0].is_string()) || (capture && !args[0].is_boolean())) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid input query argument shape");
        }

        if (capture) {
          (field == "capturePointer" ? m_captureRequested : m_textRequested) = args[0].get<bool>();
          return std::optional<std::string>{};
        }

        std::string name;

        if (named) {
          name = args[0].get<std::string>();
          const auto matches = [&name](const auto& entry) { return name == entry.first; };
          const bool known = field == "key" ? std::ranges::any_of(Platform::keyNames(), matches)
                                            : std::ranges::any_of(Platform::buttonNames(), matches);

          if (!known) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Unknown input " + std::string(field) + " name: " + name);
          }
        }

        if (field == "textInputRequested") {
          return std::optional{Json(m_textRequested).dump()};
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

  auto InputSource::publish(const Platform::InputState& state, bool pointerCaptured,
                            bool textInputActive) -> Core::Result<std::uint64_t> {
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
                 {"pointerCaptured", pointerCaptured},
                 {"textInputActive", textInputActive},
                 {"text", std::string(state.text())}};

      for (const auto& [name, key] : Platform::keyNames()) {
        frame["keys"][name] = button(state.key(key));
      }

      for (const auto& [name, value] : Platform::buttonNames()) {
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

  auto InputSource::textInputRequested() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Input source was moved");
    }

    auto checked = m_impl->check();
    return checked ? Core::Result<bool>{m_impl->m_textRequested}
                   : Core::Result<bool>{std::unexpected(checked.error())};
  }

  auto InputSource::pointerCaptureRequested() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Input source was moved");
    }

    auto checked = m_impl->check();
    return checked ? Core::Result<bool>{m_impl->m_captureRequested}
                   : Core::Result<bool>{std::unexpected(checked.error())};
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
                            return impl->invoke(field, args);
                          },
                          (std::string_view(field) == "capturePointer"
                           || std::string_view(field) == "requestTextInput")
                              ? HostAccess::ReadWrite
                              : HostAccess::ReadOnly});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Bind input source: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Script

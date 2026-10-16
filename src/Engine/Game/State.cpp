module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "../Serialization/Json.hpp"

module Dot.Game.State;

import Dot.Core.Result;
import Dot.Core.Text;

namespace Dot::Game {
  namespace {
    using Json = Serialization::Detail::Json;
    constexpr auto maxNumber = 9007199254740991.0;

    auto keyValid(std::string_view key) -> bool {
      return !key.empty() && key.size() <= 128 && Core::isValidUtf8(key)
             && std::ranges::none_of(key, [](unsigned char c) { return c < 32 || c == 127; });
    }

    auto checkedValue(const Json& value) -> void {
      if (value.is_number()) {
        const auto number = value.get<double>();

        if (!std::isfinite(number) || std::abs(number) > maxNumber) {
          throw std::invalid_argument("State number exceeds finite JavaScript safe magnitude");
        }
      }

      if (value.is_structured()) {
        for (const auto& child : value) {
          checkedValue(child);
        }
      }
    }

    auto parse(std::string_view data, const StateLimits& limits, int depth) -> Json {
      if (limits.bytes < 2 || limits.bytes > 1024 * 1024 || limits.entries == 0
          || limits.entries > 4096 || data.empty() || data.size() > limits.bytes
          || !Core::isValidUtf8(data)) {
        throw std::invalid_argument("Invalid state limits or JSON bytes");
      }

      auto value = Serialization::Detail::parseJson(data, depth);
      checkedValue(value);
      return value;
    }

    auto encoded(const Json& data, const StateLimits& limits) -> std::string {
      if (!data.is_object() || data.size() > limits.entries) {
        throw std::invalid_argument("State requires a bounded root object");
      }

      for (const auto& entry : data.items()) {
        if (!keyValid(entry.key())) {
          throw std::invalid_argument("Invalid literal state key");
        }
      }

      auto bytes = data.dump();

      if (bytes.size() > limits.bytes) {
        throw std::invalid_argument("Canonical state exceeds byte limit");
      }

      return bytes;
    }

    auto error(const std::exception& value) -> std::unexpected<Core::Error> {
      return Core::fail(dynamic_cast<const std::invalid_argument*>(&value)
                                || dynamic_cast<const Json::exception*>(&value)
                            ? Core::ErrorCode::InvalidArgument
                            : Core::ErrorCode::External,
                        "Game state: " + std::string(value.what()));
    }
  }  // namespace

  auto normalizeStateData(std::string_view data, const StateLimits& limits)
      -> Core::Result<std::string> {
    try {
      return encoded(parse(data, limits, 32), limits);
    } catch (const std::exception& value) {
      return error(value);
    }
  }

  struct State::Impl {
    StateLimits m_limits;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_revision{1};
    Json m_data;
    std::string m_encoded;

    Impl(StateLimits limits, Json data, std::string bytes)
        : m_limits(limits), m_data(std::move(data)), m_encoded(std::move(bytes)) {
    }

    auto check() const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Game state requires its owner thread");
      }

      return {};
    }

    auto publish(Json candidate) -> Core::Result<std::uint64_t> {
      if (m_data == candidate) {
        return m_revision;
      }

      if (m_revision == std::numeric_limits<std::uint64_t>::max() - 1) {
        return Core::fail(Core::ErrorCode::Unavailable, "Game state revisions exhausted");
      }

      auto bytes = encoded(candidate, m_limits);
      m_data.swap(candidate);
      m_encoded.swap(bytes);
      return ++m_revision;
    }
  };

  State::State(std::shared_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  State::State(const State&) noexcept = default;

  auto State::operator=(const State&) noexcept -> State& = default;

  State::State(State&&) noexcept = default;

  auto State::operator=(State&&) noexcept -> State& = default;

  State::~State() = default;

  auto State::create(std::string_view initial, const StateLimits& limits) -> Core::Result<State> {
    try {
      auto data = parse(initial, limits, 32);
      auto bytes = encoded(data, limits);
      return State(std::make_shared<Impl>(limits, std::move(data), std::move(bytes)));
    } catch (const std::exception& value) {
      return error(value);
    }
  }

  auto State::snapshot() const -> Core::Result<StateSnapshot> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game state was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      return StateSnapshot{m_impl->m_revision, m_impl->m_encoded};
    } catch (const std::exception& value) {
      return error(value);
    }
  }

  auto State::get(std::string_view key) const -> Core::Result<std::optional<std::string>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game state was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!keyValid(key)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid literal state key");
    }

    try {
      auto found = m_impl->m_data.find(std::string(key));
      return found == m_impl->m_data.end() ? std::optional<std::string>{}
                                           : std::optional{found->dump()};
    } catch (const std::exception& value) {
      return error(value);
    }
  }

  auto State::set(std::string_view key, std::string_view value) -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game state was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!keyValid(key)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid literal state key");
    }

    try {
      auto decoded = parse(value, m_impl->m_limits, 31);
      auto candidate = m_impl->m_data;
      candidate[std::string(key)] = std::move(decoded);
      return m_impl->publish(std::move(candidate));
    } catch (const std::exception& problem) {
      return error(problem);
    }
  }

  auto State::remove(std::string_view key) -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game state was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!keyValid(key)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid literal state key");
    }

    try {
      auto candidate = m_impl->m_data;
      candidate.erase(std::string(key));
      return m_impl->publish(std::move(candidate));
    } catch (const std::exception& value) {
      return error(value);
    }
  }

  auto State::replace(std::string_view data, std::uint64_t expectedRevision)
      -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game state was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (expectedRevision != m_impl->m_revision) {
      return Core::fail(Core::ErrorCode::InvalidState, "Stale game state revision");
    }

    try {
      return m_impl->publish(parse(data, m_impl->m_limits, 32));
    } catch (const std::exception& value) {
      return error(value);
    }
  }
}  // namespace Dot::Game

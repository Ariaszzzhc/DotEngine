#pragma once

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Dot::Serialization::Detail {
  using Json = nlohmann::json;

  /** Private codec helper: reject duplicates and stop before deep recursion.
   * 私有编解码辅助函数：拒绝重复字段，在深层递归前停止解析。 */
  inline auto parseJson(std::string_view source) -> Json {
    std::vector<std::unordered_set<std::string>> keys;
    return Json::parse(source.begin(), source.end(),
                       [&](int depth, Json::parse_event_t event, Json& value) {
                         if (depth > 64) {
                           throw std::invalid_argument("JSON nesting exceeds 64");
                         }

                         if (event == Json::parse_event_t::object_start) {
                           keys.emplace_back();
                         } else if (event == Json::parse_event_t::object_end) {
                           keys.pop_back();
                         } else if (event == Json::parse_event_t::key
                                    && !keys.back().insert(value.get<std::string>()).second) {
                           throw std::invalid_argument("Duplicate JSON field");
                         }

                         return true;
                       });
  }

  inline auto requireFields(const Json& object, std::initializer_list<std::string_view> required,
                            std::initializer_list<std::string_view> optional = {}) -> void {
    if (!object.is_object()) {
      throw std::invalid_argument("Expected a JSON object");
    }

    for (auto field : required) {
      if (!object.contains(field)) {
        throw std::invalid_argument("Missing JSON field: " + std::string(field));
      }
    }

    for (const auto& item : object.items()) {
      bool allowed = false;

      for (auto field : required) {
        allowed = allowed || item.key() == field;
      }

      for (auto field : optional) {
        allowed = allowed || item.key() == field;
      }

      if (!allowed) {
        throw std::invalid_argument("Unknown JSON field: " + item.key());
      }
    }
  }

  inline auto number(const Json& value) -> float {
    if (!value.is_number()) {
      throw std::invalid_argument("Expected a JSON number");
    }

    const double parsed = value.get<double>();

    if (!std::isfinite(parsed) || std::abs(parsed) > std::numeric_limits<float>::max()) {
      throw std::invalid_argument("JSON number exceeds float range");
    }

    return static_cast<float>(parsed);
  }

  template <std::size_t N>
  auto numbers(const Json& value) -> std::array<float, N> {
    if (!value.is_array() || value.size() != N) {
      throw std::invalid_argument("Invalid JSON vector dimensions");
    }

    std::array<float, N> result;

    for (std::size_t index = 0; index < N; ++index) {
      result[index] = number(value[index]);
    }

    return result;
  }

  inline auto id(const Json& value) -> std::uint64_t {
    if (!value.is_string()) {
      throw std::invalid_argument("Entity IDs require decimal strings");
    }

    const auto& text = value.get_ref<const std::string&>();
    std::uint64_t result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);

    if (text.empty() || text.front() == '0' || parsed.ec != std::errc{}
        || parsed.ptr != text.data() + text.size()
        || result == std::numeric_limits<std::uint64_t>::max()) {
      throw std::invalid_argument("Invalid canonical entity ID");
    }

    return result;
  }
}  // namespace Dot::Serialization::Detail

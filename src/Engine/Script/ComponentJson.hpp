#pragma once

#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "../Serialization/Json.hpp"

namespace Dot::Script::Detail {
  namespace Codec = Serialization::Detail;
  using Json = Codec::Json;

  template <typename Vec>
  auto xyz(const Vec& value) -> Json {
    return {{"x", value.x}, {"y", value.y}, {"z", value.z}};
  }

  template <typename Vec>
  auto xyzw(const Vec& value) -> Json {
    return {{"x", value.x}, {"y", value.y}, {"z", value.z}, {"w", value.w}};
  }

  template <typename Vec>
  auto vector(const Json& value) -> Vec {
    Codec::requireFields(value, {"x", "y", "z"});
    return {Codec::number(value["x"]), Codec::number(value["y"]), Codec::number(value["z"])};
  }

  template <typename Vec>
  auto color(const Json& value) -> Vec {
    Codec::requireFields(value, {"x", "y", "z", "w"});
    return {Codec::number(value["x"]), Codec::number(value["y"]), Codec::number(value["z"]),
            Codec::number(value["w"])};
  }

  template <typename Enum>
  auto named(const Json& value, std::initializer_list<std::pair<std::string_view, Enum>> entries)
      -> Enum {
    const auto name = value.get<std::string>();

    for (const auto& [text, result] : entries) {
      if (name == text) {
        return result;
      }
    }

    throw std::invalid_argument("Unknown component enum: " + name);
  }
}  // namespace Dot::Script::Detail

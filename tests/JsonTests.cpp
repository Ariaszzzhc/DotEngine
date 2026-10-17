#include <doctest/doctest.h>

#include <stdexcept>
#include <string>

#include "../src/Engine/Serialization/Json.hpp"

TEST_CASE("json.strict_parser_rejects_nul_request_prefixes_but_keeps_escaped_unicode") {
  using Dot::Serialization::Detail::parseJson;
  using namespace std::string_literals;

  for (const auto& source : {"null\0ignored"s, "{\"x\":true}\0[]"s, "\"a\0b\""s, "\0"s}) {
    CHECK_THROWS_AS(parseJson(source), std::invalid_argument);
  }

  const auto escaped = parseJson(R"({"text":"\u0000","nested":[1,true,null]})");
  CHECK(escaped["text"].get<std::string>() == std::string(1, '\0'));
  CHECK(escaped["nested"].size() == 3);
  CHECK_THROWS(parseJson(R"({"x":true} ignored)"));
  CHECK_THROWS_AS(parseJson(R"({"x":true,"x":false})"), std::invalid_argument);
  CHECK_THROWS_AS(parseJson(R"({"x":{"y":[]}})", 1), std::invalid_argument);
}

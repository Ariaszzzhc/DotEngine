#include <doctest/doctest.h>

#include <array>
#include <string>
#include <string_view>

import Dot.Core.Text;
import Dot.Core.Result;

using namespace Dot::Core;

TEST_CASE("text.empty_ascii_and_embedded_nul") {
  CHECK(isValidUtf8(""));
  CHECK(utf8CodePoints("").value() == 0);
  CHECK(utf8CodePoints("DotEngine").value() == 9);
  const std::string text{"A\0B", 3};
  CHECK(isValidUtf8(text));
  CHECK(utf8CodePoints(text).value() == 3);
}

TEST_CASE("text.multilingual_and_combining_scalars") {
  CHECK(isValidUtf8("中文🙂"));
  CHECK(utf8CodePoints("中文🙂").value() == 3);
  CHECK(utf8CodePoints("e\xCC\x81").value() == 2);
  CHECK(utf8CodePoints("\xC3\xA9").value() == 1);
}

TEST_CASE("text.valid_scalar_encoding_boundaries") {
  for (std::string_view text :
       {"\x7F", "\xC2\x80", "\xDF\xBF", "\xE0\xA0\x80", "\xED\x9F\xBF", "\xEE\x80\x80",
        "\xEF\xBF\xBF", "\xF0\x90\x80\x80", "\xF4\x8F\xBF\xBF"}) {
    CHECK(isValidUtf8(text));
    CHECK(utf8CodePoints(text).value() == 1);
  }
}

TEST_CASE("text.reject_overlong_surrogate_and_out_of_range_encodings") {
  for (std::string_view text :
       {"\xC0\x80", "\xC1\xBF", "\xE0\x80\x80", "\xF0\x80\x80\x80", "\xED\xA0\x80", "\xED\xBF\xBF",
        "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xFF"}) {
    CHECK_FALSE(isValidUtf8(text));
    auto result = utf8CodePoints(text);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ErrorCode::InvalidArgument);
    CHECK(result.error().message.find("byte 0") != std::string::npos);
  }
}

TEST_CASE("text.reject_bad_continuations_and_truncation") {
  for (std::string_view text : {"\x80", "\xBF", "\xC2", "\xE0\xA0", "\xF0\x90\x80", "\xC2 ",
                                "\xE0\xA0 ", "\xF0\x90\x80 "}) {
    CHECK_FALSE(isValidUtf8(text));
    CHECK_FALSE(utf8CodePoints(text));
  }

  auto result = utf8CodePoints("ABC\xC2 ");
  REQUIRE_FALSE(result);
  CHECK(result.error().message.find("byte 4") != std::string::npos);
}

TEST_CASE("text.every_single_byte_has_the_expected_validity") {
  for (unsigned value = 0; value < 256; ++value) {
    const std::array text{static_cast<char>(value)};
    const std::string_view view(text.data(), text.size());
    CHECK(isValidUtf8(view) == (value < 128));
  }
}

TEST_CASE("text.long_sequences_count_scalars_without_normalization") {
  std::string text;

  for (int index = 0; index < 10000; ++index) {
    text += "A中文🙂";
  }

  REQUIRE(isValidUtf8(text));
  CHECK(utf8CodePoints(text).value() == 40000);
  text += '\x80';
  CHECK_FALSE(isValidUtf8(text));
}

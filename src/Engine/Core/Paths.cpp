module;

#include <algorithm>
#include <string_view>

module Dot.Core.Paths;

import Dot.Core.Text;

namespace Dot::Core {
  namespace {
    auto equalAscii(std::string_view left, std::string_view right) noexcept -> bool {
      return left.size() == right.size() && std::ranges::equal(left, right, [](char a, char b) {
               const auto upper = [](char c) {
                 return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
               };
               return upper(a) == upper(b);
             });
    }
  }  // namespace

  auto isPortableRelativePath(std::string_view value) noexcept -> bool {
    if (value.empty() || value.size() > 4096 || value.front() == '/' || value.back() == '/'
        || value.find_first_of("\\:<>\"|?*") != std::string_view::npos || !isValidUtf8(value)
        || std::ranges::any_of(value, [](unsigned char c) { return c < 32 || c == 127; })) {
      return false;
    }

    std::size_t begin = 0;

    while (begin < value.size()) {
      const auto end = value.find('/', begin);
      const auto part = value.substr(begin, end == std::string_view::npos ? end : end - begin);

      if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' '
          || part.size() > 240) {
        return false;
      }

      const auto stem = part.substr(0, part.find('.'));

      if (equalAscii(stem, "CON") || equalAscii(stem, "PRN") || equalAscii(stem, "AUX")
          || equalAscii(stem, "NUL")) {
        return false;
      }

      if (stem.size() > 3
          && (equalAscii(stem.substr(0, 3), "COM") || equalAscii(stem.substr(0, 3), "LPT"))) {
        const auto digit = stem.substr(3);

        if ((digit.size() == 1 && digit.front() >= '1' && digit.front() <= '9') || digit == "¹"
            || digit == "²" || digit == "³") {
          return false;
        }
      }

      if (end == std::string_view::npos) {
        break;
      }

      begin = end + 1;
    }

    return true;
  }
}  // namespace Dot::Core

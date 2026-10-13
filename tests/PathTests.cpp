#include <doctest/doctest.h>

#include <string>
#include <vector>

import Dot.Core.Paths;

TEST_CASE("paths.portable_names_cover_unicode_devices_and_boundaries") {
  for (const auto* name : {"assets/cube.obj", "scripts/行为.ts", "README", ".hidden/name", "COM0",
                           "COM10", "company.ts"}) {
    CHECK(Dot::Core::isPortableRelativePath(name));
  }

  for (const auto& name : std::vector<std::string>{"",
                                                   "/root",
                                                   "a/",
                                                   "a//b",
                                                   "a/../b",
                                                   "./a",
                                                   "C:a",
                                                   "a\\b",
                                                   "a?.ts",
                                                   "a ",
                                                   "a.",
                                                   "con.ts",
                                                   "PRN",
                                                   "nul/file",
                                                   "lpt9.obj",
                                                   "COM¹.txt",
                                                   "lpt²/thing",
                                                   "COM³",
                                                   std::string("a\0b", 3),
                                                   std::string(1, static_cast<char>(0xFF)),
                                                   std::string(241, 'a'),
                                                   std::string(4097, 'a'),
                                                   "a\n/b"}) {
    CHECK_FALSE(Dot::Core::isPortableRelativePath(name));
  }

  CHECK(Dot::Core::isPortableRelativePath(std::string(240, 'a')));
  std::string limit;

  while (limit.size() + 241 <= 4096) {
    limit += std::string(240, 'a') + "/";
  }

  limit += std::string(4096 - limit.size(), 'b');
  CHECK(limit.size() == 4096);
  CHECK(Dot::Core::isPortableRelativePath(limit));
}

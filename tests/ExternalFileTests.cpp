#include <doctest/doctest.h>

#include <filesystem>
#include <string>

import Dot.Platform.ExternalFiles;

TEST_CASE("external_file.uri_escapes_reserved_unicode_and_literal_shell_bytes") {
#ifdef _WIN32
  const std::filesystem::path path{L"C:/game folder/名字#%$'().ts"};
  const auto expected = "file:///C:/game%20folder/%E5%90%8D%E5%AD%97%23%25%24%27%28%29.ts";
#else
  const std::filesystem::path path{"/game folder/名字#%$'().ts"};
  const auto expected = "file:///game%20folder/%E5%90%8D%E5%AD%97%23%25%24%27%28%29.ts";
#endif
  CHECK(Dot::Platform::fileUri(path).value() == expected);
  CHECK_FALSE(Dot::Platform::fileUri("relative/file.ts"));
  CHECK_FALSE(Dot::Platform::fileUri(std::filesystem::path(std::string("/a\0b", 4))));
  CHECK_FALSE(Dot::Platform::fileUri(std::filesystem::path('/' + std::string(4096, 'a'))));
#ifdef _WIN32
  CHECK(Dot::Platform::fileUri(L"//server/share/file name.ts").value()
        == "file://server/share/file%20name.ts");
#endif
}

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>

auto main(int argc, char** argv) -> int {
  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    std::fputs("Version 7.0.2\n", stdout);
    return 0;
  }

  if (argc != 5 || std::string_view(argv[1]) != "--project") {
    return 2;
  }

  std::ifstream source("src/main.ts", std::ios::binary);
  const std::string text{std::istreambuf_iterator<char>{source}, {}};

  if (text == "sleep") {
    std::this_thread::sleep_for(std::chrono::seconds{10});
    return 0;
  }

  if (text == "diagnostics") {
    const std::string bytes(32768, 'x');
    std::fwrite(bytes.data(), 1, bytes.size(), stdout);
    std::fwrite(bytes.data(), 1, bytes.size(), stderr);
    return 3;
  }

  if (text == "zero") {
    return 0;
  }

  if (text == "failure") {
    std::fputs("fixture diagnostic", stderr);
    return 35;
  }

  std::filesystem::create_directory("out");
  std::ofstream("out/main.js") << "export function run(){return 42;}";

  if (text == "partial") {
    return 0;
  }

  std::ofstream("out/main.js.map") << "{}";

  if (text == "unexpected") {
    std::ofstream("out/extra.js") << "export {};";
  }

  return 0;
}

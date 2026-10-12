#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <signal.h>
#  include <unistd.h>
#endif

namespace {
  auto execute(const std::vector<std::string>& arguments) -> int {
    if (arguments.empty()) {
      return 2;
    }

    const auto& mode = arguments[0];

    if (mode == "args") {
      const auto cwd = std::filesystem::current_path().generic_u8string();
      nlohmann::json result{
          {"arguments", std::vector<std::string>(arguments.begin() + 1, arguments.end())},
          {"directory", std::string(cwd.begin(), cwd.end())}};
      const auto text = result.dump();
      std::fwrite(text.data(), 1, text.size(), stdout);
      return 0;
    }

    if (mode == "exit") {
      std::fwrite("normal output", 1, 13, stdout);
      std::fwrite("failure detail", 1, 14, stderr);
      return std::stoi(arguments.at(1));
    }

    if (mode == "stdin") {
      std::fputs(std::getchar() == EOF ? "eof" : "unexpected input", stdout);
      return 0;
    }

    if (mode == "environment") {
      const auto* value = std::getenv("DOT_PROCESS_FIXTURE_VALUE");
      std::fputs(value ? value : "missing", stdout);
      return 0;
    }

    if (mode == "emit") {
      std::array<char, 4096> out;
      std::array<char, 4096> error;
      out.fill('o');
      error.fill('e');
      const auto count = std::stoi(arguments.at(1));

      for (int index = 0; index < count; ++index) {
        std::fwrite(out.data(), 1, out.size(), stdout);
        std::fflush(stdout);
        std::fwrite(error.data(), 1, error.size(), stderr);
        std::fflush(stderr);
      }

      return 0;
    }

    if (mode == "binary") {
      const std::array<unsigned char, 4> bytes{0, 255, 128, 65};
      std::fwrite(bytes.data(), 1, bytes.size(), stdout);
      return 0;
    }

    if (mode == "sleep") {
      std::this_thread::sleep_for(std::chrono::seconds{10});
      return 0;
    }

    if (mode == "heartbeat") {
      for (int index = 0; index < 1000; ++index) {
        std::ofstream file(
            std::filesystem::path{std::u8string(arguments.at(1).begin(), arguments.at(1).end())},
            std::ios::app);
        file << 'x';
        file.close();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }

      return 0;
    }

    if (mode == "descendant") {
#if defined(_WIN32)
      std::array<wchar_t, 32768> name{};
      const auto length = GetModuleFileNameW(nullptr, name.data(), static_cast<DWORD>(name.size()));
      const auto path
          = std::filesystem::path{std::u8string(arguments.at(1).begin(), arguments.at(1).end())}
                .native();
      std::wstring command
          = L"\"" + std::wstring{name.data(), length} + L"\" heartbeat \"" + path + L"\"";
      STARTUPINFOW startup{};
      startup.cb = sizeof(startup);
      startup.dwFlags = STARTF_USESTDHANDLES;
      startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
      startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
      startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
      PROCESS_INFORMATION information{};

      if (!CreateProcessW(name.data(), command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                          &startup, &information)) {
        return 3;
      }

      CloseHandle(information.hThread);
      CloseHandle(information.hProcess);
#else
      const auto child = fork();

      if (child < 0) {
        return 3;
      }

      if (child == 0) {
        return execute({"heartbeat", arguments.at(1)});
      }
#endif
      return 0;
    }

#if !defined(_WIN32)
    if (mode == "signal") {
      raise(SIGTERM);
      return 4;
    }

    if (mode == "fd") {
      const auto descriptor = std::stoi(arguments.at(1));
      std::fputs(write(descriptor, "x", 1) < 0 ? "closed" : "inherited", stdout);
      return 0;
    }
#endif
    return 2;
  }
}  // namespace

#if defined(_WIN32)
auto wmain(int argc, wchar_t** argv) -> int {
  std::vector<std::string> arguments;

  for (int index = 1; index < argc; ++index) {
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[index], -1, nullptr,
                                           0, nullptr, nullptr);

    if (count == 0) {
      return 5;
    }

    std::string text(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[index], -1, text.data(), count, nullptr,
                        nullptr);
    text.pop_back();
    arguments.push_back(std::move(text));
  }

  return execute(arguments);
}
#else
auto main(int argc, char** argv) -> int {
  return execute(std::vector<std::string>(argv + 1, argv + argc));
}
#endif

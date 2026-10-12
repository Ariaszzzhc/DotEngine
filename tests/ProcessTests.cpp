#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#  include <fcntl.h>
#  include <signal.h>
#  include <unistd.h>
#endif

import Dot.Core.Process;
import Dot.Core.Result;

namespace {
  using namespace Dot::Core;
  using namespace std::chrono_literals;

  struct Directory {
    std::filesystem::path root;

    Directory() {
      std::random_device random;

      for (int attempt = 0; attempt < 100; ++attempt) {
        root = std::filesystem::temp_directory_path()
               / ("dot-process-" + std::to_string(random()) + "-" + std::to_string(random()));
        std::error_code error;

        if (std::filesystem::create_directory(root, error)) {
          return;
        }

        if (error) {
          throw std::runtime_error(error.message());
        }
      }

      throw std::runtime_error("Cannot create process test directory");
    }

    ~Directory() {
      std::error_code ignored;
      std::filesystem::remove_all(root, ignored);
    }
  };

  auto config(const std::filesystem::path& root, std::vector<std::string> arguments)
      -> ProcessConfig {
    return {std::filesystem::path{DOT_PROCESS_FIXTURE}, std::move(arguments), root};
  }
}  // namespace

TEST_CASE("process_literal_arguments_unicode_working_directory_and_parent_state") {
  Directory directory;
  const auto before = std::filesystem::current_path();
  auto value = config(directory.root, {"args", "", "two words", "中文🙂", "quote\"word",
                                       "C:\\slash\\", "$HOME;touch unwanted", "\\\"", "x\nline"});
  auto result = runProcess(value);
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->exitCode == 0);
  auto parsed = nlohmann::json::parse(result->standardOutput);
  CHECK(parsed["arguments"]
        == std::vector<std::string>(value.arguments.begin() + 1, value.arguments.end()));
  const auto reported = parsed["directory"].get<std::string>();
  CHECK(std::filesystem::equivalent(
      std::filesystem::path{std::u8string(reported.begin(), reported.end())}, directory.root));
  CHECK(std::filesystem::current_path() == before);
  CHECK_FALSE(std::filesystem::exists(directory.root / "unwanted"));
}

TEST_CASE("process_nonzero_exit_preserves_status_and_separate_output") {
  Directory directory;
  auto result = runProcess(config(directory.root, {"exit", "7"}));
  REQUIRE(result);
  CHECK(result->exitCode == 7);
  CHECK(result->standardOutput == "normal output");
  CHECK(result->standardError == "failure detail");
}

TEST_CASE("process_stdin_is_eof_and_raw_binary_output_is_preserved") {
  Directory directory;
  auto input = runProcess(config(directory.root, {"stdin"}));
  REQUIRE(input);
  CHECK(input->standardOutput == "eof");
  auto binary = runProcess(config(directory.root, {"binary"}));
  REQUIRE(binary);
  CHECK(binary->standardOutput == std::string{"\0\xff\x80\x41", 4});
}

TEST_CASE("process_concurrent_stdout_stderr_capture_exceeds_pipe_capacity_without_deadlock") {
  Directory directory;
  auto result = runProcess(config(directory.root, {"emit", "64"}));
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->standardOutput == std::string(64 * 4096, 'o'));
  CHECK(result->standardError == std::string(64 * 4096, 'e'));
}

TEST_CASE("process_output_limit_combines_both_streams_and_cleans_child") {
  Directory directory;
  const auto before = processResources();
  auto value = config(directory.root, {"emit", "10000"});
  value.outputBytes = 8192;
  auto failed = runProcess(value);
  REQUIRE_FALSE(failed);
  CHECK(failed.error().code == ErrorCode::Unavailable);
  CHECK(failed.error().message.find("output exceeds") != std::string::npos);
  CHECK(processResources().handles == before.handles);
  CHECK(processResources().children == before.children);
  value.arguments = {"emit", "1"};
  auto exact = runProcess(value);
  REQUIRE(exact);
  CHECK(exact->standardOutput.size() + exact->standardError.size() == 8192);
}

TEST_CASE("process_timeout_cancels_and_reaps_owned_child") {
  Directory directory;
  const auto before = processResources();
  auto value = config(directory.root, {"sleep"});
  value.timeout = 50ms;
  const auto started = std::chrono::steady_clock::now();
  auto failed = runProcess(value);
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("timeout") != std::string::npos);
  CHECK(std::chrono::steady_clock::now() - started < 2s);
  CHECK(processResources().handles == before.handles);
  CHECK(processResources().children == before.children);
}

TEST_CASE("process_descendant_retaining_pipes_is_cancelled_after_parent_exit") {
  Directory directory;
  const auto marker = directory.root / "heartbeat.txt";
  const auto utf8 = marker.generic_u8string();
  auto value = config(directory.root, {"descendant", std::string(utf8.begin(), utf8.end())});
  /** Allow cold executable loading before the descendant starts its heartbeat;
   * this case verifies cancellation/pipe ownership, not subsecond startup latency.
   * 为冷启动可执行文件加载后开启子进程心跳留出时间；本例验证取消/管道所有权，
   * 不验证亚秒级启动延迟。 */
  value.timeout = 2s;
  auto failed = runProcess(value);
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("timeout") != std::string::npos);
  REQUIRE(std::filesystem::exists(marker));
  const auto size = std::filesystem::file_size(marker);
  std::this_thread::sleep_for(100ms);
  CHECK(std::filesystem::file_size(marker) == size);
  CHECK(processResources().children == 0);
}

TEST_CASE("process_bad_paths_encoding_and_limits_do_not_spawn_or_leak") {
  Directory directory;
  const auto before = processResources();
  auto valid = config(directory.root, {"args"});
  std::vector<ProcessConfig> invalid(8, valid);
  invalid[0].executable = "relative";
  invalid[1].directory = "relative";
  invalid[2].arguments = {std::string{"a\0b", 3}};
  invalid[3].arguments = {std::string{"\xc0\x80"}};
  invalid[4].timeout = 0ms;
  invalid[5].outputBytes = 0;
  invalid[6].arguments = {std::string(1024 * 1024 + 1, 'x')};
  invalid[7].arguments.resize(4097);

  for (const auto& value : invalid) {
    CHECK_FALSE(runProcess(value));
    CHECK(processResources().handles == before.handles);
    CHECK(processResources().children == before.children);
  }

  valid.executable = directory.root / "missing";
  CHECK_FALSE(runProcess(valid));
  valid = config(directory.root / "missing-directory", {"args"});
  CHECK_FALSE(runProcess(valid));
  CHECK(processResources().handles == before.handles);
  CHECK(processResources().children == before.children);
}

TEST_CASE("process_repeated_and_concurrent_runs_keep_independent_capture_and_directory") {
  Directory first;
  Directory second;
  const auto before = processResources();

  for (int attempt = 0; attempt < 12; ++attempt) {
    auto one = std::async(std::launch::async,
                          [&] { return runProcess(config(first.root, {"exit", "3"})); });
    auto two = std::async(std::launch::async,
                          [&] { return runProcess(config(second.root, {"stdin"})); });
    auto resultOne = one.get();
    auto resultTwo = two.get();
    REQUIRE(resultOne);
    REQUIRE(resultTwo);
    CHECK(resultOne->exitCode == 3);
    CHECK(resultTwo->standardOutput == "eof");
  }

  CHECK(processResources().handles == before.handles);
  CHECK(processResources().children == before.children);
}

TEST_CASE("process_unicode_executable_path_is_explicit_and_not_searched") {
  Directory directory;
  const auto name = directory.root / std::filesystem::path{u8"程序 with space.exe"};
  std::filesystem::copy_file(std::filesystem::path{DOT_PROCESS_FIXTURE}, name);
  auto value = config(directory.root, {"exit", "0"});
  value.executable = name;
  auto result = runProcess(value);
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->exitCode == 0);
}

#if !defined(_WIN32)
TEST_CASE("process_signal_failure_and_unrelated_descriptor_inheritance_are_explicit") {
  Directory directory;
  auto signaled = runProcess(config(directory.root, {"signal"}));
  REQUIRE_FALSE(signaled);
  CHECK(signaled.error().message.find("signal") != std::string::npos);
  const auto file = directory.root / "unrelated.txt";
  const int descriptor = open(file.c_str(), O_CREAT | O_WRONLY, 0600);
  REQUIRE(descriptor >= 0);
  auto checked = runProcess(config(directory.root, {"fd", std::to_string(descriptor)}));
  close(descriptor);
  REQUIRE(checked);
  CHECK(checked->standardOutput == "closed");
  CHECK(std::filesystem::file_size(file) == 0);
}
#endif

#if !defined(_WIN32)
TEST_CASE("process_closed_parent_stdin_does_not_alias_child_pipe_actions") {
  Directory directory;

  struct Restore {
    int saved{fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 3)};

    ~Restore() {
      if (saved >= 0) {
        dup2(saved, STDIN_FILENO);
        close(saved);
      }
    }
  };

  auto result = [&] {
    Restore restore;
    REQUIRE(restore.saved >= 0);
    REQUIRE(close(STDIN_FILENO) == 0);
    return runProcess(config(directory.root, {"stdin"}));
  }();
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->standardOutput == "eof");
}

TEST_CASE("process_rejects_ignored_child_signal_without_changing_parent_disposition") {
  Directory directory;

  struct Restore {
    struct sigaction saved{};

    ~Restore() {
      sigaction(SIGCHLD, &saved, nullptr);
    }
  } restore;

  REQUIRE(sigaction(SIGCHLD, nullptr, &restore.saved) == 0);
  struct sigaction ignored{};
  ignored.sa_handler = SIG_IGN;
  sigemptyset(&ignored.sa_mask);
  REQUIRE(sigaction(SIGCHLD, &ignored, nullptr) == 0);
  auto result = runProcess(config(directory.root, {"stdin"}));
  REQUIRE_FALSE(result);
  CHECK(result.error().code == ErrorCode::InvalidState);
  struct sigaction current{};
  REQUIRE(sigaction(SIGCHLD, nullptr, &current) == 0);
  CHECK(current.sa_handler == SIG_IGN);
}
#endif

#if !defined(_WIN32)
TEST_CASE("process_inherits_environment_without_modifying_parent_values") {
  Directory directory;

  struct Restore {
    std::optional<std::string> previous;

    ~Restore() {
      if (previous) {
        setenv("DOT_PROCESS_FIXTURE_VALUE", previous->c_str(), 1);
      } else {
        unsetenv("DOT_PROCESS_FIXTURE_VALUE");
      }
    }
  } restore;

  const auto* old = std::getenv("DOT_PROCESS_FIXTURE_VALUE");

  if (old) {
    restore.previous = old;
  }

  REQUIRE(setenv("DOT_PROCESS_FIXTURE_VALUE", "literal inherited value", 1) == 0);
  auto result = runProcess(config(directory.root, {"environment"}));
  REQUIRE(result);
  CHECK(result->standardOutput == "literal inherited value");
  CHECK(std::string(std::getenv("DOT_PROCESS_FIXTURE_VALUE")) == "literal inherited value");
}
#endif

module;

#if defined(__linux__) && !defined(_GNU_SOURCE)
#  define _GNU_SOURCE
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <poll.h>
#  include <signal.h>
#  include <spawn.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <crt_externs.h>
#  else
extern "C" char** environ;
#  endif
#endif

module Dot.Core.Process;

import Dot.Core.Result;
import Dot.Core.Text;

namespace Dot::Core {
  namespace {
    std::atomic<std::uint64_t> liveHandles{};
    std::atomic<std::uint64_t> liveChildren{};
    using Clock = std::chrono::steady_clock;

    auto validate(const ProcessConfig& config) -> Result<void> {
      if (!config.executable.is_absolute() || !config.directory.is_absolute()
          || config.timeout.count() < 1 || config.timeout > std::chrono::minutes{10}
          || config.outputBytes == 0 || config.outputBytes > 64 * 1024 * 1024
          || config.arguments.size() > 4096) {
        return fail(ErrorCode::InvalidArgument, "Invalid process paths/limits");
      }

      for (const auto& path : {config.executable, config.directory}) {
        const auto utf8 = path.generic_u8string();
        const std::string text(utf8.begin(), utf8.end());

        if (text.size() > 4096 || text.find('\0') != std::string::npos || !isValidUtf8(text)) {
          return fail(ErrorCode::InvalidArgument,
                      "Process paths require bounded UTF-8 without NUL");
        }
      }

      std::size_t bytes = 0;

      for (const auto& argument : config.arguments) {
        if (argument.size() > 1024 * 1024 - bytes || argument.find('\0') != std::string::npos
            || !isValidUtf8(argument)) {
          return fail(ErrorCode::InvalidArgument, "Process arguments exceed limits/encoding");
        }

        bytes += argument.size();
      }

      return {};
    }

    auto append(ProcessResult& result, bool error, const char* bytes, std::size_t count,
                std::uint64_t limit) -> Result<void> {
      const auto used = result.standardOutput.size() + result.standardError.size();

      if (count > limit - used) {
        return fail(ErrorCode::Unavailable, std::format("Process output exceeds {} bytes", limit));
      }

      (error ? result.standardError : result.standardOutput).append(bytes, count);
      return {};
    }

#if defined(_WIN32)
    struct Handle {
      HANDLE value{};

      Handle() = default;

      explicit Handle(HANDLE owned) : value(owned) {
        if (valid()) {
          liveHandles.fetch_add(1);
        }
      }

      Handle(const Handle&) = delete;
      auto operator=(const Handle&) -> Handle& = delete;

      Handle(Handle&& other) noexcept : value(std::exchange(other.value, nullptr)) {
      }

      auto operator=(Handle&& other) noexcept -> Handle& {
        if (this != &other) {
          close();
          value = std::exchange(other.value, nullptr);
        }

        return *this;
      }

      ~Handle() {
        close();
      }

      auto valid() const noexcept -> bool {
        return value && value != INVALID_HANDLE_VALUE;
      }

      auto close() noexcept -> void {
        if (valid()) {
          CloseHandle(value);
          liveHandles.fetch_sub(1);
        }

        value = nullptr;
      }
    };

    auto systemError(std::string_view operation) -> std::unexpected<Error> {
      return fail(ErrorCode::Io, std::format("{}: Win32 error {}", operation, GetLastError()));
    }

    auto quote(std::wstring_view argument) -> std::wstring {
      std::wstring result = L"\"";
      std::size_t slashes = 0;

      for (auto character : argument) {
        if (character == L'\\') {
          ++slashes;
          continue;
        }

        result.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += character;
        slashes = 0;
      }

      result.append(slashes * 2, L'\\');
      result += L'"';
      return result;
    }

    struct Attributes {
      std::vector<std::byte> storage;
      LPPROC_THREAD_ATTRIBUTE_LIST value{};

      ~Attributes() {
        if (value) {
          DeleteProcThreadAttributeList(value);
        }
      }
    };

    struct Child {
      Handle process;
      Handle thread;
      Handle job;
      bool assigned{};
      bool owned{};

      ~Child() {
        if (owned) {
          if (assigned) {
            TerminateJobObject(job.value, 1);
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};

            while (QueryInformationJobObject(job.value, JobObjectBasicAccountingInformation,
                                             &accounting, sizeof(accounting), nullptr)
                   && accounting.ActiveProcesses != 0) {
              std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
          } else {
            TerminateProcess(process.value, 1);
          }

          WaitForSingleObject(process.value, INFINITE);
          liveChildren.fetch_sub(1);
        }
      }
    };

    auto execute(const ProcessConfig& config) -> Result<ProcessResult> {
      SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
      const auto pipe = [&](Handle& reader, Handle& writer) -> Result<void> {
        HANDLE read = nullptr;
        HANDLE write = nullptr;

        if (!CreatePipe(&read, &write, &security, 0)) {
          return systemError("Create process pipe");
        }

        reader = Handle{read};
        writer = Handle{write};

        if (!SetHandleInformation(reader.value, HANDLE_FLAG_INHERIT, 0)) {
          return systemError("Restrict process pipe inheritance");
        }

        return {};
      };
      Handle outRead, outWrite, errRead, errWrite;
      auto prepared = pipe(outRead, outWrite);

      if (!prepared) {
        return std::unexpected(prepared.error());
      }

      prepared = pipe(errRead, errWrite);

      if (!prepared) {
        return std::unexpected(prepared.error());
      }

      Handle input{CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                               OPEN_EXISTING, 0, nullptr)};
      Child child;
      child.job = Handle{CreateJobObjectW(nullptr, nullptr)};

      if (!input.valid() || !child.job.valid()) {
        return systemError("Create process stdin/job");
      }

      JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
      limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

      if (!SetInformationJobObject(child.job.value, JobObjectExtendedLimitInformation, &limits,
                                   sizeof(limits))) {
        return systemError("Configure process job");
      }

      Attributes attributes;
      SIZE_T bytes = 0;
      InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
      attributes.storage.resize(bytes);
      auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.storage.data());

      if (!InitializeProcThreadAttributeList(list, 1, 0, &bytes)) {
        return systemError("Initialize process inheritance attributes");
      }

      attributes.value = list;
      std::array<HANDLE, 3> inherited{input.value, outWrite.value, errWrite.value};

      if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(),
                                     sizeof(inherited), nullptr, nullptr)) {
        return systemError("Set process handle allowlist");
      }

      auto command = quote(config.executable.native());

      for (const auto& argument : config.arguments) {
        command += L' ';
        command += quote(
            std::filesystem::path{std::u8string(argument.begin(), argument.end())}.native());
      }

      if (command.size() >= 32767) {
        return fail(ErrorCode::InvalidArgument, "Process command line exceeds Win32 limit");
      }

      STARTUPINFOEXW startup{};
      startup.StartupInfo.cb = sizeof(startup);
      startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
      startup.StartupInfo.hStdInput = input.value;
      startup.StartupInfo.hStdOutput = outWrite.value;
      startup.StartupInfo.hStdError = errWrite.value;
      startup.lpAttributeList = list;
      PROCESS_INFORMATION information{};

      if (!CreateProcessW(config.executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                          EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW,
                          nullptr, config.directory.c_str(), &startup.StartupInfo, &information)) {
        return systemError("Launch process");
      }

      child.process = Handle{information.hProcess};
      child.thread = Handle{information.hThread};
      child.owned = true;
      liveChildren.fetch_add(1);

      if (!AssignProcessToJobObject(child.job.value, child.process.value)) {
        return systemError("Assign process job");
      }

      child.assigned = true;

      if (ResumeThread(child.thread.value) == static_cast<DWORD>(-1)) {
        return systemError("Resume process");
      }

      outWrite.close();
      errWrite.close();
      input.close();
      ProcessResult result{};
      const auto deadline = Clock::now() + config.timeout;
      const auto read = [&](Handle& handle, bool error) -> Result<void> {
        std::array<char, 8192> buffer{};

        for (int batch = 0; batch < 16 && handle.valid(); ++batch) {
          DWORD available = 0;

          if (!PeekNamedPipe(handle.value, nullptr, 0, nullptr, &available, nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
              handle.close();
              break;
            }

            return systemError("Inspect process output");
          }

          if (available == 0) {
            break;
          }

          DWORD count = 0;
          const auto wanted = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));

          if (!ReadFile(handle.value, buffer.data(), wanted, &count, nullptr)) {
            return systemError("Read process output");
          }

          auto added = append(result, error, buffer.data(), count, config.outputBytes);

          if (!added) {
            return added;
          }
        }

        return {};
      };

      while (true) {
        if (config.stop.stop_requested()) {
          return fail(ErrorCode::Unavailable, "Process cancelled");
        }

        auto out = read(outRead, false);
        auto err = read(errRead, true);

        if (!out || !err) {
          return std::unexpected(!out ? out.error() : err.error());
        }

        const auto state = WaitForSingleObject(child.process.value, 0);

        if (state == WAIT_FAILED) {
          return systemError("Observe process completion");
        }

        if (state == WAIT_OBJECT_0 && !outRead.valid() && !errRead.valid()) {
          DWORD code = 0;

          if (!GetExitCodeProcess(child.process.value, &code)) {
            return systemError("Read process exit code");
          }

          result.exitCode = code;
          return result;
        }

        if (Clock::now() >= deadline) {
          return fail(ErrorCode::Unavailable, "Process timeout while awaiting exit/output EOF");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{1});
      }
    }
#else
    struct Descriptor {
      int value{-1};

      Descriptor() = default;

      explicit Descriptor(int owned) : value(owned) {
        if (value >= 0) {
          liveHandles.fetch_add(1);
        }
      }

      Descriptor(const Descriptor&) = delete;
      auto operator=(const Descriptor&) -> Descriptor& = delete;

      Descriptor(Descriptor&& other) noexcept : value(std::exchange(other.value, -1)) {
      }

      auto operator=(Descriptor&& other) noexcept -> Descriptor& {
        if (this != &other) {
          close();
          value = std::exchange(other.value, -1);
        }

        return *this;
      }

      ~Descriptor() {
        close();
      }

      auto close() noexcept -> void {
        if (value >= 0) {
          ::close(value);
          liveHandles.fetch_sub(1);
        }

        value = -1;
      }
    };

    auto systemError(std::string_view operation, int code = errno) -> std::unexpected<Error> {
      return fail(ErrorCode::Io,
                  std::format("{}: {}", operation,
                              std::error_code{code, std::generic_category()}.message()));
    }

    struct Actions {
      posix_spawn_file_actions_t value{};
      bool ready{};

      ~Actions() {
        if (ready) {
          posix_spawn_file_actions_destroy(&value);
        }
      }
    };

    struct Attributes {
      posix_spawnattr_t value{};
      bool ready{};

      ~Attributes() {
        if (ready) {
          posix_spawnattr_destroy(&value);
        }
      }
    };

    struct Child {
      pid_t pid{-1};

      ~Child() {
        if (pid > 0) {
          /** Keep the leader waitable until group teardown, preventing PID reuse.
           * 组清理前保留可回收首进程，避免 PID 复用。 */
          kill(-pid, SIGKILL);
          int status = 0;

          while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
          }

          liveChildren.fetch_sub(1);
        }
      }
    };

    auto changeDirectory(posix_spawn_file_actions_t* actions, const char* path) -> int {
#  if defined(__APPLE__)
#    if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
      if (__builtin_available(macOS 26.0, *)) {
        return posix_spawn_file_actions_addchdir(actions, path);
      }
#    endif
      /** Older macOS keeps the compatible extension; SDK 26 deprecated its name.
       * 旧 macOS 使用兼容扩展，SDK 26 仅弃用其名称。 */
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wdeprecated-declarations"
      return posix_spawn_file_actions_addchdir_np(actions, path);
#    pragma clang diagnostic pop
#  else
      return posix_spawn_file_actions_addchdir_np(actions, path);
#  endif
    }

    auto execute(const ProcessConfig& config) -> Result<ProcessResult> {
      struct sigaction signalAction{};

      if (sigaction(SIGCHLD, nullptr, &signalAction) != 0) {
        return systemError("Inspect child signal disposition");
      }

      if (signalAction.sa_handler == SIG_IGN || (signalAction.sa_flags & SA_NOCLDWAIT) != 0) {
        return fail(ErrorCode::InvalidState, "Process execution requires waitable children");
      }

      Descriptor outRead, outWrite, errRead, errWrite;
      const auto pipe = [&](Descriptor& reader, Descriptor& writer) -> Result<void> {
        std::array<int, 2> descriptors{};
#  if defined(__linux__)
        const auto status = pipe2(descriptors.data(), O_CLOEXEC);
#  else
        const auto status = ::pipe(descriptors.data());
#  endif

        if (status != 0) {
          return systemError("Create process pipe");
        }

        reader = Descriptor{descriptors[0]};
        writer = Descriptor{descriptors[1]};

        for (auto* descriptor : {&reader, &writer}) {
          if (descriptor->value < 3) {
            const auto duplicate = fcntl(descriptor->value, F_DUPFD_CLOEXEC, 3);

            if (duplicate < 0) {
              return systemError("Normalize process pipe descriptor");
            }

            *descriptor = Descriptor{duplicate};
          }

          if (fcntl(descriptor->value, F_SETFD, FD_CLOEXEC) < 0) {
            return systemError("Set process close-on-exec");
          }
        }

        const auto flags = fcntl(reader.value, F_GETFL);

        if (flags < 0 || fcntl(reader.value, F_SETFL, flags | O_NONBLOCK) < 0) {
          return systemError("Set nonblocking process capture");
        }

        return {};
      };
      auto prepared = pipe(outRead, outWrite);

      if (!prepared) {
        return std::unexpected(prepared.error());
      }

      prepared = pipe(errRead, errWrite);

      if (!prepared) {
        return std::unexpected(prepared.error());
      }

      Actions actions;
      Attributes attributes;
      auto code = posix_spawn_file_actions_init(&actions.value);

      if (code != 0) {
        return systemError("Initialize process file actions", code);
      }

      actions.ready = true;
      code = posix_spawnattr_init(&attributes.value);

      if (code != 0) {
        return systemError("Initialize process attributes", code);
      }

      attributes.ready = true;
      const auto action = [&](int status) -> Result<void> {
        return status == 0 ? Result<void>{}
                           : Result<void>{systemError("Configure process spawn", status)};
      };
      auto changed = action(changeDirectory(&actions.value, config.directory.c_str()));

      if (!changed) {
        return std::unexpected(changed.error());
      }

      for (const auto status :
           {posix_spawn_file_actions_addopen(&actions.value, STDIN_FILENO, "/dev/null", O_RDONLY,
                                             0),
            posix_spawn_file_actions_adddup2(&actions.value, outWrite.value, STDOUT_FILENO),
            posix_spawn_file_actions_adddup2(&actions.value, errWrite.value, STDERR_FILENO)}) {
        changed = action(status);

        if (!changed) {
          return std::unexpected(changed.error());
        }
      }

#  if defined(__APPLE__)
      for (const auto descriptor : {outRead.value, outWrite.value, errRead.value, errWrite.value}) {
        changed = action(posix_spawn_file_actions_addclose(&actions.value, descriptor));

        if (!changed) {
          return std::unexpected(changed.error());
        }
      }
#  else
      changed = action(posix_spawn_file_actions_addclosefrom_np(&actions.value, 3));

      if (!changed) {
        return std::unexpected(changed.error());
      }
#  endif

      sigset_t mask;
      sigemptyset(&mask);
      changed = action(posix_spawnattr_setsigmask(&attributes.value, &mask));

      if (!changed) {
        return std::unexpected(changed.error());
      }

      sigset_t defaults;
      sigemptyset(&defaults);

      for (const auto signal : {SIGPIPE, SIGINT, SIGTERM, SIGHUP}) {
        sigaddset(&defaults, signal);
      }

      changed = action(posix_spawnattr_setsigdefault(&attributes.value, &defaults));

      if (!changed) {
        return std::unexpected(changed.error());
      }

      short flags = POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
#  if defined(__APPLE__)
      flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#  endif
      changed = action(posix_spawnattr_setflags(&attributes.value, flags));

      if (!changed) {
        return std::unexpected(changed.error());
      }

      changed = action(posix_spawnattr_setpgroup(&attributes.value, 0));

      if (!changed) {
        return std::unexpected(changed.error());
      }

      std::vector<std::string> arguments;
      arguments.reserve(config.arguments.size() + 1);
      arguments.push_back(config.executable.native());
      arguments.insert(arguments.end(), config.arguments.begin(), config.arguments.end());
      std::vector<char*> argv;
      argv.reserve(arguments.size() + 1);

      for (auto& argument : arguments) {
        argv.push_back(argument.data());
      }

      argv.push_back(nullptr);
      Child child;
      pid_t pid = -1;
#  if defined(__APPLE__)
      auto** environment = *_NSGetEnviron();
#  else
      auto** environment = environ;
#  endif
      code = posix_spawn(&pid, config.executable.c_str(), &actions.value, &attributes.value,
                         argv.data(), environment);

      if (code != 0) {
        return systemError("Launch process", code);
      }

      child.pid = pid;
      liveChildren.fetch_add(1);
      outWrite.close();
      errWrite.close();
      const auto deadline = Clock::now() + config.timeout;
      ProcessResult result{};
      const auto read = [&](Descriptor& descriptor, bool error) -> Result<void> {
        std::array<char, 8192> buffer{};

        for (int batch = 0; batch < 16 && descriptor.value >= 0; ++batch) {
          const auto count = ::read(descriptor.value, buffer.data(), buffer.size());

          if (count == 0) {
            descriptor.close();
            break;
          }

          if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              break;
            }

            if (errno == EINTR) {
              continue;
            }

            return systemError("Read process output");
          }

          auto added = append(result, error, buffer.data(), static_cast<std::size_t>(count),
                              config.outputBytes);

          if (!added) {
            return added;
          }
        }

        return {};
      };

      while (true) {
        if (config.stop.stop_requested()) {
          return fail(ErrorCode::Unavailable, "Process cancelled");
        }

        auto out = read(outRead, false);
        auto err = read(errRead, true);

        if (!out || !err) {
          return std::unexpected(!out ? out.error() : err.error());
        }

        siginfo_t information{};

        if (waitid(P_PID, static_cast<id_t>(pid), &information, WEXITED | WNOHANG | WNOWAIT) < 0) {
          if (errno == EINTR) {
            continue;
          }

          if (errno == ECHILD) {
            child.pid = -1;
            liveChildren.fetch_sub(1);
          }

          return systemError("Observe owned process completion");
        }

        if (information.si_pid == pid && information.si_code != CLD_EXITED) {
          return fail(ErrorCode::External,
                      std::format("Process terminated by signal {}", information.si_status));
        }

        if (information.si_pid == pid && outRead.value < 0 && errRead.value < 0) {
          result.exitCode = static_cast<std::uint32_t>(information.si_status);
          return result;
        }

        if (Clock::now() >= deadline) {
          return fail(ErrorCode::Unavailable, "Process timeout while awaiting exit/output EOF");
        }

        std::array<pollfd, 2> polls{{{outRead.value, POLLIN, 0}, {errRead.value, POLLIN, 0}}};
        const auto polled = poll(polls.data(), polls.size(), 2);

        if (polled < 0 && errno != EINTR) {
          return systemError("Poll process output");
        }
      }
    }
#endif
  }  // namespace

  auto runProcess(const ProcessConfig& config) -> Result<ProcessResult> {
    try {
      auto checked = validate(config);

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (config.stop.stop_requested()) {
        return fail(ErrorCode::Unavailable, "Process cancelled before launch");
      }

      auto result = execute(config);

      if (!result) {
        auto error = std::move(result.error());
        const auto name = config.executable.generic_u8string();
        error.message
            = std::format("Process '{}': {}", std::string{name.begin(), name.end()}, error.message);
        return std::unexpected(std::move(error));
      }

      return result;
    } catch (const std::exception& error) {
      return fail(ErrorCode::External, std::format("Run process: {}", error.what()));
    }
  }

  auto processResources() noexcept -> ProcessResources {
    return {liveHandles.load(), liveChildren.load()};
  }
}  // namespace Dot::Core

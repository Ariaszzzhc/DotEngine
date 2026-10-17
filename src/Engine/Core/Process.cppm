module;

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

export module Dot.Core.Process;

import Dot.Core.Result;

export namespace Dot::Core {
  struct ProcessConfig {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path directory;
    std::chrono::milliseconds timeout{10000};
    std::uint64_t outputBytes{1024 * 1024};
    std::stop_token stop{};
  };

  struct ProcessResult {
    std::uint32_t exitCode;
    std::string standardOutput;
    std::string standardError;
  };

  struct ProcessResources {
    std::uint64_t handles;
    std::uint64_t children;
  };

  /** Run an explicit absolute executable, with literal UTF-8 arguments and an
   * absolute working directory, without a shell or parent cwd/environment edits.
   * Stdin is EOF; stdout/stderr capture raw bytes under a combined 1..64 MiB limit.
   * At most 4096 arguments/1 MiB argument bytes and 1 ms..10 min timeout. Nonzero
   * normal exits retain their real status/output; launch/I/O/signal/timeout/limit
   * failures return contextual errors. No partial output is returned on failure.
   * A requested stop prevents launch or cancels an active group/job with an
   * Unavailable result; cancellation still reaps owned processes before return.
   * 停止请求阻止启动或取消活动组/作业，返回 Unavailable，仍须完成拥有进程的回收。
   * Group/job descendants are terminated and the direct child is reaped before
   * returning; POSIX orphaned grandchildren are reaped by the operating system. POSIX descendants
   * must not detach; callers must not reap this function's children or ignore SIGCHLD. Kernel
   * teardown is not a hard real-time guarantee. Calls are independent and may run concurrently.
   * 显式绝对可执行路径、字面 UTF-8 参数与绝对工作目录，不调用 shell，也不改变父进程
   * 工作目录/环境。标准输入为 EOF，合并限制 1 字节到 64 MiB 的原始输出；最多 4096 个
   * 参数/1 MiB 参数字节，超时 1 毫秒到 10 分钟。非零正常退出保留真实状态和输出，启动/
   * I/O/信号/超时/超限返回上下文错误且不返回部分输出。返回前终止组/作业后代并回收直接子进程，POSIX
   * 孤儿孙进程由系统回收；POSIX 后代不得脱离进程组，调用者不得回收本函数的子进程或忽略 SIGCHLD；
   * 内核清理不保证硬实时，可并发独立调用。 */
  [[nodiscard]] auto runProcess(const ProcessConfig& config) -> Result<ProcessResult>;
  /** Quiescent process-wide ownership counters, excluding child-internal handles.
   * 静止时观察进程内所有权计数，不包含子进程内部句柄。 */
  [[nodiscard]] auto processResources() noexcept -> ProcessResources;
}  // namespace Dot::Core

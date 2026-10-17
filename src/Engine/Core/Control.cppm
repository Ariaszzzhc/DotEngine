module;

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module Dot.Core.Control;

import Dot.Core.Files;
import Dot.Core.Result;

export namespace Dot::Core {
  using ControlHandler = std::function<Result<std::string>(std::string_view)>;

  /** Owned local product mailbox, without windows/devices. Each ID executes once
   * per session; identical submission is retryable, different content rejects.
   * Creator-thread polling processes at most sixteen sorted requests per call,
   * with 256 pending/4096 retained requests and 1 MiB per JSON object. Callers
   * serialize filesystem access. After dispatch, any response failure is terminal:
   * effects may have committed and must never be replayed. Explicit close reports
   * cleanup errors and retains ownership for retry; destruction is best effort.
   * 拥有本地产品邮箱，不打开窗口/设备；每 ID 每会话仅执行一次，相同内容可重试提交，
   * 不同内容拒绝。创建线程每次按序处理至多 16 项，限制待处理 256 项、保留 4096 项、
   * 每 JSON 对象 1 MiB。调用者串行文件访问；分发后响应失败即终止，副作用可能已提交，
   * 不可重放。显式关闭报告清理错误并保留所有权供重试，销毁尽力清理。 */
  class Control {
  public:
    [[nodiscard]] static auto create(const FileSystem& parent, std::string_view metadata = "{}")
        -> Result<Control>;
    Control(const Control&) = delete;
    auto operator=(const Control&) -> Control& = delete;
    Control(Control&&) noexcept;
    auto operator=(Control&&) noexcept -> Control&;
    ~Control();

    [[nodiscard]] auto files() const -> Result<FileSystem>;
    [[nodiscard]] auto failure() const -> Result<std::optional<Error>>;
    [[nodiscard]] auto process(const ControlHandler& handler,
                               const std::function<bool()>& keepRunning = {})
        -> Result<std::size_t>;
    [[nodiscard]] auto close() -> Result<void>;

  private:
    struct Impl;
    explicit Control(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };

  /** Queue an owned UTF-8 JSON object without executing it. Receipt path is not
   * success: inspect its response, or observe process exit for terminal shutdown.
   * ID is 1..64 ASCII letters/digits/_/-; device names/case aliases reject.
   * Metadata must advertise protocol 1/ready.
   * 排队拥有的 UTF-8 JSON 对象，不执行；返回响应路径不代表成功，须读实际响应，终止
   * 关闭则观察进程退出。ID 为 1..64 个 ASCII 字母/数字/_/-，拒绝设备名及大小写别名；
   * 元数据须为协议 1/ready。 */
  [[nodiscard]] auto submitControlRequest(const std::filesystem::path& control, std::string_view id,
                                          std::string_view request)
      -> Result<std::filesystem::path>;
}  // namespace Dot::Core

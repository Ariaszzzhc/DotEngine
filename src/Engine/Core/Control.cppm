module;

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Core.Control;

import Dot.Core.Files;
import Dot.Core.Result;

export namespace Dot::Core {
  using ControlHandler = std::function<Result<std::string>(std::string_view)>;

  struct ControlLimits {
    std::size_t responseBytes{64 * 1024 * 1024};
    std::size_t receiptBytes{256 * 1024 * 1024};
    std::size_t sessions{64};
  };

  struct ControlReceiptStatus {
    std::filesystem::path control;
    bool closed{};
    bool blocked{};
    std::size_t count{};
    std::size_t bytes{};
    std::size_t capacity{};
    std::optional<Error> fault;
    std::vector<std::string> uncertain;
    std::optional<std::vector<std::string>> unprocessed;
  };

  /** Owned local product mailbox, without windows/devices. Each ID executes once
   * per session; identical submission is retryable, different content rejects.
   * Creator-thread polling processes at most sixteen sorted requests per call,
   * with 256 pending/4096 retained requests and 1 MiB per JSON object. Callers
   * serialize filesystem access. Each scan indexes bounded request/response entries;
   * matching regular files identify retained replies without per-history path resolution.
   * Submission rejects malformed mailboxes and orphan responses for its chosen ID.
   * After dispatch, any response failure is terminal:
   * effects may have committed and must never be replayed. Explicit close reports
   * cleanup errors and retains ownership for retry; destruction is best effort.
   * Replies live separately under the parent's reserved .dot-stage-control cache and
   * survive endpoint retirement until explicit clear. At most 64 sessions, 4096 replies
   * per session; response limit 2 bytes..64 MiB, reply budget response limit + 6 MiB +
   * 4096 bytes..1 GiB. Full budgets pause dispatch before effects; consuming a reply
   * replaces its body with a tombstone, retaining once-only identity. No time-based eviction.
   * 拥有本地产品邮箱，不打开窗口/设备；每 ID 每会话仅执行一次，相同内容可重试提交，
   * 不同内容拒绝。创建线程每次按序处理至多 16 项，限制待处理 256 项、保留 4096 项、
   * 每 JSON 对象 1 MiB。调用者串行文件访问；扫描索引有界目录，用同名普通文件识别
   * 保留响应，无需逐历史项解析路径；提交拒绝损坏邮箱及孤立目标响应。
   * 分发后响应失败即终止，副作用可能已提交，
   * 不可重放。显式关闭报告清理错误并保留所有权供重试，销毁尽力清理。响应独立保留于
   * 父目录预留的 .dot-stage-control 缓存，端点退役后仍可读，须显式清理。至多 64 会话、
   * 每会话 4096 回执；响应限额 2 字节..64 MiB，回执预算为响应限额 + 6 MiB + 4096
   * 字节..1 GiB。容量不足在副作用前暂停分发；消费以小标记替换正文，仍保留仅执行一次
   * 的身份，不按时间淘汰结果。 */
  class Control {
  public:
    [[nodiscard]] static auto create(const FileSystem& parent, std::string_view metadata = "{}",
                                     const ControlLimits& limits = {}) -> Result<Control>;
    Control(const Control&) = delete;
    auto operator=(const Control&) -> Control& = delete;
    Control(Control&&) noexcept;
    auto operator=(Control&&) noexcept -> Control&;
    ~Control();

    [[nodiscard]] auto files() const -> Result<FileSystem>;
    [[nodiscard]] auto receipts() const -> Result<FileSystem>;
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
   * success: inspect its persistent response; shutdown acknowledgement is separate from exit.
   * ID is 1..64 ASCII letters/digits/_/-; device names/case aliases reject.
   * Metadata must advertise protocol 1/ready.
   * 排队拥有的 UTF-8 JSON 对象，不执行；返回响应路径不代表成功，须读实际响应，终止
   * 关闭确认与进程退出分别观察。ID 为 1..64 个 ASCII 字母/数字/_/-，拒绝设备名及大小写别名；
   * 元数据须为协议 1/ready。 */
  [[nodiscard]] auto submitControlRequest(const std::filesystem::path& control, std::string_view id,
                                          std::string_view request)
      -> Result<std::filesystem::path>;

  /** Read owned receipt status without requiring a live endpoint. A closed archive describes
   * control retirement, not successful application teardown. Local writers must serialize edits.
   * 不要求端点存活即可读取回执状态；已关闭归档表示控制退役，不表示应用退出成功，调用者串行文件修改。
   */
  [[nodiscard]] auto inspectControlReceipts(const std::filesystem::path& directory)
      -> Result<ControlReceiptStatus>;

  /** Consume an applied reply only after the caller has read/preserved it. Retained tombstones
   * prevent replay and explicitly report consumed:true on later reads/retries. Idempotent.
   * 调用者读取/保留结果后才消费；保留小标记防止重放，后续读取/重试明确返回
   * consumed:true，可重复确认。 */
  [[nodiscard]] auto acknowledgeControlResponse(const std::filesystem::path& response)
      -> Result<void>;

  /** Remove only a validated closed native receipt session whose live endpoint is absent.
   * Empty/absent owned leaves permit interrupted cleanup retries. Reject active, aliased,
   * malformed or nonregular trees; never clear arbitrary caller files.
   * 已空/消失的拥有目录允许清理中断后重试。仅清理已关闭、实时端点已消失的原生回执会话；
   * 拒绝活动、别名、损坏或特殊文件树，不清理任意用户目录。
   */
  [[nodiscard]] auto clearControlReceipts(const std::filesystem::path& directory) -> Result<void>;

  /** Shared status/ack-control/clear-control CLI result encoding; no application/window startup.
   * 共享 control-status/ack-control/clear-control 命令结果编码，不启动应用或窗口。 */
  [[nodiscard]] auto performControlReceiptAction(std::string_view command,
                                                 const std::filesystem::path& target)
      -> Result<std::string>;
}  // namespace Dot::Core

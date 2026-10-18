module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

export module Dot.Game.SaveQueue;

import Dot.Core.Result;
import Dot.Core.Files;
export import Dot.Game.Saves;

export namespace Dot::Game {
  enum class SaveOperation { List, Read, Write, Remove };
  enum class SaveJobState { Pending, Running, Succeeded, Failed, Cancelled };

  struct SaveQueueLimits {
    SaveLimits saves;
    std::size_t jobs{32};
    std::size_t bytes{4 * 1024 * 1024};
  };

  /** Validate queue/storage limits without filesystem access.
   * 不访问文件系统，校验作业队列与存储限额。 */
  [[nodiscard]] auto validateSaveQueueLimits(const SaveQueueLimits& limits) -> Core::Result<void>;

  struct SaveRequest {
    SaveOperation operation;
    std::string slot;
    std::optional<StateSnapshot> snapshot;
    bool overwrite{};
  };

  using SaveOutput = std::variant<std::monostate, std::vector<SaveSlot>, StateSnapshot, SaveSlot>;

  struct SaveJob {
    std::uint64_t id;
    SaveOperation operation;
    SaveJobState state;
    std::optional<Core::Error> error;
    SaveOutput output;
  };

  /** One owned FIFO disk worker. Submission captures owned snapshots and validates CPU inputs;
   * no creating-thread filesystem calls. Jobs include retained terminal receipts until take.
   * Reservation bounds cover payload strings, with separate bounded per-job/codec overhead;
   * bytes 1 KiB..64 MiB, jobs 1..256. A read reserves the configured state limit, list reserves
   * 4096 bounded slot names. IDs are process-unique and never reused. Status is memory-only;
   * take returns no payload while pending/running and consumes terminal receipts once. Cancel
   * only pending work: started disk mutations finish, never pretend cancellation rolled them back.
   * Finish rejects submissions, drains accepted work and joins; it may wait for disk completion.
   * Unconsumed failed writes/deletions make finish fail, preserving their receipts. Destruction
   * also drains/joins without detaching. Operations require the creator, except quiescent finish/
   * destruction may run elsewhere to support owner retirement. External processes must serialize
   * edits to the explicitly supplied save root. 单个拥有的 FIFO 磁盘工作线程；提交复制快照并作 CPU
   * 校验，创建线程不访问文件系统。
   * 作业含保留至提取的终态回执；预留约束覆盖负载字符串，作业/编解码开销另有界。
   * 字节限额 1 KiB..64 MiB、作业 1..256；读取预留状态限额、列表预留 4096 个有界槽名。
   * 身份在进程内唯一且不复用；状态查询仅访问内存，提取等待态不含负载，终态仅消费一次。
   * 仅可取消未开始作业，已开始的磁盘修改必须结束，不伪称取消已回滚。结束拒绝新提交、
   * 排空已接受作业并回收线程，可能等待磁盘；未消费的写入/删除失败使结束返回错误并保留
   * 回执。销毁同样排空/回收，不分离线程。一般调用限创建线程；静止结束/销毁可在其他线程，
   * 以支持所有者退役；外部
   * 进程须串行修改显式提供的存档目录。 */
  class SaveQueue {
  public:
    [[nodiscard]] static auto create(const Core::FileSystem& files,
                                     const SaveQueueLimits& limits = {}) -> Core::Result<SaveQueue>;
    SaveQueue(const SaveQueue&) = delete;
    auto operator=(const SaveQueue&) -> SaveQueue& = delete;
    SaveQueue(SaveQueue&&) noexcept;
    auto operator=(SaveQueue&&) noexcept -> SaveQueue&;
    ~SaveQueue();
    [[nodiscard]] auto submit(SaveRequest request) -> Core::Result<std::uint64_t>;
    [[nodiscard]] auto status(std::uint64_t id) const -> Core::Result<SaveJob>;
    [[nodiscard]] auto jobs() const -> Core::Result<std::vector<SaveJob>>;
    [[nodiscard]] auto take(std::uint64_t id) -> Core::Result<SaveJob>;
    [[nodiscard]] auto cancel(std::uint64_t id) -> Core::Result<void>;
    [[nodiscard]] auto finish() -> Core::Result<void>;

  private:
    struct Impl;
    explicit SaveQueue(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Game

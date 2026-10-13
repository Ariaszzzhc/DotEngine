module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Editor.Document;

import Dot.Core.Result;
import Dot.Project;
import Dot.World.Scene;

export namespace Dot::Editor {
  struct DocumentConfig {
    std::size_t historyEntries{128};
    std::uint64_t historyBytes{256 * 1024 * 1024};
  };

  struct DocumentStats {
    std::uint64_t revision;
    std::uint64_t scene;
    std::optional<World::EntityId> selected;
    std::optional<std::uint64_t> nextEntity;
    bool dirty;
    bool canUndo;
    bool canRedo;
    std::size_t historyEntries;
    std::uint64_t historyBytes;
  };

  struct EditReport {
    std::uint64_t revision;
    std::uint64_t scene;
    bool changed;
    std::vector<std::uint64_t> created;
  };

  /** Owns an authoring scene and bounded undo history, without runtime backends.
   * Operations require the creating thread. Moves preserve the stable scene
   * address; edits/undo/redo replace its scope and invalidate prior EntityIds.
   * Published entity values are never reused, including after undo/branching.
   * Copies of the project describe the same files; callers serialize saves.
   * 拥有编辑场景及有界撤销历史，不初始化运行后端；操作限创建线程。移动保留稳定场景
   * 地址，编辑/撤销/重做替换范围并使旧 EntityId 失效；已发布实体值在撤销/分支后也不
   * 复用。项目副本描述相同文件，调用者须串行保存。 */
  class Document {
  public:
    [[nodiscard]] static auto open(const Project::Project& project,
                                   const DocumentConfig& config = {}) -> Core::Result<Document>;
    Document(const Document&) = delete;
    auto operator=(const Document&) -> Document& = delete;
    Document(Document&&) noexcept;
    auto operator=(Document&&) noexcept -> Document&;
    ~Document();

    /** Borrowed read-only scene, valid until ownership is replaced or destroyed.
     * Moving the source preserves its borrows; move assignment invalidates the
     * destination's previous borrows. Re-read entity handles after changed edits/
     * undo/redo. 借用只读场景，有效至所有权被替换或销毁；移动源保留其借用，移动赋值
     * 使目标此前的借用失效。有效编辑/撤销/重做后须重新取得实体句柄。 */
    [[nodiscard]] auto scene() const -> Core::Result<const World::Scene*>;
    [[nodiscard]] auto stats() const -> Core::Result<DocumentStats>;
    [[nodiscard]] auto snapshot() const -> Core::Result<std::string>;
    /** Strict JSON array, 1..256 operations, <=1 MiB/depth 64. Prepare the final
     * scene before committing all changes, history and selection together.
     * Component/transform values use the native scene format. No-op batches
     * preserve revision/scope/history. Created reports contain surviving values.
     * History keeps 1..1024 entries under 1 byte..512 MiB; older entries are evicted
     * when needed. The saved baseline may retain one additional <=64 MiB snapshot.
     * 严格 JSON 数组，1..256 操作、至多 1 MiB/64 层；完整准备最终场景后，一起提交修改、
     * 历史及选择。组件/变换使用原生场景格式；无变化批次保留版本/范围/历史，仅报告仍存活
     * 的新实体值。历史限制 1..1024 条及 1 字节..512 MiB，必要时淘汰旧条目；保存基线可
     * 额外保留一个至多 64 MiB 快照。 */
    [[nodiscard]] auto apply(std::string_view operations, std::uint64_t revision)
        -> Core::Result<EditReport>;
    [[nodiscard]] auto select(std::optional<World::EntityId> entity, std::uint64_t revision)
        -> Core::Result<void>;
    [[nodiscard]] auto undo(std::uint64_t revision) -> Core::Result<void>;
    [[nodiscard]] auto redo(std::uint64_t revision) -> Core::Result<void>;
    /** Check project metadata and saved scene baseline before atomic replacement;
     * external changes fail as conflicts. Callers must still serialize filesystem
     * edits, since checking and replacement are not an OS compare-and-swap.
     * Failed saves retain dirty/revision/history. 原子替换前检查项目描述与已保存场景
     * 基线，外部修改作为冲突失败；检查及替换不是 OS 比较交换，仍须串行文件编辑。
     * 保存失败保留未保存状态、版本与历史。 */
    [[nodiscard]] auto save(std::uint64_t revision) -> Core::Result<void>;
    /** Shared automation/UI protocol: inspect, edit, select, undo, redo, save.
     * Mutations require a canonical decimal revision; stale requests fail before
     * effects. Responses own a full snapshot and created values. If response
     * allocation fails after commit, the revision may already have advanced.
     * 自动化/UI 共用协议：inspect/edit/select/undo/redo/save；修改须携带规范十进制
     * 版本，过期请求在副作用前失败。响应拥有完整快照及新实体值；提交后响应分配失败时，
     * 版本可能已推进。 */
    [[nodiscard]] auto dispatch(std::string_view request) -> Core::Result<std::string>;

  private:
    struct Impl;
    explicit Document(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Editor

module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
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
    std::vector<World::EntityId> selection;
    std::optional<std::uint64_t> nextEntity;
    bool dirty;
    bool canUndo;
    bool canRedo;
    std::size_t historyEntries;
    std::uint64_t historyBytes;
    std::string scenePath;
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
   * Published values are not reused after undo/branching; saves preserve the
   * allocation floor across reopen. History/dirty state track authored data;
   * snapshots and file saves include the current non-undoable floor.
   * Copies of the project describe the same files; callers serialize saves.
   * 拥有编辑场景及有界撤销历史，不初始化运行后端；操作限创建线程。移动保留稳定场景
   * 地址，编辑/撤销/重做替换范围并使旧 EntityId 失效；已发布实体值在撤销/分支后也不
   * 复用，保存后重新打开仍保留分配上界。历史及未保存标记仅跟踪编辑内容，快照/文件
   * 同时包含当前不可撤销的分配上界。项目副本描述相同文件，调用者须串行保存。 */
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
    /** Replace the active authoring scene only after loading/preparing it fully.
     * Paths are existing portable .dotscene files under scenes/assets, or the
     * configured startup file. Dirty scenes require explicit discard. Success
     * clears scene history/selection, preserves the Scene object's address and
     * advances revision/scope; previously published entity values stay reserved.
     * 完整加载准备后才替换当前编辑场景；路径为 scenes/assets 下已有的可移植 .dotscene
     * 或配置启动文件。未保存场景须显式丢弃；成功清空历史/选择、保留 Scene 对象地址并
     * 推进版本/范围，已发布实体值仍保留不复用。 */
    [[nodiscard]] auto openScene(std::string_view path, std::uint64_t revision,
                                 bool discard = false) -> Core::Result<void>;
    /** Publish a new empty scene in an existing scenes/assets directory, then
     * activate it as a clean document. Existing destinations are never replaced.
     * 在已有 scenes/assets 目录发布空场景，再作为干净文档启用，不替换已有目标。 */
    [[nodiscard]] auto createScene(std::string_view path, std::string_view name,
                                   std::uint64_t revision, bool discard = false)
        -> Core::Result<void>;
    /** Save current content at a new scenes/assets path and retarget subsequent
     * saves. Retains scope/selection/history; undo may become dirty against the new
     * baseline. Does not modify the old file or project startup setting.
     * 在 scenes/assets 新路径保存当前内容并切换后续保存目标；保留范围/选择/历史，撤销
     * 可相对新基线变脏，不修改旧文件或项目启动配置。 */
    [[nodiscard]] auto saveAs(std::string_view path, std::uint64_t revision) -> Core::Result<void>;
    /** Validate a complete existing-entity draft and return strict edit operations,
     * without changing scene, selection, revision or history. Primary camera/listener
     * transfers demote the previous owner in the same prepared transaction.
     * An unchanged draft returns []; otherwise apply the owned plan with the same revision.
     * 校验现有实体的完整草稿并返回严格编辑操作，不改变场景、选择、版本或历史。
     * 主相机/听者切换在同一预备事务内取消此前所有者的主标记。
     * 无变化返回 []；否则须使用同一版本应用独立持有的操作计划。 */
    [[nodiscard]] auto prepareEntityEdit(std::string_view entity, std::uint64_t revision) const
        -> Core::Result<std::string>;
    /** Strict JSON array, 1..256 operations, <=1 MiB/depth 64. Prepare the final
     * scene before committing all changes, history and selection together.
     * Component/transform values use the native scene format. No-op batches
     * preserve revision/scope/history. Created reports contain surviving values.
     * History (authored scene text plus selection ID bytes) keeps 1..1024 entries under
     * 1 byte..512 MiB; older entries are evicted
     * when needed. The saved baseline may retain one additional <=64 MiB snapshot.
     * 严格 JSON 数组，1..256 操作、至多 1 MiB/64 层；完整准备最终场景后，一起提交修改、
     * 历史及选择。组件/变换使用原生场景格式；无变化批次保留版本/范围/历史，仅报告仍存活
     * 的新实体值。历史限制 1..1024 条及 1 字节..512 MiB，必要时淘汰旧条目；保存基线可
     * 额外保留一个至多 64 MiB 快照。 */
    [[nodiscard]] auto apply(std::string_view operations, std::uint64_t revision)
        -> Core::Result<EditReport>;
    /** Typed atomic transform batch, at most 10000 current-scene handles.
     * Uses the same prepared scene/history commit as edit, without JSON operation
     * count/transport limits. Empty/no-op batches preserve revision and history.
     * 至多 10000 个当前场景句柄的类型化变换事务，共用 edit 的场景/历史提交，不受 JSON
     * 操作数量/传输限制；空批次和无变化批次保留版本及历史。 */
    [[nodiscard]] auto applyTransforms(std::span<const World::TransformUpdate> updates,
                                       std::uint64_t revision) -> Core::Result<EditReport>;
    [[nodiscard]] auto select(std::optional<World::EntityId> entity, std::uint64_t revision)
        -> Core::Result<void>;
    /** Ordered unique live handles, at most 10000; last is the active entity.
     * Empty clears selection. Selection advances revision only when changed,
     * never creates undo entries or changes scene dirty state. History includes
     * selection storage in its byte budget; rejection preserves all state.
     * 至多 10000 个有序且唯一的有效句柄，末项为主选；空集清空选择。仅变化时推进版本，
     * 不增加撤销条目或改变场景未保存状态，选择存储计入历史字节预算，失败保留全部状态。 */
    [[nodiscard]] auto selectMany(std::span<const World::EntityId> entities, std::uint64_t revision)
        -> Core::Result<void>;
    [[nodiscard]] auto undo(std::uint64_t revision) -> Core::Result<void>;
    [[nodiscard]] auto redo(std::uint64_t revision) -> Core::Result<void>;
    /** Check project metadata and saved scene baseline before atomic replacement;
     * external changes fail as conflicts. Callers must still serialize filesystem
     * edits, since checking and replacement are not an OS compare-and-swap.
     * Failed saves retain dirty/revision/history. 原子替换前检查项目描述与已保存场景
     * 基线，外部修改作为冲突失败；检查及替换不是 OS 比较交换，仍须串行文件编辑。
     * 保存失败保留未保存状态、版本与历史。 */
    /** Save selected subtree content to a new assets scene without changing the
     * active document, allocator, selection, revision or undo history. Existing
     * files/aliases are never replaced; native references must remain internal.
     * 将所选子树保存为新的 assets 场景，不改变当前文档、分配器、选择、版本及历史。
     * 不替换已有文件/别名，原生引用须留在内容内部。 */
    [[nodiscard]] auto saveSelectionAs(std::string_view path, std::string_view name,
                                       std::uint64_t revision) -> Core::Result<void>;
    [[nodiscard]] auto save(std::uint64_t revision) -> Core::Result<void>;
    /** Shared automation/UI protocol: inspect, edit, select/select-many,
     * duplicate-selection/delete-selection, group-selection/ungroup-selection,
     * save-selection-scene, undo, redo, save. Scene-content save publishes a new
     * assets file without changing document state. instantiate-scene edits select
     * a new placement root and remap native references through a prepared candidate.
     * Hierarchy edits preserve world poses when requested;
     * grouping uses minimal selected roots and ungrouping removes only
     * component-free containers. Selection/history/identity changes commit together.
     * Duplicate/delete operations expand descendant unions once. Copies retain external
     * parents/native components, remap internal parents, demote primary camera/
     * listener flags, and select mapped input IDs. Script properties stay opaque;
     * arbitrary numbers/strings are not interpreted as entity references.
     * 内容保存仅发布新资产文件，不改变文档；场景插入选择新的放置根并在候选中重映射原生引用。
     * 层级编辑可显式保持世界姿态；成组处理最小选择根，拆组仅删除无功能组件容器。
     * 选择、历史及标识变更共同提交。复制/删除对子树并集只处理一次；副本保留外部父级/
     * 原生组件，重映射内部父级，取消
     * 主相机/监听器标记并选中对应输入 ID。脚本属性保持不透明，不将任意值视为实体引用。
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

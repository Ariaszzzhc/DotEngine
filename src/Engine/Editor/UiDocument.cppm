module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

export module Dot.Editor.UiDocument;

export import Dot.UI.Layout;
import Dot.Core.Result;
import Dot.Project;

export namespace Dot::Editor {
  struct UiHistoryLimits {
    std::size_t states{128};
    std::size_t bytes{32 * 1024 * 1024};
  };

  struct UiResourceSnapshot {
    std::string path;
    UI::Layout value;
    std::uint64_t revision;
    bool dirty;
    bool canUndo;
    bool canRedo;
    std::size_t historyStates;
    std::size_t historyBytes;
  };

  /** Typed UI resource draft with bounded immutable history. Source bytes are
   * retained exactly for undo; semantic no-ops retain revision/redo. All calls
   * require the creating thread; mutations require the current resource revision.
   * Snapshots share owned immutable definitions and survive source/document retirement.
   * Save reuses resource metadata/path/conflict checks and atomic replacement;
   * saving retains history, destruction never saves. History counts serialized
   * bytes, in addition to shared UI data/current text/disk baseline. Oldest
   * states evict to fit; one state exceeding the budget fails without mutation.
   * 类型化 UI资源草稿与有界不可变历史。保留原文字节供精确撤销；语义无变化保留版本及
   * 重做。调用限创建线程，修改须匹配资源版本；快照共享拥有的不可变定义，可晚于源/文档
   * 销毁。保存复用资源路径/元数据/磁盘冲突与原子替换，不清历史，销毁不保存。历史统计
   * 序列化字节，另有共享 UI、当前文本和磁盘基线；淘汰最旧状态，单状态超预算时不修改。 */
  class UiDocument {
  public:
    [[nodiscard]] static auto open(const Project::Project& project, std::string_view path,
                                   UiHistoryLimits limits = {}) -> Core::Result<UiDocument>;
    UiDocument(const UiDocument&) = delete;
    auto operator=(const UiDocument&) -> UiDocument& = delete;
    UiDocument(UiDocument&&) noexcept;
    auto operator=(UiDocument&&) noexcept -> UiDocument&;
    ~UiDocument();
    [[nodiscard]] auto snapshot() const -> Core::Result<UiResourceSnapshot>;
    [[nodiscard]] auto replace(UI::Layout value, std::uint64_t revision) -> Core::Result<void>;
    /** Inspect a candidate history value without changing cursor/revision. Clients
     * may validate a live preview before committing undo/redo of the same revision.
     * 只读取得候选历史定义，不移动游标/版本；调用方可在同版本撤销/重做前验证实时预览。 */
    [[nodiscard]] auto historyValue(bool forward, std::uint64_t revision) const
        -> Core::Result<UI::Layout>;
    [[nodiscard]] auto undo(std::uint64_t revision) -> Core::Result<void>;
    [[nodiscard]] auto redo(std::uint64_t revision) -> Core::Result<void>;
    [[nodiscard]] auto save(std::uint64_t revision) -> Core::Result<void>;

  private:
    struct Impl;
    explicit UiDocument(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Editor

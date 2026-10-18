module;

#include <filesystem>

export module Dot.Core.StagedDirectory;

import Dot.Core.Files;
import Dot.Core.Result;

export namespace Dot::Core {
  /** Owns a private sibling staging directory. Destruction removes unpublished
   * contents; successful publication transfers ownership to the caller. Atomic
   * no-replace rename never overwrites an existing directory/file/symlink. No
   * crash-durability or hostile concurrent filesystem isolation is promised.
   * 拥有私有同级暂存目录，销毁清理未发布内容，成功发布后由调用者拥有。原子不替换
   * 重命名不覆盖已有目录/文件/链接，不保证崩溃持久性或恶意并发文件系统隔离。 */
  class StagedDirectory {
  public:
    [[nodiscard]] static auto create(const FileSystem& parent) -> Result<StagedDirectory>;
    StagedDirectory(const StagedDirectory&) = delete;
    auto operator=(const StagedDirectory&) -> StagedDirectory& = delete;
    StagedDirectory(StagedDirectory&&) noexcept;
    auto operator=(StagedDirectory&&) noexcept -> StagedDirectory&;
    ~StagedDirectory();
    /** Borrowing descriptor; staging owner must remain alive and unpublished.
     * 借用描述，暂存所有者须保持存活且未发布。 */
    [[nodiscard]] auto files() const -> Result<FileSystem>;
    /** Explicit cleanup reports errors and retains ownership for retry; removal
     * can be partial. Destruction remains best effort. Empty ownership is a no-op.
     * 显式清理报告错误并保留所有权以便重试，删除可能部分完成；销毁仍尽力清理，
     * 不再拥有目录时操作无变化。 */
    [[nodiscard]] auto discard() -> Result<void>;
    /** Destination is one relative filename in the same parent. Publication
     * errors retain staging for retry; a post-rename open failure explicitly
     * reports that publication already occurred, without deleting the destination.
     * 目标为同父目录内单个相对名称，发布错误保留暂存可重试；重命名后的打开失败明确
     * 报告已发布，不删除目标。 */
    [[nodiscard]] auto publish(const std::filesystem::path& name) -> Result<FileSystem>;
    /** Replace an existing regular directory and return ownership of its complete
     * previous tree as sibling staging. The caller serializes readers/writers.
     * Prepare the backup before mutation; publication failure restores the old
     * name. Failed rollback preserves old data at the reported backup path.
     * Success is two no-replace renames, not a cross-platform atomic exchange or
     * crash-durability guarantee. Both trees remain owned on pre-publication failure.
     * 替换现存普通目录，返回其完整旧树的同级暂存所有权，调用者须串行读写。
     * 修改前准备备份，发布失败恢复旧名称；回滚失败保留旧数据并报告备份路径。
     * 成功由两次不替换重命名组成，不保证跨平台原子交换或崩溃持久性；发布前失败保留两棵树。 */
    [[nodiscard]] auto replace(const std::filesystem::path& name) -> Result<StagedDirectory>;
    /** Same-filesystem destination parent may differ; retain the previous tree in the staging
     * parent, allowing private backups outside a published resource catalog.
     * 同文件系统的目标父目录可不同；旧树保留在暂存父目录，允许备份置于资源目录之外。 */
    [[nodiscard]] auto replace(const FileSystem& destinationParent,
                               const std::filesystem::path& name) -> Result<StagedDirectory>;

  private:
    StagedDirectory(FileSystem parent, std::filesystem::path staging) noexcept;
    auto clear() noexcept -> void;
    FileSystem m_parent;
    std::filesystem::path m_staging;
  };
}  // namespace Dot::Core

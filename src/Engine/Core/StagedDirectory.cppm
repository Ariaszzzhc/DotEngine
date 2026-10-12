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
    /** Destination is one relative filename in the same parent. Publication
     * errors retain staging for retry; a post-rename open failure explicitly
     * reports that publication already occurred, without deleting the destination.
     * 目标为同父目录内单个相对名称，发布错误保留暂存可重试；重命名后的打开失败明确
     * 报告已发布，不删除目标。 */
    [[nodiscard]] auto publish(const std::filesystem::path& name) -> Result<FileSystem>;

  private:
    StagedDirectory(FileSystem parent, std::filesystem::path staging) noexcept;
    auto clear() noexcept -> void;
    FileSystem m_parent;
    std::filesystem::path m_staging;
  };
}  // namespace Dot::Core

module;
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Core.Files;
import Dot.Core.Result;

export namespace Dot::Core {
  /**
   * Root-relative file access with canonical path validation. 相对根目录访问并校验规范化路径。
   * This is a project-path constraint, not isolation from concurrent hostile filesystem changes.
   * 这是项目路径约束，不能隔离恶意并发修改文件系统的进程。
   */
  class FileSystem {
  public:
    FileSystem(const FileSystem&);
    auto operator=(const FileSystem&) -> FileSystem&;
    /** Copies retain independent root descriptors; moving clears the source root.
     * 副本各自保留根路径描述，移动清空来源，后续访问明确失败。 */
    FileSystem(FileSystem&&) noexcept;
    auto operator=(FileSystem&&) noexcept -> FileSystem&;
    [[nodiscard]] static auto open(const std::filesystem::path& root) -> Result<FileSystem>;
    [[nodiscard]] auto root() const noexcept -> const std::filesystem::path&;
    [[nodiscard]] auto resolve(const std::filesystem::path& relative) const
        -> Result<std::filesystem::path>;
    [[nodiscard]] auto createDirectories(const std::filesystem::path& relative) const
        -> Result<void>;
    [[nodiscard]] auto readBytes(const std::filesystem::path& relative,
                                 std::size_t maxBytes = 64 * 1024 * 1024) const
        -> Result<std::vector<std::byte>>;
    [[nodiscard]] auto readText(const std::filesystem::path& relative,
                                std::size_t maxBytes = 64 * 1024 * 1024) const
        -> Result<std::string>;
    /**
     * Flush a sibling temporary file, then atomically replace the destination.
     * 刷新同目录临时文件，再原子替换目标；失败会清理临时文件。
     * Parent directories must already exist. File and directory crash durability is not guaranteed.
     * 父目录须已存在；不保证文件及目录在系统崩溃后的持久性。
     */
    [[nodiscard]] auto writeAtomic(const std::filesystem::path& relative,
                                   std::span<const std::byte> data) const -> Result<void>;
    [[nodiscard]] auto writeTextAtomic(const std::filesystem::path& relative,
                                       std::string_view text) const -> Result<void>;
    /** Flush a complete owned sibling, then publish with a native no-replace rename.
     * Concurrent creators of the same destination have one winner; existing files, directories
     * and final symlinks (including dangling links) are never replaced or followed. Resolve
     * the parent inside the root; it must exist. Conflict returns InvalidState. No fallback to
     * replacing rename or partially visible writes; unsupported filesystems fail explicitly.
     * This is not hostile parent-path isolation, project-wide alias locking or crash durability.
     * 刷新完整独立同级暂存后以原生不替换改名发布，同目标并发创建仅一个成功；不覆盖已有
     * 文件/目录/末级链接，也不跟随悬空末级链接。父目录须存在且解析于根内；冲突返回
     * InvalidState，不降级为覆盖或暴露部分写入。不支持的文件系统明确失败；不提供恶意
     * 父路径隔离、项目级别名锁或崩溃持久性保证。 */
    [[nodiscard]] auto createAtomic(const std::filesystem::path& relative,
                                    std::span<const std::byte> data) const -> Result<void>;
    [[nodiscard]] auto createTextAtomic(const std::filesystem::path& relative,
                                        std::string_view text) const -> Result<void>;

  private:
    explicit FileSystem(std::filesystem::path root);
    std::filesystem::path m_root;
  };
}  // namespace Dot::Core

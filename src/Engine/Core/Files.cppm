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

  private:
    explicit FileSystem(std::filesystem::path root);
    std::filesystem::path m_root;
  };
}  // namespace Dot::Core

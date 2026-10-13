module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Editor.Resources;

import Dot.Core.Result;
import Dot.Project;

export namespace Dot::Editor {
  struct ResourceLimits {
    std::size_t textBytes{1024 * 1024};
    std::size_t binaryBytes{64 * 1024 * 1024};
    std::size_t entries{8192};
    std::size_t depth{64};
  };

  struct ResourceEntry {
    std::string path;
    std::uint64_t bytes;
    bool directory;
    bool text;
    bool readOnly;
  };

  struct TextSnapshot {
    std::string path;
    std::string text;
    std::uint64_t revision;
    bool dirty;
    bool readOnly;
  };

  class TextDocument;

  /** Project-root resource authoring for assets and the configured script tree.
   * SDK files are read-only. Rejects symlinks/special files, non-portable paths
   * and ASCII case aliases. Calls require the creating thread; text documents
   * retain shared descriptors after store moves/destruction. Callers serialize
   * filesystem edits; checks/replacement are not an OS compare-and-swap and
   * do not isolate hostile races. No existing file is implicitly overwritten.
   * 项目根内 assets 与配置脚本目录的资源编辑，SDK 只读；拒绝链接/特殊文件、不可移植
   * 路径及 ASCII 大小写别名。调用限创建线程，文本文档在服务移动/销毁后仍持有共享描述。
   * 调用者串行文件修改；检查/替换不是 OS 比较交换，不隔离恶意竞争，不隐式覆盖已有文件。 */
  class ResourceStore {
  public:
    [[nodiscard]] static auto open(const Project::Project& project,
                                   const ResourceLimits& limits = {})
        -> Core::Result<ResourceStore>;
    ResourceStore(const ResourceStore&) = delete;
    auto operator=(const ResourceStore&) -> ResourceStore& = delete;
    ResourceStore(ResourceStore&&) noexcept;
    auto operator=(ResourceStore&&) noexcept -> ResourceStore&;
    ~ResourceStore();
    [[nodiscard]] auto list() const -> Core::Result<std::vector<ResourceEntry>>;
    [[nodiscard]] auto createDirectory(std::string_view path) -> Core::Result<void>;
    [[nodiscard]] auto createText(std::string_view path, std::string_view text)
        -> Core::Result<void>;
    /** Copy bounded bytes from an explicit regular source, preserving it. Text
     * destinations also validate UTF-8/no NUL; parent must already exist.
     * 从显式普通源文件复制有界字节并保留源，文本目标额外校验 UTF-8/无 NUL，父目录须存在。 */
    [[nodiscard]] auto importFile(const std::filesystem::path& source, std::string_view destination)
        -> Core::Result<void>;
    [[nodiscard]] auto openText(std::string_view path) const -> Core::Result<TextDocument>;

  private:
    struct Impl;
    explicit ResourceStore(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> m_impl;
    friend class TextDocument;
  };

  /** Owned UTF-8 draft and exact disk baseline. Revisions are local/nonzero;
   * replace/save require the current revision, failures retain the draft. Save
   * checks project metadata and disk bytes before atomic replacement; parents
   * must exist. No-op replace/save preserve revision, destruction never saves.
   * 拥有 UTF-8 草稿及精确磁盘基线，版本为本地非零值；修改/保存须匹配当前版本，失败
   * 保留草稿。保存校验项目描述及磁盘字节后原子替换，父目录须存在；无变化操作保留版本，
   * 销毁不保存。 */
  class TextDocument {
  public:
    TextDocument(const TextDocument&) = delete;
    auto operator=(const TextDocument&) -> TextDocument& = delete;
    TextDocument(TextDocument&&) noexcept;
    auto operator=(TextDocument&&) noexcept -> TextDocument&;
    ~TextDocument();
    [[nodiscard]] auto snapshot() const -> Core::Result<TextSnapshot>;
    [[nodiscard]] auto replace(std::string_view text, std::uint64_t revision) -> Core::Result<void>;
    [[nodiscard]] auto save(std::uint64_t revision) -> Core::Result<void>;

  private:
    struct Impl;
    explicit TextDocument(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
    friend class ResourceStore;
  };
}  // namespace Dot::Editor

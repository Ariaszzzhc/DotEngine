module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module Dot.Editor.ModelReimport;

import Dot.Core.Result;
import Dot.Project;

export namespace Dot::Editor {
  enum class ReimportState { Preparing, Review, Validating, Ready, Applied, Reverted, Failed };
  enum class ReimportChoice { Unresolved, Source, Project };

  struct ReimportChange {
    std::string path;
    std::size_t beforeBytes{};
    std::size_t afterBytes{};
    bool added{};
    ReimportChoice choice{ReimportChoice::Unresolved};
  };

  struct ReimportStatus {
    ReimportState state;
    std::uint64_t revision;
    std::string path;
    std::string error;
    std::size_t count{};
    std::vector<ReimportChange> changes;
    std::vector<std::string> warnings;
  };

  struct ReimportFile {
    bool text{};
    bool truncated{};
    std::string before;
    std::string after;
  };

  /** Reviewed, stable-layout model replacement. One owned CPU worker prepares a private project
   * snapshot and validates all its scenes; no World/device handles cross threads. Every changed
   * existing file needs an explicit source/project choice. Structural identity changes are
   * rejected; files absent from the new import are retained. Exact project catalog/bytes are
   * checked before apply/revert. Callers stop asset readers and serialize external writers during
   * publication. This is not a hostile-race or crash-atomic filesystem transaction. Applied reviews
   * own the old bundle until revert/destruction; destruction never reverts. Calls require the
   * creating thread; destruction cancels/joins the bounded worker. Limits: 8192 entries, depth 64,
   * 64 MiB/file, 512 MiB project snapshot and 512 MiB generated bundle.
   * 经审阅的稳定布局模型替换；单个 CPU 工作线程准备私有项目快照并校验全部场景，不跨线程
   * 传递 World/设备句柄。现存变更文件必须显式选源/项目；拒绝结构身份变化，保留新导入未生成
   * 的文件。应用/恢复前逐字节核对项目目录；调用者暂停资源读取并串行外部写入。不保证恶意
   * 竞争隔离或崩溃原子性；应用后拥有旧目录至恢复/销毁，销毁不自动恢复。调用限创建线程，
   * 销毁取消并回收有界工作线程。上限：8192 项、64 层、每文件 64 MiB、项目/生成各 512 MiB。 */
  class ModelReimport {
  public:
    [[nodiscard]] static auto begin(const Project::Project& project,
                                    const std::filesystem::path& source, std::string destination,
                                    std::optional<std::uint32_t> scene = {})
        -> Core::Result<ModelReimport>;
    ModelReimport(const ModelReimport&) = delete;
    auto operator=(const ModelReimport&) -> ModelReimport& = delete;
    ModelReimport(ModelReimport&&) noexcept;
    auto operator=(ModelReimport&&) noexcept -> ModelReimport&;
    ~ModelReimport();
    [[nodiscard]] auto status(std::size_t offset = 0, std::size_t limit = 64)
        -> Core::Result<ReimportStatus>;
    /** At most 16 KiB per side, UTF-8/no NUL text only; binary data reports sizes in status.
     * 每侧至多 16 KiB，仅展示 UTF-8/无 NUL 文本，二进制大小由状态返回。 */
    [[nodiscard]] auto file(std::string path, std::uint64_t revision) -> Core::Result<ReimportFile>;
    [[nodiscard]] auto choose(std::string path, ReimportChoice choice, std::uint64_t revision)
        -> Core::Result<void>;
    [[nodiscard]] auto validate(std::uint64_t revision) -> Core::Result<void>;
    [[nodiscard]] auto apply(std::uint64_t revision) -> Core::Result<void>;
    [[nodiscard]] auto revert(std::uint64_t revision) -> Core::Result<void>;

  private:
    struct Impl;
    explicit ModelReimport(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Editor

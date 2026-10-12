module;

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

export module Dot.Project;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.World.Scene;

export namespace Dot::Project {
  struct Description {
    std::string name{"Untitled"};
    std::string startScene{"scenes/main.dotscene"};
    std::string scriptsDirectory{"scripts"};
    std::uint32_t width{1280};
    std::uint32_t height{720};
    std::uint32_t fixedHz{60};
    bool hotReload{true};
    auto operator==(const Description&) const -> bool = default;
  };

  /** Strict version-1 JSON within 64 KiB. Names are 1..128 UTF-8 bytes; window
   * dimensions 1..8192 and fixed rate 1..240 Hz. Paths are portable canonical
   * relative UTF-8 without dot/traversal/reserved components. Parsing/encoding
   * validates metadata only and does not initialize subsystems.
   * 64 KiB 内严格版本 1 JSON；名称为 1..128 字节 UTF-8，窗口尺寸 1..8192，固定频率
   * 1..240 Hz。路径为可移植规范相对 UTF-8，不含点/回溯/保留段；解析/编码仅校验元数据，
   * 不初始化子系统。 */
  [[nodiscard]] auto validateDescription(const Description& description) -> Core::Result<void>;
  [[nodiscard]] auto encodeDescription(const Description& description) -> Core::Result<std::string>;
  [[nodiscard]] auto decodeDescription(std::string_view source) -> Core::Result<Description>;

  /** Rooted authoring descriptor. Open validates startup scene and required SDK
   * files/directories with no symlink components. Scene loads return independent
   * scopes. Copies retain descriptor snapshots, not file ownership; callers must
   * serialize authoring edits. No backend/compiler/window is started implicitly.
   * 根目录编辑描述，打开校验启动场景与 SDK 文件/目录，并禁止符号链接段；加载场景生成
   * 独立范围。副本保留描述快照但不拥有文件，调用者须串行化编辑；不隐式启动后端/编译器/
   * 窗口。 */
  class Project {
  public:
    [[nodiscard]] static auto open(const std::filesystem::path& root) -> Core::Result<Project>;
    /** Prepare complete SDK, attached Mover script and native camera/cube scene,
     * then exclusively publish one directory under an existing parent. Failure
     * before publication leaves the destination untouched and removes staging.
     * 描述、SDK、附着 Mover 脚本及原生相机/立方体场景全部准备后，在已有父目录下不替换
     * 发布单个目录；发布前失败不改变目标并清理暂存。 */
    [[nodiscard]] static auto create(const Core::FileSystem& parent,
                                     const std::filesystem::path& folder,
                                     const Description& description, const Core::FileSystem& sdk)
        -> Core::Result<Project>;
    Project(const Project&);
    auto operator=(const Project&) -> Project&;
    Project(Project&&) noexcept;
    auto operator=(Project&&) noexcept -> Project&;
    ~Project();

    [[nodiscard]] auto description() const -> Core::Result<Description>;
    [[nodiscard]] auto files() const -> Core::Result<Core::FileSystem>;
    [[nodiscard]] auto loadStartup() const -> Core::Result<World::Scene>;
    /** Validate resources then atomically replace project.dotproject. Memory
     * changes only after file success; failed edits preserve the old descriptor.
     * 校验资源后原子替换 project.dotproject，仅文件成功后修改内存，失败保留旧描述。 */
    [[nodiscard]] auto save(const Description& description) -> Core::Result<void>;
    /** Atomically save the current startup scene; parent directories must exist.
     * 原子保存当前启动场景，父目录须存在。 */
    [[nodiscard]] auto saveStartup(const World::Scene& scene) const -> Core::Result<void>;

  private:
    Project(Core::FileSystem files, Description description) noexcept;
    Core::FileSystem m_files;
    Description m_description;
  };
}  // namespace Dot::Project

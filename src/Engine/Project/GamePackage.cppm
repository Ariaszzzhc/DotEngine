module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

export module Dot.Project.GamePackage;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Project;
import Dot.World.Scene;
import Dot.Script.Compiler;

export namespace Dot::Project {
  struct PackageLimits {
    std::size_t entries{8192};
    std::uint64_t fileBytes{64 * 1024 * 1024};
    std::uint64_t totalBytes{512 * 1024 * 1024};
    std::size_t depth{64};
  };

  /** Published scene/resource/ESM data package, independent of authoring paths
   * and compiler tools. Open validates and captures content into an owned private
   * snapshot; copies retain that snapshot, borrowed files require an owner alive.
   * Caller serializes input changes. Format validation is not authenticity,
   * hostile filesystem-race isolation or crash durability.
   * 发布场景/资源/ESM 数据包，不依赖编辑路径或编译工具；打开校验并捕获拥有的私有快照，
   * 副本保留快照，借用文件须持有所有者。输入变化由调用者串行化；格式校验不保证真实性、
   * 恶意文件竞争隔离或崩溃持久性。 */
  class GamePackage {
  public:
    /** Compile source and stage validated current scene plus bounded regular
     * resources, then publish without replacing an existing directory. No source
     * files/maps/control directories are delivered, no authoring scene is saved.
     * 编译源码，暂存已校验的当前场景和有界普通资源后不替换发布；不交付源码/映射/控制
     * 目录，也不保存编辑场景。 This publishes data; executable bundling is separate.
     * 此接口发布数据，可执行文件打包是独立步骤。 */
    [[nodiscard]] static auto publish(const Project& project, const World::Scene& scene,
                                      const Script::TypeScriptCompiler& compiler,
                                      const Core::FileSystem& parent,
                                      const std::filesystem::path& folder,
                                      const PackageLimits& limits = {})
        -> Core::Result<Core::FileSystem>;
    [[nodiscard]] static auto open(const Core::FileSystem& package,
                                   const PackageLimits& limits = {}) -> Core::Result<GamePackage>;
    GamePackage(const GamePackage&) noexcept;
    auto operator=(const GamePackage&) noexcept -> GamePackage&;
    GamePackage(GamePackage&&) noexcept;
    auto operator=(GamePackage&&) noexcept -> GamePackage&;
    ~GamePackage();
    [[nodiscard]] auto description() const -> Core::Result<Description>;
    [[nodiscard]] auto files() const -> Core::Result<Core::FileSystem>;
    [[nodiscard]] auto scene() const -> Core::Result<World::Scene>;
    [[nodiscard]] auto program() const -> Core::Result<Script::CompiledProgram>;

  private:
    struct Impl;
    explicit GamePackage(std::shared_ptr<const Impl> impl) noexcept;
    std::shared_ptr<const Impl> m_impl;
  };
}  // namespace Dot::Project

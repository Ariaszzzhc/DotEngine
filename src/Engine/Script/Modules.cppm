module;

#include <span>
#include <string>
#include <string_view>

export module Dot.Script.Modules;

import Dot.Core.Result;
import Dot.Core.Files;

export namespace Dot::Script {
  /** Engine-owned immutable ESM registration. Empty source denotes native exports;
   * declarations are complete ambient modules injected into compiler generations.
   * 引擎拥有的不可变 ESM 注册；空源码表示原生导出，声明为注入编译代的完整环境模块。 */
  struct EngineModule {
    std::string_view name;
    std::string_view declarations;
    std::string_view source;
  };

  [[nodiscard]] auto engineModules() noexcept -> std::span<const EngineModule>;
  /** Only exact registered dot: names exist; unknown names never fall back to disk.
   * 仅精确注册的 dot: 名称存在，未知名称绝不回退到磁盘加载。 */
  [[nodiscard]] auto engineModule(std::string_view name) -> Core::Result<EngineModule>;
  /** Resolve a project import without executing it. Relative names must have no
   * filename extension and resolve to exactly one regular .js/.mjs output file.
   * No index/directory/package guessing, symlinks, traversal outside root or case
   * aliases. Entry paths are a separate compiled-file API.
   * 不执行代码地解析项目导入；相对名称不含扩展名，须精确对应一个普通 .js/.mjs 输出文件；
   * 禁止目录/index/包猜测、链接、根外回溯和大小写别名；入口路径是独立的编译文件 API。 */
  [[nodiscard]] auto resolveImport(const Core::FileSystem& files, std::string_view base,
                                   std::string_view request) -> Core::Result<std::string>;
}  // namespace Dot::Script

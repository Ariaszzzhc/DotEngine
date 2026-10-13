module;

#include <filesystem>
#include <string_view>

export module Dot.Project.Templates;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Project;
import Dot.Script.Compiler;

export namespace Dot::Project {
  /** Copy a bounded template to one new portable folder with the current SDK.
   * Validate the native project, compilation, static resources and script setup
   * before exclusive publication; authoring sources/destinations stay untouched
   * on preparation failures. Source changes must be serialized. Bounds: 8192
   * entries, 64 levels, 64 MiB/file and 512 MiB total, excluding replaced SDK.
   * 将有界模板及当前 SDK 复制到新的可移植目录；独占发布前校验原生项目、编译、静态
   * 资源与脚本准备，准备失败不改变源或目标。源变化须串行化；限额为 8192 条目、64 层、
   * 单个 64 MiB 和总共 512 MiB，不计替换 SDK。 Does not execute gameplay hooks.
   * 不执行玩法生命周期钩子。 */
  [[nodiscard]] auto instantiateTemplate(const Core::FileSystem& source,
                                         const Core::FileSystem& parent,
                                         const std::filesystem::path& folder, std::string_view name,
                                         const Core::FileSystem& sdk,
                                         const Script::TypeScriptCompiler& compiler)
      -> Core::Result<Project>;
}  // namespace Dot::Project

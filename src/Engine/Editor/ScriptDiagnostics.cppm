module;

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Editor.ScriptDiagnostics;

import Dot.Core.Result;

export namespace Dot::Editor {
  struct ScriptDiagnostic {
    std::string path;
    std::uint32_t line;
    std::uint32_t column;
    std::string message;
  };

  /** Map pinned compiler src/file.ts(line,column) diagnostics to project sources.
   * Only portable source paths become actions; other diagnostics remain raw text.
   * Input is UTF-8/no NUL within 2 MiB, at most 4096 entries. No filesystem access.
   * 将固定编译器 src/file.ts(行,列) 诊断映射回项目源码，仅可移植源码路径变成操作；
   * 其他诊断保留原文本。输入为无 NUL UTF-8、至多 2 MiB/4096 项，不访问文件系统。 */
  [[nodiscard]] auto scriptDiagnostics(std::string_view text, std::string_view scriptsDirectory)
      -> Core::Result<std::vector<ScriptDiagnostic>>;
}  // namespace Dot::Editor

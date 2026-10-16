module;

#include <string_view>

export module Dot.Core.Paths;

export namespace Dot::Core {
  /** Lexical UTF-8 root-relative path rules shared by project authoring.
   * Slash separators, 1..4096 bytes, components <=240 bytes; no traversal,
   * controls, Win32 device names (including superscript digits), reserved
   * characters or trailing dots/spaces. Filesystem availability is separate.
   * 项目编辑共用的 UTF-8 相对路径词法规则：斜杠分隔、1..4096 字节、每段至多 240
   * 字节；禁止回溯、控制字符、Win32 设备名（含上标数字）、保留字符及结尾点/空格。
   * 文件系统实际可用性须另行检查。 */
  [[nodiscard]] auto isPortableRelativePath(std::string_view path) noexcept -> bool;

  /** Stable application identity: 1..64 lowercase ASCII letters/digits/_/-,
   * excluding portable reserved names. Independent of display name and location.
   * 稳定应用身份：1..64 个小写 ASCII 字母、数字、下划线或连字符，排除可移植保留名称；
   * 独立于显示名称与位置。 */
  [[nodiscard]] auto isApplicationId(std::string_view value) noexcept -> bool;
}  // namespace Dot::Core

module;

#include <filesystem>
#include <string>
#include <string_view>

export module Dot.UI.Files;

export import Dot.UI.Layout;
import Dot.Core.Files;
import Dot.Core.Result;

export namespace Dot::UI {
  /** Strict <=1 MiB/depth-12 native UI JSON; persists defaults, never live overrides.
   * Structural forms may contain incomplete references; Apply requires Layout::create.
   * Files require portable assets/ paths with .dotui suffix, existing parent directories
   * and regular components without symlinks. Save atomically replaces a validated file.
   * 严格 <=1 MiB/12 层原生 UI JSON，保存默认定义而非实例覆盖。结构化表单可暂缺引用，
   * 应用须经 Layout::create。资源须为 assets/ 下 .dotui 可移植路径，父目录存在，各组件
   * 为普通文件/目录且无符号链接；保存以原子方式替换已验证文件。 */
  [[nodiscard]] auto decodeLayoutForm(std::string_view source) -> Core::Result<Description>;
  [[nodiscard]] auto decodeLayout(std::string_view source) -> Core::Result<Layout>;
  [[nodiscard]] auto encodeLayout(const Layout& layout) -> Core::Result<std::string>;
  [[nodiscard]] auto loadLayout(const Core::FileSystem& files, const std::filesystem::path& path)
      -> Core::Result<Layout>;
  [[nodiscard]] auto saveLayout(const Core::FileSystem& files, const std::filesystem::path& path,
                                const Layout& layout) -> Core::Result<void>;
}  // namespace Dot::UI

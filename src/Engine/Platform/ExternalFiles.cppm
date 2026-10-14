module;

#include <filesystem>
#include <string>

export module Dot.Platform.ExternalFiles;

import Dot.Core.Result;

export namespace Dot::Platform {
  /** Encode an absolute UTF-8 filesystem path as a file URI without accessing it.
   * Reserved/non-ASCII bytes are escaped; no command or shell text is generated.
   * 将绝对 UTF-8 文件路径编码为 file URI，不访问文件；转义保留/非 ASCII 字节，
   * 不生成命令或 shell 文本。 */
  [[nodiscard]] auto fileUri(const std::filesystem::path& path) -> Core::Result<std::string>;
  /** Ask the system default application to open an existing regular file on the
   * main thread. Success means the OS launch request was accepted, not that the
   * external application loaded it; ownership never transfers to the engine.
   * 主线程请求系统默认应用打开现存普通文件；成功仅表示启动请求被接受，不保证外部
   * 应用已加载，不将外部应用所有权移交引擎。 */
  [[nodiscard]] auto openExternalFile(const std::filesystem::path& path) -> Core::Result<void>;
}  // namespace Dot::Platform

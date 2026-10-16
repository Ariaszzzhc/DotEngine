module;

#include <string_view>

export module Dot.Platform.UserData;

import Dot.Core.Result;
import Dot.Core.Files;

export namespace Dot::Platform {
  /** Resolve/create SDL's user-and-application-specific writable directory.
   * Validates the stable identity before filesystem effects. Requires no SDL
   * video/audio initialization; failures never fall back to installation/project.
   * 解析并创建 SDL 的用户/应用专属可写目录；文件副作用前校验稳定身份。不要求初始化
   * SDL 视频/音频，失败不回退至安装或项目目录。 */
  [[nodiscard]] auto userDataDirectory(std::string_view applicationId)
      -> Core::Result<Core::FileSystem>;
}  // namespace Dot::Platform

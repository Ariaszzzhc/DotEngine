module;

#include <filesystem>
#include <string>
#include <string_view>

export module Dot.World.SceneFiles;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.World.Scene;

export namespace Dot::World {
  /** Versioned strict JSON, at most 64 MiB and 10000 entities. IDs are decimal
   * strings to preserve uint64 precision across scripting/automation tools.
   * Decoding constructs an independent scene; failures do not mutate a caller's
   * live scene. Loaded values persist, while scene-scoped handles are fresh.
   * 带版本的严格 JSON，最多 64 MiB 和 10000 个实体；ID 用十进制字符串保留 uint64 精度。
   * 解码构造独立场景，失败不更改调用者的现有场景；保留实体值但生成新的场景范围句柄。
   */
  [[nodiscard]] auto encodeScene(const Scene& scene) -> Core::Result<std::string>;
  [[nodiscard]] auto decodeScene(std::string_view source) -> Core::Result<Scene>;
  [[nodiscard]] auto loadScene(const Core::FileSystem& files, const std::filesystem::path& relative)
      -> Core::Result<Scene>;
  /** Atomic .dotscene replacement; parent directories must exist.
   * 原子替换 .dotscene 文件，父目录须存在。 */
  [[nodiscard]] auto saveScene(const Core::FileSystem& files, const std::filesystem::path& relative,
                               const Scene& scene) -> Core::Result<void>;
}  // namespace Dot::World

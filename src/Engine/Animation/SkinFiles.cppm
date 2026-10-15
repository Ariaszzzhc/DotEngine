module;

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

export module Dot.Animation.SkinFiles;

export import Dot.Animation.Skin;
import Dot.Core.Result;
import Dot.Core.Files;

export namespace Dot::Animation {
  /** Single little-endian .dotskin layout: eight magic bytes DOTSKIN\0, uint32
   * joint/vertex counts, column-major float32 inverse binds, then eight uint32
   * joints/eight float32 weights per vertex. Exact length <=64 MiB; no padding,
   * native struct dumps or version dispatch. Validate before publication.
   * 单一小端 .dotskin：八字节 DOTSKIN\0、uint32 关节/顶点数、列主序 float32 逆绑定，
   * 每顶点八 uint32 索引及八 float32 权重。严格长度、至多 64 MiB，无填充/结构体转储/
   * 版本分支；发布前完成校验。 */
  [[nodiscard]] auto decodeSkin(std::span<const std::byte> bytes) -> Core::Result<Skin>;
  [[nodiscard]] auto encodeSkin(const Skin& skin) -> Core::Result<std::vector<std::byte>>;
  /** Portable rooted regular paths only; reject symlinks/special files. Save
   * encodes fully before atomic replacement and requires existing parents.
   * 仅可移植根内普通路径，拒绝链接/特殊文件；完整编码后原子替换，要求父目录存在。 */
  [[nodiscard]] auto loadSkin(const Core::FileSystem& files, const std::filesystem::path& path)
      -> Core::Result<Skin>;
  [[nodiscard]] auto saveSkin(const Core::FileSystem& files, const std::filesystem::path& path,
                              const Skin& skin) -> Core::Result<void>;
}  // namespace Dot::Animation

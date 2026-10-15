module;

#include <filesystem>
#include <string>
#include <string_view>

export module Dot.Animation.Files;

export import Dot.Animation.Clip;
import Dot.Core.Result;
import Dot.Core.Files;

export namespace Dot::Animation {
  /** Single strict .dotanimation JSON structure, <=64 MiB/depth 64. Unknown,
   * duplicate/missing fields and invalid channels fail before publication.
   * Loading reads only the clip; portable project-relative regular paths reject
   * symlinks/special files. Save atomically replaces only after full encoding;
   * existing parents are required.
   * 单一严格 .dotanimation JSON，至多 64 MiB/64 层；未知、重复、缺字段及无效通道失败。
   * 仅加载片段，可移植项目相对普通文件路径拒绝链接/特殊文件；完整编码后才原子替换，
   * 要求父目录存在。 */
  [[nodiscard]] auto decodeClip(std::string_view source) -> Core::Result<Clip>;
  [[nodiscard]] auto encodeClip(const Clip& clip) -> Core::Result<std::string>;
  [[nodiscard]] auto loadClip(const Core::FileSystem& files, const std::filesystem::path& path)
      -> Core::Result<Clip>;
  [[nodiscard]] auto saveClip(const Core::FileSystem& files, const std::filesystem::path& path,
                              const Clip& clip) -> Core::Result<void>;
}  // namespace Dot::Animation

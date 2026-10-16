module;

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

export module Dot.Assets.ImageExport;

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Graphics.Image;

export namespace Dot::Assets {
  struct ImageExportReport {
    std::string path;
    std::uint64_t bytes{};
  };

  /** Prepared single-file PNG export under an explicit existing directory.
   * A portable filename (one component, .png case-insensitive) is required.
   * Reject links/special files, ASCII case aliases and read-only destinations.
   * Existing content is bounded to 64 MiB and replaced only with explicit consent
   * and an unchanged byte baseline. Directory scans are bounded to 65536 entries.
   * Calls require the creating thread; callers serialize filesystem edits. Checks
   * are not an OS compare-and-swap or protection against hostile races/crashes.
   * 在明确的现有目录中预备单文件 PNG 导出，要求可移植的单段文件名及 .png 后缀（不分大小写）。
   * 拒绝链接、特殊文件、ASCII 大小写别名与只读目标；已有内容最多 64 MiB，仅在明确允许且
   * 字节基线未变时替换。目录扫描至多 65536 项。调用限创建线程，调用方串行文件修改；
   * 校验不构成系统比较交换，也不隔离恶意竞争或保证崩溃持久性。 */
  class ImageExport {
  public:
    [[nodiscard]] static auto prepare(const Core::FileSystem& parent, std::string_view filename,
                                      bool overwrite) -> Core::Result<ImageExport>;
    ImageExport(const ImageExport&) = delete;
    auto operator=(const ImageExport&) -> ImageExport& = delete;
    ImageExport(ImageExport&&) noexcept;
    auto operator=(ImageExport&&) noexcept -> ImageExport&;
    ~ImageExport();
    /** Encode the owned frame's display RGBA bytes without color conversion.
     * Validation/encoding failures preserve the destination and plan; success is
     * single-use and returns an owned absolute path after atomic replacement.
     * 对画面的显示 RGBA 字节编码，不转换颜色。校验/编码失败保留目标与计划；
     * 成功通过原子替换提交，仅可使用一次，并返回独立的绝对路径。 */
    [[nodiscard]] auto write(const Graphics::Image& image) -> Core::Result<ImageExportReport>;

  private:
    struct Impl;
    explicit ImageExport(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Assets

module;

#include <cstdint>
#include <memory>
#include <optional>
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
    /** Encode one owned RGBA frame on a worker; no destination write until poll.
     * Input is bounded to 16,777,216 pixels. Single asynchronous attempt per plan;
     * poll returns empty while encoding, then commits with the same baseline guard.
     * Repeated completed polls return the same report/error without another write.
     * 在工作线程编码一张拥有的 RGBA 图像，poll 前不写目标；最多 16,777,216 像素。
     * 每计划仅一次异步尝试；编码中轮询为空，完成后用相同基线保护提交；
     * 重复完成轮询返回同一结果/错误，不重复写入。 */
    [[nodiscard]] auto begin(Graphics::Image image) -> Core::Result<void>;
    [[nodiscard]] auto poll() -> Core::Result<std::optional<ImageExportReport>>;
    /** Cancel publication without joining a codec. Poll until terminal before
     * releasing on an interactive thread; destruction joins any owned worker.
     * Worker touches no filesystem or borrowed pixels. Completed commits cannot
     * be cancelled. Owner-thread calls only, including after moving the plan.
     * 取消发布但不等待编解码；交互线程须轮询至结束再释放，析构会等待拥有的工作线程。
     * 工作线程不访问文件系统或借用像素；已提交文件不可取消，移动后仍仅限原所属线程。 */
    [[nodiscard]] auto cancel() -> Core::Result<void>;

  private:
    struct Impl;
    explicit ImageExport(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Assets

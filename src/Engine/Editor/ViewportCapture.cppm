module;

#include <cstdint>
#include <optional>

export module Dot.Editor.ViewportCapture;

import Dot.Core.Result;
import Dot.Graphics.Image;
import Dot.Math;

export namespace Dot::Editor {
  enum class ViewportMode { Editor, Game, Play };

  enum class CaptureState { Empty, Queued, Ready, Saved, Cancelled, Failed };

  struct CaptureContext {
    std::uint64_t revision{};
    std::uint64_t scene{};
    std::uint64_t session{};
    ViewportMode mode{ViewportMode::Editor};
    bool transform{};
    bool animation{};
    std::uint64_t hudRevision{};
    /** Play-only presentation identity; hidden UI contributes no HUD revision.
     * Epoch rejects an old visible frame after a hide/show cycle.
     * Play 展示身份；隐藏 UI 不贡献 HUD 修订，递增修订拒绝隐藏/重显后的旧可见帧。 */
    bool gameUiVisible{true};
    std::uint64_t gameUiRevision{};
    auto operator==(const CaptureContext&) const -> bool = default;
  };

  struct AnimationCapture {
    std::uint64_t owner{};
    std::uint64_t generation{};
    std::uint32_t clip{};
    double time{};
    bool bindPose{};
  };

  struct CaptureFrame {
    CaptureContext context{};
    std::uint64_t serial{};
    std::uint64_t playFrame{};
    std::uint32_t width{};
    std::uint32_t height{};
    Math::Mat4 viewProjection{};
    bool materialPreview{};
    std::optional<AnimationCapture> animation{};
  };

  struct ViewportCaptureStats {
    std::uint64_t id{};
    CaptureState state{CaptureState::Empty};
    CaptureFrame frame;
    std::optional<Core::Error> error;
  };

  /** One owned scene image, independent of subsequent rendering or native dialogs.
   * The frontend serializes calls and supplies actual completed frame provenance.
   * Completion requires a newer frame in the requested scene/revision/Play session;
   * failures/cancellation release pixels. No rendering, device or filesystem work.
   * 拥有一张场景图像，不受后续渲染或原生对话框影响。前端串行调用并提供实际完成帧的
   * 来源；完成要求更新的同场景/修订/Play 会话帧。失败/取消释放像素，无设备或文件操作。 */
  class ViewportCapture {
  public:
    ViewportCapture();
    ViewportCapture(const ViewportCapture&) = delete;
    auto operator=(const ViewportCapture&) -> ViewportCapture& = delete;
    [[nodiscard]] auto request(CaptureContext context, std::uint64_t after)
        -> Core::Result<std::uint64_t>;
    /** Invalid completion leaves the queued request available for explicit failure.
     * 无效完成保留队列请求，供前端明确报告失败。 */
    [[nodiscard]] auto complete(std::uint64_t id, CaptureFrame frame, Graphics::Image image)
        -> Core::Result<void>;
    [[nodiscard]] auto fail(std::uint64_t id, Core::Error error) -> Core::Result<void>;
    [[nodiscard]] auto discard(std::uint64_t id) -> Core::Result<void>;
    /** Borrow valid until saved/discarded/destruction. Save failures must retain
     * Ready and pixels for retry; acknowledge only after a successful file commit.
     * 借用有效至已保存/丢弃/销毁；保存失败须保留 Ready 与像素，提交文件后才可确认。 */
    [[nodiscard]] auto image(std::uint64_t id) const -> Core::Result<const Graphics::Image*>;
    [[nodiscard]] auto saved(std::uint64_t id) -> Core::Result<void>;
    [[nodiscard]] auto stats() const -> ViewportCaptureStats;

  private:
    ViewportCaptureStats m_stats;
    Graphics::Image m_image;
  };
}  // namespace Dot::Editor

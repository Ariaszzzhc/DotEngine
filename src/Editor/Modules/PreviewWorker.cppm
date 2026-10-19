module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Editor.PreviewWorker;

import Dot.Core.Result;
import Dot.Core.Files;
export import Dot.Editor.AssetPreview;
import Dot.Assets.Material;

export namespace Dot::Editor {
  struct PreviewRequest {
    std::string slot;
    std::string path;
    std::uint32_t size{128};
    std::optional<Assets::MaterialDescription> material;
    bool priority{};
  };

  struct PreviewCompletion {
    std::uint64_t ticket;
    std::string slot;
    Core::Result<AssetPreviewImage> result;
  };

  struct PreviewWorkerStats {
    std::size_t pending{};
    std::size_t completed{};
    bool active{};
    bool ready{};
    bool stopped{};
    bool paused{};
    std::uint64_t submitted{};
    std::uint64_t started{};
    std::uint64_t finished{};
    std::uint64_t discarded{};
    std::string initializationError;
  };

  using PreviewExecutor
      = std::function<Core::Result<AssetPreviewImage>(const PreviewRequest&, std::stop_token)>;
  /** Invoke the factory on the worker; create/invoke/destroy its executor there.
   * Factory captures release on the owner after join. Providers must own captures
   * and honor request/output bounds; no UI borrows.
   * 工作线程调用工厂，并创建、调用、销毁返回的执行器；工厂捕获在等待线程结束后由
   * 所有者释放。提供者须拥有捕获对象并遵守请求/结果边界，不得借用 UI 状态。 */
  using PreviewFactory = std::function<Core::Result<PreviewExecutor>()>;

  /** One owned thread, at most 64 pending requests and 8 completed images.
   * Same-slot submission supersedes queued/completed work and discards late active
   * results. Priority requests precede FIFO thumbnails. Submit/poll never execute
   * provider work or wait for it. Owner-thread API, moves preserve stable storage.
   * Stop/destruction discard queued work and join active work; provider/GPU calls
   * may need to finish. No detached threads or forced GPU interruption.
   * 拥有单线程，至多 64 待处理请求和 8 完成图；同槽位提交合并旧请求并丢弃迟到结果，
   * 优先请求排在 FIFO 缩略图之前。提交/轮询不执行或等待提供者工作，API 限所有者线程。
   * 停止/销毁清队列并等待活动工作结束，不分离线程或强行中断 GPU。 */
  class PreviewWorker {
  public:
    [[nodiscard]] static auto start(PreviewFactory factory) -> Core::Result<PreviewWorker>;
    PreviewWorker(const PreviewWorker&) = delete;
    auto operator=(const PreviewWorker&) -> PreviewWorker& = delete;
    PreviewWorker(PreviewWorker&&) noexcept;
    auto operator=(PreviewWorker&&) noexcept -> PreviewWorker&;
    ~PreviewWorker();
    /** Owned portable supported path, output 16..256 and nonempty UTF-8 slot <=1024
     * bytes. Validate drafts before enqueue. Tickets are local/nonzero/monotonic.
     * 请求拥有可移植且已支持的路径、16..256 输出和至多 1024 字节非空 UTF-8 槽位；
     * 入队前校验材质草稿，票号为本地非零递增值。 */
    [[nodiscard]] auto submit(PreviewRequest request) -> Core::Result<std::uint64_t>;
    [[nodiscard]] auto takeCompleted() -> Core::Result<std::vector<PreviewCompletion>>;
    [[nodiscard]] auto cancel(std::string_view slot) -> Core::Result<void>;
    /** Discard all generations; optionally recreate provider on its own thread
     * before the next job. Does not join or block on rendering.
     * 丢弃所有代次，可选择在下一任务前由工作线程重建提供者，不等待渲染。 */
    [[nodiscard]] auto invalidate(bool resetProvider = false) -> Core::Result<void>;
    /** Pause starting queued work; an active operation may finish normally.
     * 暂停启动排队工作，活动操作仍可正常结束。 */
    [[nodiscard]] auto setPaused(bool paused) -> Core::Result<void>;
    [[nodiscard]] auto stats() const -> Core::Result<PreviewWorkerStats>;
    [[nodiscard]] auto stop() -> Core::Result<void>;

  private:
    struct Impl;
    explicit PreviewWorker(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };

  /** Real provider: read packaged studio HDRI and create/render/destroy the
   * isolated device on the worker. Initialization errors remain explicit until
   * reset; no direct-light/sample fallback replaces a missing HDRI.
   * 实际提供者在工作线程读取摄影棚 HDRI 并创建、渲染、销毁独立设备；初始化失败明确
   * 保留至重置，不用直射光或样例替代缺失 HDRI。 */
  [[nodiscard]] auto createAssetPreviewWorker(Core::FileSystem files, std::filesystem::path studio)
      -> Core::Result<PreviewWorker>;
}  // namespace Dot::Editor

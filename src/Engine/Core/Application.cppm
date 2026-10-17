module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module Dot.Core.Application;

import Dot.Core.Events;
import Dot.Core.Result;
import Dot.Platform.Events;
import Dot.Platform.Input;
import Dot.Platform.Window;

export namespace Dot::Core {
  struct ApplicationConfig {
    Platform::WindowConfig window;
    std::chrono::nanoseconds fixedStep{16'666'667};
    std::size_t maxUpdates{8};
    std::chrono::nanoseconds frameDelay{1'000'000};
    bool pauseOnFocusLoss{true};
  };

  struct FrameState {
    bool running;
    bool paused;
    std::uint64_t index;
    std::size_t updates;
    double interpolation;
    std::chrono::nanoseconds elapsed;
    std::chrono::nanoseconds dropped;
  };

  struct LoopCallbacks {
    std::function<Result<void>(double)> fixedUpdate;
    std::function<Result<void>(double)> update;
    std::function<Result<void>(const FrameState&)> render;
    /** Once after event consumption, before fixed/update callbacks, including
     * paused frames. Publish input here; the view is borrowed for this call only.
     * Failure/stop prevents all later frame callbacks. 事件处理后、固定/逐帧更新前每帧
     * 调用一次，含暂停帧；在此发布输入，视图仅在本次调用有效，失败/停止阻止后续帧回调。 */
    std::function<Result<void>(const Platform::InputState&)> prepareFrame;
    /** Return false to defer a native quit/close request, e.g. for an unsaved
     * document dialog. Failure stops the loop; absent handler accepts closure.
     * 对原生退出/关闭请求返回 false 以延后，例如未保存文档对话框；失败停止循环，
     * 未提供处理器时接受关闭。 */
    std::function<Result<bool>()> acceptClose;
    /** Once after fixed updates and before update/render, on unpaused frames.
     * Provides the exact supplied duration and completed fixed-step metadata for
     * clients with an independent simulation clock. Stop/failure skips later calls.
     * 非暂停帧在固定更新后、逐帧更新/渲染前调用，提供精确传入时长和已完成固定步元数据，
     * 供独立模拟时钟使用；停止/失败会跳过后续回调。 */
    std::function<Result<void>(const FrameState&)> updateFrame;
  };

  /** CPU wall intervals for one actual step; absent phases did not execute.
   * Render includes resource preparation, GUI and any synchronous driver waits.
   * Elapsed is supplied simulation time, not this step's measured cost. No GPU
   * duration, process CPU utilization or initialization outside step is implied.
   * 一次真实 step 的 CPU 墙钟区间，未执行阶段为空；渲染包含资源准备、GUI 和同步驱动
   * 等待。elapsed 是传入的模拟时间，并非本次开销；不代表 GPU 时间、CPU 占用或 step 外初始化。 */
  struct ApplicationFrameTiming {
    FrameState frame{};
    std::uint64_t totalNanoseconds{};
    std::optional<std::uint64_t> eventsNanoseconds;
    std::optional<std::uint64_t> prepareNanoseconds;
    std::optional<std::uint64_t> fixedNanoseconds;
    std::optional<std::uint64_t> updateNanoseconds;
    std::optional<std::uint64_t> renderNanoseconds;
    std::optional<Error> error;
  };

  /** Independent greatest executed phase samples since enablement. Each owns its
   * complete frame/error context; absent callbacks remain null. Earlier ties win.
   * Peaks can belong to different steps and must not be added into one frame cost.
   * 启用后各已执行阶段的独立最大样本，拥有完整帧/错误上下文；未执行为空，相等保留较早帧。
   * 峰值可来自不同 step，不可相加当作单帧开销。 */
  struct ApplicationPhasePeaks {
    std::optional<ApplicationFrameTiming> events;
    std::optional<ApplicationFrameTiming> prepare;
    std::optional<ApplicationFrameTiming> fixed;
    std::optional<ApplicationFrameTiming> update;
    std::optional<ApplicationFrameTiming> render;
  };

  struct ApplicationProfiling {
    bool enabled{};
    std::uint64_t session{};
    std::uint64_t sampled{};
    std::vector<ApplicationFrameTiming> frames;
    std::optional<ApplicationFrameTiming> slowest;
    std::optional<ApplicationFrameTiming> longestInterval;
    ApplicationPhasePeaks phasePeaks;
  };

  /** Owned JSON with decimal-string identities/nanoseconds and explicit null
   * phases. Compact mode retains latest/slowest/longest-input-interval/phase peaks.
   * 拥有独立 JSON，身份/纳秒采用十进制字符串，未执行阶段为
   * null；精简模式保留最新、最慢、最长传入间隔及各阶段峰值帧。 */
  [[nodiscard]] auto encodeApplicationProfiling(const ApplicationProfiling& profile,
                                                bool history = true) -> Result<std::string>;

  /**
   * Main-thread application loop with fixed simulation and per-frame callbacks.
   * 主线程应用循环，分别提供固定步长模拟和逐帧回调。
   * Focus loss pauses game updates while events and rendering continue.
   * 失焦暂停游戏更新，事件与渲染继续进行。
   */
  class Application {
  public:
    [[nodiscard]] static auto create(const ApplicationConfig& config = {}) -> Result<Application>;
    Application(const Application&) = delete;
    auto operator=(const Application&) -> Application& = delete;
    Application(Application&&) noexcept;
    auto operator=(Application&&) noexcept -> Application&;
    ~Application();

    [[nodiscard]] auto step(std::chrono::nanoseconds elapsed, const LoopCallbacks& callbacks = {})
        -> Result<FrameState>;
    /** A zero frame budget runs until stopped. 帧预算为零时持续运行至停止。 */
    [[nodiscard]] auto run(const LoopCallbacks& callbacks = {}, std::size_t frameBudget = 0)
        -> Result<void>;
    /** Safe from another thread while the owner remains alive. 所有者存活期间可从其他线程请求停止。
     */
    auto stop() noexcept -> void;
    [[nodiscard]] auto running() const noexcept -> bool;
    /** Borrowed resources; do not transfer window ownership. 借用资源，不得转移窗口所有权。 */
    [[nodiscard]] auto window() -> Platform::Window&;
    [[nodiscard]] auto window() const -> const Platform::Window&;
    [[nodiscard]] auto input() const -> const Platform::InputState&;
    [[nodiscard]] auto events() -> Signal<Platform::Event>&;
    /** Opt-in bounded diagnostics, on the owner thread. Keep the last 120 steps,
     * slowest step, longest supplied interval and five independent phase peaks,
     * including callback failures. Repeating
     * the current value preserves history; disabling releases it. A toggle inside
     * a callback invalidates that in-progress sample without borrowing dead state.
     * 所有者线程可选诊断，保留最近 120 次
     * step、最慢帧、最长间隔帧及五个独立阶段峰值，含回调失败；同值
     * 设置保留历史，关闭释放数据。回调内切换丢弃当前样本，不借用已释放状态。 */
    [[nodiscard]] auto setProfiling(bool enabled) -> Result<void>;
    [[nodiscard]] auto profiling() const -> Result<ApplicationProfiling>;

  private:
    struct Impl;
    explicit Application(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Core

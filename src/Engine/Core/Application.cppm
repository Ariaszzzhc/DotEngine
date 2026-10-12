module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

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
  };

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
    [[nodiscard]] auto input() const -> const Platform::InputState&;
    [[nodiscard]] auto events() -> Signal<Platform::Event>&;

  private:
    struct Impl;
    explicit Application(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Core

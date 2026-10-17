module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module Dot.Script.GameClock;

import Dot.Core.Result;
export import Dot.Script.Host;

export namespace Dot::Script {
  enum class GameStepState { Pending, Running, Completed, Cancelled, Failed };
  [[nodiscard]] auto gameStepStateName(GameStepState state) -> std::string_view;

  struct GameStepSnapshot {
    std::uint64_t id;
    GameStepState state;
    auto operator==(const GameStepSnapshot&) const -> bool = default;
  };

  struct GameClockSnapshot {
    std::uint64_t revision;
    bool paused;
    std::optional<GameStepSnapshot> step{};
  };

  /** Scene-local simulation pause/single-step requests, latched by the session at phase
   * boundaries. Frame/UI callbacks, input, rendering, loading and audio playback
   * remain active. Owner-thread handles retain observations after retirement;
   * constructors/migration may read but cannot change pause. No wall-time debt.
   * 场景本地模拟暂停/单步请求，在会话阶段边界锁存；帧/UI 回调、输入、渲染、加载及音频播放
   * 继续运行。所有者线程句柄退役后保留观察；构造/迁移只读，不累积暂停墙钟时间。 */
  class GameClock {
  public:
    [[nodiscard]] static auto create() -> Core::Result<GameClock>;
    [[nodiscard]] auto snapshot() const -> Core::Result<GameClockSnapshot>;
    [[nodiscard]] auto perform(std::string_view request) -> Core::Result<std::string>;
    [[nodiscard]] auto bindings() const -> Core::Result<HostBindings>;
    /** Session-only phase protocol: consume one pending request at a variable
     * boundary, then report the actual fixed/update/native outcome. Never changes
     * pause. Resume cancels pending work; retirement fails running work. While
     * running, pause writes/new requests fail. No wall-time or input injection.
     * 会话阶段协议：在变量帧边界取走单个请求，随后报告固定/变量/原生阶段实际结果，不改
     * 暂停。恢复取消待执行请求，退役使执行中请求失败；执行中拒绝暂停修改及新请求，不注入
     * 墙钟或输入。 */
    [[nodiscard]] auto beginStep() -> Core::Result<bool>;
    [[nodiscard]] auto finishStep(bool succeeded) -> Core::Result<void>;
    auto retire() noexcept -> void;

  private:
    struct Impl;
    explicit GameClock(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

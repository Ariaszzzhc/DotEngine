module;

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

export module Dot.Script.GameClock;

import Dot.Core.Result;
export import Dot.Script.Host;

export namespace Dot::Script {
  struct GameClockSnapshot {
    std::uint64_t revision;
    bool paused;
  };

  /** Scene-local simulation pause request, latched by the session at phase
   * boundaries. Frame/UI callbacks, input, rendering, loading and audio playback
   * remain active. Owner-thread handles retain observations after retirement;
   * constructors/migration may read but cannot change pause. No wall-time debt.
   * 场景本地模拟暂停请求，在会话阶段边界锁存；帧/UI 回调、输入、渲染、加载及音频播放
   * 继续运行。所有者线程句柄退役后保留观察；构造/迁移只读，不累积暂停墙钟时间。 */
  class GameClock {
  public:
    [[nodiscard]] static auto create() -> Core::Result<GameClock>;
    [[nodiscard]] auto snapshot() const -> Core::Result<GameClockSnapshot>;
    [[nodiscard]] auto perform(std::string_view request) -> Core::Result<std::string>;
    [[nodiscard]] auto bindings() const -> Core::Result<HostBindings>;
    auto retire() noexcept -> void;

  private:
    struct Impl;
    explicit GameClock(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

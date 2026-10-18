module;

#include <memory>
#include <string>
#include <string_view>

export module Dot.Project.PlayerControl;

import Dot.Core.Result;
import Dot.Project.GameSession;
import Dot.Platform.Input;

export namespace Dot::Project {
  /** Product control over a borrowed stable game session, without windows/devices.
   * Never advances simulation or acknowledges OS focus/pointer capture. Input owns
   * bounded native frames through Platform.AgentInput; scene replacement cancels
   * old scoped input. Close requests frontend shutdown; it does not invoke hooks.
   * Owner-thread calls only; session outlives this object, moves retain identity.
   * Records at most 32 script failures with explicit 8-KiB message truncation.
   * 借用稳定游戏会话的产品控制，不打开窗口/设备，不推进模拟或伪造系统聚焦/鼠标捕获。
   * Platform.AgentInput 拥有有界原生帧，场景替换取消旧范围输入。关闭仅请求前端退出，
   * 不执行钩子。操作限创建线程，会话须更长寿，移动保留身份；至多记录 32 条脚本错误，
   * 每条消息超过 8 KiB 时明确标记截断。 */
  class PlayerControl {
  public:
    [[nodiscard]] static auto create(GameSession& session) -> Core::Result<PlayerControl>;
    PlayerControl(const PlayerControl&) = delete;
    auto operator=(const PlayerControl&) -> PlayerControl& = delete;
    PlayerControl(PlayerControl&&) noexcept;
    auto operator=(PlayerControl&&) noexcept -> PlayerControl&;
    ~PlayerControl();

    /** Strict JSON: status/inspect/close; input(scope,value), cancel-input(scope),
     * game-data(value), clock(scope,value), request-scene(scope,path), scene-status(request),
     * animation-state(scope,entity), animation-layer(scope,entity,name,value).
     * Mutations require Running; pre-start/closing game data permits snapshot/get.
     * Layer control commits a native pose even while scheduling is paused.
     * Initial preparation has scope zero and a null clock; only status/close work
     * before scene services exist. Status reports actual preparation/error/counters
     * and observes lifecycle/input without encoding the world or advancing time.
     * Command acknowledgement is not input/scene/shutdown completion.
     * 严格 JSON 操作为上述字段，修改要求 Running，启动前/关闭中仅允许内存 snapshot/get；
     * 调度暂停时仍可显式提交原生动画层姿态。
     * 初始准备时范围为零、时钟为空，服务存在前仅 status/close 可用；
     * status 报告真实准备阶段/错误/计数，观察生命周期/输入，不编码世界、不推进时间；
     * 命令被接受不代表输入/场景替换/退出已完成。 */
    [[nodiscard]] auto perform(std::string_view request) -> Core::Result<std::string>;
    [[nodiscard]] auto next(const Platform::InputState& native)
        -> Core::Result<Platform::InputState>;
    [[nodiscard]] auto record(const Script::ScriptReport& report) -> Core::Result<void>;
    [[nodiscard]] auto usingAgentInput() const -> Core::Result<bool>;
    [[nodiscard]] auto closing() const -> Core::Result<bool>;

  private:
    struct Impl;
    explicit PlayerControl(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Project

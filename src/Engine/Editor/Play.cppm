module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

export module Dot.Editor.Play;

import Dot.Core.Result;
import Dot.Project;
import Dot.Project.GameSession;
import Dot.Platform.Input;
import Dot.World.Scene;
import Dot.Script.Compiler;
import Dot.Script.LiveScripts;

export namespace Dot::Editor {
  enum class PlayState { Running, Faulted, Stopped };

  struct PlayConfig {
    PlayConfig() noexcept;
    Project::GameSessionConfig session;
    std::size_t maxUpdates{8};
  };

  struct PlayStats {
    PlayState state;
    std::uint32_t fixedHz;
    std::uint64_t frames;
    std::uint64_t fixedSteps;
    std::chrono::nanoseconds dropped;
  };

  struct PlayFrame {
    std::chrono::nanoseconds elapsed{};
    std::chrono::nanoseconds dropped{};
    std::vector<Script::LiveScriptReport> reports;
  };

  /** Owns a freshly cloned, started game session and independent fixed timing.
   * Never saves or borrows the authoring scene. Ordinary component/reload errors
   * are retained in lastFrame; realm/native faults require stop and recreation.
   * Calls require the creating thread without reentry; moves preserve ownership.
   * Stop runs hooks and releases backends even on failure, retaining the final
   * read-only scene. Destruction releases resources without invoking hooks.
   * 拥有新克隆并启动的游戏会话及独立固定步计时，不保存或借用编辑场景。普通组件/
   * 重载错误保留在 lastFrame，realm/原生故障须停止并重建。调用限创建线程且不可重入，
   * 移动保持所有权。停止即使失败也释放后端并保留最终只读场景；销毁不执行钩子。 */
  class PlaySession {
  public:
    [[nodiscard]] static auto create(const Project::Project& project,
                                     const Script::TypeScriptCompiler& compiler,
                                     const World::Scene& authoring, const PlayConfig& config = {})
        -> Core::Result<PlaySession>;
    PlaySession(const PlaySession&) = delete;
    auto operator=(const PlaySession&) -> PlaySession& = delete;
    PlaySession(PlaySession&&) noexcept;
    auto operator=(PlaySession&&) noexcept -> PlaySession&;
    ~PlaySession();
    /** Publish one input frame, run at most 1..64 configured fixed steps, then
     * update scripts/audio once with variable delta capped at one second.
     * Excess fixed time is reported as dropped. Negative elapsed time fails
     * before input/timing effects; partial completed callbacks are not rolled back.
     * 发布一帧输入，执行配置的至多 1..64 个固定步，然后更新一次脚本/音频，变动时长
     * 至多一秒。额外固定时长报告为 dropped；负时长在输入/计时副作用前失败，不回滚
     * 已完成的回调。 */
    [[nodiscard]] auto advance(const Platform::InputState& input, std::chrono::nanoseconds elapsed)
        -> Core::Result<void>;
    [[nodiscard]] auto stop() -> Core::Result<Script::ScriptReport>;
    [[nodiscard]] auto scene() const -> Core::Result<const World::Scene*>;
    [[nodiscard]] auto stats() const -> Core::Result<PlayStats>;
    [[nodiscard]] auto lastFrame() const -> Core::Result<PlayFrame>;

  private:
    struct Impl;
    explicit PlaySession(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Editor

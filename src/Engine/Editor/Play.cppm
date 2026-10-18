module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Editor.Play;

import Dot.Animation.ScenePlayers;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Project;
import Dot.Project.GameSession;
import Dot.Platform.Input;
import Dot.World.Scene;
import Dot.Script.Compiler;
import Dot.Script.LiveScripts;

export namespace Dot::Editor {
  enum class PlayState { Running, Faulted, Stopped, Preparing, Cancelling };
  enum class PlayPreparation { Audio, Scene, Activation, Teardown, Scripts };

  struct PlayConfig {
    PlayConfig() noexcept;
    Project::GameSessionConfig session;
    /** Optional parent for owned disposable children; defaults to system temp.
     * Never supplies player saves directly. 可指定临时子目录的父目录，默认系统临时目录；
     * 不可直接使用玩家存档目录。 */
    std::optional<Core::FileSystem> temporarySavesParent;
    std::size_t maxUpdates{8};
  };

  struct PlayStats {
    PlayState state;
    std::uint32_t fixedHz;
    std::uint64_t frames;
    std::uint64_t fixedSteps;
    std::chrono::nanoseconds dropped;
    Script::SceneLocation location{};
    std::optional<Script::SceneRequest> transition;
    std::optional<Script::LiveScriptsStats> source{};
    bool started{true};
    std::optional<Core::Error> preparationError{};
    std::optional<PlayPreparation> preparation{};
    /** Initial jobs/stale results before publication, separate from live reload attempts.
     * 发布前初始任务/旧结果计数，与运行中的重载尝试分开。 */
    std::uint64_t preparationCompileAttempts{};
    std::uint64_t discardedScriptCompilations{};
    bool retiringAudio{};
    std::optional<Script::GameClockSnapshot> clock{};
    std::uint64_t simulatedUpdates{};
  };

  struct PlayFrame {
    std::chrono::nanoseconds elapsed{};
    std::chrono::nanoseconds dropped{};
    std::vector<Script::LiveScriptReport> reports;
  };

  /** Owns cloned play preparation or a started game session and independent fixed timing.
   * Never saves or borrows the authoring scene. Ordinary component/reload errors
   * are retained in lastFrame; realm/native faults require stop and recreation.
   * Calls require the creating thread without reentry; moves preserve ownership.
   * Published Stop runs hooks and releases backends even on failure, retaining the final
   * read-only scene. Destruction releases resources without invoking hooks.
   * 拥有新克隆的准备状态或已启动游戏会话及独立固定步计时，不保存或借用编辑场景。普通组件/
   * 重载错误保留在 lastFrame，realm/原生故障须停止并重建。调用限创建线程且不可重入，
   * 移动保持所有权。发布后停止即使失败也释放后端并保留最终只读场景；销毁不执行钩子。 */
  class PlaySession {
  public:
    [[nodiscard]] static auto create(const Project::Project& project,
                                     const Script::TypeScriptCompiler& compiler,
                                     const World::Scene& authoring, const PlayConfig& config = {},
                                     std::optional<std::string> scenePath = std::nullopt)
        -> Core::Result<PlaySession>;
    /** Freeze authoring and begin real endpoint preparation. pollPreparation
     * performs owner-thread scene/script setup and polls native activation;
     * only completed activation publishes Running. Stop cancels unpublished work
     * without hooks; keep polling until teardown finishes. Initial compilation
     * shares the owned snapshot worker with hot reload; changed captures retire
     * obsolete work before owner-thread scene/script construction. Source capture
     * and scene service setup still execute on the owner.
     * 冻结编辑内容并开始真实端点准备；轮询在所有者线程建立场景/脚本并检查原生激活，
     * 实际完成后才发布 Running。Stop 无钩子取消未发布工作，须持续轮询至清理结束。
     * 初次编译与热重载共用拥有的快照工作线程，改变的捕获使旧任务退休，再在所有者
     * 线程构造场景/脚本；源码捕获和场景服务建立仍在所有者线程执行。 */
    [[nodiscard]] static auto begin(const Project::Project& project,
                                    const Script::TypeScriptCompiler& compiler,
                                    const World::Scene& authoring, const PlayConfig& config = {},
                                    std::optional<std::string> scenePath = std::nullopt)
        -> Core::Result<PlaySession>;
    [[nodiscard]] auto pollPreparation() -> Core::Result<PlayState>;
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
    [[nodiscard]] auto advance(const Platform::InputState& input, std::chrono::nanoseconds elapsed,
                               bool pointerCaptured = false,
                               std::optional<Project::GameInputArea> area = std::nullopt,
                               bool textInputActive = false) -> Core::Result<void>;
    [[nodiscard]] auto requestScene(std::string_view path) -> Core::Result<std::uint64_t>;
    [[nodiscard]] auto sceneRequest(std::uint64_t id) const -> Core::Result<Script::SceneRequest>;
    [[nodiscard]] auto gameData() const -> Core::Result<Script::GameData>;
    [[nodiscard]] auto inspectAnimation(World::EntityId owner) const -> Core::Result<std::string>;
    [[nodiscard]] auto animationState(World::EntityId owner) const
        -> Core::Result<Animation::ScenePlayerState>;
    [[nodiscard]] auto animationParameters(
        World::EntityId owner, std::span<const Animation::ControllerParameter> parameters)
        -> Core::Result<void>;
    [[nodiscard]] auto resetAnimationController(World::EntityId owner) -> Core::Result<void>;
    [[nodiscard]] auto controlAnimationLayer(World::EntityId owner, std::string_view name,
                                             const Animation::LayerControl& operation)
        -> Core::Result<void>;
    [[nodiscard]] auto gameClock() const -> Core::Result<Script::GameClock>;
    [[nodiscard]] auto hud() const -> Core::Result<Script::Hud>;
    [[nodiscard]] auto textInputRequested() const -> Core::Result<bool>;
    [[nodiscard]] auto pointerCaptureRequested() const -> Core::Result<bool>;
    [[nodiscard]] auto stop() -> Core::Result<Script::ScriptReport>;
    /** Borrow expires when a later advance replaces the scene; reacquire each frame.
     * 后续 advance 替换场景会使借用失效，每帧须重新获取。 */
    [[nodiscard]] auto scene() const -> Core::Result<const World::Scene*>;
    [[nodiscard]] auto stats() const -> Core::Result<PlayStats>;
    /** Poll/retry source work without advancing Play timing or input.
     * 不推进 Play 计时或输入地轮询/重试源码任务。 */
    [[nodiscard]] auto pollScripts(bool force = false) -> Core::Result<Script::ReloadStatus>;
    [[nodiscard]] auto lastFrame() const -> Core::Result<PlayFrame>;

  private:
    struct Impl;
    explicit PlaySession(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Editor

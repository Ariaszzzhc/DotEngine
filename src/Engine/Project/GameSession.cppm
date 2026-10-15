module;

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

export module Dot.Project.GameSession;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Project;
import Dot.World.Scene;
import Dot.Platform.Input;
import Dot.Script.Compiler;
export import Dot.Script.LiveScripts;
export import Dot.Script.SceneScripts;
export import Dot.Script.InputSource;
export import Dot.Script.PhysicsSource;
export import Dot.Audio.SceneAudio;

export namespace Dot::Project {
  enum class SessionAudio { Offline, Playback };
  enum class SessionState { Ready, Running, Faulted, Closed };

  struct GameSessionConfig {
    Script::InputSourceConfig input;
    Script::PhysicsSourceConfig physics;
    Script::SceneScriptsConfig scripts;
    Audio::PlaybackConfig audio;
    Audio::SceneAudioLimits audioLimits;
    SessionAudio audioMode{SessionAudio::Offline};
    std::chrono::milliseconds pollInterval{100};
  };

  struct GameSessionStats {
    SessionState state;
    std::uint32_t fixedHz;
    bool hotReload;
    std::optional<Script::SceneScriptsStats> scripts;
  };

  /** Owns one freshly loaded project scene and its input, scripts, real physics
   * native animation and offline/native audio. No window or renderer is opened. Operations require
   * the creating thread without reentry; moves keep stable service ownership.
   * Destruction only releases resources; close invokes stop hooks and releases
   * services even after faults. The shared scene remains inspectable after close.
   * 拥有新加载项目场景及输入、脚本、真实物理和离线/原生音频，不打开窗口或渲染器。
   * 操作限创建线程且不可重入，移动保持服务所有权稳定；销毁仅释放资源，close 执行停止
   * 钩子且在故障后也释放服务，关闭后仍可检查共享场景。 */
  class GameSession {
  public:
    [[nodiscard]] static auto create(const Project& project,
                                     const Script::TypeScriptCompiler& compiler,
                                     const GameSessionConfig& config = {})
        -> Core::Result<GameSession>;
    /** Clone a validated authoring scene into a fresh scope without saving or
     * borrowing it. Project resources/scripts remain rooted at the project.
     * 克隆已校验的编辑场景到新范围，不保存也不借用源场景；资源/脚本仍以项目为根。 */
    [[nodiscard]] static auto createFromScene(const Project& project,
                                              const Script::TypeScriptCompiler& compiler,
                                              const World::Scene& scene,
                                              const GameSessionConfig& config = {})
        -> Core::Result<GameSession>;
    /** Start-ready session from an owned compiled generation, without reading
     * TypeScript or invoking a compiler. Description must disable hot reload;
     * authoring is cloned, and asset-root files must outlive the session.
     * 从拥有的编译代建立待启动会话，不读 TypeScript 也不调用编译器；描述须关闭热重载。
     * 克隆编辑场景，资产根目录文件须比会话存活更久。 */
    [[nodiscard]] static auto createCompiled(const Core::FileSystem& assets,
                                             const Description& description,
                                             const World::Scene& scene,
                                             const Script::CompiledProgram& program,
                                             const GameSessionConfig& config = {})
        -> Core::Result<GameSession>;
    GameSession(const GameSession&) = delete;
    auto operator=(const GameSession&) -> GameSession& = delete;
    GameSession(GameSession&&) noexcept;
    auto operator=(GameSession&&) noexcept -> GameSession&;
    ~GameSession();

    [[nodiscard]] auto scene() const -> Core::Result<std::shared_ptr<World::Scene>>;
    [[nodiscard]] auto stats() const -> Core::Result<GameSessionStats>;
    [[nodiscard]] auto publish(const Platform::InputState& input) -> Core::Result<std::uint64_t>;
    [[nodiscard]] auto start() -> Core::Result<Script::ScriptReport>;
    /** Requires start. Fixed steps dispatch scripts using the preceding contact
     * batch then advance physics by 1/fixedHz. Update dispatches scripts then
     * advances animation then synchronizes audio once after all fixed steps; delta is finite [0,1].
     * Initial input is an empty published frame. Poll errors retain/report old scripts; individual
     * script failures are isolated/reported, realm/native failures fault the session until close.
     * Invalid deltas have no effects. There is no rollback of completed callbacks or native steps.
     * 要求已启动；固定步使脚本读取上一接触批次，再推进物理 1/fixedHz；update 在全部
     * 固定步之后分发脚本并同步一次音频，时长须有限 [0,1]；初始输入为已发布空帧。轮询错误保留并
     * 报告旧脚本，单脚本错误隔离并报告，realm/原生失败使会话故障直至关闭；无效时长无
     * 副作用，不回滚已完成回调或原生步。 */
    [[nodiscard]] auto fixedUpdate() -> Core::Result<Script::LiveScriptReport>;
    [[nodiscard]] auto update(double seconds) -> Core::Result<Script::LiveScriptReport>;
    [[nodiscard]] auto renderAudio(std::span<float> samples) -> Core::Result<void>;
    [[nodiscard]] auto close() -> Core::Result<Script::ScriptReport>;

  private:
    [[nodiscard]] static auto prepare(const Project& project,
                                      const Script::TypeScriptCompiler& compiler,
                                      World::Scene scene, const GameSessionConfig& config)
        -> Core::Result<GameSession>;
    [[nodiscard]] static auto prepareSession(const Core::FileSystem& assets,
                                             Description description, World::Scene scene,
                                             const GameSessionConfig& config,
                                             const Script::TypeScriptCompiler* compiler,
                                             const Script::CompiledProgram* program)
        -> Core::Result<GameSession>;
    struct Impl;
    explicit GameSession(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Project

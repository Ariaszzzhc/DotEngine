module;

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

export module Dot.Script.LiveScripts;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.World.Scene;
import Dot.Script.Compiler;
import Dot.Script.SceneScripts;

export namespace Dot::Script {
  struct LiveScriptsConfig {
    SceneScriptsConfig scripts;
    std::string directory{"scripts"};
    std::chrono::milliseconds pollInterval{100};
  };

  struct ReloadStatus {
    bool checked{};
    bool changed{};
    bool committed{};
    bool compiling{};
    bool discarded{};
    std::optional<Core::Error> error;
    std::optional<ReloadReport> reload;
  };

  struct LiveScriptReport {
    ReloadStatus source;
    ScriptReport frame;
  };

  struct LiveScriptsStats {
    /** Attempts launched by this controller; prepared construction starts at zero.
     * 此控制器启动的尝试；使用已准备程序构造时从零开始。 */
    std::uint64_t compileAttempts;
    std::uint64_t reloads;
    std::optional<Core::Error> error;
    SceneScriptsStats scripts;
    bool compiling{};
    std::uint64_t discardedCompilations{};
  };

  /** Source-content polling around the compiler/lifecycle service. Captures
   * immutable bounded snapshots and compiles exactly those bytes, with no hash/
   * timestamp inference or OS-specific watcher dependency. Failed candidates keep
   * old scripts running; identical failed source is not compiled again unless
   * forced. Poll interval is 0..60000 ms; force also retries unchanged contents.
   * Operations require the original owner thread/scene without reentry. Copies
   * project/compiler descriptors, preserves caller environment and owns script
   * storage; destruction invokes no script. 包装编译/生命周期服务的源码内容轮询，
   * 捕获不可变有界快照并精确编译其字节，不依赖哈希/时间戳推断或特定 OS 监听器。
   * 候选失败保留旧脚本运行，相同失败源码仅强制时重编译；间隔 0..60000 毫秒。
   * 操作限原所有者线程/场景且不可重入，复制项目/编译器描述，保留环境并拥有脚本存储，
   * 销毁不调用脚本。 */
  class LiveScripts {
  public:
    [[nodiscard]] static auto create(std::shared_ptr<World::Scene> scene,
                                     const Core::FileSystem& project,
                                     const TypeScriptCompiler& compiler,
                                     const LiveScriptsConfig& config = {})
        -> Core::Result<LiveScripts>;
    /** Prepare an already compiled source generation without running a compiler.
     * Its owned source provenance must exactly match the current project capture;
     * published JS-only programs cannot seed a source-watching session.
     * 使用已编译源码代准备，不调用编译器；拥有的源码来源须与当前项目捕获精确一致，
     * 仅含已发布 JS 的程序不可初始化源码监听会话。 */
    [[nodiscard]] static auto createPrepared(std::shared_ptr<World::Scene> scene,
                                             const Core::FileSystem& project,
                                             const TypeScriptCompiler& compiler,
                                             const CompiledProgram& program,
                                             const LiveScriptsConfig& config = {})
        -> Core::Result<LiveScripts>;
    LiveScripts(const LiveScripts&) = delete;
    auto operator=(const LiveScripts&) -> LiveScripts& = delete;
    LiveScripts(LiveScripts&&) noexcept;
    auto operator=(LiveScripts&&) noexcept -> LiveScripts&;
    ~LiveScripts();

    /** Capture on the owner, compile one owned snapshot in the background, then
     * recapture before owner-thread migration. Changed/invalid sources cancel and
     * discard obsolete jobs; only the latest capture may commit. Force schedules
     * an unchanged retry, never waits; repeated force coalesces into an active job.
     * Stop/destruction cancel and join the worker and reap its compiler process.
     * 源码在所有者线程捕获，单个后台任务编译拥有的快照，迁移前重新捕获核对；源码改变/
     * 无效时取消并丢弃旧任务，仅当前源码可提交。强制请求异步重试，同一活动任务合并请求。
     * 停止/销毁取消并回收工作线程及编译进程。 */
    [[nodiscard]] auto poll(bool force = false) -> Core::Result<ReloadStatus>;
    /** Start the prepared generation without polling. Calls are idempotent;
     * lifecycle failures are reported by the underlying scene service.
     * 不轮询而启动已准备代；调用幂等，生命周期错误由场景服务报告。 */
    [[nodiscard]] auto start() -> Core::Result<ScriptReport>;
    /** Poll then dispatch the old or newly committed generation; report both
     * reload diagnostics and lifecycle failures explicitly. Invalid deltas fail
     * before polling or script effects. 轮询后分发旧/已提交新代，明确报告重载诊断与
     * 生命周期错误；无效时间在轮询或脚本副作用之前失败。 */
    [[nodiscard]] auto frame(double seconds) -> Core::Result<LiveScriptReport>;
    [[nodiscard]] auto update(double seconds) -> Core::Result<LiveScriptReport>;
    [[nodiscard]] auto fixedUpdate(double seconds) -> Core::Result<LiveScriptReport>;
    /** Dispatch one prepared phase without polling/replacing its generation.
     * Allows a frame/UI phase and conditional gameplay to share one source result.
     * 分发已准备阶段，不轮询/替换编译代；帧/UI 与条件游戏阶段共用一次源码结果。 */
    [[nodiscard]] auto dispatchPrepared(ScriptPhase phase, double seconds)
        -> Core::Result<ScriptReport>;
    [[nodiscard]] auto stop() -> Core::Result<ScriptReport>;
    /** Quiescent cancellation, including another destruction thread. Release
     * outstanding compilation without invoking hooks or stopping the generation;
     * later owner polls can retry. 静止时取消，可用于异线程销毁；释放未完成编译，
     * 不调用钩子或停止脚本代；之后所有者可再次轮询重试。 */
    auto cancelCompilation() noexcept -> void;
    [[nodiscard]] auto stats() const -> Core::Result<LiveScriptsStats>;

  private:
    [[nodiscard]] static auto prepare(std::shared_ptr<World::Scene> scene,
                                      const Core::FileSystem& project,
                                      const TypeScriptCompiler& compiler, SourceSnapshot source,
                                      const CompiledProgram& program,
                                      const LiveScriptsConfig& config,
                                      std::uint64_t compileAttempts) -> Core::Result<LiveScripts>;
    struct Impl;
    explicit LiveScripts(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

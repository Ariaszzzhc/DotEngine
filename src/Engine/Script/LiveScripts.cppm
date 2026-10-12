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
    std::optional<Core::Error> error;
    std::optional<ReloadReport> reload;
  };

  struct LiveScriptReport {
    ReloadStatus source;
    ScriptReport frame;
  };

  struct LiveScriptsStats {
    std::uint64_t compileAttempts;
    std::uint64_t reloads;
    std::optional<Core::Error> error;
    SceneScriptsStats scripts;
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
    LiveScripts(const LiveScripts&) = delete;
    auto operator=(const LiveScripts&) -> LiveScripts& = delete;
    LiveScripts(LiveScripts&&) noexcept;
    auto operator=(LiveScripts&&) noexcept -> LiveScripts&;
    ~LiveScripts();

    [[nodiscard]] auto poll(bool force = false) -> Core::Result<ReloadStatus>;
    /** Start the prepared generation without polling. Calls are idempotent;
     * lifecycle failures are reported by the underlying scene service.
     * 不轮询而启动已准备代；调用幂等，生命周期错误由场景服务报告。 */
    [[nodiscard]] auto start() -> Core::Result<ScriptReport>;
    /** Poll then dispatch the old or newly committed generation; report both
     * reload diagnostics and lifecycle failures explicitly. Invalid deltas fail
     * before polling or script effects. 轮询后分发旧/已提交新代，明确报告重载诊断与
     * 生命周期错误；无效时间在轮询或脚本副作用之前失败。 */
    [[nodiscard]] auto update(double seconds) -> Core::Result<LiveScriptReport>;
    [[nodiscard]] auto fixedUpdate(double seconds) -> Core::Result<LiveScriptReport>;
    [[nodiscard]] auto stop() -> Core::Result<ScriptReport>;
    [[nodiscard]] auto stats() const -> Core::Result<LiveScriptsStats>;

  private:
    struct Impl;
    explicit LiveScripts(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

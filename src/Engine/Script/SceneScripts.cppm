module;

#include <cstdint>
#include <memory>
#include <vector>

export module Dot.Script.SceneScripts;

import Dot.Core.Result;
import Dot.World.Scene;
import Dot.Script.Compiler;
import Dot.Script.Runtime;

export namespace Dot::Script {
  enum class ScriptPhase { Prepare, Start, Update, FixedUpdate, Stop };

  struct ScriptFailure {
    World::EntityId entity;
    ScriptPhase phase;
    Core::Error error;
  };

  struct ScriptReport {
    std::vector<ScriptFailure> failures;
    std::uint32_t created{};
    std::uint32_t removed{};
    std::uint32_t calls{};
    bool realmFaulted{};
  };

  struct SceneScriptsConfig {
    RuntimeConfig runtime;
    std::uint32_t maxComponents{512};
    std::uint64_t reloadStateBytes{8 * 1024 * 1024};
  };

  struct ReloadReport {
    std::uint32_t components;
    std::uint32_t migrated;
    std::uint64_t stateBytes;
    ScriptReport retired;
  };

  struct SceneScriptsStats {
    std::uint32_t components;
    std::uint32_t pending;
    std::uint32_t running;
    std::uint32_t disabled;
    std::uint32_t failed;
    bool stopped;
    bool faulted;
    RuntimeStats runtime;
  };

  /** Owns a compiled generation/realm and shares the original scene. Operations
   * require its creating thread without reentry; quiescent destruction elsewhere
   * releases storage but never invokes script. Use stop() for explicit callbacks.
   * One authored component per entity, deterministic creation-ID order. Config
   * limits 1..5000 descriptions and reserves twice that many runtime slots for
   * replacement staging; invalid capacity combinations fail explicitly.
   * 拥有编译代/realm，共享原场景；操作限创建线程且不可重入，异线程静止销毁仅释放
   * 存储且不执行脚本，显式回调使用 stop()。每实体一个描述，按创建 ID 确定性排序；
   * 配置限制 1..5000 个描述，运行时须保留两倍实例容量用于替换准备，不匹配明确失败。 */
  class SceneScripts {
  public:
    [[nodiscard]] static auto create(std::shared_ptr<World::Scene> scene,
                                     const CompiledProgram& program,
                                     const SceneScriptsConfig& config = {})
        -> Core::Result<SceneScripts>;
    SceneScripts(const SceneScripts&) = delete;
    auto operator=(const SceneScripts&) -> SceneScripts& = delete;
    SceneScripts(SceneScripts&&) noexcept;
    auto operator=(SceneScripts&&) noexcept -> SceneScripts&;
    ~SceneScripts();

    /** Snapshot authoring and prepare all changed candidates before replacing
     * existing objects. Disabled descriptions allocate no object/load no module.
     * Initializers/constructors/hook discovery may read but cannot mutate the
     * scene. Ordinary preparation failures preserve existing bindings; realm-global
     * JS state is not rolled back. Loader/resource faults release all handles.
     * 快照编辑描述，全部候选准备成功后替换已有对象；禁用描述不分配对象/加载模块。
     * 初始化/构造/钩子查询只允许读场景，普通准备失败保留绑定，但不回滚 realm 全局
     * JS 状态；加载/资源故障释放全部句柄。 */
    [[nodiscard]] auto sync() -> Core::Result<ScriptReport>;
    /** Start once, then dispatch optional void/Promise<void> hooks. Both frame
     * operations sync authoring first; seconds must be finite in [0,1]. Changes
     * caused by callbacks are visible to later callbacks; missing owners are
     * skipped and removed. Ordinary callback failures stop/release only that
     * component; realm faults stop further dispatch. Reports explicitly contain
     * callback failures: successful dispatch is not successful script execution.
     * 启动一次后分发可选 void/Promise<void> 钩子，每次帧操作先同步编辑描述，秒数须
     * 为 [0,1] 有限值。副作用对后续回调可见，失去所有者的对象跳过并移除；普通错误
     * 仅停止/释放该组件，realm 故障中止后续分发。报告明确含回调错误，分发成功不
     * 表示脚本成功。 */
    [[nodiscard]] auto start() -> Core::Result<ScriptReport>;
    [[nodiscard]] auto update(double seconds) -> Core::Result<ScriptReport>;
    [[nodiscard]] auto fixedUpdate(double seconds) -> Core::Result<ScriptReport>;
    /** Prepare a new realm/generation before touching old state. Optional paired
     * onSaveState()/onLoadState(state) migrate bounded finite JSON for compatible
     * live descriptions. All preparation/migration denies native scene writes.
     * Save hooks must be observational: their own JS mutations cannot be rolled
     * back. Candidate failure keeps the old service; successful swap preserves
     * running status/native scene for compatible components, without old stops
     * or repeated starts. Removed/disabled/changed types stop after commit and
     * report cleanup failures; new descriptions start normally later. A faulted realm can be
     * replaced without exporting its state. 在触及旧状态前准备新 realm/编译代；兼容活动描述
     * 可经成对 onSaveState()/onLoadState(state) 迁移有界有限 JSON。全部准备/迁移禁止
     * 原生场景写入；保存钩子须只观察，自身 JS 修改不能回滚。候选失败保留旧服务，
     * 成功切换兼容组件保留状态与场景，不停止旧对象或重复启动；移除/禁用/改类型在
     * 提交后停止并报告清理错误，新描述后续正常启动；
     * 故障 realm 可跳过状态导出后替换。 */
    [[nodiscard]] auto reload(const CompiledProgram& program) -> Core::Result<ReloadReport>;
    /** Terminal/idempotent stop, invokes started components once then releases
     * handles even after failures. Native side effects are not rolled back; lost
     * entities may make cleanup callbacks fail. 终止且幂等，已启动组件停止一次后即使
     * 失败也释放句柄；不回滚原生副作用，实体消失可使清理回调失败。 */
    [[nodiscard]] auto stop() -> Core::Result<ScriptReport>;
    [[nodiscard]] auto stats() const -> Core::Result<SceneScriptsStats>;

  private:
    struct Impl;
    explicit SceneScripts(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

module;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

export module Dot.Script.Runtime;

import Dot.Core.Result;
import Dot.Core.Files;
export import Dot.Script.Host;

export namespace Dot::Script {
  struct ModuleId {
    std::uint64_t runtime{};
    std::uint64_t value{};
    auto operator==(const ModuleId&) const -> bool = default;
  };

  struct InstanceId {
    std::uint64_t runtime{};
    std::uint64_t value{};
    auto operator==(const InstanceId&) const -> bool = default;
  };

  struct RuntimeConfig {
    std::uint64_t memoryBytes{16 * 1024 * 1024};
    std::uint32_t stackBytes{128 * 1024};
    std::uint32_t sourceBytes{1024 * 1024};
    std::uint32_t jsonBytes{1024 * 1024};
    /** Project modules only; the fixed registered engine modules are counted in
     * stats but do not consume the project quota. 仅限制项目模块，固定引擎模块计入
     * 统计但不消耗项目额度。 */
    std::uint32_t maxModules{256};
    std::uint32_t maxJobs{1024};
    std::uint32_t milliseconds{100};
    std::uint32_t maxHostCalls{256};
    std::uint32_t maxInstances{1024};
  };

  struct RuntimeMemory {
    std::uint64_t bytes;
    std::uint64_t allocations;
  };

  struct RuntimeStats {
    RuntimeMemory memory;
    std::size_t modules;
    std::size_t entries;
    bool faulted;
    std::size_t instances;
  };

  /** Owned QuickJS-NG realm with a project-root ESM loader. Operations require
   * the creating thread and cannot reenter; moves preserve handles. Quiescent
   * destruction may run elsewhere. VM payload allocations, source/JSON sizes,
   * module/job counts, stack, and cooperative execution time are bounded; host
   * containers/allocator headers and filesystem wait time are not a sandbox.
   * No standard-library OS/network/timer bindings are installed.
   * 拥有 QuickJS-NG realm 与项目根目录 ESM 加载器；操作限创建线程且不可重入，移动保留
   * 句柄，静止后可异线程销毁。限制 VM 载荷分配、源码/JSON 大小、模块/任务数量、栈和
   * 协作式执行时间；宿主容器/分配头及文件系统等待不构成沙箱，不安装标准库 OS/网络/计时绑定。 */
  class Runtime {
  public:
    [[nodiscard]] static auto create(const Core::FileSystem& files,
                                     const RuntimeConfig& config = {},
                                     const HostBindings& bindings = {}) -> Core::Result<Runtime>;
    /** Parse/link static module graphs without evaluating code, constructors or
     * host callbacks. Uses the actual reserved-module/relative loader and a separate
     * bounded 256-MiB, 1024-project-module, one-second-cooperative-per-graph realm. Dynamic imports
     * are validated when executed. 不执行代码/构造器/宿主回调地解析并链接静态模块图，
     * 使用实际加载器和独立有界 realm（256 MiB、1024 项目模块、每图协作式 1
     * 秒），动态导入在执行时校验。
     */
    [[nodiscard]] static auto validateProgram(const Core::FileSystem& files,
                                              std::span<const std::string> modules)
        -> Core::Result<void>;
    Runtime(const Runtime&) = delete;
    auto operator=(const Runtime&) -> Runtime& = delete;
    Runtime(Runtime&&) noexcept;
    auto operator=(Runtime&&) noexcept -> Runtime&;
    ~Runtime();

    [[nodiscard]] static auto backendMemory() noexcept -> RuntimeMemory;
    /** Root-relative canonical UTF-8 .js/.mjs compiled entry. Project imports use
     * extensionless ./ or ../ names, resolving exactly one .js/.mjs file without
     * directory/package guessing. Engine imports use the exact registered dot:
     * namespace (dot:engine and native dot:host); no filesystem fallback. Registered
     * services use invoke; missing services fail explicitly. Host calls share one
     * operation limit. Modules cache per realm; top-level await uses bounded jobs.
     * Load/resolution failures fault the realm; create another to recover.
     * 根相对规范 UTF-8 .js/.mjs 编译入口；项目导入使用无扩展名 ./ 或 ../ 名称，须唯一
     * 对应 .js/.mjs 文件，不猜测目录或包；引擎导入为精确注册 dot: 名称（dot:engine、
     * 原生 dot:host），不回退磁盘。invoke 访问服务，缺失服务明确失败；宿主调用共用每
     * 操作限额。模块在 realm 内缓存，顶层 await 使用有界任务；加载/解析失败须新建 realm。 */
    [[nodiscard]] auto loadModule(std::string_view relative) -> Core::Result<ModuleId>;
    /** Invoke an exported function with a JSON array of at most 64 arguments.
     * Results must be finite, JSON-serializable values without omitted undefined/
     * function/symbol fields; top-level undefined is explicitly nullopt. Promises
     * must settle through bounded jobs. Ordinary clean JS exceptions are recoverable
     * but do not roll back script mutations. VM allocation/time/job limits, unresolved
     * promises, unhandled rejections, and loader failures fault the realm.
     * 以最多 64 个 JSON 数组参数调用导出函数；结果须有限且可序列化，不省略 undefined/
     * 函数/符号字段，顶层 undefined 明确返回 nullopt。Promise 须通过有界任务完成；
     * 无待执行任务的普通 JS 异常可恢复但不回滚脚本修改，资源/时间/任务超限、未完成
     * Promise、未处理拒绝及加载失败使 realm 故障。 */
    [[nodiscard]] auto call(ModuleId module, std::string_view function,
                            std::string_view arguments = "[]")
        -> Core::Result<std::optional<std::string>>;
    /** Construct an exported synchronous class/function with up to 64 JSON
     * arguments, retaining its actual object and prototype/private state. Promise
     * results are rejected after bounded job draining. Capacity is checked before
     * constructor side effects; ordinary constructor errors preserve the realm
     * but do not roll back earlier script/native mutations. Handles are scoped
     * and never reused. A script may deliberately return an aliased object.
     * 以最多 64 项 JSON 参数构造导出的同步类/函数，保留实际对象、原型与私有状态；
     * 有界清理任务后拒绝 Promise 结果，在构造副作用前检查容量。普通构造错误保留
     * realm，但不回滚此前脚本/原生修改；句柄有范围且不复用，脚本可刻意返回别名对象。 */
    [[nodiscard]] auto createInstance(ModuleId module, std::string_view constructor,
                                      std::string_view arguments = "[]")
        -> Core::Result<InstanceId>;
    /** Invoke with the owned object as this, including inherited methods and
     * bounded asynchronous results. Argument validation precedes property lookup;
     * getters may execute script under the same operation budget. Missing or
     * noncallable methods fail explicitly. 使用拥有的对象作为 this，支持继承方法与
     * 有界异步结果；属性查询前校验参数，getter 在同一预算内执行，缺失/不可调用方法失败。 */
    [[nodiscard]] auto callMethod(InstanceId instance, std::string_view method,
                                  std::string_view arguments = "[]")
        -> Core::Result<std::optional<std::string>>;
    /** Optional hook discovery; undefined is absent, present nonfunctions are
     * errors. Getters/jobs use normal budgets; discovery is not a pure inspection.
     * 可选钩子查询，undefined 表示缺失，存在但非函数则失败；getter/任务使用通常预算，
     * 查询并非纯检查。 */
    [[nodiscard]] auto hasMethod(InstanceId instance, std::string_view method)
        -> Core::Result<bool>;
    /** Release only the runtime's strong object reference without invoking script.
     * Other script references/cycles may retain it. Allowed on a faulted realm for
     * cleanup; requires the owner thread without reentry. 仅释放运行时的对象强引用，
     * 不调用脚本，其它脚本引用/环可继续保留对象；故障后可清理，须所有者线程且不可重入。 */
    [[nodiscard]] auto releaseInstance(InstanceId instance) -> Core::Result<void>;
    [[nodiscard]] auto stats() const -> Core::Result<RuntimeStats>;

  private:
    struct Impl;
    explicit Runtime(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

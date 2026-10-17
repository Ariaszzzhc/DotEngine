module;

#include <memory>

export module Dot.Script.Compilation;

import Dot.Core.Result;
import Dot.Script.Compiler;

export namespace Dot::Script {
  /** One owned immutable compilation, never an active scene/runtime. Queries and
   * take require the creating thread; readiness never waits for compilation.
   * Take consumes completion once. Quiescent stop/destruction may run elsewhere;
   * destruction cancels/joins and reaps the owned compiler, never detaches it.
   * 单个拥有的不可变编译任务，不接触活动场景/虚拟机；查询和提取限创建线程，
   * 就绪检查不等待编译，结果仅提取一次。静止时可异线程请求停止/销毁；销毁取消并
   * 回收工作线程和编译进程，不分离线程。 */
  class CompilationJob {
  public:
    [[nodiscard]] static auto begin(const TypeScriptCompiler& compiler,
                                    const SourceSnapshot& source) -> Core::Result<CompilationJob>;
    CompilationJob(const CompilationJob&) = delete;
    auto operator=(const CompilationJob&) -> CompilationJob& = delete;
    CompilationJob(CompilationJob&&) noexcept;
    auto operator=(CompilationJob&&) noexcept -> CompilationJob&;
    ~CompilationJob();
    [[nodiscard]] auto ready() const -> Core::Result<bool>;
    [[nodiscard]] auto source() const -> Core::Result<SourceSnapshot>;
    [[nodiscard]] auto take() -> Core::Result<CompiledProgram>;
    auto requestStop() noexcept -> void;

  private:
    struct Impl;
    explicit CompilationJob(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

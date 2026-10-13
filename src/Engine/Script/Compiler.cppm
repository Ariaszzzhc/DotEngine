module;

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Script.Compiler;

import Dot.Core.Result;
import Dot.Core.Files;

export namespace Dot::Script {
  struct CompilerLimits {
    std::uint32_t sourceFiles{1024};
    std::uint32_t directoryEntries{8192};
    std::uint64_t fileBytes{1024 * 1024};
    std::uint64_t sourceBytes{16 * 1024 * 1024};
    std::uint64_t outputBytes{32 * 1024 * 1024};
    std::uint64_t diagnosticBytes{1024 * 1024};
    std::chrono::milliseconds timeout{10000};
  };

  struct CompiledStats {
    std::uint32_t sourceFiles;
    std::uint64_t sourceBytes;
    std::uint64_t outputBytes;
  };

  /** Immutable bounded source capture. Exact content/path comparisons avoid
   * timestamp-only changes/hash collisions. Captures are tied to no open files;
   * compileSnapshot emits exactly these bytes despite subsequent authoring edits.
   * 不可变有界源码捕获，精确内容/路径比较避免仅时间戳变化和哈希碰撞；不持有打开的
   * 文件，compileSnapshot 即使随后编辑改变，也严格编译这些字节。 */
  class SourceSnapshot {
  public:
    SourceSnapshot(const SourceSnapshot&) noexcept;
    auto operator=(const SourceSnapshot&) noexcept -> SourceSnapshot&;
    SourceSnapshot(SourceSnapshot&&) noexcept;
    auto operator=(SourceSnapshot&&) noexcept -> SourceSnapshot&;
    ~SourceSnapshot();
    [[nodiscard]] auto equals(const SourceSnapshot& other) const noexcept -> bool;

  private:
    struct Impl;
    explicit SourceSnapshot(std::shared_ptr<const Impl> impl) noexcept;
    std::shared_ptr<const Impl> m_impl;
    friend class TypeScriptCompiler;
  };

  /** Process-wide live generation owners, observable when calls are quiescent.
   * 无活动调用时可观察的进程内存活代所有权计数。 */
  [[nodiscard]] auto compilerGenerations() noexcept -> std::uint64_t;

  /** Shared ownership of an independent source/configuration/output generation.
   * Last-owner destruction removes its temporary directory, best effort. Keep a
   * copy alive for every Runtime using files(); a FileSystem alone owns no files.
   * Callers must not modify the generation. Queries on moved-from objects fail.
   * 共享拥有独立的源码/配置/输出代；最后所有者销毁时尽力删除临时目录。每个使用
   * files() 的 Runtime 须保持一个副本存活，FileSystem 本身不拥有文件。调用者不得
   * 修改此代内容，移后对象查询失败。 */
  class CompiledProgram {
  public:
    CompiledProgram(const CompiledProgram&) noexcept;
    auto operator=(const CompiledProgram&) noexcept -> CompiledProgram&;
    CompiledProgram(CompiledProgram&&) noexcept;
    auto operator=(CompiledProgram&&) noexcept -> CompiledProgram&;
    ~CompiledProgram();

    [[nodiscard]] auto files() const -> Core::Result<Core::FileSystem>;
    [[nodiscard]] auto modules() const -> Core::Result<std::vector<std::string>>;
    [[nodiscard]] auto diagnostics() const -> Core::Result<std::string>;
    [[nodiscard]] auto stats() const -> Core::Result<CompiledStats>;

    /** Atomically publish portable JS/MJS modules plus a strict version-1
     * manifest into a new directory within default load limits. Source maps and
     * TypeScript are omitted. Existing destinations remain untouched; the caller
     * owns the published directory.
     * 将可移植 JS/MJS 模块及严格版本 1 清单原子发布到新目录，省略源码映射/TypeScript；
     * 发布须满足默认加载限额；不覆盖已有目标，发布目录由调用者持有。 */
    [[nodiscard]] auto publish(const Core::FileSystem& parent,
                               const std::filesystem::path& folder) const
        -> Core::Result<Core::FileSystem>;
    /** Validate a bounded published tree and copy its ESM bytes into an owned
     * private generation without any compiler/tool/process. Later package edits
     * do not change this snapshot. Requires serialized input files; manifests
     * validate shape/count/size, not code authenticity or hostile races.
     * 校验有界发布树并将 ESM 字节复制到拥有的私有代，不运行编译器/工具/进程；后续包
     * 修改不改变快照。输入修改须串行，清单校验结构/数量/大小，不保证代码真实性或恶意竞争隔离。 */
    [[nodiscard]] static auto loadPublished(const Core::FileSystem& package,
                                            const CompilerLimits& limits = {})
        -> Core::Result<CompiledProgram>;

  private:
    struct Impl;
    explicit CompiledProgram(std::shared_ptr<const Impl> impl) noexcept;
    std::shared_ptr<const Impl> m_impl;
    friend class TypeScriptCompiler;
  };

  /** Explicit standalone TypeScript 7.0.2 tool and standard declarations beside
   * it. No Node, shell, PATH search, compiler installation, or environment edits.
   * Calls are independent and may run concurrently on immutable project input.
   * 显式指定独立 TypeScript 7.0.2 工具，其旁须有标准声明；不使用 Node/shell/PATH
   * 查找、安装或环境修改，项目输入不变时可并发独立调用。 */
  class TypeScriptCompiler {
  public:
    [[nodiscard]] static auto create(const std::filesystem::path& executable,
                                     const CompilerLimits& limits = {})
        -> Core::Result<TypeScriptCompiler>;
    /** Copy bounded UTF-8 .ts/.mts files (including declarations) from a canonical
     * root-relative directory into a private generation. Reject symlinks, special
     * files, .tsx/.cts/.js/.mjs/.jsx sources and directory depth above 32; other
     * regular files are ignored. An explicit file list, noResolve and no ambient
     * package types prevent adding sources outside the snapshot. Project tsconfig
     * is never used. Emit strict ES2022 ESM and source maps with original sources.
     * Inject engine-owned dot: declarations outside project sources/statistics.
     * Resolve the emitted static graph without executing code before publication;
     * relative imports omit extensions, including .mts through private type aliases.
     * Dynamic imports are checked when executed by the same runtime resolver.
     * Exit/output/timeout/type errors return real context and discard the entire
     * candidate; existing generations and source files are untouched. No source
     * edits may race capture. Limits bound capture/output inspection and process
     * time/diagnostics; they are not OS memory/disk quotas or hostile isolation.
     * 将规范相对目录内有界 UTF-8 .ts/.mts（含声明）复制到私有代，拒绝符号链接、特殊
     * 文件、.tsx/.cts/.js/.mjs/.jsx 源码及超过 32 层的目录，其余普通文件忽略。显式
     * 文件清单、noResolve 与禁用环境包类型避免添加快照外源码，不采用项目 tsconfig。
     * 注入引擎拥有的 dot: 声明，不计项目源码统计；发布前只链接静态导入图而不执行代码。
     * 相对导入不含扩展名，.mts 使用私有类型映射；动态导入执行时由共用解析器校验。
     * 输出严格 ES2022 ESM 与内含原源码的映射；退出/输出/超时/类型错误保留真实上下文
     * 并丢弃整代，已有代及源文件不变。捕获时禁止并发改源码；限制捕获/输出检查及进程
     * 时间/诊断，不构成操作系统内存/磁盘配额或恶意隔离。 */
    [[nodiscard]] auto compileProject(const Core::FileSystem& project,
                                      std::string_view directory = "scripts") const
        -> Core::Result<CompiledProgram>;
    [[nodiscard]] auto captureProject(const Core::FileSystem& project,
                                      std::string_view directory = "scripts") const
        -> Core::Result<SourceSnapshot>;
    /** Revalidate source count/bytes, then emit without reading current authoring
     * files; enumeration/depth limits apply at capture time. 重新校验源码数量/字节，
     * 不读当前编辑文件；枚举/深度限制在捕获时应用。 */
    [[nodiscard]] auto compileSnapshot(const SourceSnapshot& snapshot) const
        -> Core::Result<CompiledProgram>;

  private:
    TypeScriptCompiler(std::filesystem::path executable, CompilerLimits limits);
    std::filesystem::path m_executable;
    CompilerLimits m_limits;
  };
}  // namespace Dot::Script

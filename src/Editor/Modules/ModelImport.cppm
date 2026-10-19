module;

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

export module Dot.Editor.ModelImport;

import Dot.Core.Result;
import Dot.Project;
export import Dot.Editor.Resources;

export namespace Dot::Editor {
  enum class ModelImportState {
    Preparing,
    Ready,
    Publishing,
    Published,
    Cancelling,
    Cancelled,
    Failed
  };

  struct ModelImportStatus {
    ModelImportState state;
    std::uint64_t revision;
    std::string path;
    std::optional<ModelImportReport> result;
    std::optional<Core::Error> error;
  };

  /** One owned worker prepares an unpublished native bundle outside the live resource catalog.
   * Begin/status/publish/cancel perform memory operations on the creator; explicit publish
   * accepts intent, the worker rechecks metadata/path/capacity before no-replace publication.
   * Ready is not publication. Cancellation is cooperative around bounded codec calls and
   * finishes only after cleanup; publishing cannot be cancelled or imply rollback. Shutdown
   * cancels unpublished work and joins, possibly waiting for the current bounded codec call;
   * accepted publication finishes. Source writers must be serialized by the caller. No
   * World/device handles cross threads and no source/project fallback is created. Limits are
   * the model decoder/resource catalog limits, with at most one owned bundle per instance.
   * 单个拥有的工作线程在实时资源目录外准备未发布原生包；开始/状态/发布/取消仅在创建线程
   * 操作内存。显式发布仅接受意图，工作线程复核描述/路径/容量后不替换发布；就绪不等于发布。
   * 取消在有界编解码调用之间协作，清理后才结束；发布中不可取消或伪称回滚。销毁取消未发布
   * 工作并回收线程，可能等待当前有界调用；已接受发布须完成。调用者串行源文件写入，不跨线程
   * 传递 World/设备句柄，不创建替代源/项目；遵守模型/目录限额，每实例至多拥有一个原生包。 */
  class ModelImport {
  public:
    [[nodiscard]] static auto begin(const Project::Project& project,
                                    const std::filesystem::path& source, std::string destination,
                                    std::optional<std::uint32_t> scene = {},
                                    const ResourceLimits& limits = {}) -> Core::Result<ModelImport>;
    ModelImport(const ModelImport&) = delete;
    auto operator=(const ModelImport&) -> ModelImport& = delete;
    ModelImport(ModelImport&&) noexcept;
    auto operator=(ModelImport&&) noexcept -> ModelImport&;
    ~ModelImport();
    [[nodiscard]] auto status() const -> Core::Result<ModelImportStatus>;
    [[nodiscard]] auto publish(std::uint64_t revision) -> Core::Result<void>;
    [[nodiscard]] auto cancel(std::uint64_t revision) -> Core::Result<void>;

  private:
    struct Impl;
    explicit ModelImport(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Editor

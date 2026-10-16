module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module Dot.Script.SceneFlow;

export import Dot.Script.Host;
import Dot.Core.Result;

export namespace Dot::Script {
  enum class SceneRequestState { Pending, Loading, Succeeded, Failed, Cancelled };

  struct SceneLocation {
    std::uint64_t scope;
    std::optional<std::string> path;
  };

  struct SceneRequest {
    std::uint64_t id;
    std::string path;
    SceneRequestState state{SceneRequestState::Pending};
    bool committed{};
    std::optional<Core::Error> error;
  };

  /** Owned frame-boundary request queue; never loads files or executes scripts.
   * One pending/loading request, with the last 16 records retained. Requests from
   * retired scene scopes fail. Copies/bindings share storage; calls require the
   * creating thread. Bound current reads identify that realm's own scene, including
   * during preparation. Only the native scheduler may begin/commit/fail/retire.
   * 拥有帧边界请求队列，不加载文件或执行脚本；至多一个待处理/加载请求，保留最近 16 条
   * 记录。旧场景范围不能再提交请求；副本/绑定共享存储，调用限创建线程。绑定的 current
   * 查询始终标识该 realm 自身场景（包括准备期），仅原生调度器可开始/提交/失败/退役。 */
  class SceneFlow {
  public:
    [[nodiscard]] static auto create(std::uint64_t scope, std::optional<std::string> path)
        -> Core::Result<SceneFlow>;
    SceneFlow(const SceneFlow&) noexcept;
    auto operator=(const SceneFlow&) noexcept -> SceneFlow&;
    SceneFlow(SceneFlow&&) noexcept;
    auto operator=(SceneFlow&&) noexcept -> SceneFlow&;
    ~SceneFlow();

    [[nodiscard]] auto location() const -> Core::Result<SceneLocation>;
    [[nodiscard]] auto request(std::string_view path, std::uint64_t scope)
        -> Core::Result<std::uint64_t>;
    [[nodiscard]] auto status(std::uint64_t id) const -> Core::Result<SceneRequest>;
    [[nodiscard]] auto latest() const -> Core::Result<std::optional<SceneRequest>>;
    [[nodiscard]] auto pending() const -> Core::Result<std::optional<SceneRequest>>;
    [[nodiscard]] auto begin(std::uint64_t id) -> Core::Result<void>;
    /** Publish owned prepared path/scope without allocating on the success path.
     * New Start callbacks may queue the following request; no recursive dispatch.
     * 用已准备的拥有路径/范围发布，成功路径不分配；新 Start 可排队下一请求，不递归调度。 */
    [[nodiscard]] auto commit(std::uint64_t id, std::uint64_t scope, std::string path)
        -> Core::Result<void>;
    [[nodiscard]] auto fail(std::uint64_t id, Core::Error error) -> Core::Result<void>;
    /** Quiescent owner retirement, also usable on another destruction thread.
     * Cancel uncommitted work; retained reads remain available. No hooks run.
     * 静止拥有者退役也可用于异线程销毁；取消未提交工作，保留查询，不执行钩子。 */
    auto retire() noexcept -> void;
    [[nodiscard]] auto bindings(std::uint64_t scope, std::optional<std::string> path) const
        -> Core::Result<HostBindings>;

  private:
    struct Impl;
    explicit SceneFlow(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

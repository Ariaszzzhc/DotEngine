module;

#include <memory>
#include <span>

export module Dot.Script.AnimationSession;

export import Dot.Script.Host;
import Dot.Core.Result;
import Dot.Core.Files;
import Dot.World.Scene;
import Dot.Animation.ScenePlayers;

export namespace Dot::Script {
  /** Owns native scene players and their shared scene. Callbacks retain service
   * ownership after moves/destruction of the wrapper; all calls require its owner
   * thread and original scene scope. Reads are observational, controls ReadWrite
   * and denied during script preparation/migration. No script-side scheduling.
   * 拥有原生播放器及共享场景；包装器移动/销毁后回调仍持有服务。调用限创建线程及最初
   * 场景范围；查询只观察，控制为 ReadWrite，脚本准备/迁移期禁止，无脚本侧调度。 */
  class AnimationSession {
  public:
    [[nodiscard]] static auto create(std::shared_ptr<World::Scene> scene,
                                     const Core::FileSystem& files)
        -> Core::Result<AnimationSession>;
    /** Complete initial decoded libraries, with native validation and no initial
     * disk fallback. Later description changes retain ordinary file-refresh rules.
     * 完整初始解码库，保留原生校验且不回退首次磁盘加载；后续描述修改仍按通常规则重读文件。 */
    [[nodiscard]] static auto createPrepared(std::shared_ptr<World::Scene> scene,
                                             const Core::FileSystem& files,
                                             std::span<const Animation::ClipAsset> decoded)
        -> Core::Result<AnimationSession>;
    AnimationSession(const AnimationSession&) = delete;
    auto operator=(const AnimationSession&) -> AnimationSession& = delete;
    AnimationSession(AnimationSession&&) noexcept;
    auto operator=(AnimationSession&&) noexcept -> AnimationSession&;
    ~AnimationSession();
    /** Native scheduling only; scripts cannot advance or recreate their players.
     * 仅原生调度，脚本不能推进或重建自己的播放器。 */
    [[nodiscard]] auto advance(double seconds) -> Core::Result<void>;
    [[nodiscard]] auto bindings() const -> Core::Result<HostBindings>;

  private:
    [[nodiscard]] static auto createSession(std::shared_ptr<World::Scene> scene,
                                            const Core::FileSystem& files,
                                            std::span<const Animation::ClipAsset> decoded,
                                            bool prepared) -> Core::Result<AnimationSession>;
    struct Impl;
    explicit AnimationSession(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

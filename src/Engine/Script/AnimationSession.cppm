module;

#include <memory>
#include <span>
#include <string>
#include <string_view>

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
    /** Complete initial decoded libraries/controllers, with native validation and no initial
     * disk fallback. Later description changes retain ordinary file-refresh rules.
     * 完整初始解码片段/状态机，保留原生校验且不回退首次磁盘加载；后续描述修改仍按通常规则重读文件。
     */
    [[nodiscard]] static auto createPrepared(std::shared_ptr<World::Scene> scene,
                                             const Core::FileSystem& files,
                                             std::span<const Animation::ClipAsset> decoded,
                                             std::span<const Animation::ControllerAsset> controllers
                                             = {}) -> Core::Result<AnimationSession>;
    AnimationSession(const AnimationSession&) = delete;
    auto operator=(const AnimationSession&) -> AnimationSession& = delete;
    AnimationSession(AnimationSession&&) noexcept;
    auto operator=(AnimationSession&&) noexcept -> AnimationSession&;
    ~AnimationSession();
    /** Native scheduling only; scripts cannot advance or recreate their players.
     * 仅原生调度，脚本不能推进或重建自己的播放器。 */
    [[nodiscard]] auto advance(double seconds) -> Core::Result<void>;
    [[nodiscard]] auto bindings() const -> Core::Result<HostBindings>;
    /** The same observational native state transport used by the script SDK.
     * 与脚本 SDK 共用的只读原生状态传输。 */
    [[nodiscard]] auto inspect(World::EntityId owner) const -> Core::Result<std::string>;
    [[nodiscard]] auto state(World::EntityId owner) const
        -> Core::Result<Animation::ScenePlayerState>;
    [[nodiscard]] auto parameters(World::EntityId owner,
                                  std::span<const Animation::ControllerParameter> parameters)
        -> Core::Result<void>;
    [[nodiscard]] auto resetController(World::EntityId owner) -> Core::Result<void>;
    [[nodiscard]] auto controlLayer(World::EntityId owner, std::string_view name,
                                    const Animation::LayerControl& operation) -> Core::Result<void>;

  private:
    [[nodiscard]] static auto createSession(std::shared_ptr<World::Scene> scene,
                                            const Core::FileSystem& files,
                                            std::span<const Animation::ClipAsset> decoded,
                                            std::span<const Animation::ControllerAsset> controllers,
                                            bool prepared) -> Core::Result<AnimationSession>;
    struct Impl;
    explicit AnimationSession(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

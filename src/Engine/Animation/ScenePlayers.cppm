module;

#include <cstddef>
#include <memory>

export module Dot.Animation.ScenePlayers;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.World.Scene;

export namespace Dot::Animation {
  struct ScenePlayerStats {
    std::size_t players{};
    std::size_t channels{};
    std::size_t keys{};
  };

  /** Owns playback and a shared scene, no window/device/wall clock. Synchronizes
   * authoring additions/removals/replacements before variable updates; unchanged
   * players retain clocks/rest poses. New descriptions restart; assets refresh
   * on replacement. Conflicting writers and rigid bodies on animated nodes or
   * ancestors fail explicitly. Each player's pose/clock commit is atomic; earlier
   * players are not rolled back if a later player fails. Owner-thread API.
   * 拥有播放及共享场景，不创建窗口/设备/时钟。变量帧前同步增删改，未变播放器保留时钟及
   * 初始姿态，新描述重启并重读资产。拒绝冲突写入及动画节点/祖先刚体。每个播放器的姿态/
   * 时钟原子提交，后者失败不回滚此前播放器。API 限创建线程。 */
  class ScenePlayers {
  public:
    [[nodiscard]] static auto create(std::shared_ptr<World::Scene> scene,
                                     const Core::FileSystem& assets) -> Core::Result<ScenePlayers>;
    ScenePlayers(const ScenePlayers&) = delete;
    auto operator=(const ScenePlayers&) -> ScenePlayers& = delete;
    ScenePlayers(ScenePlayers&&) noexcept;
    auto operator=(ScenePlayers&&) noexcept -> ScenePlayers&;
    ~ScenePlayers();
    /** Finite seconds [0,1]. First advance applies auto-play start pose (reverse
     * starts at end); paused players leave authored pose untouched.
     * 有限秒数 [0,1]；首次推进应用自动播放起始姿态，反向从末尾开始，暂停保留编辑姿态。 */
    [[nodiscard]] auto advance(double seconds) -> Core::Result<void>;
    [[nodiscard]] auto stats() const -> Core::Result<ScenePlayerStats>;

  private:
    struct Impl;
    explicit ScenePlayers(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Animation

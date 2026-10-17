module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

export module Dot.Animation.ScenePlayers;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Animation.Clip;
import Dot.World.Scene;
export import Dot.Animation.SceneAnimator;

export namespace Dot::Animation {
  struct ClipAsset {
    std::string asset;
    Clip value;
  };

  struct ScenePlayerStats {
    std::size_t players{};
    std::size_t channels{};
    std::size_t keys{};
  };

  struct ScenePlayerState {
    std::uint32_t clip;
    std::string asset;
    std::string name;
    double duration;
    PlaybackState playback;
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
    /** Optional immutable decoded assets replace disk loads by exact path for this
     * service lifetime. At most 1024 unique portable .dotanimation paths under assets;
     * each must occur in an enabled player's initial library. Native binding,
     * sampling, aggregate and writer-authority checks remain unchanged. No files
     * are created or changed; the service owns copies of the shared clip handles.
     * 可选不可变解码资源按精确路径替代本服务生命周期内的磁盘加载。至多 1024 个唯一
     * 可移植 assets 下 .dotanimation，均须用于初始启用播放器库；保留原生绑定、采样、
     * 总量和写入权限校验，不创建/修改文件，服务拥有共享片段句柄的副本。 */
    [[nodiscard]] static auto create(std::shared_ptr<World::Scene> scene,
                                     const Core::FileSystem& assets,
                                     std::span<const ClipAsset> decoded = {})
        -> Core::Result<ScenePlayers>;
    /** Require complete initial decoded libraries without disk fallback. Validate
     * native bindings/authority normally, then release the preparation cache;
     * later description replacements load current files. Keep the existing scene
     * limits: 1024 clip references, 30000 channels and 1000000 keys.
     * 要求完整初始解码库，不回退磁盘；保留原生绑定/权限校验，随后释放准备缓存，
     * 后续描述替换重读当前文件；保留既有场景限制：1024 片段引用、30000 通道、1000000 键。 */
    [[nodiscard]] static auto createPrepared(std::shared_ptr<World::Scene> scene,
                                             const Core::FileSystem& assets,
                                             std::span<const ClipAsset> decoded)
        -> Core::Result<ScenePlayers>;
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
    /** Observational snapshot of the last synchronized player. Missing/disabled,
     * changed descriptions, foreign handles and retired scene scopes fail.
     * 最近同步播放器的只读快照；缺失/禁用、描述已改、跨场景句柄及失效范围明确失败。 */
    [[nodiscard]] auto state(World::EntityId owner) const -> Core::Result<ScenePlayerState>;
    /** Controls synchronize descriptions first, without advancing a clock. They
     * never modify saved descriptions. Clip changes restore old animated fields
     * and apply the new start atomically; same-clip play resumes unless restart.
     * Reverse restart starts at the end. Stop restores original binding poses.
     * 控制先同步描述，不推进时钟、不改保存描述。换片原子恢复旧通道并应用新起点；同片段
     * 默认恢复播放，restart 才重启，反向从末尾开始。停止恢复最初绑定姿态。 */
    [[nodiscard]] auto play(World::EntityId owner, std::optional<std::uint32_t> clip = {},
                            bool restart = false) -> Core::Result<void>;
    /** Fade to a library clip's directional start without editing saved data.
     * Reserve the union of outgoing/incoming entity writers, including interrupted
     * channels, until successful completion/seek/stop/switch. Validate competing
     * players and rigid-body ancestors before changing pose or reservations.
     * Same-clip fades restart; zero seconds switches immediately. Pause retains
     * reservations. Invalid updates retain both clocks, pose and writer ownership.
     * 淡化到库内片段正/反向起点，不改保存描述；保留源/目标及被打断通道的实体写入权，
     * 至成功完成/seek/stop/换片后释放。变更前校验其他播放器及刚体祖先；同片淡化重启，
     * 零时长立即切换。暂停仍保留写入权，失败保留双时钟、姿态及所有权。 */
    [[nodiscard]] auto crossFade(World::EntityId owner, std::uint32_t clip, double seconds)
        -> Core::Result<void>;
    [[nodiscard]] auto pause(World::EntityId owner) -> Core::Result<void>;
    [[nodiscard]] auto stop(World::EntityId owner) -> Core::Result<void>;
    [[nodiscard]] auto seek(World::EntityId owner, double seconds) -> Core::Result<void>;
    [[nodiscard]] auto configure(World::EntityId owner, double speed, bool looping)
        -> Core::Result<void>;

  private:
    [[nodiscard]] static auto createPlayers(std::shared_ptr<World::Scene> scene,
                                            const Core::FileSystem& assets,
                                            std::span<const ClipAsset> decoded, bool prepared)
        -> Core::Result<ScenePlayers>;
    struct Impl;
    explicit ScenePlayers(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Animation

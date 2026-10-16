module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module Dot.Editor.AnimationPreview;

import Dot.Animation.ScenePlayers;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.World.Scene;

export namespace Dot::Editor {
  struct AnimationPreviewStats {
    std::uint64_t owner;
    std::uint64_t source;
    std::uint64_t generation;
    Animation::ScenePlayerState player;
    std::vector<std::string> clips;
    std::optional<Core::Error> error;
    bool bindPose{};
    std::optional<std::string> draft{};
  };

  /** Isolated animation audition using the same native players as game sessions.
   * Clone the complete scene, disable auto-play in the clone and pause every other
   * player. Their writer reservations and physics authority remain validated.
   * No scripts, physics simulation, audio, window or renderer is initialized.
   * Source scene/animation files may be retired after creation; clips and poses are owned.
   * Owner-thread API; moves preserve borrowed preview scene addresses.
   * 使用游戏会话共用的原生播放器独立预览动画。克隆完整场景，关闭克隆内自动播放，
   * 其他播放器保持暂停，仍校验写入权与物理权限。不初始化脚本、物理模拟、音频或窗口/
   * 渲染器。创建后可销毁源场景/动画文件，片段与姿态由预览拥有；操作限创建线程，移动保留场景借用。
   */
  class AnimationPreview {
  public:
    /** Require an enabled player. Start paused at the selected directional start.
     * A decoded draft must belong to its library; select it in the clone only and
     * retain it through native ScenePlayers, without temporary files or source writes.
     * 要求启用的播放器，初始暂停在片段正/反向起点。可选解码草稿须属于库，仅在克隆中
     * 选中它，通过原生播放器持有，不创建临时文件、不修改源场景。 */
    [[nodiscard]] static auto create(const World::Scene& source, const Core::FileSystem& assets,
                                     std::uint64_t owner,
                                     std::optional<Animation::ClipAsset> draft = {})
        -> Core::Result<AnimationPreview>;
    AnimationPreview(const AnimationPreview&) = delete;
    auto operator=(const AnimationPreview&) -> AnimationPreview& = delete;
    AnimationPreview(AnimationPreview&&) noexcept;
    auto operator=(AnimationPreview&&) noexcept -> AnimationPreview&;
    ~AnimationPreview();
    [[nodiscard]] auto play(std::uint32_t clip, bool restart) -> Core::Result<void>;
    [[nodiscard]] auto pause() -> Core::Result<void>;
    /** Seek within [0,duration], preserving paused/playing state. Bind pose resets
     * animated fields and pauses; neither operation changes saved descriptions.
     * 在 [0,时长] 内定位，保留播放/暂停状态；bindPose 恢复动画字段并暂停，均不修改保存描述。 */
    [[nodiscard]] auto seek(double seconds) -> Core::Result<void>;
    [[nodiscard]] auto bindPose() -> Core::Result<void>;
    [[nodiscard]] auto configure(double speed, bool looping) -> Core::Result<void>;
    /** Explicit elapsed seconds [0,1], without a hidden wall clock. Native sample
     * failures retain the last valid pose and fault audition until recreation.
     * Invalid controls preserve pose/state; generations invalidate stale readbacks.
     * 显式经过秒数 [0,1]，无隐藏时钟。原生采样失败保留有效姿态并使预览故障直至重建；
     * 无效控制保留姿态/状态，代数用于拒绝过期读回。 */
    [[nodiscard]] auto advance(double seconds) -> Core::Result<void>;
    [[nodiscard]] auto stats() const -> Core::Result<AnimationPreviewStats>;
    [[nodiscard]] auto scene() const -> Core::Result<const World::Scene*>;

  private:
    struct Impl;
    explicit AnimationPreview(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Editor

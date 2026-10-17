module;

#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>

export module Dot.Project.ScenePreparation;

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.World.Scene;
import Dot.Animation.ScenePlayers;

export namespace Dot::Project {
  class ScenePreparation;

  /** Immutable proof of CPU graphics/skin validation and owned initial animation
   * clips for exact scene bytes/root. Animation binding/authority still validate
   * on the owner; only immutable clips cross threads.
   * Rehydration creates a scene on the calling thread, never transfers thread-bound
   * World handles. No GPU residency proof. Files remain caller-owned: serialize
   * asset edits through publication, as with synchronous preflight.
   * 精确场景字节/根目录的不可变 CPU 图形/蒙皮校验结果及初始动画片段；绑定/权限仍由
   * 所有者校验，仅不可变片段跨线程。在调用线程重建场景，不转移
   * 线程绑定 World 句柄。不保证 GPU 驻留；文件由调用者拥有，须串行修改直至发布。 */
  class PreparedScene {
  public:
    PreparedScene(const PreparedScene&) noexcept;
    auto operator=(const PreparedScene&) noexcept -> PreparedScene&;
    PreparedScene(PreparedScene&&) noexcept;
    auto operator=(PreparedScene&&) noexcept -> PreparedScene&;
    ~PreparedScene();
    [[nodiscard]] auto scene() const -> Core::Result<World::Scene>;
    /** Borrowed until the last owning proof releases. Consumers copy immutable
     * clip handles before publication; no worker scene/player handles escape.
     * 借用至最后一个拥有结果释放；使用者在发布前复制不可变片段，不泄漏工作线程场景/播放器。 */
    [[nodiscard]] auto animationClips() const
        -> Core::Result<std::span<const Animation::ClipAsset>>;
    [[nodiscard]] auto accepts(const Core::FileSystem& files, const World::Scene& scene) const
        -> Core::Result<bool>;

  private:
    struct Data;
    explicit PreparedScene(std::shared_ptr<const Data> data) noexcept;
    [[nodiscard]] static auto validate(const Core::FileSystem& files, const std::string& path,
                                       const std::optional<std::string>& initial,
                                       std::stop_token stop) -> Core::Result<PreparedScene>;
    friend class ScenePreparation;
    std::shared_ptr<const Data> m_data;
  };

  /** One owned CPU worker; no active scene, hooks, VM, audio or device access.
   * Creator-thread ready/take never wait for decoding. Result is consumed once;
   * stop/destruction cancel between bounded files and join, without detaching.
   * Optional initial bytes preserve an unsaved initial scene's return semantics.
   * 单个拥有的 CPU 工作线程，不接触活动场景、钩子、VM、音频或设备；创建线程就绪查询/
   * 提取不等待解码，结果仅提取一次。停止/销毁在有界文件间取消并回收，不分离线程；
   * 可选初始字节保留返回未保存初始场景的语义。 */
  class ScenePreparation {
  public:
    [[nodiscard]] static auto begin(const Core::FileSystem& files, std::string path,
                                    std::optional<std::string> initial = {})
        -> Core::Result<ScenePreparation>;
    ScenePreparation(const ScenePreparation&) = delete;
    auto operator=(const ScenePreparation&) -> ScenePreparation& = delete;
    ScenePreparation(ScenePreparation&&) noexcept;
    auto operator=(ScenePreparation&&) noexcept -> ScenePreparation&;
    ~ScenePreparation();
    [[nodiscard]] auto ready() const -> Core::Result<bool>;
    [[nodiscard]] auto take() -> Core::Result<PreparedScene>;
    auto requestStop() noexcept -> void;

  private:
    struct Impl;
    explicit ScenePreparation(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Project

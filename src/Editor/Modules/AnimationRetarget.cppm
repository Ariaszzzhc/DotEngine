module;

#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Editor.AnimationRetarget;

export import Dot.Animation.Retarget;
export import Dot.Editor.AnimationCreation;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Project;
import Dot.World.Scene;

export namespace Dot::Editor {
  struct RetargetProfile {
    std::string sourceScene;
    std::uint64_t sourceEntity;
    std::string targetScene;
    std::uint64_t targetEntity;
    std::string clip;
    Animation::RetargetSettings settings;
  };

  struct RetargetReference {
    std::uint64_t entity;
    std::string name;
    std::vector<std::string> clips;
    Animation::RetargetRig rig;
  };

  struct RetargetCreation {
    AnimationCreation animation;
    Animation::RetargetResult report;
    RetargetProfile profile;
    std::string sourceBytes;
    std::string targetBytes;
    std::string clipBytes;
    std::string metadataBytes;
  };

  /** Strict portable, bounded saved profile. References are saved native scene poses;
   * node identities/names are explicit, not inferred humanoid roles or borrowed scene handles.
   * 严格、可移植且有界的保存配置；参考为原生场景保存姿态，节点身份/名称显式指定，
   * 不猜测人形语义、不借用场景句柄。 */
  [[nodiscard]] auto decodeRetargetProfile(std::string_view text) -> Core::Result<RetargetProfile>;
  [[nodiscard]] auto encodeRetargetProfile(const RetargetProfile& profile)
      -> Core::Result<std::string>;
  [[nodiscard]] auto inspectRetargetReference(const Core::FileSystem& files, std::string_view path)
      -> Core::Result<std::vector<RetargetReference>>;
  /** Verify current target binding names/topology against the chosen reference. The owner's
   * instance name/placement may differ; internal unbound helper paths must remain exact.
   * 校验目标绑定名称/拓扑；所有者实例名称/放置可不同，骨架内部未绑定辅助节点路径须一致。 */
  [[nodiscard]] auto validateRetargetTarget(const World::Scene& scene, std::uint64_t owner,
                                            const Animation::RetargetRig& reference)
      -> Core::Result<void>;
  [[nodiscard]] auto validateRetargetInputs(const Core::FileSystem& files,
                                            const RetargetCreation& creation) -> Core::Result<void>;

  /** One owned CPU job freezes the target authoring scene and project metadata on begin.
   * Worker-owned scenes never cross threads. Ready/take return immutable clips/plain metadata
   * after baking and real native preview/skin preflight; no files, component edits, hooks or
   * devices are published. Validate exact source/reference/clip/metadata bytes again before
   * exclusive resource creation. Caller serializes other resource writers through publication.
   * Request cancellation, poll ready and reap before retiring; destruction joins bounded work.
   * 单个拥有 CPU 任务在开始时冻结目标编辑场景和项目描述；线程内场景不跨线程，烘焙及真实
   * 原生预览/蒙皮预检后返回不可变片段和普通元数据，不发布文件/组件、不运行钩子或设备。
   * 独占创建前再核对源/参考/片段/项目描述精确字节；调用方串行资源写入至发布结束。
   * 请求取消后轮询并回收才终止；销毁等待当前有界工作。 */
  class AnimationRetarget {
  public:
    [[nodiscard]] static auto begin(const Project::Project& project, const World::Scene& scene,
                                    std::uint64_t owner, std::string path, RetargetProfile profile)
        -> Core::Result<AnimationRetarget>;
    AnimationRetarget(const AnimationRetarget&) = delete;
    auto operator=(const AnimationRetarget&) -> AnimationRetarget& = delete;
    AnimationRetarget(AnimationRetarget&&) noexcept;
    auto operator=(AnimationRetarget&&) noexcept -> AnimationRetarget&;
    ~AnimationRetarget();
    [[nodiscard]] auto ready() const -> Core::Result<bool>;
    [[nodiscard]] auto take() -> Core::Result<RetargetCreation>;
    auto requestStop() noexcept -> void;

  private:
    struct Impl;
    explicit AnimationRetarget(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Editor

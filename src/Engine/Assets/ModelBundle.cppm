module;

#include <filesystem>
#include <vector>

export module Dot.Assets.ModelBundle;

import Dot.Core.Files;
import Dot.Core.Result;
export import Dot.Animation.Clip;
export import Dot.Animation.SceneAnimator;
export import Dot.World.Scene;

export namespace Dot::Assets {
  struct ModelInstance {
    World::Scene scene;
    std::vector<Animation::Clip> animations;
    std::vector<Animation::NodeBinding> bindings;
  };

  /** Load an owned native model scene, clips and live node bindings. Descriptor
   * is strict UTF-8 JSON <=1 MiB; clips total <=64 MiB, 1024 clips/30000 tracks/
   * one million keys. Scene retains its own limits. Every referenced path stays
   * in this bundle under assets; links/special files fail. No animation starts
   * implicitly. Scene moves retain bindings; callers can bind SceneAnimator.
   * 加载独立拥有的模型场景、片段及有效节点绑定。严格 UTF-8 JSON 描述至多 1 MiB；片段
   * 合计 64 MiB/1024 个/30000 通道/百万键，场景遵守自身限制。引用限 assets 下本包，
   * 拒绝链接/特殊文件。不隐式启动播放；场景移动保留绑定，可交给 SceneAnimator。 */
  [[nodiscard]] auto loadModelBundle(const Core::FileSystem& files,
                                     const std::filesystem::path& descriptor)
      -> Core::Result<ModelInstance>;
}  // namespace Dot::Assets

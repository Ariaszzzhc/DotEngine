module;

export module Dot.Assets.SceneSkins;

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.World.Scene;

export namespace Dot::Assets {
  /** Validate enabled saved skins before scripts/session/export publication.
   * Load real regular assets, match vertex/joint counts and validate the initial
   * deformation on CPU. At most four million influenced vertices in the scene;
   * unique files are shared during validation. No device, scene writes or retained
   * pointers; failures carry owner context. Runtime poses are checked by rendering.
   * 在脚本/会话/导出发布前校验启用蒙皮，读取真实普通资源，匹配顶点/关节数并以 CPU
   * 校验初始变形。场景至多四百万受影响顶点，校验期间共用文件；不启动设备、写场景
   * 或保留指针，失败包含所有者上下文；运行中姿态由渲染器检查。 */
  [[nodiscard]] auto validateSceneSkins(const Core::FileSystem& files, const World::Scene& scene)
      -> Core::Result<void>;
}  // namespace Dot::Assets

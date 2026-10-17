module;

#include <cstdint>
#include <string>
#include <string_view>

export module Dot.Editor.AnimationCreation;

import Dot.Animation.Clip;
import Dot.Animation.Source;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.World.Scene;

export namespace Dot::Editor {
  struct AnimationCreation {
    Animation::Clip clip;
    Animation::PlayerProperties player;
    std::string text;
  };

  /** Seed two identical local TRS keys from the author's current pose. Existing
   * players retain their complete bindings/settings and append the selected clip;
   * ordinary objects start with node zero bound to themselves. Validate an isolated
   * enabled player, enabled players' libraries, writer authority and skins before returning.
   * No files or source scene are changed; publication and scene history are separate.
   * 从当前编辑姿态生成两个相同局部 TRS 关键帧；已有播放器保留完整绑定/设置并追加选中片段，
   * 普通物体以节点零绑定自身。返回前隔离校验启用播放器的库、写入权限与蒙皮。
   * 不修改文件或源场景，资源发布与场景历史是独立操作。 */
  [[nodiscard]] auto preparePoseAnimation(const World::Scene& scene, const Core::FileSystem& files,
                                          std::uint64_t owner, std::string_view path,
                                          std::string_view name, double duration)
      -> Core::Result<AnimationCreation>;
}  // namespace Dot::Editor

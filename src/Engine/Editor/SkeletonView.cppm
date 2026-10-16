module;

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

export module Dot.Editor.SkeletonView;

import Dot.Animation.Skeleton;
import Dot.Animation.Skin;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Graphics.Geometry;
import Dot.Graphics.Overlay;
import Dot.World.Scene;

export namespace Dot::Editor {
  struct SkeletonViewFrame {
    std::uint64_t serial;
    Animation::SkeletonPose pose;
    std::vector<std::optional<Math::Vec2>> projected;
    Graphics::ViewportOverlay overlay;
  };

  struct SkeletonViewStats {
    std::optional<std::uint64_t> entity;
    std::optional<std::uint64_t> selected;
    std::optional<SkeletonViewFrame> frame;
    std::optional<Core::Error> error;
  };

  /** Read-only x-ray viewer, following an entity value in the supplied live scene.
   * Cache immutable skin data by project root/path until explicit invalidation.
   * Controls preflight current bindings before committing; invalid controls retain
   * prior viewer state. Frames own pose/projection data and never change the scene.
   * Render failures clear stale geometry and remain observable/retryable. Calls
   * require the creating thread; snapshots outlive the viewer and source.
   * 只读透视骨架查看器，跟随所传实时场景的实体数值；按项目根/路径缓存不可变蒙皮，
   * 显式失效后重读。控制预检当前绑定后提交，无效控制保留旧状态；帧拥有姿态/投影数据，
   * 不修改场景。绘制失败清除旧几何并可观察/重试；限创建线程调用，快照可晚于源与查看器销毁。 */
  class SkeletonView {
  public:
    SkeletonView();
    SkeletonView(const SkeletonView&) = delete;
    auto operator=(const SkeletonView&) -> SkeletonView& = delete;
    [[nodiscard]] auto set(const World::Scene& scene, const Core::FileSystem& assets,
                           std::optional<std::uint64_t> entity) -> Core::Result<void>;
    [[nodiscard]] auto select(const World::Scene& scene, const Core::FileSystem& assets,
                              std::optional<std::uint64_t> joint) -> Core::Result<void>;
    /** Null joint frames the entire skeleton, otherwise the joint/parent segment.
     * Small world-space padding makes coincident/end joints frameable.
     * 空关节取景整个骨架，否则取关节/父骨段；小幅世界空间留边可取景重合点和末端关节。 */
    [[nodiscard]] auto bounds(const World::Scene& scene, const Core::FileSystem& assets,
                              std::optional<std::uint64_t> joint) -> Core::Result<Graphics::Bounds>;
    [[nodiscard]] auto render(const World::Scene& scene, const Core::FileSystem& assets,
                              const Math::Mat4& viewProjection, std::uint64_t serial)
        -> Core::Result<Graphics::ViewportOverlay>;
    [[nodiscard]] auto invalidateAssets() -> Core::Result<void>;
    [[nodiscard]] auto stats() const -> Core::Result<SkeletonViewStats>;

  private:
    struct Cache {
      std::filesystem::path root;
      std::string asset;
      Animation::Skin skin;
    };

    [[nodiscard]] auto check() const -> Core::Result<void>;
    [[nodiscard]] auto prepare(const World::Scene& scene, const Core::FileSystem& assets,
                               std::uint64_t entity) const
        -> Core::Result<std::pair<Cache, Animation::SkeletonPose>>;
    std::thread::id m_thread;
    std::optional<std::uint64_t> m_entity;
    std::optional<std::uint64_t> m_selected;
    std::optional<Cache> m_cache;
    std::optional<SkeletonViewFrame> m_frame;
    std::optional<Core::Error> m_error;
  };
}  // namespace Dot::Editor

module;

#include <memory>
#include <vector>

export module Dot.Animation.SceneSkin;

export import Dot.Animation.Skin;
import Dot.Core.Result;
import Dot.World.Scene;

export namespace Dot::Animation {
  /** Owns immutable skin data and ordered scene-local joint IDs, never a scene
   * address. Binding requires an enabled skin component with an exact joint count.
   * Palette evaluates current hierarchy transforms in mesh-local space. Foreign,
   * deleted or changed bindings fail; moving a scene preserves its identity.
   * Owner-thread API; moves invalidate the source, no scene writes or GPU work.
   * 拥有不可变蒙皮及有序场景关节 ID，不借用场景地址；绑定要求启用组件和准确关节数。
   * 调色板使用当前层级变换并转换至网格局部空间；范围、实体或绑定变更明确失败。
   * 场景移动保留身份；API 限所有者线程，移动使源无效，不修改场景或启动 GPU。 */
  class SceneSkin {
  public:
    [[nodiscard]] static auto bind(const World::Scene& scene, World::EntityId owner, Skin skin)
        -> Core::Result<SceneSkin>;
    SceneSkin(const SceneSkin&) = delete;
    auto operator=(const SceneSkin&) -> SceneSkin& = delete;
    SceneSkin(SceneSkin&&) noexcept;
    auto operator=(SceneSkin&&) noexcept -> SceneSkin&;
    ~SceneSkin();
    [[nodiscard]] auto skin() const -> Core::Result<Skin>;
    [[nodiscard]] auto palette(const World::Scene& scene) const
        -> Core::Result<std::vector<Math::Mat4>>;

  private:
    struct Impl;
    explicit SceneSkin(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Animation

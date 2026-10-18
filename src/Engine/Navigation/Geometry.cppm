module;

#include <memory>
#include <span>
#include <stop_token>

export module Dot.Navigation.Geometry;

export import Dot.Navigation.Surface;
import Dot.Core.Result;
import Dot.Physics.Mesh;
import Dot.World.Scene;

export namespace Dot::Navigation {
  /** Exact static shape/resource/transform state after geometry proof. Query checks
   * compare bounded collider descriptions and cached world matrices, without recooking
   * triangle arrays. Non-geometric body properties and moving actors do not invalidate it.
   * Collision assets must remain immutable for this scope, as in scene simulation caches.
   * 几何证明后的精确静态形状/资源/变换状态。查询比较有界碰撞描述与缓存世界矩阵，
   * 不重复展开三角面；非几何属性与运动角色不使其失效。碰撞资源在此范围内须保持不可变，
   * 与场景仿真缓存一致。 */
  class GeometryState {
  public:
    GeometryState() = default;
    [[nodiscard]] static auto capture(const World::Scene& scene) -> Core::Result<GeometryState>;
    [[nodiscard]] auto unchanged(const World::Scene& scene) const -> Core::Result<bool>;

  private:
    struct Data;
    explicit GeometryState(std::shared_ptr<const Data> data) noexcept;
    std::shared_ptr<const Data> m_data;
  };

  /** Cook solid static colliders using simulation scaling and world transforms.
   * Sensors, characters and moving bodies are excluded. Meshes retain their actual
   * triangles/front faces; boxes are exact. Analytic spheres/capsules are conservatively
   * circumscribed within 0.05*cellSize radial error, or fail the shared geometry budget.
   * No files/devices; supplied immutable collision assets must match every static mesh.
   * 按仿真缩放规则及世界变换烘焙实体静态碰撞体，排除传感器、角色及运动刚体。
   * 网格保留实际三角面与正面，盒体精确；球体/胶囊使用径向误差不超过 0.05*cellSize
   * 的保守外包曲面，超出统一几何预算明确失败。不访问文件/设备，调用方须提供静态网格资源。 */
  [[nodiscard]] auto sceneGeometry(const World::Scene& scene, const BuildSettings& settings,
                                   std::span<const Physics::MeshAsset> meshes = {},
                                   std::stop_token stop = {}) -> Core::Result<InputGeometry>;
}  // namespace Dot::Navigation

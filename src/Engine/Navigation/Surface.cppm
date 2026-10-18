module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

export module Dot.Navigation.Surface;

import Dot.Core.Result;
import Dot.Math;

export namespace Dot::Navigation {
  /** Metres/degrees; bounded voxel resolution and one capsule's clearance.
   * Build quantizes height/radius upward and climb downward. Tile cells require
   * powers of two 32..256. Reject unsupported budgets rather than changing settings.
   * 单位为米/度，有界体素分辨率及单个胶囊净空；高度/半径向上、攀爬向下量化。
   * 分块边长须为 32..256 的二次幂，超出预算明确失败，不擅改参数。 */
  struct BuildSettings {
    float cellSize{.1F};
    float cellHeight{.05F};
    float agentHeight{1.83F};
    float agentRadius{.35F};
    float maxClimb{.3F};
    float maxSlopeDegrees{50};
    std::uint32_t tileCells{128};
    auto operator==(const BuildSettings&) const -> bool = default;
  };

  struct InputGeometry {
    std::vector<Math::Vec3> vertices;
    std::vector<std::uint32_t> indices;
  };

  /** Portable polygon/detail descriptions, never vendor structs or allocator bytes.
   * Coarse vertices are cell coordinates; detail vertices retain world-space heights.
   * 可移植多边形/细节描述，不包含第三方结构或分配器字节；粗顶点为体素坐标，细节顶点
   * 保留世界空间高度。 */
  struct Polygon {
    std::vector<std::uint16_t> vertices;
    std::vector<Math::Vec3> detailVertices;
    std::vector<std::array<std::uint8_t, 4>> detailTriangles;
  };

  struct Tile {
    std::uint32_t x{}, z{};
    Math::Vec3 minimum{}, maximum{};
    std::vector<std::array<std::uint16_t, 3>> vertices;
    std::vector<Polygon> polygons;
  };

  struct SurfaceDescription {
    BuildSettings settings;
    Math::Vec3 origin{};
    /** Canonical XXH3-128 geometry identity, not a cryptographic authenticity seal.
     * Editor publication additionally revalidates exact input bytes.
     * 规范化 XXH3-128 几何身份，不是密码学认证；编辑器发布另行精确复验输入字节。 */
    std::string source;
    std::vector<Tile> tiles;
  };

  struct RouteQuery {
    Math::Vec3 start{}, goal{};
    Math::Vec3 searchExtent{.5F, .5F, .5F};
    std::uint32_t maxPoints{256};
  };

  struct Route {
    Math::Vec3 start{}, goal{};
    std::vector<Math::Vec3> points;
  };

  struct WallSegment {
    Math::Vec3 from, to;
  };

  struct LocalBoundary {
    Math::Vec3 position;
    std::vector<WallSegment> walls;
  };

  struct SurfaceTrace {
    Math::Vec3 position, normal;
    bool reached{};
  };

  struct SurfaceStats {
    std::size_t tiles{}, polygons{}, vertices{}, detailTriangles{};
  };

  [[nodiscard]] auto validateSettings(const BuildSettings& settings) -> Core::Result<void>;
  /** Validate finite nondegenerate world triangles before deriving stable content identity.
   * At most 262144 vertices/triangles; positions within +/-1e6. No files/devices.
   * 先校验有限非退化世界三角面再生成稳定内容身份；顶点/三角面各至多 262144，坐标正负百万；
   * 不访问文件或设备。 */
  [[nodiscard]] auto geometryIdentity(const InputGeometry& geometry) -> Core::Result<std::string>;

  /** Immutable owned tiled Recast/Detour surface. Copies/readers may cross threads;
   * queries own scratch storage, never share a mutable vendor query. Builds cancel
   * between bounded tiles/phases; at most 256 tiles/262144 polygons. No world/device/IO.
   * 不可变拥有的分块 Recast/Detour 表面；副本/读取可跨线程，查询独占工作区，不共享可变
   * 第三方查询对象。构建在有界分块/阶段间取消，至多 256 块/262144 多边形；不创建世界、
   * 设备或访问文件。 */
  class Surface {
  public:
    Surface() = default;
    [[nodiscard]] static auto build(const InputGeometry& geometry, const BuildSettings& settings,
                                    std::stop_token stop = {}) -> Core::Result<Surface>;
    [[nodiscard]] static auto create(SurfaceDescription description, std::stop_token stop = {})
        -> Core::Result<Surface>;
    /** Borrow until the last owning copy releases. 借用至最后一个拥有副本释放。 */
    [[nodiscard]] auto description() const -> Core::Result<const SurfaceDescription*>;
    [[nodiscard]] auto stats() const -> Core::Result<SurfaceStats>;
    /** Explicit bounded projection, complete corridors only. Disconnected goals, exhausted
     * search/corner storage and missing polygons fail instead of returning a fake full route.
     * Returned endpoints expose projection; physics remains authoritative for actual movement.
     * 显式有界投影，仅返回完整路径；断开的目标、搜索/拐点容量不足或缺失多边形明确失败，
     * 不伪造完整路线。端点显示实际投影，移动仍以物理为准。 */
    [[nodiscard]] auto route(const RouteQuery& query) const -> Core::Result<Route>;
    /** Connected local walls for velocity planning; <=256 visited polygons/512 segments.
     * Exhaustion fails, never drops unknown obstacles. 连通局部边界用于速度规划，至多访问
     * 256 多边形/512 边段；容量不足明确失败，不丢弃未知障碍。 */
    [[nodiscard]] auto boundary(Math::Vec3 position, float radius,
                                Math::Vec3 searchExtent = {.5F, .5F, .5F}) const
        -> Core::Result<LocalBoundary>;
    /** Trace a center-line through walkable polygons, retaining projected floor height.
     * Does not sweep a capsule or move physics. <=256 corridor entries; errors are explicit.
     * 沿可行走多边形追踪中心线并返回投影地面高度，不扫掠胶囊或移动物理；至多 256 路径项。 */
    [[nodiscard]] auto trace(Math::Vec3 start, Math::Vec3 goal,
                             Math::Vec3 searchExtent = {.5F, .5F, .5F}) const
        -> Core::Result<SurfaceTrace>;

  private:
    struct Data;
    explicit Surface(std::shared_ptr<const Data> data) noexcept;
    std::shared_ptr<const Data> m_data;
  };
}  // namespace Dot::Navigation

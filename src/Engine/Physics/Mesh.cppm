module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

export module Dot.Physics.Mesh;

import Dot.Core.Result;
import Dot.Math;

export namespace Dot::Physics {
  struct MeshDescription {
    std::vector<Math::Vec3> vertices;
    std::vector<std::uint32_t> indices;
  };

  struct MeshView {
    std::span<const Math::Vec3> vertices;
    std::span<const std::uint32_t> indices;
  };

  /** Immutable indexed collision triangles, counter-clockwise front faces.
   * Pure CPU validation, no registration/world/device/file access. At most 262144
   * vertices and triangles, local coordinates within +/-10000. Empty, nonfinite,
   * out-of-range and degenerate triangles fail instead of being dropped. Views
   * borrow until the last owning copy is released; immutable copies cross threads.
   * 不可变索引碰撞三角形，逆时针为正面；纯 CPU 校验，不注册后端或访问世界、设备、文件。
   * 顶点/三角形各至多 262144，局部坐标限制正负一万。空、非有限、越界及退化三角形
   * 明确拒绝，不静默删除。视图借用至最后拥有副本释放，不可变副本可跨线程。 */
  class TriangleMesh {
  public:
    TriangleMesh() = default;
    TriangleMesh(const TriangleMesh&);
    auto operator=(const TriangleMesh&) -> TriangleMesh&;
    TriangleMesh(TriangleMesh&&) noexcept;
    auto operator=(TriangleMesh&&) noexcept -> TriangleMesh&;
    ~TriangleMesh();
    [[nodiscard]] static auto create(MeshDescription description) -> Core::Result<TriangleMesh>;
    [[nodiscard]] auto view() const -> Core::Result<MeshView>;
    /** Validate signed nonuniform scaling without changing geometry. Components
     * have magnitude [0.0001,10000], resulting vertices within +/-1e6. Reflections
     * preserve the intended front face through the backend's signed scale.
     * 只读校验带符号非等比缩放；分量绝对值 [0.0001,10000]，结果顶点限制正负百万。
     * 反射通过后端的带符号缩放保留预期正面。 */
    [[nodiscard]] auto validateScale(Math::Vec3 scale) const -> Core::Result<void>;
    [[nodiscard]] auto bytes() const noexcept -> std::size_t;
    [[nodiscard]] auto triangles() const noexcept -> std::size_t;
    auto operator==(const TriangleMesh& other) const -> bool;

  private:
    struct Data;
    explicit TriangleMesh(std::shared_ptr<const Data> data) noexcept;
    std::shared_ptr<const Data> m_data;
  };

  struct MeshAsset {
    std::string path;
    TriangleMesh value;
  };
}  // namespace Dot::Physics

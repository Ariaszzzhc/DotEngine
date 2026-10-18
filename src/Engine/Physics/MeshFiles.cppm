module;

#include <cstddef>
#include <span>
#include <stop_token>
#include <string_view>
#include <vector>

export module Dot.Physics.MeshFiles;

export import Dot.Physics.Mesh;
import Dot.Core.Result;
import Dot.Core.Files;
import Dot.World.Scene;

export namespace Dot::Physics {
  /** Read a regular portable assets/ .dotmesh, with no symlink components.
   * Extract positions/indices and validate every collision triangle. No simplification,
   * hull or primitive substitution. Scene preparation owns <=64 unique meshes/64 MiB
   * of collision geometry. No world/device creation; immutable assets cross threads.
   * 读取 assets/ 下可移植普通 .dotmesh，各组件不得为符号链接。提取位置/索引并校验每个
   * 碰撞三角形，不简化、转凸包或替换基本形状。场景准备拥有至多 64 份/64 MiB 碰撞几何；
   * 不创建世界或设备，不可变资源可跨线程。 */
  /** Freeze exactly the bytes decoded by a background authoring operation.
   * 后台编辑操作冻结并解码同一份字节，避免重复读取导致输入身份错配。 */
  [[nodiscard]] auto readCollisionMeshBytes(const Core::FileSystem& files, std::string_view path)
      -> Core::Result<std::vector<std::byte>>;
  [[nodiscard]] auto decodeCollisionMesh(std::span<const std::byte> bytes)
      -> Core::Result<TriangleMesh>;
  [[nodiscard]] auto loadCollisionMesh(const Core::FileSystem& files, std::string_view path)
      -> Core::Result<TriangleMesh>;
  [[nodiscard]] auto loadSceneCollisionMeshes(const Core::FileSystem& files,
                                              const Dot::World::Scene& scene,
                                              std::stop_token stop = {})
      -> Core::Result<std::vector<MeshAsset>>;
}  // namespace Dot::Physics

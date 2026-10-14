module;

#include <cstdint>

export module Dot.Graphics.Occlusion;

import Dot.Core.Result;
import Dot.Math;

export namespace Dot::Graphics {
  /** Full-resolution geometric SSAO: world radius [.05,5] meters, surface bias
   * [0,.2] meters, strength [0,4], 4/8/16 search directions, 2..8 steps and bilateral
   * filter radius 0..4 pixels. All work is bounded independently of camera distance.
   * 全分辨率几何 SSAO：世界半径 [.05,5] 米、表面偏移 [0,.2] 米、强度 [0,4]，
   * 4/8/16 个搜索方向、2..8 步以及 0..4 像素双边过滤半径；工作量不随相机距离增加。 */
  struct OcclusionSettings {
    float radius{0.5F};
    float bias{0.02F};
    float strength{1};
    std::uint32_t directions{8};
    std::uint32_t steps{4};
    std::uint32_t filterRadius{2};
    auto operator==(const OcclusionSettings&) const -> bool = default;
  };

  struct OcclusionCamera {
    Math::Mat4 viewProjection;
    Math::Mat4 inverse;
    Math::Vec3 right;
    Math::Vec3 up;
  };

  [[nodiscard]] auto validateOcclusion(const OcclusionSettings& settings) -> Core::Result<void>;
  /** Validate an invertible finite [0,1] camera frustum and orthogonal image-plane
   * axes without a device. Reconstruction uses top-left UV and actual device depth.
   * 不依赖设备，校验可逆有限 [0,1] 视锥与正交图像平面轴；重建使用左上 UV 及真实设备深度。 */
  [[nodiscard]] auto occlusionCamera(const Math::Mat4& viewProjection)
      -> Core::Result<OcclusionCamera>;
  [[nodiscard]] auto reconstructPosition(const OcclusionCamera& camera, Math::Vec2 uv, float depth)
      -> Core::Result<Math::Vec3>;
  /** Current four working textures use 24 logical bytes per pixel, at most 256 MiB.
   * 当前四张工作纹理每像素共 24 个逻辑字节，最多 256 MiB。 */
  [[nodiscard]] auto occlusionBytes(std::uint32_t width, std::uint32_t height)
      -> Core::Result<std::uint64_t>;
}  // namespace Dot::Graphics

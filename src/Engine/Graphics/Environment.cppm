module;

#include <cstdint>

export module Dot.Graphics.Environment;

import Dot.Core.Result;
import Dot.Math;
import Dot.Graphics.Image;

export namespace Dot::Graphics {
  enum class EnvironmentSurface { Radiance, Diffuse, Specular, Brdf };

  /** GPU preprocessing dimensions are powers of two; radiance/specular 16..512,
   * diffuse 4..128, BRDF 16..256, deterministic sample count 32..1024. Defaults
   * suit an interactive preview; authored production quality may raise samples.
   * GPU 预处理尺寸须为二次幂；辐射/反射 16..512、漫反射 4..128、BRDF 16..256，
   * 确定性采样数 32..1024；缺省适合交互预览，可为制作质量提高采样数。 */
  struct EnvironmentConfig {
    std::uint32_t radianceSize{256};
    std::uint32_t diffuseSize{32};
    std::uint32_t specularSize{128};
    std::uint32_t brdfSize{128};
    std::uint32_t samples{256};
  };

  struct EnvironmentInfo {
    EnvironmentConfig config;
    std::uint32_t radianceLevels;
    std::uint32_t specularLevels;
    std::uint64_t bytes;
  };

  /** Opaque linear 2:1 latitude-longitude panorama, top row +Y, center -Z,
   * longitude increasing toward +X. RGB must fit finite RGBA16F outputs. No
   * exposure/clamping replaces malformed or out-of-range radiance.
   * 不透明线性 2:1 经纬全景，顶行 +Y、中心 -Z、经度增大朝 +X；RGB 须可表示为
   * 有限 RGBA16F 输出，不用曝光/截断替代无效或越界辐射。 */
  [[nodiscard]] auto validateEnvironment(const HdrImage& panorama,
                                         const EnvironmentConfig& config = {})
      -> Core::Result<EnvironmentInfo>;
  /** WebGPU cubemap faces +X,-X,+Y,-Y,+Z,-Z; UV in [0,1], top-left origin.
   * Directions match native cube sampling, including face edge orientation.
   * WebGPU 立方体面顺序 +X,-X,+Y,-Y,+Z,-Z；UV 为 [0,1]、左上原点，方向匹配
   * 原生立方体采样（包括面边缘朝向）。 */
  [[nodiscard]] auto environmentDirection(std::uint32_t face, Math::Vec2 uv)
      -> Core::Result<Math::Vec3>;
}  // namespace Dot::Graphics

module;

#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>

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
    auto operator==(const EnvironmentConfig&) const -> bool = default;
  };

  struct EnvironmentInfo {
    EnvironmentConfig config;
    std::uint32_t radianceLevels;
    std::uint32_t specularLevels;
    std::uint64_t bytes;
  };

  /** Validate processing work/dimensions without an image or device.
   * 不依赖图像或设备，校验处理工作量及尺寸。 */
  [[nodiscard]] auto validateEnvironmentConfig(const EnvironmentConfig& config)
      -> Core::Result<void>;

  /** Opaque linear 2:1 latitude-longitude panorama, top row +Y, center -Z,
   * longitude increasing toward +X. RGB must fit finite RGBA16F outputs. No
   * exposure/clamping replaces malformed or out-of-range radiance.
   * 不透明线性 2:1 经纬全景，顶行 +Y、中心 -Z、经度增大朝 +X；RGB 须可表示为
   * 有限 RGBA16F 输出，不用曝光/截断替代无效或越界辐射。 */
  [[nodiscard]] auto validateEnvironment(const HdrImage& panorama,
                                         const EnvironmentConfig& config = {})
      -> Core::Result<EnvironmentInfo>;

  /** Immutable owned HDR panorama/mips, prepared without a GPU. Consumes input
   * storage, validates opaque RGBA16F-range radiance, then uses latitude solid-angle
   * weights. At most 16,777,216 base pixels; logical pyramid below 342 MiB.
   * Copies share immutable storage. Cancellation is checked between bounded mips.
   * 不依赖 GPU 准备的不可变 HDR 全景/mip；接管输入存储，校验不透明且可表示于
   * RGBA16F 的辐射，再按纬度立体角加权。基础层最多 16,777,216 像素，金字塔逻辑
   * 数据少于 342 MiB；副本共用不可变存储，在各有界 mip 之间检查取消。 */
  class EnvironmentSource {
  public:
    [[nodiscard]] static auto prepare(HdrImage panorama, std::stop_token stop = {})
        -> Core::Result<EnvironmentSource>;
    EnvironmentSource(const EnvironmentSource&) noexcept;
    auto operator=(const EnvironmentSource&) noexcept -> EnvironmentSource&;
    EnvironmentSource(EnvironmentSource&&) noexcept;
    auto operator=(EnvironmentSource&&) noexcept -> EnvironmentSource&;
    ~EnvironmentSource();
    /** Borrow immutable levels for this source's lifetime; moved sources fail.
     * 在源对象存活期间借用不可变层级；移走后的对象返回错误。 */
    [[nodiscard]] auto levels() const -> Core::Result<std::span<const HdrImage>>;
    /** Validate output configuration without rescanning already validated pixels.
     * 校验输出配置，不重复扫描已经校验的像素。 */
    [[nodiscard]] auto description(const EnvironmentConfig& config) const
        -> Core::Result<EnvironmentInfo>;

  private:
    struct Impl;
    explicit EnvironmentSource(std::shared_ptr<const Impl> impl) noexcept;
    std::shared_ptr<const Impl> m_impl;
  };

  /** WebGPU cubemap faces +X,-X,+Y,-Y,+Z,-Z; UV in [0,1], top-left origin.
   * Directions match native cube sampling, including face edge orientation.
   * WebGPU 立方体面顺序 +X,-X,+Y,-Y,+Z,-Z；UV 为 [0,1]、左上原点，方向匹配
   * 原生立方体采样（包括面边缘朝向）。 */
  [[nodiscard]] auto environmentDirection(std::uint32_t face, Math::Vec2 uv)
      -> Core::Result<Math::Vec3>;
}  // namespace Dot::Graphics

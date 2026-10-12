module;

export module Dot.Audio.Spatial;

export import Dot.Math;
import Dot.Core.Result;

export namespace Dot::Audio {
  enum class Attenuation { Inverse, Linear, Exponential };

  struct Listener {
    Math::Vec3 position{};
    Math::Quat rotation{};
    Math::Vec3 velocity{};
    auto operator==(const Listener&) const -> bool = default;
  };

  struct SpatialProperties {
    bool enabled{};
    Attenuation attenuation{Attenuation::Inverse};
    float minDistance{1};
    float maxDistance{1000};
    float rolloff{1};
    float dopplerFactor{};
    float innerConeDegrees{360};
    float outerConeDegrees{360};
    float outerConeGain{};
    auto operator==(const SpatialProperties&) const -> bool = default;
  };

  struct SpatialSettings : SpatialProperties {
    Math::Vec3 position{};
    Math::Quat rotation{};
    Math::Vec3 velocity{};
    auto operator==(const SpatialSettings&) const -> bool = default;
  };

  /** Right-handed metres, forward -Z/up +Y. Position components within +/-1e6,
   * speed <=100 m/s, normalized nonzero finite rotations. Directions derive from
   * rotation, rather than independently editable degenerate direction/up vectors.
   * 右手米制坐标，前向 -Z、向上 +Y；位置分量在 +/-1e6 内，速率不超 100 米/秒，
   * 旋转须有限非零并归一化，方向由旋转推导，避免独立方向/向上向量退化。 */
  [[nodiscard]] auto validatedListener(const Listener& listener) -> Core::Result<Listener>;
  /** Validate even disabled descriptions. Distances are [0.001,1e6], min < max;
   * rolloff [0,10] (zero removes distance attenuation), Doppler [0,1], cone angles
   * [0,360] with inner <= outer, and outer gain [0,1]. The velocity bound keeps
   * Doppler denominators nonsingular at the backend's standard speed of sound.
   * 禁用描述也校验；距离在 [0.001,1e6] 且最小值小于最大值，衰减系数 [0,10]（零表示
   * 无距离衰减）、多普勒系数 [0,1]、锥角 [0,360] 且内角不大于外角，外锥增益 [0,1]；
   * 速度界限使后端标准声速下的多普勒分母保持非奇异。 */
  [[nodiscard]] auto validateSpatialProperties(const SpatialProperties& properties)
      -> Core::Result<void>;
  [[nodiscard]] auto validatedSpatial(const SpatialSettings& settings)
      -> Core::Result<SpatialSettings>;
}  // namespace Dot::Audio

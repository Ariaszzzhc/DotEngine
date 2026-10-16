module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

export module Dot.Animation.Edit;

export import Dot.Animation.Clip;
import Dot.Core.Result;

export namespace Dot::Animation {
  struct PutKey {
    std::uint32_t node;
    Path path;
    Interpolation interpolation;
    Key key;
  };

  struct RemoveKey {
    std::uint32_t node;
    Path path;
    float time;
  };

  struct UpdateKey {
    std::uint32_t node;
    Path path;
    float time;
    Key key;
  };

  inline constexpr std::size_t MaxRetimeKeys = 16384;

  struct KeyReference {
    std::uint32_t node;
    Path path;
    float time;
  };

  struct KeyTimeChange {
    std::uint32_t node;
    Path path;
    float time;
    float target;
  };

  struct RetimeKeys {
    std::vector<KeyTimeChange> keys;
  };

  struct RemoveTrack {
    std::uint32_t node;
    Path path;
  };

  struct SetInterpolation {
    std::uint32_t node;
    Path path;
    Interpolation interpolation;
  };

  struct RenameClip {
    std::string name;
  };

  using ClipEdit = std::variant<PutKey, UpdateKey, RetimeKeys, RemoveKey, RemoveTrack,
                                SetInterpolation, RenameClip>;

  /** Atomic ordered batch, <=256 edits, keyed by logical node/path and exact float
   * seconds. Put replaces an equal-time key or inserts in order; new tracks use
   * its interpolation, existing tracks require a matching mode. Update requires an
   * existing exact-time key and replaces/reorders it, rejecting an occupied new time;
   * it never overwrites a neighboring key. RetimeKeys resolves 1..16384 unique
   * source keys simultaneously: selected sources vacate together, destinations
   * must remain unique and cannot overwrite unselected keys. It changes only time,
   * preserving values/tangents/modes; numeric no-ops retain source float bits.
   * Removing the last
   * key removes its track. Explicit interpolation conversion clears tangents;
   * unchanged modes retain them. The final native Clip must validate, including
   * nonempty channels, cubic key count and rotation/scale constraints. Failure
   * retains the input; no file/scene/device is touched. Empty batches share input.
   * 有序原子批次，至多 256 项，按逻辑节点/路径及精确 float 秒定位。Put 替换同时间键或
   * 有序插入，新轨道使用所给模式，现有轨道要求模式一致。Update 要求精确原时间键存在，
   * 替换并重排该键，新时间已占用则拒绝，绝不覆盖相邻键。RetimeKeys 同时处理 1..16384
   * 个唯一源键，允许占用组内腾出的位置，拒绝目标重叠及覆盖未选键；只改时间，保留值/
   * 切线/模式，数值无变化保留原 float 位。移除末键会移除轨道；显式更换
   * 插值清除切线，同模式保留。最终片段须通过原生非空/三次键数/旋转及缩放校验；失败
   * 保留输入，不触及文件/场景/设备，空批次共享原片段。 */
  [[nodiscard]] auto editClip(const Clip& clip, std::span<const ClipEdit> edits)
      -> Core::Result<Clip>;
}  // namespace Dot::Animation

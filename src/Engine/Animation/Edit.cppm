module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

export module Dot.Animation.Edit;

export import Dot.Animation.KeyGroup;
import Dot.Core.Result;
import Dot.Math;

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

  struct KeyTimeChange {
    std::uint32_t node;
    Path path;
    float time;
    float target;
  };

  struct RetimeKeys {
    std::vector<KeyTimeChange> keys;
  };

  /** Adjust 1..16384 unique existing keys without retiming. Translation adds a
   * local channel offset; scale multiplies componentwise; rotation postmultiplies
   * XYZW by a finite unit quaternion. Cubic scale/rotation derivatives receive
   * the same linear transform; translation derivatives stay unchanged. No tangent
   * normalization/sign correction or implicit interpolation change. Identity
   * components preserve source float bits; final native clip validation is atomic.
   * 不移时地调整 1..16384 个唯一现有键；平移加局部通道偏移，缩放逐分量相乘，旋转
   * XYZW 右乘有限单位四元数。三次缩放/旋转导数使用同一线性变换，平移导数不变；
   * 不归一化切线、不更改符号或隐式改插值。恒等分量保留原 float 位，最终原生校验原子生效。 */
  struct TransformKeys {
    std::vector<KeyReference> keys;
    Math::Vec3 translation{};
    Math::Quat rotation{};
    Math::Vec3 scale{1, 1, 1};
  };

  enum class KeyCollision { Reject, Replace };

  struct PasteKeys {
    KeyGroup group;
    float time;
    KeyCollision collision{KeyCollision::Reject};
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

  using ClipEdit = std::variant<PutKey, UpdateKey, RetimeKeys, TransformKeys, PasteKeys, RemoveKey,
                                RemoveTrack, SetInterpolation, RenameClip>;

  /** Atomic ordered batch, <=256 edits, keyed by logical node/path and exact float
   * seconds. Put replaces an equal-time key or inserts in order; new tracks use
   * its interpolation, existing tracks require a matching mode. Update requires an
   * existing exact-time key and replaces/reorders it, rejecting an occupied new time;
   * it never overwrites a neighboring key. RetimeKeys resolves 1..16384 unique
   * source keys simultaneously: selected sources vacate together, destinations
   * must remain unique and cannot overwrite unselected keys. It changes only time,
   * preserving values/tangents/modes; numeric no-ops retain source float bits.
   * PasteKeys places an owned group's earliest key at time. Existing channels
   * require matching modes; absent channels inherit their source mode. Reject is
   * the default collision policy; Replace explicitly replaces only equal-time keys.
   * Partial cubic pastes must still leave valid final cubic tracks. No binding
   * remap/retarget is implicit; the caller validates scene ownership and authority.
   * Removing the last
   * key removes its track. Explicit interpolation conversion clears tangents;
   * unchanged modes retain them. The final native Clip must validate, including
   * nonempty channels, cubic key count and rotation/scale constraints. Failure
   * retains the input; no file/scene/device is touched. Empty batches share input.
   * 有序原子批次，至多 256 项，按逻辑节点/路径及精确 float 秒定位。Put 替换同时间键或
   * 有序插入，新轨道使用所给模式，现有轨道要求模式一致。Update 要求精确原时间键存在，
   * 替换并重排该键，新时间已占用则拒绝，绝不覆盖相邻键。RetimeKeys 同时处理 1..16384
   * 个唯一源键，允许占用组内腾出的位置，拒绝目标重叠及覆盖未选键；只改时间，保留值/
   * 切线/模式，数值无变化保留原 float 位。移除末键会移除轨道；显式更换插值清除
   * 切线，同模式保留。PasteKeys 将组内最早键放到给定时间，现有通道须同模式，新通道
   * 沿用源模式；默认拒绝占用，显式 Replace 仅替换同时间键。三次部分粘贴仍须使最终
   * 轨道合法；不隐式重定向绑定，场景归属/权限由调用方校验。最终片段须通过原生非空/
   * 三次键数/旋转及缩放校验；失败保留输入，不触及文件/场景/设备，空批次共享原片段。 */
  [[nodiscard]] auto editClip(const Clip& clip, std::span<const ClipEdit> edits)
      -> Core::Result<Clip>;
}  // namespace Dot::Animation

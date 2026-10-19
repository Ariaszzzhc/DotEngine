module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module Dot.Editor.AnimationKeyMove;

export import Dot.Animation.Edit;
import Dot.Core.Result;

export namespace Dot::Editor {
  struct KeyMoveItem {
    std::uint32_t node;
    Animation::Path path;
    float sourceTime;
    Animation::Key key;
    bool occupied;
  };

  struct KeyMoveSnapshot {
    std::vector<KeyMoveItem> keys;
    float time;
    bool changed;
    bool occupied;
  };

  /** Own an immutable clip and 1..16384 unique source keys. The last key is the
   * anchor; updates specify its absolute float time and apply the same double
   * offset to every original source before float rounding. Updates affect ghosts
   * only, without copying the clip or touching scene/file/device/clock/history.
   * A selected source vacates for the group; destinations occupied by unselected
   * keys or collapsed by float rounding remain visible, and edit rejects them
   * without losing the preview. Values/tangents/modes remain unchanged. Returning
   * to the anchor origin restores all source float representations exactly.
   * The caller validates document identity/revision and native pose before one
   * history publication. Move retires the source handle; destruction discards the
   * preview. Independent instances share clips; synchronize mutable access.
   * 拥有不可变片段和 1..16384 个唯一源键，最后一键为锚点；更新指定锚点绝对 float 时间，
   * 每个原键加同一 double 偏移后转 float。仅改变标记，不复制片段、不触及场景/文件/
   * 设备/时钟/历史。组内源键同时腾位，未选键占用及 float 舍入重叠可见且可恢复；edit
   * 无损拒绝冲突。值/切线/模式不变，返回锚点原时间精确恢复原位表示。调用方校验文档
   * 身份/版本和原生姿态后统一提交一步历史。移动使源失效，销毁丢弃；可共享片段，修改须同步。 */
  class AnimationKeyMove {
  public:
    [[nodiscard]] static auto begin(Animation::Clip source,
                                    std::span<const Animation::KeyReference> keys)
        -> Core::Result<AnimationKeyMove>;
    AnimationKeyMove(const AnimationKeyMove&) = delete;
    auto operator=(const AnimationKeyMove&) -> AnimationKeyMove& = delete;
    AnimationKeyMove(AnimationKeyMove&&) noexcept;
    auto operator=(AnimationKeyMove&&) noexcept -> AnimationKeyMove&;
    ~AnimationKeyMove();
    /** Reject non-finite/out-of-range anchor or any translated key outside
     * [0,86400], retaining the preceding preview. Occupied destinations are allowed.
     * 锚点非有限/越界或任一平移键超出 [0,86400] 时保留上次预览；允许显示占用目标。 */
    [[nodiscard]] auto update(float time) -> Core::Result<void>;
    [[nodiscard]] auto snapshot() const -> Core::Result<KeyMoveSnapshot>;
    [[nodiscard]] auto edit() const -> Core::Result<Animation::RetimeKeys>;

  private:
    struct SourceKey {
      std::size_t track;
      Animation::Key key;
    };

    AnimationKeyMove(Animation::Clip source, std::vector<SourceKey> keys);
    Animation::Clip m_source;
    std::vector<SourceKey> m_keys;
    float m_target{};
  };
}  // namespace Dot::Editor

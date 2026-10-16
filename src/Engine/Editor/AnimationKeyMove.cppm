module;

#include <cstddef>
#include <cstdint>

export module Dot.Editor.AnimationKeyMove;

export import Dot.Animation.Edit;
import Dot.Core.Result;

export namespace Dot::Editor {
  struct KeyMoveSnapshot {
    std::uint32_t node;
    Animation::Path path;
    float sourceTime;
    Animation::Key key;
    bool changed;
    bool occupied;
  };

  /** Own an immutable clip and a disposable absolute-time key move. Updating only
   * changes the ghost key, with no clip copy, scene, file, device or hidden clock.
   * Occupied destinations remain visible in snapshots; edit rejects them without
   * losing the ghost. Values/tangents and channel interpolation are unchanged.
   * The caller validates document identity/revision and native pose before commit;
   * edit returns one ordinary UpdateKey, never publishes history or writes data.
   * Move invalidates the source handle. Destruction discards the preview. Separate
   * instances may share the clip; synchronize access to one mutable instance.
   * 拥有不可变片段及可丢弃的绝对时间移键草稿；更新只改变时间标记，不复制片段、不涉及
   * 场景/文件/设备/隐藏时钟。快照显示目标占用；edit 无损拒绝冲突。值/切线及插值不变。
   * 调用方在提交前校验文档身份/版本和原生姿态；edit 仅返回一项 UpdateKey，不发布历史或
   * 写文件。移动使源句柄失效，销毁丢弃预览。独立实例可共享片段，同一可变实例须外部同步。 */
  class AnimationKeyMove {
  public:
    [[nodiscard]] static auto begin(Animation::Clip source, std::uint32_t node,
                                    Animation::Path path, float time)
        -> Core::Result<AnimationKeyMove>;
    AnimationKeyMove(const AnimationKeyMove&) = delete;
    auto operator=(const AnimationKeyMove&) -> AnimationKeyMove& = delete;
    AnimationKeyMove(AnimationKeyMove&&) noexcept;
    auto operator=(AnimationKeyMove&&) noexcept -> AnimationKeyMove&;
    ~AnimationKeyMove();
    /** Finite native float seconds in [0,86400]; invalid samples retain the
     * previous target. Source key lookup uses exact float equality.
     * 时间为 [0,86400] 内有限原生 float 秒；无效采样保留上次目标，原键按精确 float 定位。 */
    [[nodiscard]] auto update(float time) -> Core::Result<void>;
    [[nodiscard]] auto snapshot() const -> Core::Result<KeyMoveSnapshot>;
    [[nodiscard]] auto edit() const -> Core::Result<Animation::UpdateKey>;

  private:
    AnimationKeyMove(Animation::Clip source, std::size_t track, Animation::Key key);
    Animation::Clip m_source;
    std::size_t m_track{};
    Animation::Key m_key;
    float m_target{};
  };
}  // namespace Dot::Editor

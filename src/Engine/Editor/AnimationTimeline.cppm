module;

#include <cstddef>
#include <cstdint>
#include <vector>

export module Dot.Editor.AnimationTimeline;

import Dot.Animation.Clip;
import Dot.Core.Result;

export namespace Dot::Editor {
  struct TimelineBin {
    float first;
    float last;
    std::size_t count;
  };

  struct TimelineRow {
    std::size_t index;
    std::uint32_t node;
    Animation::Path path;
    std::size_t total;
    std::size_t visible;
    std::vector<float> times;
    std::vector<TimelineBin> bins;
  };

  struct TimelinePage {
    std::size_t total;
    std::size_t offset;
    std::vector<TimelineRow> rows;
  };

  /** Immutable clip-backed dope-sheet index, ordered by logical node then path.
   * Pages contain <=32 rows, <=256 exact times per row or <=128 occupied density
   * bins with actual first/last times and counts. Dense rows never pretend a bin
   * is one key. Inclusive rectangular selection resolves exact original keys and
   * rejects >16384 before returning; no file/scene/history/device mutation.
   * Owns its clip after source retirement; moved handles fail. Const queries may
   * run concurrently, with no mutable shared cache.
   * 拥有不可变片段的多轨索引，按逻辑节点及通道排序。每页至多 32 行，每行至多 256 个
   * 精确时间，否则返回至多 128 个有实际首尾时间/数量的密度桶，不把聚合冒充单键。
   * 闭区间框选解析原始精确键，超过 16384 先失败；不改文件/场景/历史/设备。
   * 源退役后仍有效，移后句柄失败；常量查询可并发，不共享可变缓存。 */
  class AnimationTimeline {
  public:
    [[nodiscard]] static auto create(Animation::Clip clip) -> Core::Result<AnimationTimeline>;
    AnimationTimeline(const AnimationTimeline&) = delete;
    auto operator=(const AnimationTimeline&) -> AnimationTimeline& = delete;
    AnimationTimeline(AnimationTimeline&&) noexcept;
    auto operator=(AnimationTimeline&&) noexcept -> AnimationTimeline&;
    ~AnimationTimeline();
    [[nodiscard]] auto rows() const -> Core::Result<std::size_t>;
    [[nodiscard]] auto row(std::uint32_t node, Animation::Path path) const
        -> Core::Result<std::size_t>;
    /** Page time span >=1us within [0,86400], offset <= row count, count 1..32,
     * density columns 1..128. Empty tail pages are valid.
     * 页面时间范围为 [0,86400] 且跨度至少 1us，偏移不超过总行数，每页 1..32 行、
     * 1..128 个密度列；允许空尾页。 */
    [[nodiscard]] auto page(double from, double to, std::size_t offset, std::size_t count,
                            std::size_t columns) const -> Core::Result<TimelinePage>;
    /** Inclusive time/row ranges, >=1 live row. Equal times select that exact
     * instant; zero matching keys succeeds. Results follow row then time order.
     * 行及时间使用闭区间，至少一有效行；相同首尾时间选该精确时刻，无匹配键成功返回空。
     * 结果按行及时间排列。 */
    [[nodiscard]] auto select(double from, double to, std::size_t first, std::size_t last) const
        -> Core::Result<std::vector<Animation::KeyReference>>;

  private:
    AnimationTimeline(Animation::Clip clip, std::vector<std::size_t> tracks);
    Animation::Clip m_clip;
    std::vector<std::size_t> m_tracks;
  };
}  // namespace Dot::Editor

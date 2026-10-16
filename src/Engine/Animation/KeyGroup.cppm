module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

export module Dot.Animation.KeyGroup;

export import Dot.Animation.Clip;
import Dot.Core.Result;

export namespace Dot::Animation {
  struct GroupKey {
    std::uint32_t node;
    Path path;
    Interpolation interpolation;
    Key key;
  };

  struct KeyGroupDescription {
    std::vector<GroupKey> keys;
    float start;
    float end;
    std::size_t channels;
  };

  /** Immutable owned selection of 1..16384 unique exact source keys. Records
   * retain selection order, values, tangents and each source channel's mode;
   * start/end are the minimum/maximum original time. Partial cubic selections
   * are valid groups, not standalone clips. Source clip retirement/editing cannot
   * change the group; copies share storage, default/moved handles report errors.
   * No scene, bindings, files, devices, history or OS clipboard is touched.
   * 不可变地拥有 1..16384 个唯一精确源键，保留选择顺序、值、切线及通道模式；start/end
   * 为原时间范围。可包含三次轨道的部分键，并非独立片段。源片段销毁/修改不影响组，
   * 副本共享存储，默认/移后句柄报错；不涉及场景/绑定/文件/设备/历史/系统剪贴板。 */
  class KeyGroup {
  public:
    KeyGroup() = default;
    [[nodiscard]] static auto create(const Clip& source, std::span<const KeyReference> keys)
        -> Core::Result<KeyGroup>;
    [[nodiscard]] auto description() const -> Core::Result<const KeyGroupDescription*>;
    /** Place the earliest source key at finite native time [0,86400], adding one
     * double offset before float rounding. Every resulting time must fit and be
     * unique within its node/path. Original placement preserves source float bits.
     * Returns owned records; invalid placement retains the group. Target channel
     * modes/collisions and final clip validity are the paste edit's responsibility.
     * 将最早键放到 [0,86400] 的有限原生时间，各原时间加同一 double 偏移后转 float。
     * 全部目标须在范围内且同通道不重叠，原位精确保留 float 位；结果拥有数据，失败不改组。
     * 目标通道模式/占用及最终片段校验由粘贴编辑负责。 */
    [[nodiscard]] auto at(float time) const -> Core::Result<std::vector<GroupKey>>;

  private:
    struct Data;
    explicit KeyGroup(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> m_data;
  };
}  // namespace Dot::Animation

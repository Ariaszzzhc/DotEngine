module;

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Dot.UI.Layout;

import Dot.Core.Result;
import Dot.Math;
export import Dot.Graphics.Text;

export namespace Dot::UI {
  enum class Kind { Panel, Label, Button };

  /** Parent-local normalized rectangle; text styling uses the native 720-height
   * reference. Only panels may contain children. Panels have no text; visible
   * buttons always block pointer hits. Other elements explicitly choose blocking.
   * 父节点局部归一化矩形；文字样式沿用原生 720 高度基准。仅面板可包含子节点；面板不含
   * 文字，可见按钮始终阻挡指针；其他元素显式选择是否阻挡。 */
  struct Node {
    std::string key;
    std::optional<std::string> parent{};
    Kind kind{Kind::Panel};
    Graphics::TextBox box{};
    bool enabled{true};
    bool blocksPointer{true};
    auto operator==(const Node&) const -> bool = default;
  };

  struct Description {
    std::string name;
    std::vector<Node> nodes;
    auto operator==(const Description&) const -> bool = default;
  };

  struct NodeOverride {
    std::string key;
    std::optional<std::string> text{};
    std::optional<bool> visible{};
    std::optional<bool> enabled{};
  };

  struct ResolvedNode {
    Node local;
    Graphics::TextBox box;
    bool enabled;
  };

  [[nodiscard]] auto validKey(std::string_view key) noexcept -> bool;

  /** Immutable bounded UI definition and owned instance overrides. <=32 nodes,
   * <=8 levels and <=32768 text bytes; each box follows native text validation.
   * Definition order specifies sibling/root paint and Tab order; resolution emits
   * parent-before-child preorder. Parent visibility/enabled state is inherited.
   * Rectangles remain inside their parents without clipping/reflow substitution;
   * collapsed floating-point rectangles fail. No scene/window/device/file access.
   * Candidates never mutate the source; snapshots survive retirement. Owners
   * serialize mutations, while immutable configuration copies may cross threads.
   * 不可变有界 UI 定义及自有实例覆盖：至多 32 节点、8 层、32768 文字字节，每框使用原生
   * 文字校验。定义顺序决定同级/根的绘制与 Tab 顺序，解析输出父先于子的前序。继承父级
   * 显隐/启用状态；矩形须在父内，不用裁剪/重排伪装成功，浮点折叠明确失败。不操作场景、
   * 窗口、设备或文件；候选不改源，快照可晚于退役；调用方串行修改，不可变配置可跨线程。 */
  class Layout {
  public:
    Layout() = default;
    Layout(const Layout&);
    auto operator=(const Layout&) -> Layout&;
    Layout(Layout&&) noexcept;
    auto operator=(Layout&&) noexcept -> Layout&;
    ~Layout();
    [[nodiscard]] static auto create(Description description) -> Core::Result<Layout>;
    /** Borrowed while the configuration lives. 借用有效期同不可变配置。 */
    [[nodiscard]] auto description() const -> Core::Result<const Description*>;
    [[nodiscard]] auto resolve() const -> Core::Result<std::vector<ResolvedNode>>;
    /** Typed atomic subset; unknown/duplicate keys, panel text and invalid budgets
     * reject without touching the original. Empty/same-value patches are no-ops.
     * 类型化原子子集；未知/重复键、面板文字及超预算明确拒绝，不改源；空/同值更新无变化。 */
    [[nodiscard]] auto updated(std::span<const NodeOverride> changes) const -> Core::Result<Layout>;
    [[nodiscard]] auto reset() const -> Core::Result<Layout>;
    /** Authoring hit test, including passive elements, reverse paint order.
     * Half-open [0,1) input; hidden nodes are not selectable.
     * 编辑命中包括被动元素，按逆绘制顺序；输入为半开 [0,1)，不选择隐藏节点。 */
    [[nodiscard]] auto pick(Math::Vec2 point) const -> Core::Result<std::optional<std::string>>;
    auto operator==(const Layout& other) const -> bool;

  private:
    struct Data;
    struct Values;
    Layout(std::shared_ptr<const Data> data, std::shared_ptr<const Values> values) noexcept;
    std::shared_ptr<const Data> m_data;
    std::shared_ptr<const Values> m_values;
  };

  struct Asset {
    std::string path;
    Layout value;
  };
}  // namespace Dot::UI

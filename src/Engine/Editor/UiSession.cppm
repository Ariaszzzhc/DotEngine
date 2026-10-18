module;

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Editor.UiSession;

import Dot.Core.Result;
import Dot.Project;
export import Dot.Editor.UiDocument;
import Dot.Graphics.Text;

export namespace Dot::Editor {
  /** Native resource authoring session, shared by graphical and JSON clients.
   * Typed forms may be incomplete; Apply/history validate a fresh native HUD before
   * publication. Rect gestures preview candidates and produce one undo entry on
   * commit. Cancellation restores the exact form/default preview. Preview options
   * and selection never alter saved values. All operations require owner thread;
   * mutations require exact buffer/revision, picks also require preview generation.
   * Structural commands stage a form: generated keys are deterministic, rename
   * updates child references, removal deletes a subtree, reparent keeps local
   * rectangles and reorder moves within siblings. Apply remains explicit.
   * 结构操作暂存表单：生成确定性键，改名更新子引用，删除整棵子树，改父级保留局部矩形，
   * 排序仅在同级移动；仍须显式应用。
   * 图形与 JSON 客户端共用的原生资源编辑会话。类型表单可暂缺引用；应用/历史在发布前验证
   * 新原生 HUD。矩形手势预览候选，提交仅生成一项撤销，取消恢复原表单/默认预览。预览选项
   * 与选择不改保存值。操作限所有者线程；修改须匹配缓冲/版本，命中另须匹配预览代。 */
  class UiSession {
  public:
    [[nodiscard]] static auto handles(std::string_view command) noexcept -> bool;
    [[nodiscard]] static auto open(const Project::Project& project, std::string_view path,
                                   std::uint64_t buffer) -> Core::Result<UiSession>;
    UiSession(const UiSession&) = delete;
    auto operator=(const UiSession&) -> UiSession& = delete;
    UiSession(UiSession&&) noexcept;
    auto operator=(UiSession&&) noexcept -> UiSession&;
    ~UiSession();
    [[nodiscard]] auto snapshot() const -> Core::Result<std::string>;
    [[nodiscard]] auto perform(std::string_view request) -> Core::Result<std::string>;
    [[nodiscard]] auto draws() const -> Core::Result<std::vector<Graphics::TextLayer>>;
    [[nodiscard]] auto generation() const -> Core::Result<std::uint64_t>;
    [[nodiscard]] auto pending() const -> Core::Result<bool>;

  private:
    struct Impl;
    explicit UiSession(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Editor

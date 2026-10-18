module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Script.Hud;

export import Dot.Graphics.Text;
export import Dot.UI.Layout;
export import Dot.Script.Host;
import Dot.Core.Result;
import Dot.Math;

export namespace Dot::Script {
  struct HudButtonLayer {
    std::string key;
    Graphics::TextBox value;
    bool enabled;
  };

  struct HudSnapshot {
    std::uint64_t scope;
    std::uint64_t revision;
    std::vector<Graphics::TextLayer> labels;
    std::vector<HudButtonLayer> buttons;
    std::optional<std::string> focused;
    std::optional<std::string> hovered;
    std::optional<std::string> pressed;
    std::uint64_t sequence{};
    std::uint64_t buttonRevision{};
  };

  /** Normalized top-left pointer; null means outside/unavailable. Disabling input
   * cancels pointer gestures without activating releases. Native frame edges are
   * published once, independently of fixed steps. Keyboard focus survives blur.
   * 左上归一化指针；null 表示外部/不可用。禁用输入取消指针手势，不激活释放；
   * 原生帧边沿只发布一次，独立于固定步；键盘焦点跨失焦保留。 */
  struct HudInput {
    std::optional<Math::Vec2> pointer;
    bool down{};
    bool pressed{};
    bool released{};
    bool next{};
    bool previous{};
    bool confirm{};
    bool enabled{true};
  };

  struct HudInputResult {
    bool pointer{};
    bool keyboard{};
  };

  /** Scene-local labels/buttons, no windows/devices. One ordered key space holds
   * <=32 elements/32768 text bytes. Owned snapshots/actions and creating-thread
   * operations; retirement preserves reads, rejects writes. Native pointer,
   * keyboard and strict activate operations share action validation/queueing.
   * Keep at most 64 sequenced actions; events(after) explicitly reports loss.
   * 场景本地文字/按钮，不创建窗口或设备；一个有序键空间至多 32 元素/32768 文字字节。
   * 快照/动作拥有数据，操作限创建线程；退役保留读取、拒绝写入。原生指针、键盘与严格
   * activate 共用动作校验/入队；至多保留 64 个有序动作，events(after) 明确报告丢失。 */
  class Hud {
  public:
    [[nodiscard]] static auto create(std::uint64_t scope, std::optional<UI::Asset> layout = {})
        -> Core::Result<Hud>;
    [[nodiscard]] auto snapshot() const -> Core::Result<HudSnapshot>;
    /** Button/pointer-blocker description revision; passive labels and interaction styling
     * do not advance it. 按钮/指针阻挡元素的定义修订，被动文字标签和交互样式不推进此值。 */
    [[nodiscard]] auto buttonRevision() const -> Core::Result<std::uint64_t>;
    [[nodiscard]] auto draws() const -> Core::Result<std::vector<Graphics::TextLayer>>;
    [[nodiscard]] auto publish(const HudInput& input) -> Core::Result<HudInputResult>;
    [[nodiscard]] auto perform(std::string_view request) -> Core::Result<std::string>;
    [[nodiscard]] auto bindings() const -> Core::Result<HostBindings>;
    auto retire() noexcept -> void;

  private:
    struct Impl;
    explicit Hud(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

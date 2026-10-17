module;

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Script.Hud;

export import Dot.Graphics.Text;
export import Dot.Script.Host;
import Dot.Core.Result;

export namespace Dot::Script {
  struct HudSnapshot {
    std::uint64_t scope;
    std::uint64_t revision;
    std::vector<Graphics::TextLayer> labels;
  };

  /** Scene-local owned labels, independent of devices/windows. Owner thread only;
   * copies/bindings retain state, retire rejects writes while preserving snapshots.
   * At most 32 unique ASCII keys, 32768 aggregate text bytes, deterministic key order.
   * perform is shared by scripts and native product operations; constructors and
   * migration may only snapshot. No persistence or input interception.
   * 场景本地拥有的文字层，与设备/窗口无关；限所有者线程；副本/绑定保留状态，
   * 退役禁止写入但保留快照。最多 32 个 ASCII 键、总计 32768 文字字节，按键确定排序。
   * 脚本及产品操作共用 perform；构造/迁移仅可读，不持久化或截获输入。 */
  class Hud {
  public:
    [[nodiscard]] static auto create(std::uint64_t scope) -> Core::Result<Hud>;
    [[nodiscard]] auto snapshot() const -> Core::Result<HudSnapshot>;
    [[nodiscard]] auto perform(std::string_view request) -> Core::Result<std::string>;
    [[nodiscard]] auto bindings() const -> Core::Result<HostBindings>;
    auto retire() noexcept -> void;

  private:
    struct Impl;
    explicit Hud(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Game.Saves;

export import Dot.Game.State;
import Dot.Core.Result;
import Dot.Core.Files;

export namespace Dot::Game {
  /** Pure portable slot-name validation; performs no filesystem access.
   * 纯可移植槽名校验，不访问文件系统。 */
  [[nodiscard]] auto validateSaveSlot(std::string_view slot) -> bool;

  struct SaveLimits {
    StateLimits state;
    std::size_t slots{256};
  };

  struct SaveSlot {
    std::string name;
    std::uint64_t bytes;
  };

  /** Borrow an existing game-owned save directory, never a project/install path
   * chosen implicitly. Flat lowercase portable slot names, no symlinks/aliases or
   * special files. Ignore unrelated extensions, enumerate at most 4096 entries.
   * Operations require the creating thread; moves retain identity. File edits
   * must be serialized by the caller; no hostile race isolation/authentication.
   * Atomic replacement uses FileSystem's flushed sibling temporary file; directory
   * crash durability is not promised. Destruction performs no writes.
   * 借用已有的游戏存档目录，不隐式选择项目/安装路径；槽名为小写可移植平面名称，拒绝
   * 符号链接、大小写别名及特殊文件。忽略其他扩展名，至多枚举 4096 项。操作限创建线程，
   * 移动保留归属；调用者须串行修改文件，不保证恶意竞争隔离或真实性。原子替换使用
   * FileSystem 已刷新的同级临时文件，不承诺目录崩溃持久性；销毁不写文件。 */
  class Saves {
  public:
    [[nodiscard]] static auto open(const Core::FileSystem& root, const SaveLimits& limits = {})
        -> Core::Result<Saves>;
    Saves(const Saves&) = delete;
    auto operator=(const Saves&) -> Saves& = delete;
    Saves(Saves&&) noexcept;
    auto operator=(Saves&&) noexcept -> Saves&;
    ~Saves();

    /** Listing reports bytes without parsing slot contents, so corrupt data can
     * still be located/deleted. 仅列出大小不解析内容，损坏的数据仍可定位/删除。 */
    [[nodiscard]] auto list() const -> Core::Result<std::vector<SaveSlot>>;
    /** Snapshot revision is provenance, not a revision to assign on load.
     * Loading returns owned data and never mutates a live State. Apply it through
     * State::replace with the current revision to prevent losing intervening edits.
     * 快照修订表示来源，读取时不得将其直接用作当前修订；读取拥有数据且不修改实时 State，
     * 通过带当前修订的 State::replace 应用，防止覆盖中间的编辑。 */
    [[nodiscard]] auto read(std::string_view slot) const -> Core::Result<StateSnapshot>;
    /** Existing slots require explicit overwrite. Prepare payload/metadata before
     * file publication; failures preserve the previous slot under the serialized
     * filesystem contract. 现有槽须明确允许覆盖，发布前准备全部数据/元信息；在文件操作
     * 串行化契约下，失败保留原存档。 */
    [[nodiscard]] auto write(std::string_view slot, const StateSnapshot& snapshot, bool overwrite)
        -> Core::Result<SaveSlot>;
    /** Missing slots fail rather than reporting a deletion that did not happen.
     * 不存在的槽返回错误，不伪报已删除。 */
    [[nodiscard]] auto remove(std::string_view slot) -> Core::Result<void>;

  private:
    struct Impl;
    explicit Saves(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Game

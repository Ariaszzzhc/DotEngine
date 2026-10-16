module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module Dot.Game.State;

import Dot.Core.Result;

export namespace Dot::Game {
  struct StateLimits {
    std::size_t bytes{256 * 1024};
    std::size_t entries{256};
  };

  struct StateSnapshot {
    std::uint64_t revision;
    std::string data;
  };

  /** Strict finite scalar-Unicode JSON object, depth <=32. Root keys are literal
   * 1..128-byte UTF-8 names without ASCII controls; numbers stay within JavaScript's
   * safe-integer magnitude. Large counters/IDs must be strings. Normalize before
   * publication; reject duplicate keys, excessive size/depth and invalid limits.
   * 严格有限标量 Unicode JSON 对象，深度至多 32；根键为 1..128 字节 UTF-8 字面名称，
   * 不含 ASCII 控制字符。数字限制在 JavaScript 安全整数幅度内，大计数/ID 使用字符串。
   * 发布前规范化，拒绝重复键、超限数据及非法限额。 */
  [[nodiscard]] auto normalizeStateData(std::string_view data, const StateLimits& limits = {})
      -> Core::Result<std::string>;

  /** Session-independent owned gameplay data, without files/devices/script realms.
   * Copies share the store; snapshots/values own their bytes. Operations require
   * the creating thread, quiescent destruction may run elsewhere. Successful
   * semantic edits increment revision; no-ops preserve it. Replace uses optimistic
   * revision matching. Validate/allocate completely before publication.
   * 独立于场景的游戏数据，不包含文件、设备或脚本 realm。副本共享存储，快照/值拥有字节；
   * 操作限创建线程，静止销毁可异线程执行。语义更改递增修订，无变更保留修订，整表替换
   * 校验预期修订；完整校验及分配后才发布。 */
  class State {
  public:
    [[nodiscard]] static auto create(std::string_view initial = "{}",
                                     const StateLimits& limits = {}) -> Core::Result<State>;
    State(const State&) noexcept;
    auto operator=(const State&) noexcept -> State&;
    State(State&&) noexcept;
    auto operator=(State&&) noexcept -> State&;
    ~State();

    [[nodiscard]] auto snapshot() const -> Core::Result<StateSnapshot>;
    /** Missing is nullopt, distinct from the JSON null value. Keys are not paths.
     * 缺失返回 nullopt，与 JSON null 不同；键不解释为路径。 */
    [[nodiscard]] auto get(std::string_view key) const -> Core::Result<std::optional<std::string>>;
    [[nodiscard]] auto set(std::string_view key, std::string_view value)
        -> Core::Result<std::uint64_t>;
    [[nodiscard]] auto remove(std::string_view key) -> Core::Result<std::uint64_t>;
    [[nodiscard]] auto replace(std::string_view data, std::uint64_t expectedRevision)
        -> Core::Result<std::uint64_t>;

  private:
    struct Impl;
    explicit State(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> m_impl;
  };
}  // namespace Dot::Game

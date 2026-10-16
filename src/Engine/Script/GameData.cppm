module;

#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module Dot.Script.GameData;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Game.Saves;
import Dot.Script.Host;

export namespace Dot::Script {
  struct GameDataConfig {
    std::string initial{"{}"};
    std::optional<Core::FileSystem> saves;
    Game::SaveLimits limits;
  };

  /** Game-wide owned state and explicitly rooted saves, shared across realms.
   * perform is the strict JSON product protocol used by script/editor/agents.
   * No supplied save root means disk operations fail, never simulated success.
   * Creator thread only; retirement permits final in-memory observations but no
   * writes or filesystem access. Copies retain identity and owned observations.
   * 跨 realm 共享的游戏级状态与显式根目录存档；perform 是脚本/编辑器/智能体共用的
   * 严格 JSON 产品协议。未提供存档目录时磁盘操作明确失败，不模拟成功。调用限创建线程；
   * 退役后仅允许内存观察，禁止写入与文件访问；副本保留身份及拥有的观察数据。 */
  class GameData {
  public:
    [[nodiscard]] static auto create(const GameDataConfig& config = {}) -> Core::Result<GameData>;
    GameData(const GameData&) noexcept;
    auto operator=(const GameData&) noexcept -> GameData&;
    GameData(GameData&&) noexcept;
    auto operator=(GameData&&) noexcept -> GameData&;
    ~GameData();

    [[nodiscard]] auto perform(std::string_view request) -> Core::Result<std::string>;
    [[nodiscard]] auto bindings() const -> Core::Result<HostBindings>;
    auto retire() noexcept -> void;

  private:
    struct Impl;
    explicit GameData(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

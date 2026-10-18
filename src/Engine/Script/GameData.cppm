module;

#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module Dot.Script.GameData;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Game.SaveQueue;
import Dot.Script.Host;

export namespace Dot::Script {
  struct GameDataConfig {
    std::string initial{"{}"};
    std::optional<Core::FileSystem> saves;
    Game::SaveQueueLimits limits;
  };

  /** Game-wide owned state and explicitly rooted saves, shared across realms.
   * perform is the strict JSON product protocol used by script/editor/agents.
   * No supplied save root means disk operations fail, never simulated success.
   * Creator thread only. Disk operations return owned background job tickets. Retirement
   * drains accepted work and reports unconsumed mutation failures before returning;
   * quiescent retirement may run on another thread. Later calls permit only memory
   * observations/receipt collection, never new disk work
   * or live load publication. Copies retain identity and owned observations.
   * 跨 realm 共享的游戏级状态与显式根目录存档；perform 是脚本/编辑器/智能体共用的
   * 严格 JSON 产品协议。未提供存档目录时磁盘操作明确失败，不模拟成功。调用限创建线程；
   * 磁盘操作返回后台作业票据；退役排空已接受作业并报告未消费修改失败，再返回。
   * 静止退役可在其他线程；后续仅允许内存观察/回执提取，不允许新磁盘作业或实时加载发布；副本保留身份与观察。
   */
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
    [[nodiscard]] auto retire() -> Core::Result<void>;

  private:
    struct Impl;
    explicit GameData(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

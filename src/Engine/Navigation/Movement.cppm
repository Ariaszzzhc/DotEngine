module;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

export module Dot.Navigation.Movement;

export import Dot.Navigation.Steering;
import Dot.Navigation.Surface;
import Dot.Core.Result;
import Dot.Math;

export namespace Dot::Navigation {
  struct MoveRequest {
    Math::Vec3 goal;
    float speed{1.2F};
    float acceleration{8};
    float stoppingDistance{.12F};
    float neighborDistance{4};
    float timeHorizon{2.5F};
    auto operator==(const MoveRequest&) const -> bool = default;
  };

  struct MovementBody {
    SteeringBody body;
    bool grounded{};
  };

  struct MovementIntent {
    std::uint64_t entity;
    Math::Vec3 velocity;
  };

  enum class MovePhase { Moving, Arrived, Failed };

  struct MoveState {
    MoveRequest request;
    MovePhase phase;
    /** Last planned horizontal input, not achieved motion. 最近规划的水平输入，不是已实现运动。 */
    Math::Vec3 velocity;
    float distance{};
    bool blocked{};
    std::size_t corner{}, corners{}, neighbors{};
    std::optional<Core::Error> error;
  };

  [[nodiscard]] auto validateMoveRequest(const MoveRequest& request) -> Core::Result<void>;

  /** Native scoped path following and reciprocal velocity planning, never pose integration.
   * Request failures retain the old intent. Advance consumes one simultaneous measured snapshot,
   * prepares all <=256 intents before publication, and removes missing owners. Per-agent planning
   * failures stop/report failure; arrival always derives from actual grounded feet. Copies of
   * immutable paths/walls are shared; controller state is owned and not concurrent. Physics must
   * consume returned intents and provide the next actual observation, never predicted positions.
   * 原生范围内路径跟随与互惠速度规划，不积分位姿。请求失败保留旧意图；推进消费同一实际
   * 快照，准备全部至多 256 个意图后发布，并移除缺失所有者。单角色规划失败停止并报告，
   * 到达始终依据实际着地脚底。不可变路径/边界共享，控制状态独占且不并发；物理消费意图并
   * 提供下次实际观测，不得以预测位置替代。 */
  class Movement {
  public:
    [[nodiscard]] static auto create(Surface surface) -> Core::Result<Movement>;
    Movement(const Movement&) = delete;
    auto operator=(const Movement&) -> Movement& = delete;
    Movement(Movement&&) noexcept;
    auto operator=(Movement&&) noexcept -> Movement&;
    ~Movement();
    [[nodiscard]] auto move(const MovementBody& body, const MoveRequest& request)
        -> Core::Result<void>;
    [[nodiscard]] auto stop(std::uint64_t entity) -> Core::Result<void>;
    [[nodiscard]] auto state(const MovementBody& body) const
        -> Core::Result<std::optional<MoveState>>;
    [[nodiscard]] auto advance(std::span<const MovementBody> bodies, double seconds)
        -> Core::Result<std::vector<MovementIntent>>;
    [[nodiscard]] auto entities() const -> Core::Result<std::vector<std::uint64_t>>;
    [[nodiscard]] auto fail(const Core::Error& error) -> Core::Result<void>;
    [[nodiscard]] auto fail(std::uint64_t entity, const Core::Error& error) -> Core::Result<void>;

  private:
    struct Impl;
    explicit Movement(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Navigation

module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

export module Dot.Animation.Controller;

import Dot.Core.Result;

export namespace Dot::Animation {
  using ParameterValue = std::variant<bool, double>;

  struct ControllerParameter {
    std::string name;
    ParameterValue value{false};
    auto operator==(const ControllerParameter&) const -> bool = default;
  };

  struct ControllerState {
    std::string name;
    std::string clip;
    double speed{1};
    bool looping{true};
    auto operator==(const ControllerState&) const -> bool = default;
  };

  enum class ConditionOp { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };

  struct ControllerCondition {
    std::string parameter;
    ConditionOp operation{ConditionOp::Equal};
    ParameterValue value{false};
    auto operator==(const ControllerCondition&) const -> bool = default;
  };

  struct ControllerTransition {
    std::string name;
    std::string from;
    std::string to;
    std::vector<ControllerCondition> conditions;
    double fadeSeconds{.12};
    double minimumSeconds{};
    std::optional<double> exitTime{};
    auto operator==(const ControllerTransition&) const -> bool = default;
  };

  struct ControllerDescription {
    std::string name;
    std::string initial;
    std::vector<ControllerParameter> parameters;
    std::vector<ControllerState> states;
    std::vector<ControllerTransition> transitions;
    auto operator==(const ControllerDescription&) const -> bool = default;
  };

  struct ControllerSnapshot {
    std::string state;
    double elapsedSeconds;
    std::vector<ControllerParameter> parameters;
    std::optional<std::string> lastTransition;
    std::uint64_t transitions;
  };

  /** Immutable definitions with owned typed parameters and state age. At most 64
   * parameters/states, 512 ordered transitions and eight AND conditions per edge.
   * The first eligible outgoing edge wins, at most once per positive simulation
   * call. Exit time includes directional normalized progress crossed in this tick, so
   * looping end thresholds cannot be skipped by wrapping. Zero-duration
   * clips have progress one. Age/fades use simulation seconds, independently of
   * clip speed. Candidates never sample clips, touch scenes/files or publish state;
   * the owner commits only after the complete native pose succeeds. No expression
   * VM, input inference, wall clock or automatic persistence. Owner serialization
   * is required. Copies share definitions/parameter banks; snapshots own data and moves retire
   * sources. 不可变定义及自有类型参数/状态年龄；至多 64 参数/状态、512 条有序边、每边八个 AND
   * 条件。每次正时长模拟仅选择第一条满足条件的出边。退出时间检查本次步长跨过的方向归一进度，避免循环回绕漏过末尾阈值；
   * 零时长进度为一；年龄/淡化使用模拟秒数，不随片段速度缩放。候选不采样片段、不操作场景/
   * 文件、不发布状态；调用方仅在完整原生姿态成功后提交。无表达式 VM、输入推断、墙钟或
   * 自动持久化。调用方须串行；副本共享定义/参数，快照拥有数据，移动使源失效。 */
  class Controller {
  public:
    Controller() = default;
    Controller(const Controller&);
    auto operator=(const Controller&) -> Controller&;
    Controller(Controller&&) noexcept;
    auto operator=(Controller&&) noexcept -> Controller&;
    ~Controller();
    [[nodiscard]] static auto create(ControllerDescription description) -> Core::Result<Controller>;
    /** Borrowed while this immutable configuration lives. 借用有效期同不可变配置。 */
    [[nodiscard]] auto description() const -> Core::Result<const ControllerDescription*>;
    [[nodiscard]] auto state() const -> Core::Result<const ControllerState*>;
    [[nodiscard]] auto snapshot() const -> Core::Result<ControllerSnapshot>;
    [[nodiscard]] auto transitionCount() const -> Core::Result<std::uint64_t>;
    [[nodiscard]] auto lastTransition() const -> Core::Result<const ControllerTransition*>;
    /** Atomic named subset update; reject duplicates/unknown names, type changes
     * and nonfinite/out-of-range numbers. Never evaluates or advances the graph.
     * 原子更新命名参数子集；拒绝重复/未知名称、类型变化及非有限/越界数字，不求值或推进图。 */
    [[nodiscard]] auto withParameters(std::span<const ControllerParameter> parameters) const
        -> Core::Result<Controller>;
    [[nodiscard]] auto advanced(double seconds, double clipTime, double duration) const
        -> Core::Result<Controller>;
    [[nodiscard]] auto reset() const -> Core::Result<Controller>;

  private:
    struct Data;
    explicit Controller(std::shared_ptr<const Data> data, std::vector<ParameterValue> values,
                        std::size_t initial);
    std::shared_ptr<const Data> m_data;
    std::shared_ptr<const std::vector<ParameterValue>> m_values;
    std::size_t m_state{};
    double m_elapsed{};
    std::optional<std::size_t> m_last;
    std::uint64_t m_transitions{};
  };
}  // namespace Dot::Animation

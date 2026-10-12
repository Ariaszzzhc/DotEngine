module;

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

export module Dot.Core.Events;

export namespace Dot::Core {
  /**
   * Synchronous, single-thread event dispatch with scoped subscriptions.
   * 同步单线程事件分发，订阅随连接对象释放。
   * New listeners wait for the next dispatch; disconnected listeners are skipped.
   * 新订阅从下一次分发开始生效；已断开的订阅会被跳过。
   * Callback exceptions propagate; the signal remains usable. 回调异常外抛，信号仍可使用。
   */
  template <typename Event>
  class Signal {
    struct Slot {
      std::function<void(const Event&)> callback;
      bool active{true};
    };

    struct State {
      std::vector<std::shared_ptr<Slot>> slots;
    };

  public:
    class Connection {
    public:
      Connection() = default;
      Connection(const Connection&) = delete;
      auto operator=(const Connection&) -> Connection& = delete;
      Connection(Connection&&) noexcept = default;

      auto operator=(Connection&& other) noexcept -> Connection& {
        if (this != &other) {
          disconnect();
          m_slot = std::move(other.m_slot);
        }

        return *this;
      }

      ~Connection() {
        disconnect();
      }

      auto disconnect() noexcept -> void {
        if (auto slot = m_slot.lock()) {
          slot->active = false;
        }

        m_slot.reset();
      }

      [[nodiscard]] auto connected() const noexcept -> bool {
        const auto slot = m_slot.lock();
        return slot && slot->active;
      }

    private:
      friend class Signal;

      explicit Connection(const std::shared_ptr<Slot>& slot) : m_slot(slot) {
      }

      std::weak_ptr<Slot> m_slot;
    };

    Signal() = default;
    Signal(const Signal&) = delete;
    auto operator=(const Signal&) -> Signal& = delete;
    Signal(Signal&&) noexcept = default;
    auto operator=(Signal&&) noexcept -> Signal& = default;

    [[nodiscard]] auto subscribe(std::function<void(const Event&)> callback) -> Connection {
      if (!callback) {
        throw std::invalid_argument("Event callback must not be empty");
      }

      if (!m_state) {
        m_state = std::make_shared<State>();
      }

      std::erase_if(m_state->slots, [](const auto& slot) { return !slot->active; });
      auto slot = std::make_shared<Slot>(std::move(callback));
      m_state->slots.push_back(slot);
      return Connection(slot);
    }

    auto publish(const Event& event) -> std::size_t {
      const auto state = m_state;

      if (!state) {
        return 0;
      }

      std::erase_if(state->slots, [](const auto& slot) { return !slot->active; });
      const auto snapshot = state->slots;
      std::size_t delivered = 0;

      for (const auto& slot : snapshot) {
        if (slot->active) {
          slot->callback(event);
          ++delivered;
        }
      }

      return delivered;
    }

    [[nodiscard]] auto listeners() const noexcept -> std::size_t {
      if (!m_state) {
        return 0;
      }

      return static_cast<std::size_t>(std::count_if(m_state->slots.begin(), m_state->slots.end(),
                                                    [](const auto& slot) { return slot->active; }));
    }

  private:
    std::shared_ptr<State> m_state;
  };
}  // namespace Dot::Core

module;

#include <atomic>
#include <cstddef>
#include <memory_resource>
#include <stdexcept>

export module Dot.Core.Memory;

export namespace Dot::Core {
  struct MemoryStats {
    std::size_t liveBytes;
    std::size_t peakBytes;
    std::size_t allocations;
    std::size_t liveAllocations;
  };

  /**
   * Non-owning upstream, which must outlive this resource and all its allocations.
   * 上游资源为非拥有引用，须比本资源和所有分配存活更久。
   * Counters are atomic; a coherent snapshot requires quiescent allocation activity.
   * 计数器为原子变量；一致快照须在分配活动静止时读取。
   */
  class TrackingResource final : public std::pmr::memory_resource {
  public:
    explicit TrackingResource(std::pmr::memory_resource* upstream = std::pmr::new_delete_resource())
        : m_upstream(upstream) {
      if (!upstream) {
        throw std::invalid_argument("Memory upstream must not be null");
      }
    }

    [[nodiscard]] auto stats() const noexcept -> MemoryStats {
      return {m_liveBytes.load(), m_peakBytes.load(), m_allocations.load(),
              m_liveAllocations.load()};
    }

  private:
    auto do_allocate(std::size_t bytes, std::size_t alignment) -> void* override {
      auto* allocation = m_upstream->allocate(bytes, alignment);
      const auto live = m_liveBytes.fetch_add(bytes) + bytes;
      auto peak = m_peakBytes.load();
      while (peak < live && !m_peakBytes.compare_exchange_weak(peak, live)) {
      }
      m_allocations.fetch_add(1);
      m_liveAllocations.fetch_add(1);
      return allocation;
    }

    auto do_deallocate(void* allocation, std::size_t bytes, std::size_t alignment)
        -> void override {
      m_upstream->deallocate(allocation, bytes, alignment);
      m_liveBytes.fetch_sub(bytes);
      m_liveAllocations.fetch_sub(1);
    }

    [[nodiscard]] auto do_is_equal(const std::pmr::memory_resource& other) const noexcept
        -> bool override {
      return this == &other;
    }

    std::pmr::memory_resource* m_upstream;
    std::atomic<std::size_t> m_liveBytes{0};
    std::atomic<std::size_t> m_peakBytes{0};
    std::atomic<std::size_t> m_allocations{0};
    std::atomic<std::size_t> m_liveAllocations{0};
  };
}  // namespace Dot::Core

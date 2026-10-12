module;
#include <atomic>
#include <cstddef>
#include <memory_resource>

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
    explicit TrackingResource(std::pmr::memory_resource* upstream
                              = std::pmr::new_delete_resource());
    [[nodiscard]] auto stats() const noexcept -> MemoryStats;

  private:
    auto do_allocate(std::size_t bytes, std::size_t alignment) -> void* override;
    auto do_deallocate(void* allocation, std::size_t bytes, std::size_t alignment) -> void override;
    [[nodiscard]] auto do_is_equal(const std::pmr::memory_resource& other) const noexcept
        -> bool override;
    std::pmr::memory_resource* m_upstream;
    std::atomic<std::size_t> m_liveBytes{0};
    std::atomic<std::size_t> m_peakBytes{0};
    std::atomic<std::size_t> m_allocations{0};
    std::atomic<std::size_t> m_liveAllocations{0};
  };
}  // namespace Dot::Core

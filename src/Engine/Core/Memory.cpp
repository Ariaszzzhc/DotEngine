module;

#include <atomic>
#include <cstddef>
#include <memory_resource>
#include <stdexcept>

module Dot.Core.Memory;

namespace Dot::Core {
  TrackingResource::TrackingResource(std::pmr::memory_resource* upstream) : m_upstream(upstream) {
    if (!upstream) {
      throw std::invalid_argument("Memory upstream must not be null");
    }
  }

  auto TrackingResource::stats() const noexcept -> MemoryStats {
    return {m_liveBytes.load(), m_peakBytes.load(), m_allocations.load(), m_liveAllocations.load()};
  }

  auto TrackingResource::do_allocate(std::size_t bytes, std::size_t alignment) -> void* {
    auto* allocation = m_upstream->allocate(bytes, alignment);
    const auto live = m_liveBytes.fetch_add(bytes) + bytes;
    auto peak = m_peakBytes.load();

    while (peak < live && !m_peakBytes.compare_exchange_weak(peak, live)) {
    }

    m_allocations.fetch_add(1);
    m_liveAllocations.fetch_add(1);
    return allocation;
  }

  auto TrackingResource::do_deallocate(void* allocation, std::size_t bytes, std::size_t alignment)
      -> void {
    m_upstream->deallocate(allocation, bytes, alignment);
    m_liveBytes.fetch_sub(bytes);
    m_liveAllocations.fetch_sub(1);
  }

  auto TrackingResource::do_is_equal(const std::pmr::memory_resource& other) const noexcept
      -> bool {
    return this == &other;
  }

}  // namespace Dot::Core

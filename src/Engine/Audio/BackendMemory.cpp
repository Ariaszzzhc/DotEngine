#include "BackendMemory.hpp"

#include <cstdlib>
#include <limits>

namespace Dot::Audio::Detail {
  BackendCounters mixerMemory;
  BackendCounters outputMemory;

  namespace {
    struct alignas(std::max_align_t) Allocation {
      std::size_t bytes;
    };
  }  // namespace

  BackendHeap::BackendHeap(std::uint64_t limit, BackendCounters& total) noexcept
      : m_limit(limit), m_total(total) {
  }

  auto BackendHeap::reserve(std::uint64_t size) noexcept -> bool {
    auto current = bytes.load();

    while (size <= m_limit && current <= m_limit - size) {
      if (bytes.compare_exchange_weak(current, current + size)) {
        return true;
      }
    }

    return false;
  }

  auto BackendHeap::allocate(std::size_t size, void* data) noexcept -> void* {
    auto& heap = *static_cast<BackendHeap*>(data);

    if (size > std::numeric_limits<std::size_t>::max() - sizeof(Allocation)
        || !heap.reserve(size)) {
      return nullptr;
    }

    auto* allocation = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));

    if (!allocation) {
      heap.bytes.fetch_sub(size);
      return nullptr;
    }

    allocation->bytes = size;
    heap.allocations.fetch_add(1);
    heap.m_total.allocations.fetch_add(1);
    heap.m_total.bytes.fetch_add(size);
    return allocation + 1;
  }

  auto BackendHeap::release(void* pointer, void* data) noexcept -> void {
    if (!pointer) {
      return;
    }

    auto& heap = *static_cast<BackendHeap*>(data);
    auto* allocation = static_cast<Allocation*>(pointer) - 1;
    heap.allocations.fetch_sub(1);
    heap.bytes.fetch_sub(allocation->bytes);
    heap.m_total.allocations.fetch_sub(1);
    heap.m_total.bytes.fetch_sub(allocation->bytes);
    std::free(allocation);
  }

  auto BackendHeap::reallocate(void* pointer, std::size_t size, void* data) noexcept -> void* {
    if (!pointer) {
      return allocate(size, data);
    }

    if (size == 0) {
      release(pointer, data);
      return nullptr;
    }

    auto& heap = *static_cast<BackendHeap*>(data);
    auto* old = static_cast<Allocation*>(pointer) - 1;
    const auto previous = old->bytes;
    const auto increase = size > previous ? size - previous : 0;

    if (size > std::numeric_limits<std::size_t>::max() - sizeof(Allocation)
        || !heap.reserve(increase)) {
      return nullptr;
    }

    auto* changed = static_cast<Allocation*>(std::realloc(old, sizeof(Allocation) + size));

    if (!changed) {
      heap.bytes.fetch_sub(increase);
      return nullptr;
    }

    changed->bytes = size;

    if (size >= previous) {
      heap.m_total.bytes.fetch_add(increase);
    } else {
      heap.bytes.fetch_sub(previous - size);
      heap.m_total.bytes.fetch_sub(previous - size);
    }

    return changed + 1;
  }

  auto BackendHeap::callbacks() noexcept -> ma_allocation_callbacks {
    return {this, allocate, reallocate, release};
  }

}  // namespace Dot::Audio::Detail

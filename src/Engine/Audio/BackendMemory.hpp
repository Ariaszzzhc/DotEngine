#pragma once

#include <miniaudio.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Dot::Audio::Detail {
  struct BackendCounters {
    std::atomic<std::uint64_t> allocations{};
    std::atomic<std::uint64_t> bytes{};
  };

  extern BackendCounters mixerMemory;
  extern BackendCounters outputMemory;

  /** Stable callback owner with concurrent byte reservations and rollback.
   * 稳定的回调所有者，支持并发字节预留和失败回退；必须比后端分配存活更久。 */
  class BackendHeap {
  public:
    BackendHeap(std::uint64_t limit, BackendCounters& total) noexcept;
    BackendHeap(const BackendHeap&) = delete;
    auto operator=(const BackendHeap&) -> BackendHeap& = delete;

    std::atomic<std::uint64_t> allocations{};
    std::atomic<std::uint64_t> bytes{};
    auto callbacks() noexcept -> ma_allocation_callbacks;

  private:
    auto reserve(std::uint64_t size) noexcept -> bool;
    static auto allocate(std::size_t size, void* data) noexcept -> void*;
    static auto release(void* pointer, void* data) noexcept -> void;
    static auto reallocate(void* pointer, std::size_t size, void* data) noexcept -> void*;

    std::uint64_t m_limit;
    BackendCounters& m_total;
  };
}  // namespace Dot::Audio::Detail

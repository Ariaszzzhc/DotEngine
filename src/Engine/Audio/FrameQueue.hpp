#pragma once

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace Dot::Audio::Detail {
  /** Bounded SPSC interleaved PCM. One producer writes; one consumer reads.
   * Quiescent reset/destruction only. Release/acquire publishes complete writes.
   * 有界单生产者、单消费者交错 PCM；仅静止时重置/销毁，发布完整写入后才允许读取。 */
  class FrameQueue {
  public:
    FrameQueue(std::uint32_t channels, std::uint32_t frames);
    FrameQueue(const FrameQueue&) = delete;
    auto operator=(const FrameQueue&) -> FrameQueue& = delete;
    auto write(std::span<const float> samples) noexcept -> bool;
    /** Zero fills any unavailable tail. No allocation, locking, or exception.
     * 不可用的尾部填零，不分配、不加锁、不抛出异常。 */
    auto read(float* output, std::uint32_t frames) noexcept -> std::uint32_t;
    auto queued() const noexcept -> std::uint32_t;
    auto available() const noexcept -> std::uint32_t;
    auto reset() noexcept -> void;

  private:
    std::uint32_t m_channels;
    std::uint32_t m_capacity;
    std::vector<float> m_samples;
    alignas(128) std::atomic<std::uint64_t> m_written{};
    alignas(128) std::atomic<std::uint64_t> m_read{};
  };
}  // namespace Dot::Audio::Detail

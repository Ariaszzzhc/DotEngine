#include "FrameQueue.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace Dot::Audio::Detail {
  FrameQueue::FrameQueue(std::uint32_t channels, std::uint32_t frames)
      : m_channels(channels), m_capacity(frames) {
    if ((channels != 1 && channels != 2) || !std::has_single_bit(frames) || frames < 64
        || frames > 262144) {
      throw std::invalid_argument("Invalid PCM queue format/capacity");
    }

    m_samples.resize(static_cast<std::size_t>(channels) * frames);
  }

  auto FrameQueue::queued() const noexcept -> std::uint32_t {
    /** Producer-side snapshot; consumer progress may only increase free space.
     * 生产者侧快照，消费者推进只会增加空余容量。 */
    const auto written = m_written.load(std::memory_order_relaxed);
    const auto read = m_read.load(std::memory_order_acquire);
    return static_cast<std::uint32_t>(written - read);
  }

  auto FrameQueue::available() const noexcept -> std::uint32_t {
    return m_capacity - queued();
  }

  auto FrameQueue::write(std::span<const float> samples) noexcept -> bool {
    if (samples.empty() || samples.size() % m_channels != 0
        || samples.size() / m_channels > available()) {
      return false;
    }

    const auto frames = static_cast<std::uint32_t>(samples.size() / m_channels);
    const auto written = m_written.load(std::memory_order_relaxed);
    const auto offset = static_cast<std::uint32_t>(written & (m_capacity - 1));
    const auto first = std::min(frames, m_capacity - offset);
    const auto count = static_cast<std::size_t>(first) * m_channels;
    std::memcpy(m_samples.data() + static_cast<std::size_t>(offset) * m_channels, samples.data(),
                count * sizeof(float));
    std::memcpy(m_samples.data(), samples.data() + count,
                static_cast<std::size_t>(frames - first) * m_channels * sizeof(float));
    m_written.store(written + frames, std::memory_order_release);
    return true;
  }

  auto FrameQueue::read(float* output, std::uint32_t frames) noexcept -> std::uint32_t {
    if (frames == 0) {
      return 0;
    }

    const auto read = m_read.load(std::memory_order_relaxed);
    const auto written = m_written.load(std::memory_order_acquire);
    const auto count = static_cast<std::uint32_t>(std::min<std::uint64_t>(frames, written - read));
    const auto offset = static_cast<std::uint32_t>(read & (m_capacity - 1));
    const auto first = std::min(count, m_capacity - offset);
    const auto samples = static_cast<std::size_t>(first) * m_channels;
    std::memcpy(output, m_samples.data() + static_cast<std::size_t>(offset) * m_channels,
                samples * sizeof(float));
    std::memcpy(output + samples, m_samples.data(),
                static_cast<std::size_t>(count - first) * m_channels * sizeof(float));
    std::fill_n(output + static_cast<std::size_t>(count) * m_channels,
                static_cast<std::size_t>(frames - count) * m_channels, 0.0F);
    m_read.store(read + count, std::memory_order_release);
    return count;
  }

  auto FrameQueue::reset() noexcept -> void {
    m_written.store(0, std::memory_order_relaxed);
    m_read.store(0, std::memory_order_relaxed);
  }
}  // namespace Dot::Audio::Detail

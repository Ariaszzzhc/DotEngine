module;

#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module Dot.Graphics.Renderer;

import Dot.Core.Result;

#include "FrameProfiler.hpp"

namespace Dot::Graphics::Detail {
  namespace {
    using Clock = std::chrono::steady_clock;
    constexpr std::size_t historySize = 120;
    constexpr std::size_t slotCount = 4;
    constexpr std::uint32_t maxPasses = 384 + 3 + 2;
    constexpr std::uint32_t queryCount = maxPasses * 2;
    constexpr std::uint64_t queryBytes = queryCount * sizeof(std::uint64_t);

    auto elapsed(Clock::time_point start) -> std::uint64_t {
      return static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
    }

    template <typename T>
    auto append(std::array<std::optional<T>, historySize>& history, std::size_t& next, T value)
        -> void {
      history[next] = std::move(value);
      next = (next + 1) % historySize;
    }

    template <typename T>
    auto ordered(const std::array<std::optional<T>, historySize>& history) -> std::vector<T> {
      std::vector<T> result;
      result.reserve(historySize);

      for (const auto& value : history) {
        if (value) {
          result.push_back(*value);
        }
      }

      std::ranges::sort(result, {}, &T::serial);
      return result;
    }
  }  // namespace

  struct FrameProfiler::Impl {
    enum class Fault { None, Readback, Timestamp, Serial, Allocation, Empty, Capacity };

    struct Readback {
      wgpu::Buffer buffer;
      std::array<std::uint64_t, queryCount> words{};
      std::atomic<unsigned> complete{};
      unsigned error{};
      std::size_t bytes{};
    };

    struct Slot {
      wgpu::QuerySet queries;
      wgpu::Buffer resolved;
      std::shared_ptr<Readback> readback;
      MeshGpuTiming sample;
      std::array<ProfilePass, maxPasses> passes{};
      std::uint32_t count{};
      bool busy{};
    };

    wgpu::Instance instance;
    wgpu::Device device;
    bool supported{};
    std::array<Slot, slotCount> slots;
    std::optional<std::size_t> activeGpu;
    bool active{};
    MeshCpuTiming current;
    Clock::time_point phase;
    std::uint64_t serial{};
    std::uint64_t dropped{};
    Fault fault{Fault::None};
    unsigned errorStatus{};
    std::array<std::optional<MeshCpuTiming>, historySize> cpu;
    std::array<std::optional<MeshGpuTiming>, historySize> gpu;
    std::size_t nextCpu{};
    std::size_t nextGpu{};

    auto poll() -> void {
      instance.ProcessEvents();

      for (auto& slot : slots) {
        if (!slot.busy || !slot.readback->complete.load(std::memory_order_acquire)) {
          continue;
        }

        slot.busy = false;

        if (slot.readback->complete.load(std::memory_order_relaxed) != 1) {
          fault = Fault::Readback;
          errorStatus = slot.readback->error;
          continue;
        }

        const auto& words = slot.readback->words;
        bool valid = slot.count > 0;
        bool nonempty{};
        std::uint32_t mask{};
        auto earliest = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t latest{};

        for (std::uint32_t range = 0; range < slot.count; ++range) {
          const auto begin = range * 2;
          const auto pass = static_cast<std::size_t>(slot.passes[range]);
          mask |= 1U << pass;
          nonempty |= words[begin] != 0 || words[begin + 1] != 0;
          earliest = std::min(earliest, words[begin]);
          latest = std::max(latest, words[begin + 1]);
          const auto previous = slot.sample.passNanoseconds[pass].value_or(0);
          const auto duration = words[begin + 1] - words[begin];

          if (words[begin + 1] < words[begin]
              || duration > std::numeric_limits<std::uint64_t>::max() - previous) {
            valid = false;
            continue;
          }

          slot.sample.passNanoseconds[pass] = previous + duration;
        }

        if (!nonempty) {
          fault = Fault::Empty;
          continue;
        }

        if (!valid || (mask & 12U) != 12U || latest < earliest) {
          fault = Fault::Timestamp;
          continue;
        }

        slot.sample.totalNanoseconds = latest - earliest;
        append(gpu, nextGpu, slot.sample);
      }
    }
  };

  FrameProfiler::FrameProfiler(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  FrameProfiler::~FrameProfiler() {
    for (auto& slot : m_impl->slots) {
      if (slot.readback) {
        if (slot.readback->buffer) {
          slot.readback->buffer.Destroy();
        }

        if (slot.resolved) {
          slot.resolved.Destroy();
        }

        if (slot.queries) {
          slot.queries.Destroy();
        }
      }
    }

    m_impl->instance.ProcessEvents();
  }

  auto FrameProfiler::create(wgpu::Instance instance, wgpu::Device device, bool gpuSupported)
      -> Core::Result<std::unique_ptr<FrameProfiler>> {
    try {
      auto impl = std::make_unique<Impl>();
      impl->instance = std::move(instance);
      impl->device = std::move(device);
      impl->supported = gpuSupported;
      auto result = std::unique_ptr<FrameProfiler>(new FrameProfiler(std::move(impl)));

      if (gpuSupported) {
        for (auto& slot : result->m_impl->slots) {
          wgpu::QuerySetDescriptor queries;
          /** Unlabelled sets retain Dawn's native counter pooling.
           * 不附查询集标签，以保留 Dawn 的原生计数器池复用。 */
          queries.type = wgpu::QueryType::Timestamp;
          queries.count = queryCount;
          slot.queries = result->m_impl->device.CreateQuerySet(&queries);
          wgpu::BufferDescriptor buffer;
          buffer.size = queryBytes;
          buffer.usage = wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc;
          slot.resolved = result->m_impl->device.CreateBuffer(&buffer);
          buffer.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
          slot.readback = std::make_shared<Impl::Readback>();
          slot.readback->buffer = result->m_impl->device.CreateBuffer(&buffer);

          if (!slot.queries || !slot.resolved || !slot.readback->buffer) {
            return Core::fail(Core::ErrorCode::External, "Allocate GPU profiling queries/buffers");
          }
        }
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create frame profiler: " + std::string(error.what()));
    }
  }

  auto FrameProfiler::begin() -> void {
    cancel();
    auto& state = *m_impl;
    state.poll();

    if (state.serial == std::numeric_limits<std::uint64_t>::max()) {
      state.fault = Impl::Fault::Serial;
      return;
    }

    state.current = {};
    state.current.serial = state.serial + 1;
    state.active = true;

    if (state.supported && state.fault == Impl::Fault::None) {
      for (std::size_t i = 0; i < state.slots.size(); ++i) {
        if (!state.slots[i].busy) {
          state.activeGpu = i;
          state.slots[i].count = 0;
          break;
        }
      }
    }

    state.phase = Clock::now();
  }

  auto FrameProfiler::prepared() -> void {
    if (m_impl->active) {
      m_impl->current.prepareNanoseconds = elapsed(m_impl->phase);
    }
  }

  auto FrameProfiler::encoding(std::uint32_t width, std::uint32_t height) -> void {
    m_impl->current.width = width;
    m_impl->current.height = height;
    m_impl->phase = Clock::now();
  }

  auto FrameProfiler::encoded() -> void {
    m_impl->current.encodeNanoseconds = elapsed(m_impl->phase);
    m_impl->phase = Clock::now();
  }

  auto FrameProfiler::cancel() -> void {
    m_impl->active = false;
    m_impl->activeGpu.reset();
  }

  auto FrameProfiler::writes(ProfilePass pass) -> wgpu::PassTimestampWrites {
    if (!m_impl->active || !m_impl->activeGpu) {
      return {};
    }

    auto& slot = m_impl->slots[*m_impl->activeGpu];

    if (slot.count == maxPasses) {
      m_impl->fault = Impl::Fault::Capacity;
      return {};
    }

    /** Pair writes inside each physical pass. Some Metal stage-boundary paths
     * abort commands with split begin-only/end-only ranges; aggregate pairs instead.
     * 每个物理通道内成对写入；部分 Metal 阶段边界路径会中止拆分的单端范围，改为汇总完整对。 */
    const auto index = slot.count++;
    slot.passes[index] = pass;
    wgpu::PassTimestampWrites result;
    result.querySet = slot.queries;
    result.beginningOfPassWriteIndex = index * 2;
    result.endOfPassWriteIndex = index * 2 + 1;
    return result;
  }

  auto FrameProfiler::resolve(const wgpu::CommandEncoder& encoder) -> void {
    if (m_impl->activeGpu) {
      auto& slot = m_impl->slots[*m_impl->activeGpu];
      const auto count = slot.count * 2;
      encoder.ResolveQuerySet(slot.queries, 0, count, slot.resolved, 0);
      encoder.CopyBufferToBuffer(slot.resolved, 0, slot.readback->buffer, 0,
                                 count * sizeof(std::uint64_t));
    }
  }

  auto FrameProfiler::submitted(const MeshFrameStats& work) -> void {
    auto& state = *m_impl;

    if (!state.active) {
      return;
    }

    state.current.submitNanoseconds = elapsed(state.phase);
    state.current.work = work;
    state.serial = state.current.serial;
    append(state.cpu, state.nextCpu, state.current);

    if (state.activeGpu) {
      auto& slot = state.slots[*state.activeGpu];
      slot.sample = {};
      slot.sample.serial = state.serial;
      slot.sample.width = state.current.width;
      slot.sample.height = state.current.height;
      slot.sample.work = work;
      slot.busy = true;
      const auto pending = slot.readback;
      pending->complete.store(0, std::memory_order_relaxed);
      pending->error = 0;
      pending->bytes = slot.count * 2 * sizeof(std::uint64_t);
      /** Callback owns only its readback state, never the profiler/renderer.
       * 回调只拥有读回状态，不借用分析器或渲染器。 */
      try {
        pending->buffer.MapAsync(
            wgpu::MapMode::Read, 0, pending->bytes, wgpu::CallbackMode::AllowProcessEvents,
            [pending](wgpu::MapAsyncStatus status, wgpu::StringView) noexcept {
              if (status != wgpu::MapAsyncStatus::Success) {
                pending->error = static_cast<unsigned>(status);
                pending->complete.store(2, std::memory_order_release);
                return;
              }

              const auto* data = pending->buffer.GetConstMappedRange(0, pending->bytes);

              if (data) {
                std::memcpy(pending->words.data(), data, pending->bytes);
              } else {
                pending->error = std::numeric_limits<unsigned>::max();
              }

              pending->buffer.Unmap();
              pending->complete.store(data ? 1U : 2U, std::memory_order_release);
            });
      } catch (const std::exception&) {
        slot.busy = false;
        state.fault = Impl::Fault::Allocation;
      }

    } else if (state.supported && state.fault == Impl::Fault::None
               && state.dropped != std::numeric_limits<std::uint64_t>::max()) {
      ++state.dropped;
    }

    cancel();
  }

  auto FrameProfiler::snapshot() -> MeshProfiling {
    m_impl->poll();
    MeshProfiling result;
    result.enabled = true;
    result.gpuSupported = m_impl->supported;
    switch (m_impl->fault) {
      case Impl::Fault::Readback:
        result.error
            = std::format("GPU timestamp readback failed (status {})", m_impl->errorStatus);
        break;
      case Impl::Fault::Timestamp:
        result.error = "GPU timestamps are incomplete or nonmonotonic";
        break;
      case Impl::Fault::Serial:
        result.error = "Mesh profiling serial exhausted";
        break;
      case Impl::Fault::Allocation:
        result.error = "Allocate GPU timestamp callback state failed";
        break;
      case Impl::Fault::Empty:
        result.error = "GPU timestamp counters are empty; query execution may have failed";
        break;
      case Impl::Fault::Capacity:
        result.error = "GPU profiling pass capacity exceeded";
        break;
      case Impl::Fault::None:
        break;
    }

    result.dropped = m_impl->dropped;
    result.gpuBufferBytes = m_impl->supported ? slotCount * queryBytes * 2 : 0;

    for (const auto& slot : m_impl->slots) {
      result.pending += slot.busy;
    }

    result.cpu = ordered(m_impl->cpu);
    result.gpu = ordered(m_impl->gpu);
    return result;
  }
}  // namespace Dot::Graphics::Detail

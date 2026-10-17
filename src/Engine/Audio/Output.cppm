module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

export module Dot.Audio.Output;

export import Dot.Audio.Mixer;
import Dot.Core.Result;

export namespace Dot::Audio {
  enum class AudioBackend { Default, CoreAudio, Wasapi, PulseAudio, Alsa, Jack };

  enum class OutputFault { None, Callback, Stopped, Rerouted, Interrupted, Start, Stop };

  struct OutputConfig {
    std::uint32_t channels{2};
    std::uint32_t sampleRate{48000};
    std::uint32_t queueFrames{4096};
    std::uint32_t periodFrames{256};
    std::uint64_t backendBytes{16 * 1024 * 1024};
    AudioBackend backend{AudioBackend::Default};
    float volume{1};
    auto operator==(const OutputConfig&) const -> bool;
  };

  struct OutputInfo {
    AudioBackend backend;
    std::string deviceName;
    std::uint32_t channels;
    std::uint32_t sampleRate;
    std::uint32_t deviceChannels;
    std::uint32_t deviceSampleRate;
    std::uint32_t queueFrames;
  };

  struct OutputStats {
    bool running;
    OutputFault fault;
    std::uint32_t queuedFrames;
    std::uint32_t availableFrames;
    std::uint64_t submittedFrames;
    std::uint64_t consumedFrames;
    std::uint64_t discardedFrames;
    std::uint64_t callbackFrames;
    std::uint64_t underrunFrames;
    std::uint64_t callbacks;
    std::uint64_t clippedSamples;
    AudioMemoryStats backend;
  };

  /** Owned real playback endpoint with a bounded SPSC PCM queue. Control,
   * submission, mixing, and queries run on the creating thread. The native
   * callback only consumes samples, clips to [-1,1], zero-fills underruns, and
   * updates atomic counters; no application callback or allocation. A move keeps
   * thread ownership. Quiescent destruction may run elsewhere and joins callbacks
   * before freeing the queue. No null-device fallback. 有界单生产者单消费者 PCM 队列的
   * 原生播放端点，控制、提交、混音及查询限创建线程；原生回调仅取样、限幅、欠载填零及
   * 更新原子统计，不调用应用代码或分配内存。移动保留线程归属；停止使用后可异线程销毁，
   * 先等待回调退出再释放队列，不回退到空设备。 */
  class Output {
  public:
    [[nodiscard]] static auto create(const OutputConfig& config = {}) -> Core::Result<Output>;
    Output(const Output&) = delete;
    auto operator=(const Output&) -> Output& = delete;
    Output(Output&&) noexcept;
    auto operator=(Output&&) noexcept -> Output&;
    ~Output();

    [[nodiscard]] static auto backendMemory() noexcept -> AudioMemoryStats;
    [[nodiscard]] auto info() const -> Core::Result<OutputInfo>;
    [[nodiscard]] auto stats() const -> Core::Result<OutputStats>;
    /** Preload while stopped or submit while running. Full-queue, non-finite,
     * incomplete-frame, or oversized input fails before copying; caller storage
     * may be released after return. 停止时可预填，运行时可提交；队列满、非有限、不完整帧
     * 或超大输入会在复制前失败，返回后调用者可释放输入存储。 */
    [[nodiscard]] auto submit(std::span<const float> interleaved) -> Core::Result<void>;
    /** Same-format, same-owner mixer only; queue capacity is checked before
     * advancing it. Device faults can still occur after mixing and are reported.
     * 仅接收同格式、同所有者混音器，在推进前检查容量；混音后仍可能发生设备故障并报错。 */
    [[nodiscard]] auto mix(Mixer& mixer, std::uint32_t frames) -> Core::Result<void>;
    /** Endpoint software gain [0,1], independent of system/global volume.
     * 端点软件增益 [0,1]，不修改系统或全局音量。 */
    [[nodiscard]] auto setVolume(float volume) -> Core::Result<void>;
    [[nodiscard]] auto start() -> Core::Result<void>;
    /** Stop joins device callbacks, retaining unconsumed queue data. Backend
     * failures fault the output; queries/stop remain available, recreate to recover.
     * 停止等待设备回调退出，保留未消费队列；后端错误使输出故障，仅保留查询/停止，重建以恢复。 */
    [[nodiscard]] auto stop() -> Core::Result<void>;
    /** Discard queued frames only while stopped; lifetime counters remain.
     * 仅停止时丢弃排队帧，生命周期统计不清零。 */
    [[nodiscard]] auto clear() -> Core::Result<void>;

  private:
    struct Impl;
    explicit Output(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Audio

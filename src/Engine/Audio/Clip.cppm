module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

export module Dot.Audio.Clip;

import Dot.Core.Result;
import Dot.Core.Files;

export namespace Dot::Audio {
  struct ClipLimits {
    std::uint64_t sourceBytes{64 * 1024 * 1024};
    std::uint64_t sampleBytes{256 * 1024 * 1024};
  };

  struct ClipInfo {
    std::uint32_t channels;
    std::uint32_t sampleRate;
    std::uint64_t frames;
    double durationSeconds;
  };

  /** Shared immutable interleaved float PCM. Samples must be finite in [-1,1],
   * with 1..8 channels and 8000..192000 Hz. Copies retain ownership; moving
   * invalidates the source. Creation/decoding/query may run on any thread.
   * 共享不可变的交错浮点 PCM，样本须有限且在 [-1,1] 内，通道数为 1..8、采样率为
   * 8000..192000 Hz；副本保留所有权，移动使源失效，可在任意线程创建、解码及查询。 */
  class Clip {
  public:
    [[nodiscard]] static auto fromPcm(std::span<const float> samples, std::uint32_t channels,
                                      std::uint32_t sampleRate,
                                      std::uint64_t maxSampleBytes = 256 * 1024 * 1024)
        -> Core::Result<Clip>;
    /** Bounded WAV (RIFF), FLAC, or MP3 decode without resampling/channel conversion.
     * WAV chunks/frame alignment are checked before decoder initialization; a
     * complete declared frame count and finite normalized samples are required.
     * 有界 WAV（RIFF）、FLAC 或 MP3 解码，不重采样或转换通道；初始化前检查 WAV 块与
     * 帧对齐，要求完整的声明帧数及有限、归一化的样本。 */
    [[nodiscard]] static auto decode(std::span<const std::byte> encoded,
                                     const ClipLimits& limits = {}) -> Core::Result<Clip>;
    Clip(const Clip&) noexcept;
    auto operator=(const Clip&) noexcept -> Clip&;
    Clip(Clip&&) noexcept;
    auto operator=(Clip&&) noexcept -> Clip&;
    ~Clip();

    [[nodiscard]] auto info() const -> Core::Result<ClipInfo>;
    /** Borrowed read-only view, valid while an owning clip copy remains alive.
     * 借用的只读视图，只要拥有数据的片段副本仍存活就有效。 */
    [[nodiscard]] auto samples() const -> Core::Result<std::span<const float>>;

  private:
    struct Impl;
    explicit Clip(std::shared_ptr<const Impl> impl) noexcept;
    std::shared_ptr<const Impl> m_impl;
  };

  /** Root-relative .wav/.flac/.mp3 access; missing/invalid files return errors.
   * 根目录相对的 .wav/.flac/.mp3 访问，缺失或无效文件返回错误。 */
  [[nodiscard]] auto loadClip(const Core::FileSystem& files, const std::filesystem::path& relative,
                              const ClipLimits& limits = {}) -> Core::Result<Clip>;
}  // namespace Dot::Audio

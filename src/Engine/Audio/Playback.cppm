module;

#include <cstdint>
#include <memory>
#include <optional>

export module Dot.Audio.Playback;

export import Dot.Audio.Output;
import Dot.Core.Result;

export namespace Dot::Audio {
  struct PlaybackConfig {
    MixerConfig mixer;
    OutputConfig output;
    std::uint32_t targetFrames{2048};
    std::uint32_t blockFrames{256};
    std::uint32_t pollMilliseconds{2};
  };

  enum class PlaybackState { Running, Closed, Faulted };

  struct PlaybackStats {
    PlaybackState state;
    MixerStats mixer;
    OutputStats output;
    std::optional<Core::Error> error;
  };

  /** Continuous real-device playback with a dedicated mixing/control worker.
   * Instance calls require the creating client thread; they synchronously return
   * the worker's result. Native callbacks never execute control/application code.
   * Moves preserve client/voice identity; quiescent destruction can run elsewhere
   * and joins the worker before releasing state. Controls affect future mixed
   * blocks; queued/driver-buffered PCM retains its existing content.
   * 独立混音/控制工作线程驱动的连续原生播放；实例调用限创建客户端线程，同步返回工作
   * 线程结果，设备回调不运行控制或应用代码。移动保留客户端/声部归属，停止使用后可异
   * 线程销毁，先等待工作线程退出再释放状态；控制影响后续混音块，已排队/驱动缓存的 PCM
   * 保留原内容。 */
  class Playback {
  public:
    [[nodiscard]] static auto create(const PlaybackConfig& config = {}) -> Core::Result<Playback>;
    Playback(const Playback&) = delete;
    auto operator=(const Playback&) -> Playback& = delete;
    Playback(Playback&&) noexcept;
    auto operator=(Playback&&) noexcept -> Playback&;
    ~Playback();

    [[nodiscard]] auto createVoice(const Clip& clip, const VoiceSettings& settings = {})
        -> Core::Result<VoiceId>;
    [[nodiscard]] auto destroyVoice(VoiceId voice) -> Core::Result<void>;
    [[nodiscard]] auto play(VoiceId voice) -> Core::Result<void>;
    [[nodiscard]] auto pause(VoiceId voice) -> Core::Result<void>;
    [[nodiscard]] auto seek(VoiceId voice, std::uint64_t sourceFrame) -> Core::Result<void>;
    [[nodiscard]] auto configureVoice(VoiceId voice, const VoiceSettings& settings)
        -> Core::Result<void>;
    [[nodiscard]] auto voice(VoiceId voice) -> Core::Result<VoiceState>;
    [[nodiscard]] auto setVolume(float volume) -> Core::Result<void>;
    [[nodiscard]] auto setOutputVolume(float volume) -> Core::Result<void>;
    [[nodiscard]] auto setListener(const Listener& listener) -> Core::Result<void>;
    [[nodiscard]] auto listener() -> Core::Result<Listener>;
    [[nodiscard]] auto setSpatial(VoiceId voice, const SpatialSettings& settings)
        -> Core::Result<void>;
    /** Owned metadata for the initialized endpoint, retained after closing.
     * 初始化端点的拥有数据元信息，关闭后仍保留。 */
    [[nodiscard]] auto info() const -> Core::Result<OutputInfo>;
    /** Fresh worker snapshot while running. Terminal snapshots preserve lifetime
     * frame/callback totals, clear live ownership after actual teardown, and retain
     * an owned fault diagnostic. 运行时读取新的工作线程快照；终止快照保留累计帧/回调统计，
     * 实际清理后清除存活所有权，并保留拥有数据的故障诊断。 */
    [[nodiscard]] auto stats() -> Core::Result<PlaybackStats>;
    /** Join mixing/device work and release voices/backend resources. Idempotent
     * on healthy closure; faults remain errors and require a new service to recover.
     * 等待混音/设备工作结束并释放声部/后端资源；正常关闭幂等，故障仍返回错误，需新建服务恢复。 */
    [[nodiscard]] auto close() -> Core::Result<void>;

  private:
    struct Impl;
    explicit Playback(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Audio

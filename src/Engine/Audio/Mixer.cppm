module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

export module Dot.Audio.Mixer;

export import Dot.Audio.Clip;
export import Dot.Audio.Spatial;
export import Dot.Audio.Voice;
import Dot.Core.Result;

export namespace Dot::Audio {
  struct VoiceId {
    std::uint64_t mixer{};
    std::uint64_t value{};
    auto operator==(const VoiceId&) const -> bool = default;
  };

  struct MixerConfig {
    std::uint32_t channels{2};
    std::uint32_t sampleRate{48000};
    std::uint32_t maxVoices{64};
    std::uint64_t backendBytes{16 * 1024 * 1024};
  };

  struct VoiceState {
    ClipInfo clip;
    VoiceSettings settings;
    bool playing;
    bool atEnd;
    SpatialSettings spatial{};
  };

  struct AudioMemoryStats {
    std::uint64_t allocations;
    std::uint64_t bytes;
  };

  struct MixerStats {
    std::size_t voices;
    std::size_t playing;
    std::uint64_t renderedFrames;
    AudioMemoryStats backend;
    bool faulted;
  };

  /** Independent offline miniaudio graph; never opens a playback device. All
   * instance operations run on the creating thread. Moves retain thread/handle
   * identity; quiescent destruction may run on another thread. Voices retain
   * their Clip copies until explicit destruction, including finished voices.
   * 独立离线 miniaudio 图，不打开播放设备；实例操作限创建线程。移动保留线程和句柄
   * 归属，停止使用后可在其他线程销毁；声部（含已结束声部）持有片段直至显式销毁。 */
  class Mixer {
  public:
    [[nodiscard]] static auto create(const MixerConfig& config = {}) -> Core::Result<Mixer>;
    Mixer(const Mixer&) = delete;
    auto operator=(const Mixer&) -> Mixer& = delete;
    Mixer(Mixer&&) noexcept;
    auto operator=(Mixer&&) noexcept -> Mixer&;
    ~Mixer();

    /** Process-wide live backend payload allocations for mixers; excludes Clip
     * storage, containers, allocator headers, and operating-system internals.
     * 进程内混音器后端的存活载荷分配统计，不含片段、容器、分配头和系统内部内存。 */
    [[nodiscard]] static auto backendMemory() noexcept -> AudioMemoryStats;
    [[nodiscard]] auto createVoice(const Clip& clip, const VoiceSettings& settings = {})
        -> Core::Result<VoiceId>;
    [[nodiscard]] auto destroyVoice(VoiceId voice) -> Core::Result<void>;
    /** Resume a paused voice; an ended voice restarts at its beginning.
     * 恢复暂停声部，已结束的声部从起点重新播放。 */
    [[nodiscard]] auto play(VoiceId voice) -> Core::Result<void>;
    [[nodiscard]] auto pause(VoiceId voice) -> Core::Result<void>;
    /** Source-frame target in [0,clip.frames), without stale prefetched samples.
     * Does not implicitly start playback. 源帧目标位于 [0,片段帧数)，清除旧的预取样本，
     * 不隐式启动播放。 */
    [[nodiscard]] auto seek(VoiceId voice, std::uint64_t sourceFrame) -> Core::Result<void>;
    /** Volume [0,1], pan [-1,1], pitch [0.25,4]; validate before changing state.
     * 音量 [0,1]、声像 [-1,1]、音高 [0.25,4]；先校验再修改状态。 */
    [[nodiscard]] auto configureVoice(VoiceId voice, const VoiceSettings& settings)
        -> Core::Result<void>;
    [[nodiscard]] auto voice(VoiceId voice) const -> Core::Result<VoiceState>;
    [[nodiscard]] auto setVolume(float volume) -> Core::Result<void>;
    [[nodiscard]] auto stats() const -> Core::Result<MixerStats>;
    [[nodiscard]] auto config() const -> Core::Result<MixerConfig>;
    /** Single listener; validate before applying. Unexpected backend update
     * failures fault the mixer. 单一监听器，先完整校验再应用，后端意外更新失败会使混音器故障。 */
    [[nodiscard]] auto setListener(const Listener& listener) -> Core::Result<void>;
    [[nodiscard]] auto listener() const -> Core::Result<Listener>;
    /** Enabled spatial voices require mono clips and zero manual pan. Pose and
     * attenuation changes apply at the next render; Doppler is explicitly opt-in.
     * 启用空间声部须为单声道且手动声像为零；位姿/衰减于下次渲染应用，多普勒需显式启用。 */
    [[nodiscard]] auto setSpatial(VoiceId voice, const SpatialSettings& settings)
        -> Core::Result<void>;
    /** Write 1..65536 interleaved output frames, advancing the graph. Mixes are
     * finite float PCM and may exceed [-1,1]; no hidden limiter. Invalid buffers
     * leave playback/output unchanged. Backend failures after advancement fault
     * the mixer; only queries and voice cleanup remain available.
     * 写入 1..65536 帧交错输出并推进图；有限浮点混音可超出 [-1,1]，无隐式限幅。
     * 无效缓冲不改变播放或输出，推进后的后端错误使混音器故障，仅保留查询与声部清理。 */
    [[nodiscard]] auto render(std::span<float> interleaved) -> Core::Result<void>;

  private:
    struct Impl;
    explicit Mixer(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Audio

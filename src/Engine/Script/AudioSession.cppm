module;

#include <memory>
#include <span>

export module Dot.Script.AudioSession;

export import Dot.Script.Host;
import Dot.Core.Result;
import Dot.Core.Files;
import Dot.World.Scene;
import Dot.Audio.SceneAudio;

export namespace Dot::Script {
  /** Owns the original shared scene and a real offline/native SceneAudio backend.
   * Moves preserve bindings, which retain backend/clip/scene ownership. Operations
   * require the creating thread; quiescent destruction joins native playback and
   * may run elsewhere. Scene moves/replacement invalidate the session explicitly.
   * 拥有原始共享场景及真实离线/原生 SceneAudio 后端；移动保留绑定，回调持有后端/片段/
   * 场景所有权。操作限创建线程，静止销毁等待原生播放退出，可在异线程进行；场景移动/
   * 替换会使会话明确失效。 */
  class AudioSession {
  public:
    [[nodiscard]] static auto create(std::shared_ptr<World::Scene> scene,
                                     const Core::FileSystem& files,
                                     const Audio::MixerConfig& config = {},
                                     const Audio::SceneAudioLimits& limits = {})
        -> Core::Result<AudioSession>;
    /** Open a real endpoint with no silent-device fallback. Controls affect
     * future mixed blocks; already queued PCM retains its existing content.
     * 打开真实端点，不回退为空设备；控制影响后续混音块，已排队 PCM 保留原内容。 */
    [[nodiscard]] static auto createPlayback(std::shared_ptr<World::Scene> scene,
                                             const Core::FileSystem& files,
                                             const Audio::PlaybackConfig& config = {},
                                             const Audio::SceneAudioLimits& limits = {})
        -> Core::Result<AudioSession>;
    AudioSession(const AudioSession&) = delete;
    auto operator=(const AudioSession&) -> AudioSession& = delete;
    AudioSession(AudioSession&&) noexcept;
    auto operator=(AudioSession&&) noexcept -> AudioSession&;
    ~AudioSession();

    /** Native scheduling: prepare authoring/poses before backend updates. Retain
     * manual playback/cursor when descriptions are unchanged. Reset reloads assets
     * and authored state; success increments generation and resets master gain.
     * 原生调度：先准备编辑/位姿再修改后端；描述不变时保留手动播放/游标。重置重新读取资源
     * 及编辑状态，成功递增代并恢复主增益。 */
    [[nodiscard]] auto synchronize(double seconds = 0) -> Core::Result<void>;
    [[nodiscard]] auto reset() -> Core::Result<void>;
    /** Offline only; native mode rejects without touching the buffer.
     * 仅限离线模式，原生模式拒绝且不修改缓冲。 */
    [[nodiscard]] auto render(std::span<float> samples) -> Core::Result<void>;
    /** Three observational reads and five controls. Reads report last synchronized
     * voices/listener; missing/foreign owners fail immediately. Source frames are
     * canonical decimal uint64 strings, including zero. Source read cursor can
     * include prefetch and is not endpoint presentation time. Scripts cannot open,
     * advance, reset or tear down the backend. 三个只读查询与五个控制；查询反映最近同步
     * 声部/监听器，缺失/跨场景所有者立即失败。源帧为规范十进制 uint64 字符串（含零）；
     * 读取游标可含预取，不表示端点播放时间。脚本不能打开、推进、重置或销毁后端。 */
    [[nodiscard]] auto bindings() const -> Core::Result<HostBindings>;

  private:
    struct Impl;
    explicit AudioSession(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script

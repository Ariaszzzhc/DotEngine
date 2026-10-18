module;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

export module Dot.Audio.SceneAudio;

export import Dot.Audio.Playback;
import Dot.Audio.Clip;
import Dot.Core.Result;
import Dot.Core.Files;
import Dot.World.Scene;

export namespace Dot::Audio {
  struct SceneAudioLimits {
    ClipLimits clip;
    std::uint64_t sampleBytes{256 * 1024 * 1024};
    std::uint32_t maxAssets{128};
    auto operator==(const SceneAudioLimits&) const -> bool = default;
  };

  struct ClipAsset {
    std::string asset;
    Clip value;
  };

  /** Decode unique referenced audio within the exact limits, without a mixer/device.
   * Cancellation is checked between bounded files. Validate spatial mono channels;
   * listener/pose checks remain with the final scene binding.
   * 在精确预算内解码唯一引用音频，不创建混音器/设备；有界文件之间检查取消。
   * 校验空间音频为单声道，监听器及位姿仍在最终场景绑定时校验。 */
  [[nodiscard]] auto loadSceneClips(const Core::FileSystem& files, const Dot::World::Scene& scene,
                                    const SceneAudioLimits& limits = {}, std::stop_token stop = {})
      -> Core::Result<std::vector<ClipAsset>>;

  struct SceneAudioStats {
    std::size_t assets;
    std::uint64_t sampleBytes;
    MixerStats mixer;
    std::optional<PlaybackStats> playback;
  };

  /** Owns root-relative immutable clips and entity/voice bindings, without a scene
   * reference. Operations require the creating thread; quiescent destruction may
   * run elsewhere and joins native playback. Scene moves preserve bindings.
   * Assets load once per authored path while referenced; reset reloads all files.
   * 拥有根目录相对不可变片段和实体/声部绑定，不持有场景引用；操作限创建线程，静止后
   * 可异线程销毁，原生模式会等待播放退出；场景移动保留绑定。引用期间每个编辑路径仅加载
   * 一次，reset 重新读取全部文件。 */
  class SceneAudio {
  public:
    /** Offline factory for deterministic PCM rendering; no device is opened.
     * Optional decoded assets are the complete initial set without file fallback;
     * later changes use ordinary referenced-cache/file rules. Decoded data is checked
     * against per-clip/aggregate PCM bounds; encoded-byte limits apply during decoding.
     * 离线工厂用于确定性 PCM 渲染，不打开设备；可选解码资源为完整初始集且不回退读文件。
     * 后续变更沿用引用缓存/文件规则；检查逐片段/总 PCM 预算，编码字节预算在解码时检查。 */
    [[nodiscard]] static auto create(const Dot::World::Scene& scene, const Core::FileSystem& files,
                                     const MixerConfig& config = {},
                                     const SceneAudioLimits& limits = {},
                                     std::optional<std::span<const ClipAsset>> prepared = {})
        -> Core::Result<SceneAudio>;
    /** Continuous real-device factory; files/poses are prepared on the client
     * thread, and a single batch commits on the playback worker.
     * 连续原生播放工厂；文件/位姿在客户端线程准备，单批次在播放工作线程提交。
     * An optional ready, unstarted deferred endpoint is consumed only after
     * exact configuration validation; it must belong to this client thread.
     * 可传入同客户端线程的已就绪、未启动延后端点；精确核对配置后转移所有权。
     * Optional PCM follows the offline factory's complete initial set contract.
     * 可选 PCM 沿用离线工厂的完整初始资源集契约。 */
    [[nodiscard]] static auto createPlayback(const Dot::World::Scene& scene,
                                             const Core::FileSystem& files,
                                             const PlaybackConfig& config = {},
                                             const SceneAudioLimits& limits = {},
                                             Playback* preparedPlayback = nullptr,
                                             std::optional<std::span<const ClipAsset>> prepared
                                             = {}) -> Core::Result<SceneAudio>;
    SceneAudio(const SceneAudio&) = delete;
    auto operator=(const SceneAudio&) -> SceneAudio& = delete;
    SceneAudio(SceneAudio&&) noexcept;
    auto operator=(SceneAudio&&) noexcept -> SceneAudio&;
    ~SceneAudio();

    /** Activate deferred native playback; offline mode is already ready.
     * 激活延后启动的原生播放；离线模式已就绪。 */
    [[nodiscard]] auto startPlayback() -> Core::Result<void>;
    /** Nonblocking activation/teardown polling; offline audio is already ready.
     * 非阻塞激活/清理轮询，离线音频已就绪。 */
    [[nodiscard]] auto beginPlayback() -> Core::Result<void>;
    [[nodiscard]] auto pollPlayback() -> Core::Result<bool>;
    [[nodiscard]] auto requestPlaybackClose() -> Core::Result<void>;
    /** Join native teardown and report its terminal error; offline is a no-op.
     * 等待原生清理并报告终止错误；离线模式无操作。 */
    [[nodiscard]] auto closePlayback() -> Core::Result<void>;
    /** Terminal backend errors are reported only after native teardown completes.
     * 仅在原生清理完成后报告终止后端错误。 */
    [[nodiscard]] auto pollPlaybackClosed() const -> Core::Result<bool>;

    /** Prepare a complete candidate before one backend batch. Recoverable file,
     * pose, capacity, and allocation failures retain the last successful binding.
     * Spatial sources require a primary listener and mono clips. World TRS must
     * have positive scale and no shear; scale affects inherited position only.
     * seconds is finite [0,1]; zero teleports with zero Doppler velocity, positive
     * values derive velocity from consecutive successful world positions (new
     * sources/listeners start at zero). Derived speed must stay <=100 m/s.
     * Unchanged assets/settings retain cursor and manual play state; playOnStart
     * applies only on creation/asset replacement. Ended voices do not auto-restart.
     * 完整准备候选状态后单批次提交；可恢复的文件、位姿、容量及分配失败保留最近成功绑定。
     * 空间源要求主监听器与单声道片段；世界 TRS 须为正缩放且无错切，缩放仅影响继承位置。
     * seconds 须有限且在 [0,1]，零表示传送并清零多普勒速度，正值从连续成功世界位置推导
     * 速度（新源/监听器初速为零），速率须不超 100 米/秒。资产/属性未变则保留游标及手动
     * 播放状态；playOnStart 仅用于创建/替换资产，已结束声部不自动重启。 */
    [[nodiscard]] auto synchronize(const Dot::World::Scene& scene, double seconds = 0)
        -> Core::Result<void>;
    /** Supply decoded clips for uncached referenced paths in one update, with no
     * file fallback. Reject duplicate/unused paths before backend mutation. Cached
     * paths retain their immutable data and voice cursors/manual state while referenced.
     * 单次更新提供未缓存引用的解码片段且不回退文件；后端修改前拒绝重复/无用路径。
     * 引用中的缓存路径保留不可变数据及声部游标/手动状态。 */
    [[nodiscard]] auto synchronizePrepared(const Dot::World::Scene& scene,
                                           std::span<const ClipAsset> prepared, double seconds = 0)
        -> Core::Result<void>;
    /** Independently prepare a new backend/cache; failure retains this service.
     * Success may bind another scene and resets voices/gain from authoring/config.
     * 独立准备新后端与缓存，失败保留当前服务；成功可绑定另一场景，并从描述/配置重置声部及增益。 */
    [[nodiscard]] auto reset(const Dot::World::Scene& scene) -> Core::Result<void>;
    [[nodiscard]] auto voice(Dot::World::EntityId entity) const -> Core::Result<VoiceState>;
    [[nodiscard]] auto play(Dot::World::EntityId entity) -> Core::Result<void>;
    [[nodiscard]] auto pause(Dot::World::EntityId entity) -> Core::Result<void>;
    [[nodiscard]] auto stop(Dot::World::EntityId entity) -> Core::Result<void>;
    [[nodiscard]] auto seek(Dot::World::EntityId entity, std::uint64_t sourceFrame)
        -> Core::Result<void>;
    [[nodiscard]] auto setVolume(float volume) -> Core::Result<void>;
    [[nodiscard]] auto listener() const -> Core::Result<Listener>;
    [[nodiscard]] auto stats() const -> Core::Result<SceneAudioStats>;
    /** Offline only; native mode returns InvalidState without changing output.
     * 仅限离线模式，原生模式返回 InvalidState 且不修改输出。 */
    [[nodiscard]] auto render(std::span<float> interleaved) -> Core::Result<void>;

  private:
    struct Impl;
    explicit SceneAudio(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Audio

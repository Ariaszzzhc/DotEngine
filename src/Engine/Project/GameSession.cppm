module;

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Project.GameSession;

import Dot.Animation.ScenePlayers;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Project;
import Dot.Project.ScenePreparation;
import Dot.World.Scene;
import Dot.Platform.Input;
import Dot.Math;
import Dot.Script.Compiler;
export import Dot.Script.LiveScripts;
export import Dot.Script.SceneScripts;
export import Dot.Script.InputSource;
export import Dot.Script.SceneFlow;
export import Dot.Script.GameData;
export import Dot.Script.Hud;
export import Dot.Script.GameClock;
export import Dot.Script.PhysicsSource;
export import Dot.Audio.SceneAudio;

export namespace Dot::Project {
  enum class SessionAudio { Offline, Playback };
  enum class SessionState { Ready, Running, Faulted, Closed, Starting, Cancelling, Preparing };
  enum class SessionPreparation { Resources, Scripts, Audio, Services, Teardown };

  /** Logical input-area size, matching InputState pointer units. Disabled areas
   * cancel UI gestures on focus/viewport loss; zero sizes are legal only when disabled.
   * No pointer is inferred without an area.
   * 与 InputState 指针单位相同的逻辑输入区域；失焦/视口不可用时禁用并取消 UI 手势，
   * 零尺寸仅在禁用时合法；没有区域时不推断指针位置。 */
  struct GameInputArea {
    Math::Vec2 size;
    bool enabled{true};
  };

  struct GameSessionConfig {
    /** Initial state/root for one game, shared by its scene replacements.
     * 单次游戏的初始状态/存档目录，场景替换共享同一存储。 */
    Script::GameDataConfig progress;
    Script::InputSourceConfig input;
    Script::PhysicsSourceConfig physics;
    Script::SceneScriptsConfig scripts;
    Audio::PlaybackConfig audio;
    Audio::SceneAudioLimits audioLimits;
    SessionAudio audioMode{SessionAudio::Offline};
    std::chrono::milliseconds pollInterval{100};
  };

  struct GameSessionStats {
    SessionState state;
    std::uint32_t fixedHz;
    bool hotReload;
    std::optional<Script::SceneScriptsStats> scripts;
    Script::SceneLocation location{};
    std::optional<Script::SceneRequest> transition;
    std::optional<Script::LiveScriptsStats> source{};
    /** Owns one retired endpoint until real teardown is reaped; not an audible-state flag.
     * 真实清理回收前拥有一个退役端点，不表示当前是否可听。 */
    bool retiringAudio{};
    /** Absent until actual initial scene services exist. 服务建立前没有实际游戏时钟。 */
    std::optional<Script::GameClockSnapshot> clock{};
    std::uint64_t simulatedFixedSteps{};
    std::uint64_t simulatedUpdates{};
    /** Completed native fixed phases across scene replacement, including debugger
     * steps; per-scene counters above still reset on replacement.
     * 跨场景的已完成原生固定阶段（含调试单步）；上方场景计数仍随替换重置。 */
    std::uint64_t lifetimeFixedSteps{};
    std::optional<SessionPreparation> preparation{};
    std::optional<Core::Error> preparationError{};
    std::uint64_t preparationCompileAttempts{};
    std::uint64_t discardedScriptCompilations{};
  };

  /** Owns the active project scene and its input, scripts, real physics,
   * native animation and offline/native audio. No window or renderer is opened. Operations require
   * the creating thread without reentry; moves keep stable service ownership.
   * Destruction only releases resources; close invokes stop hooks and releases
   * services even after faults. Native audio is prepared silently and activated
   * after the first start dispatch and successful initial pose synchronization. Scene
   * cuts retain one old audio service until actual worker teardown; the next cut
   * waits for that ownership to be reaped. Close/destruction join both endpoints.
   * The shared scene remains inspectable after close.
   * 拥有新加载项目场景及输入、脚本、真实物理和离线/原生音频，不打开窗口或渲染器。
   * 操作限创建线程且不可重入，移动保持服务所有权稳定；销毁仅释放资源，close 执行停止
   * 钩子且在故障后也释放服务；原生音频静默准备，首次启动分发结束且初始位姿同步成功后才激活。
   * 场景切换保留一个旧音频服务直至工作线程真实清理；回收前不开始下次切换。
   * 关闭/销毁等待两个端点退出，关闭后仍可检查共享场景。 */
  class GameSession {
  public:
    [[nodiscard]] static auto create(const Project& project,
                                     const Script::TypeScriptCompiler& compiler,
                                     const GameSessionConfig& config = {})
        -> Core::Result<GameSession>;
    /** Freeze saved scene metadata and own initial resource/compilation/audio tasks.
     * Poll without advancing simulation; only Ready has constructed scene services.
     * Start remains explicit. Snapshot/package metadata and native binding still have
     * owner costs; asset-root files must outlive preparation/session. No public
     * scene/game-data/input access or lifecycle dispatch before services exist.
     * 冻结保存场景元数据并拥有资源/编译/音频准备任务；轮询不推进模拟，仅 Ready 已建立
     * 场景服务，Start 仍须显式调用。快照/包元数据及原生绑定仍有所有者开销；服务建立前
     * 不开放场景/游戏数据/输入，也不分发生命周期；资产根目录文件须更长寿。 */
    [[nodiscard]] static auto begin(const Project& project,
                                    const Script::TypeScriptCompiler& compiler,
                                    const GameSessionConfig& config = {})
        -> Core::Result<GameSession>;
    [[nodiscard]] static auto beginCompiled(const Core::FileSystem& assets,
                                            const Description& description,
                                            const World::Scene& scene,
                                            const Script::CompiledProgram& program,
                                            const GameSessionConfig& config = {})
        -> Core::Result<GameSession>;
    /** Errors are retained while owned teardown is polled to Faulted; cancellation
     * reaches Closed only after tasks/endpoints are reaped. Destruction/close still
     * join outstanding work. Owner-thread, non-reentrant, no gameplay dispatch.
     * 错误保留至拥有任务清理后进入 Faulted；取消须回收任务/端点才到 Closed。
     * 销毁/close 仍等待未完成工作；限所有者线程、不可重入，不分发游戏逻辑。 */
    [[nodiscard]] auto pollPreparation() -> Core::Result<SessionState>;
    /** Clone a validated authoring scene into a fresh scope without saving or
     * borrowing it. Project resources/scripts remain rooted at the project.
     * A supplied origin keeps an owned initial snapshot for returns during this
     * session, matching export of unsaved authoring content. An optional ready,
     * unstarted deferred playback endpoint is consumed after exact configuration
     * validation and must belong to this thread.
     * An optional compiled source generation skips compilation only after exact
     * provenance/current-source validation; source-watching sessions retain hot reload.
     * Optional resource proof must match the exact frozen scene/root and audio limits;
     * native bindings consume owned immutable assets without initial file fallback.
     * 克隆已校验的编辑场景到新范围，不保存也不借用源场景；资源/脚本仍以项目为根。
     * 提供来源路径时保留初始快照，返回该路径重建初始内容，与未保存内容的导出一致；
     * 可选端点须为本线程已就绪且未启动的延后播放，精确核对配置后转移所有权；
     * 可选源码编译代须精确核对来源及当前源码才跳过编译，源码监听会话仍保留热重载；
     * 可选资源证明须匹配精确冻结场景/根目录及音频预算，原生绑定拥有不可变资源且不回退首次读文件。
     */
    [[nodiscard]] static auto createFromScene(
        const Project& project, const Script::TypeScriptCompiler& compiler,
        const World::Scene& scene, const GameSessionConfig& config = {},
        std::optional<std::string> scenePath = std::nullopt,
        Audio::Playback* preparedPlayback = nullptr,
        const Script::CompiledProgram* preparedProgram = nullptr,
        const PreparedScene* preflight = nullptr) -> Core::Result<GameSession>;
    /** Start-ready session from an owned compiled generation, without reading
     * TypeScript or invoking a compiler. Description must disable hot reload;
     * authoring is cloned, and asset-root files must outlive the session.
     * 从拥有的编译代建立待启动会话，不读 TypeScript 也不调用编译器；描述须关闭热重载。
     * 克隆编辑场景，资产根目录文件须比会话存活更久。 */
    [[nodiscard]] static auto createCompiled(const Core::FileSystem& assets,
                                             const Description& description,
                                             const World::Scene& scene,
                                             const Script::CompiledProgram& program,
                                             const GameSessionConfig& config = {})
        -> Core::Result<GameSession>;
    GameSession(const GameSession&) = delete;
    auto operator=(const GameSession&) -> GameSession& = delete;
    GameSession(GameSession&&) noexcept;
    auto operator=(GameSession&&) noexcept -> GameSession&;
    ~GameSession();

    /** Reacquire after update: a committed replacement publishes a new scope.
     * Retained shared scenes stay alive but are no longer the active simulation.
     * update 后须重新获取；提交替换发布新范围，保留的共享旧场景仍存活但不再参与模拟。 */
    [[nodiscard]] auto scene() const -> Core::Result<std::shared_ptr<World::Scene>>;
    [[nodiscard]] auto stats() const -> Core::Result<GameSessionStats>;
    /** Poll background source work without advancing gameplay. Force schedules an
     * unchanged retry; completion still validates current source and migrates on
     * this owner. Running source sessions only, never compiled exports.
     * 不推进游戏地轮询后台源码任务；强制请求可重试未变源码，完成后仍核对当前源码并在
     * 所有者线程迁移。仅限运行中的源码会话，不适用于已编译导出。 */
    [[nodiscard]] auto pollScripts(bool force = false) -> Core::Result<Script::ReloadStatus>;
    /** Owned game-wide control handle; close/fault/destruction retires writes.
     * 拥有的游戏级控制句柄；关闭、故障或销毁后禁止写入。 */
    [[nodiscard]] auto gameData() const -> Core::Result<Script::GameData>;
    /** Read the current native animation clocks/layers without syncing or advancing.
     * 只读当前原生动画时钟/层，不触发同步或推进。 */
    [[nodiscard]] auto inspectAnimation(World::EntityId owner) const -> Core::Result<std::string>;
    [[nodiscard]] auto animationState(World::EntityId owner) const
        -> Core::Result<Animation::ScenePlayerState>;
    /** Running scoped native control, including explicit pose changes while game
     * scheduling is paused. Never edits saved definitions or advances time.
     * 当前运行范围的原生控制，允许游戏调度暂停时显式改姿态；不改保存定义、不推进时间。 */
    [[nodiscard]] auto animationParameters(
        World::EntityId owner, std::span<const Animation::ControllerParameter> parameters)
        -> Core::Result<void>;
    [[nodiscard]] auto resetAnimationController(World::EntityId owner) -> Core::Result<void>;
    [[nodiscard]] auto controlAnimationLayer(World::EntityId owner, std::string_view name,
                                             const Animation::LayerControl& operation)
        -> Core::Result<void>;
    /** Current scene pause service; replacement starts a fresh unpaused clock.
     * 当前场景暂停服务；场景替换建立新的非暂停时钟。 */
    [[nodiscard]] auto gameClock() const -> Core::Result<Script::GameClock>;
    /** Current scene-local UI; reacquire after replacement. Retained handles
     * become read-only after retirement. 当前场景 UI，替换后须重新获取；退役句柄仅可读。 */
    [[nodiscard]] auto hud() const -> Core::Result<Script::Hud>;
    /** Queue replacement at the next variable boundary with no retired audio. Acceptance is
     * not completion; query the retained request result. Prepare failures keep the
     * old scene running. A committed stop/start failure faults the new session.
     * 在无退役音频的下一变动帧末尾开始替换；接受不等于完成，须查询请求结果。准备失败保留旧场景；
     * 提交后的停止/启动失败使新会话故障。 */
    [[nodiscard]] auto requestScene(std::string_view path) -> Core::Result<std::uint64_t>;
    /** Additive content keeps the current realm, clock and native simulation.
     * Preflight failure preserves gameplay; published synchronization/hook failure
     * is committed and faults the session. Source globals/UI are not imported.
     * 追加内容保留当前 realm、时钟及原生模拟；预检失败保留游戏，发布后同步/钩子失败
     * 标记已提交并使会话故障；不导入来源全局设置/UI。 */
    [[nodiscard]] auto appendScene(std::string_view path) -> Core::Result<std::uint64_t>;
    [[nodiscard]] auto removeScene(World::EntityId root) -> Core::Result<std::uint64_t>;
    [[nodiscard]] auto sceneInstances() const -> Core::Result<std::vector<Script::SceneInstance>>;
    [[nodiscard]] auto sceneRequest(std::uint64_t id) const -> Core::Result<Script::SceneRequest>;
    /** Poll resource preflight/retired audio without advancing Update/fixed time
     * or beginning pending cuts. A ready replacement may dispatch Stop/Start and
     * rebuild owner-thread services. Retired teardown faults remain errors.
     * 不推进 Update/固定时间或开始待处理切换地轮询预检/退役音频；完成后可执行 Stop/Start
     * 并重建主线程服务，退役清理故障仍报告错误。 */
    [[nodiscard]] auto pollScene() -> Core::Result<Script::ScriptReport>;
    [[nodiscard]] auto publish(const Platform::InputState& input, bool pointerCaptured = false,
                               std::optional<GameInputArea> area = std::nullopt,
                               bool textInputActive = false) -> Core::Result<std::uint64_t>;
    /** Script request only; the native frontend owns focus/capture policy and
     * publishes its actual acknowledgement with the input frame.
     * 仅为脚本请求，原生前端拥有聚焦/捕获策略，并随输入帧发布实际确认。 */
    [[nodiscard]] auto textInputRequested() const -> Core::Result<bool>;
    [[nodiscard]] auto pointerCaptureRequested() const -> Core::Result<bool>;
    [[nodiscard]] auto start() -> Core::Result<Script::ScriptReport>;
    /** Dispatch Start once and queue native activation without waiting; pollStart
     * publishes Running only on actual completion. No gameplay while Starting.
     * 分发一次 Start 后非阻塞排队原生激活，pollStart 仅在真实完成后发布 Running；
     * Starting 期间不可推进游戏。 */
    [[nodiscard]] auto beginStart() -> Core::Result<Script::ScriptReport>;
    [[nodiscard]] auto pollStart() -> Core::Result<bool>;
    /** Cancel unpublished preparation without hooks; retain until teardown completes.
     * Cancelling rejects startup/gameplay; close also skips hooks after cancellation.
     * Initial CPU jobs are reaped by pollPreparation; pollPreparationClosed only
     * observes completion. Later activation teardown uses the same close observation.
     * 无钩子地取消未发布的准备，清理完成前须保留所有权；取消中拒绝启动/游戏操作，
     * 后续 close 也跳过钩子；初始 CPU 任务由 pollPreparation 回收，pollPreparationClosed
     * 仅观察完成，后续激活清理沿用同一关闭观察接口。 */
    [[nodiscard]] auto requestPreparationClose() -> Core::Result<void>;
    [[nodiscard]] auto pollPreparationClosed() const -> Core::Result<bool>;
    /** Requires start. Each variable call first dispatches unscaled onFrame,
     * including while paused. Latch pause before each gameplay phase: paused fixed
     * calls skip scripts/physics; paused variable calls skip Update and use zero
     * animation delta. Input, explicit cuts, rendering and audio playback continue;
     * frame callbacks may intentionally edit native state. No paused time debt.
     * A queued debugger step is consumed after onFrame: dispatch one prepared
     * fixed hook/physics and one Update/animation at 1/fixedHz without unpausing.
     * Audio/frame delta stays unscaled. Invalid delta cannot consume a request.
     * 要求启动；每次变动帧先执行非缩放 onFrame，暂停时也运行；各游戏阶段前锁存暂停，
     * 暂停固定帧跳过脚本/物理，暂停变动帧跳过 Update 并以零动画时长同步。输入、显式切换、
     * 渲染和音频继续，帧回调可显式修改原生状态，不累积暂停时间债务。
     * 排队调试单步在 onFrame 后执行同代固定脚本/物理及 Update/动画，各推进 1/fixedHz，
     * 不解除暂停；音频/帧时长仍非缩放，无效时长不消耗请求。
     * Fixed steps dispatch scripts using the preceding contact
     * batch then advance physics by 1/fixedHz. Update dispatches scripts then
     * advances animation then synchronizes audio once after all fixed steps, then
     * polls at most one scene request, retaining old gameplay during CPU resource preflight. New
     * Start requests wait until the following update. Delta is finite [0,1]. Initial input is an
     * empty published frame. Poll errors retain/report old scripts; individual script failures are
     * isolated/reported, realm/native failures fault the session until close. Invalid deltas have
     * no effects. There is no rollback of completed callbacks or native steps.
     * 要求已启动；固定步使脚本读取上一接触批次，再推进物理 1/fixedHz；update 在全部
     * 固定步之后分发脚本、推进原生动画、同步一次音频，再轮询至多一个场景请求，CPU
     * 资源预检期间继续旧场景； 新 Start 请求留待下一 update，时长须有限
     * [0,1]；初始输入为已发布空帧。轮询错误保留并
     * 报告旧脚本，单脚本错误隔离并报告，realm/原生失败使会话故障直至关闭；无效时长无
     * 副作用，不回滚已完成回调或原生步。 */
    [[nodiscard]] auto fixedUpdate() -> Core::Result<Script::LiveScriptReport>;
    [[nodiscard]] auto update(double seconds) -> Core::Result<Script::LiveScriptReport>;
    [[nodiscard]] auto renderAudio(std::span<float> samples) -> Core::Result<void>;
    [[nodiscard]] auto close() -> Core::Result<Script::ScriptReport>;

  private:
    [[nodiscard]] static auto beginPreparation(const Core::FileSystem& assets,
                                               Description description, std::string sceneBytes,
                                               const GameSessionConfig& config,
                                               const Script::TypeScriptCompiler* compiler,
                                               const Script::CompiledProgram* program)
        -> Core::Result<GameSession>;
    struct Origin;
    [[nodiscard]] static auto prepare(const Project& project,
                                      const Script::TypeScriptCompiler& compiler,
                                      World::Scene scene, const GameSessionConfig& config,
                                      std::optional<std::string> scenePath,
                                      std::optional<std::string> originBytes = std::nullopt,
                                      Audio::Playback* preparedPlayback = nullptr,
                                      const Script::CompiledProgram* preparedProgram = nullptr,
                                      const PreparedScene* preflight = nullptr)
        -> Core::Result<GameSession>;
    [[nodiscard]] static auto prepareSession(
        const Core::FileSystem& assets, Description description, World::Scene scene,
        const GameSessionConfig& config, const Script::TypeScriptCompiler* compiler,
        const Script::CompiledProgram* program, std::optional<std::string> scenePath,
        const Script::SceneFlow* flow = nullptr, std::shared_ptr<const Origin> origin = {},
        const Script::GameData* progress = nullptr, Audio::Playback* preparedPlayback = nullptr,
        const PreparedScene* preflight = nullptr) -> Core::Result<GameSession>;
    [[nodiscard]] auto processContentRequest(const Script::SceneRequest& request,
                                             const PreparedScene* preflight,
                                             Script::ScriptReport& report) -> Core::Result<void>;
    [[nodiscard]] auto processSceneRequest(Script::ScriptReport& report, bool beginPending)
        -> Core::Result<void>;
    struct Impl;
    explicit GameSession(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Project

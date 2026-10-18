# Native frame diagnostics

Application CPU sampling and mesh-renderer CPU/GPU sampling are independent opt-in
services. They share the actual editor/runtime implementations. No labels infer
FPS or production readiness from a renderer-only interval.

## Application CPU steps

In Window → Rendering, enable **Profile application frames**. Strict agent operations:

```json
{"command":"set-application-profiling","enabled":true}
{"command":"inspect-profile"}
{"command":"set-application-profiling","enabled":false}
```

Unknown fields and non-boolean switches reject. These diagnostics remain available
during Play, animation audition and document drafts without changing scene/history.
The applied inspection response exposes `workspace.applicationProfiling` with the
latest completed sample and summaries. `inspect-profile.application` also includes
owned `frames` history. A request executing inside a sampled step reads previously
completed data; its own cost is published when that step finishes.

`Dot.Core.Application` owns the CPU service and exports `setProfiling`, `profiling`
and `encodeApplicationProfiling`. It measures steady-clock wall intervals around
actual `Application::step` work:

| Phase | Included work |
| --- | --- |
| events | Poll/consume native events, native close decisions and event subscribers |
| prepare | The prepareFrame callback, including input publication and editor mailbox work |
| fixed | All executed fixed-update callbacks for that step, including native gameplay services |
| update | Exact-frame and variable-update callbacks; editor Play's independent fixed/variable clock and animation audition advance here |
| render | The render callback, including scene resources, skin updates, renderer/GUI and synchronous GPU/driver waits |
| total | The whole step envelope before history publication |

An unexecuted phase is null; executed zero-duration intervals are valid. Phases are
sequential and exclude the envelope's bookkeeping. These are wall intervals, not
CPU utilization or GPU execution time. Process/window/device/session initialization,
work before/after step, shutdown, history publication and the loop's idle frame delay
are excluded. Resource work performed inside render is included. An asynchronous
worker's time is not attributed as main-thread CPU work.

Each sample owns the actual frame index, running/paused state, completed application
fixed-clock iteration count, supplied elapsed time and dropped fixed time, plus any
callback error. A null fixed phase means no fixed callback executed even if that
clock advanced. Editor Play retains its separate fixed-step/drop counters under
`workspace.play`; its simulation runs in the exact-frame update phase.
The application sample's paused flag describes application focus-loss scheduling,
independently of gameClock.paused. Game pause still runs scheduled callbacks,
frame/UI work, spatial audio synchronization and rendering. Its update cost remains
measurable; completed game simulation counters establish which gameplay phases ran.
See [game clock scheduling](GameClock.md).
`elapsedNanoseconds` is the input to step; during the normal run loop it describes
the preceding work/idle interval. It is distinct from the current step's measured
`totalNanoseconds`. A paused sample explicitly distinguishes intentionally skipped
simulation. Failed callbacks retain completed phases, the measured failing phase
and their native error; later phases remain null. Invalid arguments and calls on
already stopped owners do not manufacture a sampled step.

The service retains the latest 120 steps, the greatest measured step (`slowest`),
the greatest supplied interval (`longestInterval`) and five independent
`phasePeaks` since enablement. The phase object has `events`, `prepare`, `fixed`,
`update` and `render` keys; each is null until that phase executes, then owns
the complete frame with the greatest measured duration for that phase. Executed
zero is a valid peak. Equal maxima retain the earlier observation. Separate
phase records preserve an update hitch even when a different render-heavy step
has the greatest total and both have left the ordinary history. Each owns frame
index, all phase measurements, elapsed/dropped time, paused/running flags and any
whole-step error; a retained error does not necessarily originate in the peak
phase itself. The Rendering panel shows these same independent peaks and indices.

The five peaks may belong to different frames: do not add them into a frame time
or reinterpret the Update value on `slowest` as the maximum Update. There are at
most seven additional complete records beyond the 120-entry history (total, input
interval and five phases). Compact and history JSON both preserve all summaries.
Session/frame/sample/update identities and
nanoseconds use decimal strings; retained history count is at most 120. Snapshots
own their records. Same-value toggles preserve history; disabling releases it and
reenabling starts a fresh monotonic session. Callback toggles invalidate that
in-progress sample without borrowing the released profiling state.
Snapshots remain owned after disabling, moving or destroying the application.
Control/inspection requires the application's main-thread owner.

## Standalone and project playback

Both native players accept an optional explicit output file:

```text
DotRuntime --project <project> <absolute-compiler> [frames] [--profile <file>]
DotRuntime --game <game-package> [frames] [--profile <file>]
DotGame [--frames <count>] [--profile <file>]
```

DotRuntime accepts the profile option last. DotGame accepts the two distinct
options in either order; duplicates, missing values, unknown flags and malformed
budgets reject before native resources open. The destination parent must exist.
A directory/special output fails before window creation. After playback, the owned
snapshot is written with the same native encoder and atomic file service; an
existing regular file is replaced. Write failures are reported, and a failed game
is never reclassified as successful because diagnostic saving succeeded. Profile
collection does not change simulation catch-up limits or suppress warnings.

Use a finite frame budget for repeatable investigations. Retain the project,
resolution, configuration, adapter, visible workload, startup/cache context and
stderr with the file; elapsed time alone does not identify a particular resource
or pipeline. Compare actual identical work and image output after optimizations.
Automatic CLI regressions validate rejected options/output paths without windows;
actual native playback is a separate deliberate product check.

Native playback prepares required environment/shadow pipelines before invoking
script Start callbacks or game updates. During asynchronous compilation it continues events and
presents a clear surface with a loading title. These loading application steps are
profiled but do not consume the requested game-frame budget. Thus `sampled` may be
larger than the budget; the profile's fixed callback counts are application clock
steps, not proof of gameplay updates before startup. After startup, asynchronous
presentation may also require multiple application steps per completed game frame.
The finite game-frame budget counts completed presentations, while CPU history
records every application step, including pending-render work. Per-step medians
therefore do not establish displayed FPS or GPU cost. The phase transitions are
reported on stderr. Zero frames still means run until closed. Subsequent synchronous
resource work remains included and catch-up/drop warnings are unchanged.

## Renderer sampling

**Profile mesh rendering**, `set-profiling` and `inspect-profile.profile` retain
separate renderer CPU/GPU windows. See [editor operations](Editor.md)
and [rendering contracts](Rendering.md). Renderer preparation/encoding/submission
exclude outer scene/resource/script/physics/GUI work. Optional GPU timestamp
intervals cover physical render passes; overlapping intervals are not additive as
frame time. GPU drops/errors and unavailable capabilities remain explicit.

Initial visible skin poses are now submitted/polled in incremental resource
preparation. CPU prepare samples include owner-thread allocation/submission/polling,
while GPU worker time is not a CPU interval. Animated pose updates during rendering
still map their compact validation/bounds report synchronously. Native sampling of
Prism menu cycles identified that queue wait and Metal drawable acquisition as
substantial render-stack work; it did not establish that text rasterization caused
the largest hitch. Repeated full scene inspection also contributes control/JSON
cost, so those agent-driven records are functional investigation evidence rather
than an unobstructed player benchmark.

## Native subsystem attribution

Application phases locate an expensive envelope; an OS CPU sample can then identify
which native service is spending time inside it. Keep the exact content, executable
configuration, sample window and raw stack report. Statistical stack counts are not
invocation counts or precise per-method timings. Async rendering/waits and differing
fixed-step counts also prevent treating one before/after history as a formal FPS
benchmark. Compare the same work and preserve independent behavior/image evidence.

Scene observations now provide owned pose/collider/audio subsets. In the actual
Prism native investigation, audio synchronization no longer builds/sorts complete
entity descriptions, and physics no longer clones render/skin/animation components.
This is a reduction in native snapshot work; it does not complete resource streaming,
background native cooking, scene mutation validation costs or animation synchronization.

## Prism source-update measurements

Warm Release editor measurements on Apple M4 Pro/Metal, arm64 macOS, use the same
342-entity Prism authoring scene with 26 links and a 1480x574 viewport. Three
ordinary edit requests per case preserve scene content; all twelve corresponding
operation arrays, results and result selections agree before/after. Final captures
use the same camera/selection. Values
are application Prepare phase peaks for the request step, including mailbox/JSON/
document work. They are not GPU intervals or unobstructed game-frame measurements.

| Actual operation | Previous median | Shared native candidate median |
| --- | ---: | ---: |
| Unchanged name | 32.31 ms | 31.88 ms |
| Refresh one source placement | 58.93 ms | 57.11 ms |
| Refresh twelve shared-source placements | 343.22 ms | 59.88 ms |
| Refresh all twenty-six source placements | 700.98 ms | 61.45 ms |

The 12/26 cases reduce this prepare envelope by about 82.6%/91.2%. Their three-run
ranges are 341.06..344.26 → 59.79..60.46 ms and 699.50..709.72 → 61.42..62.31 ms.
The static native viewport PNGs before/after are byte-identical, SHA-256
`b075e0fa07517fa861351167ec1226e5c944353f03d7a25aba336f749387f380`.
This verifies the measured no-op authoring workload, not general large-game
throughput. Individual operations, source I/O and final document publication still
run synchronously; the remaining roughly 61 ms prepare step is not a frame-deadline
guarantee. Raw profiles, requests, scene/view context, binary fingerprints and
comparison records remain in ignored build/scene-work-review.

The initial background source implementation separated preparation from owner publication.
In the 342-entity/26-link Prism scene, three warm Release samples at that stage gave median native
Application Prepare envelopes of 64.397 ms for the synchronous no-op edit request,
0.818 ms peak across background begin/poll, and 20.481 ms for final owner publication.
Those preparation peaks ranged 0.737–2.498 ms; publication at that stage still
decoded/committed the scene and refreshed Workspace synchronously. The operation arrays/content match.
These CPU envelopes do not measure worker-only duration or imply a GPU/FPS gain.
Raw profiles and bounded repeated-update responsiveness observations are retained
in ignored build/source-update-review. See [source job contracts](Editor.md#background-scene-source-updates).

Sealed CPU handoff removes the repeated owner decode/encode and retires replaced CPU
scene descriptions on the existing source worker. Three matched warm Release runs
of the same 342-entity/26-link scene now give owner publication Prepare envelopes
21.951 → 7.411 ms for actual saved-source material changes and 20.653 → 0.994 ms for
no-op refreshes. The former includes changed Workspace/graphics preparation; the
latter skips scene/history/snapshot replacement. Source observations and exact
revalidation still execute on the worker. These are request-step envelopes, not
worker-only elapsed times, GPU/FPS results or hard large-scene deadlines. Original
and final authored descriptions, operation arrays, selections and native viewport
pixels match; raw three-sample profiles and binary fingerprints remain in ignored
build/scene-publication-review. Changed Workspace rebuilding, history allocation,
other resource/service ownership and GPU retirement remain separate costs.

## Control observations and scene loading

Application Prepare includes control request dispatch, response construction and
serialization. Full document/world observations can dominate that envelope even
when the game has not changed. `input-play` and `cancel-play-input` return only
`{ok,play}`; poll `play-status` with the current session for input/lifecycle progress.
Standalone `status` likewise reads player/native completion metadata without world
encoding. Use `inspect`/`inspect-play` for explicit full observations, separately
from ordinary held-input polling. GUI controls and these status routes read the
same native services; lightweight reads do not advance gameplay or publish jobs.

Compare the actual command mix, viewport, content and cache conditions alongside
profiles. A smaller Prepare maximum after replacing full-scene polling measures
that product workflow, not isolated World insertion or guaranteed frame delivery.
Active-step totals exclude idle waits; a larger supplied interval can still reveal
pacing gaps. Keep both observations rather than combining unrelated phase peaks.

准备阶段包含控制请求及回复序列化，完整场景读取可成为主要开销。输入接受及生命周期
轮询使用轻量状态，按需单独读取完整场景；记录实际命令组合、画面尺寸及缓存条件。
工作流的准备峰值不等于追加内核基准，活动步骤耗时也不等于端到端交付帧间隔。

## Animated transform branches

Actual Release Prism authoring creates a saved 832-entity scene with eight separate
Surveyor animation bindings playing Maintenance reach, and a 342-entity single-binding
scene. The eight skins share one Player parent/capsule; this is an animation workload,
not eight independent physics characters. Both ordinary native exports present at
2560x1600 on Apple M4 Pro/Metal, arm64 macOS. CPU stack samples identify
`Scene::setTransforms`/world validation inside Application Update as substantial work.
Stack counts are statistical observations, not invocation counts or method timings.

Transform batches now retain unrelated validated worlds, invalidate/recompute the
changed descendants once, and stop ancestor walks at a cached matrix with its full
depth. Exact unchanged stored scalar bits skip work. Structural edits still invalidate
the hierarchy index and validate the whole scene; all changed descendants, camera
inverses and whole-batch rollback retain the same validity requirements.

| Native workload | Before Update median | After Update median | Reduction |
| --- | ---: | ---: | ---: |
| Eight animation bindings | 2.0055 ms | 0.7850 ms | 60.9% |
| One animation binding | 0.2442 ms | 0.1722 ms | 29.5% |

Each value is the median of three run medians, each retaining 119 non-null Update
samples. Eight-binding runs alternate old/new players against byte-identical complete
exported game data; their run medians range 1.8983..2.0250 → 0.6323..0.8130 ms.
Nearest-rank p95 ranges 2.1010..2.5286 → 0.6701..0.9760 ms. Single-binding medians
range 0.2370..0.2667 → 0.1666..0.1906 ms. Initial eight-binding exports differed by
an unused Solo scene/inventory entry: preserve that preliminary investigation, then
deploy the complete new ordinary game data beside the retained old native player
for the final matched comparison. No content manifest was manually synthesized.

The first old/new eight-binding runs include an OS stack observer; later runs and
the single-binding runs do not.
Players run freely, so retained histories cover different wall-clock spans and
animation phases. Lightweight status reads follow the measurement windows; full
scene observations do not occur inside them. This measures Application Update CPU
envelopes, not isolated animation timing, GPU work, frame delivery or general FPS.
Sampling, synchronization, rendering, physics and larger populated-game costs remain.
Before/after static editor captures are byte-identical, SHA-256
`e85af3c833d6f3820f4308c74a17b9742224ea5d49d66351c9d8fd7d74502773`.
Raw profiles, stack reports, content fingerprints and comparison records remain in
ignored build/world-pose-review, alongside the ordinary editable project/exports.

## Reusing animation authority checks

The next matched measurement retains the preceding player with incremental world
poses and changes animation synchronization only. A scene-local binding stamp now
invalidates on animation property value changes, hierarchy changes, physical-owner
addition/removal and structural publication. Unchanged frames reuse completed
binding/budget/authority validation; new runtime clip/fade writers and every applied
pose still validate normally. Prepared asset updates always take full validation.

Use a copy of the same ordinary PrismEnsemble project and all 147 identical exported
game files per workload, including the manifest. Eight/one binding runs alternate
old/new three times, each retaining 119 non-null Update samples. Native presentation
is 2560x1600 on Apple M4 Pro/Metal, Release arm64 macOS. Full initial world and all
player observations precede the one-second warmup; measurement windows use no full
scene polling. First eight-binding old/new runs include a four-second OS CPU sample;
other runs warm for one second then run two more seconds without that observer.

| Native workload | Before Update median | After Update median | Reduction |
| --- | ---: | ---: | ---: |
| Eight animation bindings | 0.7675 ms | 0.3272 ms | 57.4% |
| One animation binding | 0.1675 ms | 0.1005 ms | 40.0% |

Values are medians of three run medians. Eight-binding run medians range
0.6467..0.8831 → 0.2598..0.3279 ms; nearest-rank p95 ranges
0.6760..0.9904 → 0.3045..0.4402 ms. One-binding run medians range
0.1601..0.2633 → 0.1000..0.1022 ms. This stage's static editor captures match exactly,
SHA-256 `de5dfb9d268ae0d7d261a9076d926ae6f5b11357cb65358f07e46b949e9751f0`.
All final native profiles/runtime diagnostics remain
error-free; these are free-running CPU envelopes with different retained wall-clock
spans/phases, not GPU/FPS measurements or independent-character scalability proof.
Do not combine separate stages' measurements into a controlled cumulative speedup.
Preserve profiles, stack reports, startup/player/content hashes and ordinary editable
content in ignored build/animation-sync-review. Animation sampling, per-pose validation,
rendering/physics and broader populated-game/streaming costs remain separate work.

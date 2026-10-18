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

Background source updates separate preparation from owner publication. In the
342-entity/26-link Prism scene, three warm final Release samples give median native
Application Prepare envelopes of 64.397 ms for the synchronous no-op edit request,
0.818 ms peak across background begin/poll, and 20.481 ms for final owner publication.
The preparation peaks range 0.737–2.498 ms; publication still decodes and commits the
scene and refreshes Workspace synchronously. The operation arrays/content match.
These CPU envelopes do not measure worker-only duration or imply a GPU/FPS gain.
Raw profiles and bounded repeated-update responsiveness observations are retained
in ignored build/source-update-review. See [source job contracts](Editor.md#background-scene-source-updates).

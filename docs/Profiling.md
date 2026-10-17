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
`elapsedNanoseconds` is the input to step; during the normal run loop it describes
the preceding work/idle interval. It is distinct from the current step's measured
`totalNanoseconds`. A paused sample explicitly distinguishes intentionally skipped
simulation. Failed callbacks retain completed phases, the measured failing phase
and their native error; later phases remain null. Invalid arguments and calls on
already stopped owners do not manufacture a sampled step.

The service retains the latest 120 steps, the greatest measured step (`slowest`)
and the greatest supplied interval (`longestInterval`) since enablement. Separate
summaries preserve startup/hitch evidence after ordinary history eviction. Equal
maxima retain the earlier observation. Session/frame/sample/update identities and
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
steps, not proof of gameplay updates before startup. The phase transitions are
reported on stderr. Zero frames still means run until closed. Subsequent synchronous
resource work remains included and catch-up/drop warnings are unchanged.

## Renderer sampling

**Profile mesh rendering**, `set-profiling` and `inspect-profile.profile` retain
separate renderer CPU/GPU windows. See [editor operations](Editor.md)
and [rendering contracts](Rendering.md). Renderer preparation/encoding/submission
exclude outer scene/resource/script/physics/GUI work. Optional GPU timestamp
intervals cover physical render passes; overlapping intervals are not additive as
frame time. GPU drops/errors and unavailable capabilities remain explicit.

# Game simulation pause

`dot:engine` exposes `gameClock.snapshot()`, `gameClock.setPaused(boolean)` and
`gameClock.step()`.
The scene-local native service starts unpaused at revision 1; same-value requests
retain the revision. Snapshots own their boolean/revision values, with revision
encoded as a decimal string. Constructors and reload migration may inspect but
cannot write. Wrong-thread, malformed, oversized and retired writes fail explicitly.
There is no automatic save persistence or time-scale hierarchy.

## Single simulation steps / 单步模拟

While paused, `gameClock.step()` returns a decimal request ID. It queues at most one
step; another pending/running request fails explicitly. `snapshot().step` is null
initially or an owned `{id,state}` with pending/running/completed/cancelled/failed.
Acknowledgement is not completion. Inspect the same ID's actual terminal status.

After the next valid variable call's unscaled onFrame callbacks, the session consumes
the pending request and dispatches one prepared FixedUpdate, native physics, one
Update and native animation, each with exactly `1/fixedHz` seconds. Live source
polls only once for this complete frame/fixed/update batch; no generation changes
between its phases. The clock stays paused throughout. Writes to pause and new
step requests fail while running, including inside gameplay callbacks. Invalid
variable deltas do not dispatch or consume the request; zero is a valid wall delta
and can execute a requested step. Normal paused fixed callbacks never consume it.

This is one simulation quantum, not a presentation/frame-rate setting. Unscaled
onFrame, rendering, input publication, loading and audio playback continue normally;
audio spatial synchronization uses the supplied wall delta. A step does not inject
or replay input: fixed and Update see the current published input. Game-specific
callbacks can explicitly edit native state. Completed effects are not rolled back.
Isolated component failures remain in the normal script report; a realm/native
failure faults the session and marks a running step failed, without claiming the
remaining phases completed. Completed fixed work still increments its counter.

Resume cancels a pending step. Scene replacement/close retires pending work as
cancelled and running work as failed; a new scene starts unpaused with no request.
Retained clock handles expose the final status but reject writes. The last request
survives source reload because the native clock belongs to the scene, not the realm.
Requests and lifecycle changes advance revision, with capacity reserved before
acceptance. There is no hidden multi-step backlog or accumulated paused time debt.

暂停时请求单步，下一合法变量帧在非缩放 onFrame 后，以一个 fixedHz 时间量推进同代脚本、
物理和动画并保持暂停；读取请求状态确认完成。恢复/换场景取消待执行请求，原生失败保留已完成
阶段并标记失败。单步不注入输入、不回放暂停时间，也不是视频帧或音频暂停。

```ts
import {gameClock, input, ScriptComponent} from 'dot:engine';

export class Menu extends ScriptComponent {
  #frame = '';

  onFrame(_seconds: number): void {
    const frame = input.snapshot();

    if (frame.index !== this.#frame) {
      this.#frame = frame.index;

      if (frame.keys.M.pressed) {
        gameClock.setPaused(!gameClock.snapshot().paused);
      }
    }
  }
}
```

Use the normal paired save/load hooks to preserve edge watermarks across realm
reload. Input observations do not consume edges; multiple fixed or variable calls
can observe the same published frame.

## Actual scheduling

GameSession update dispatches the independent unscaled `onFrame(seconds)` phase
before variable gameplay. It runs while paused and uses the same bounded lifecycle,
async-result, preparation, failure-isolation and ownership contracts as other hooks.
Live scripts poll once for this pair, then dispatch gameplay from that same prepared
generation. Fixed dispatch still precedes the variable frame in editor/native loops.

Pause is latched at each simulation phase boundary. A paused fixed call skips game
fixed hooks and Jolt advancement. A paused variable phase skips game Update hooks
and advances animation by zero to synchronize authored/control changes without
advancing clip clocks. An already-started callback batch/physics phase finishes;
pause requests do not roll back completed effects. A request in onFrame affects the
following variable gameplay phase immediately. Resuming there allows that Update;
fixed simulation resumes at its next scheduled boundary.

The application/Play scheduler keeps consuming wall-time intervals while paused;
resume does not replay accumulated paused physics steps. Native application profile
step counts describe scheduled callbacks, not proof of simulated physics; its paused
flag is the independent application focus-loss policy. Session
inspection supplies completed `simulatedFixedSteps` and `simulatedUpdates`; editor
Play `fixedSteps` counts actual fixed phases across its lifetime and exposes the
current scene clock and completed variable updates. Native `lifetimeFixedSteps`
counts completed fixed phases across scene replacement, including single steps;
Play fixedSteps derives from that native counter. An unpublished Play clock is
null until activation succeeds; unavailable services do not report an invented
revision. A manual fixed phase runs inside the application update callback, so
its CPU work belongs to that callback's profiling envelope; application scheduled
fixed-callback counts do not include it. Native session counters reset with scene
replacement. Scheduler discarded wall time can still be reported during a long
paused application stall.

Input, native rendering, product controls, source reload/preparation, explicit scene
requests and frame/UI callbacks continue. Frame callbacks may intentionally edit
native state, including checkpoint placement; pause is not a read-only world lock.
Audio playback remains independent and continues; spatial synchronization still
uses frame time. Explicit teleport restoration uses `audio.resetMotion()` as usual.
Pause does not stop audio devices, reset voices or mute existing queued PCM.

Scene replacement starts a fresh unpaused clock and retires old writes; failed
uncommitted replacements keep the old clock. Fault and close retire writes while
retained handles remain readable. Stop hooks still run during explicit close.

## Shared product controls

Inner operations are exact `snapshot`, `step` (only command), and `set-paused` with a
required boolean `paused`. Unknown/duplicate/extra fields fail. Editor Play uses current session and
scene scope; the native player requires its current scene scope:

```json
{"command":"clock","session":"1","scope":"83","value":{"command":"set-paused","paused":true}}
{"command":"clock","scope":"55","value":{"command":"snapshot"}}
{"command":"clock","session":"1","scope":"83","value":{"command":"step"}}
```

Each object is a separate request to the corresponding product. Closing/non-running
states permit only an available snapshot. Editor Pause simulation/Resume simulation
buttons, Step simulation and these commands use the same product implementation. They do not change
authoring history or dirty state. A successful request is a model change; inspect
completed counters/poses over subsequent real frames to verify frozen simulation.

# Game simulation pause

`dot:engine` exposes `gameClock.snapshot()` and `gameClock.setPaused(boolean)`.
The scene-local native service starts unpaused at revision 1; same-value requests
retain the revision. Snapshots own their boolean/revision values, with revision
encoded as a decimal string. Constructors and reload migration may inspect but
cannot write. Wrong-thread, malformed, oversized and retired writes fail explicitly.
There is no automatic save persistence or time-scale hierarchy.

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
current scene clock and completed variable updates. An unpublished Play clock is
null until activation succeeds; unavailable services do not report an invented
revision. Native session counters reset with scene replacement. Scheduler discarded wall time
can still be reported during a long paused application stall.

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

Inner operations are exact `snapshot` and `set-paused` with a required boolean
`paused`. Unknown/duplicate/extra fields fail. Editor Play uses current session and
scene scope; the native player requires its current scene scope:

```json
{"command":"clock","session":"1","scope":"83","value":{"command":"set-paused","paused":true}}
{"command":"clock","scope":"55","value":{"command":"snapshot"}}
```

Each object is a separate request to the corresponding product. Closing/non-running
states permit only an available snapshot. Editor Pause simulation/Resume simulation
buttons and these commands use the same product implementation. They do not change
authoring history or dirty state. A successful request is a model change; inspect
completed counters/poses over subsequent real frames to verify frozen simulation.

Prism Relay opens a keyboard checkpoint menu with M, releases relative-pointer
capture, and renders native text. F6 saves an actually grounded point; F7 explicitly
restores it while the menu remains open. M resumes and requests capture again.
Airborne pause preserves the native airborne pose, so save still refuses it. The
menu survives realm reload; a new scene starts closed. This is a keyboard menu;
clickable widgets and visual layout authoring remain unfinished.

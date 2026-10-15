# Transform animation foundation

`Dot.Animation.Clip` provides immutable shared TRS channel data. `Dot.Animation.Files`
loads/saves native `.dotanimation` clips, and `Dot.Animation.SceneAnimator` binds
logical node indices to live scene-scoped entity handles. This foundation is usable
by native engine consumers and runnable acceptance. glTF/GLB node animations now
convert to these clips and native model bindings. Saved animation players now run
through GameSession, editor Play and native exports. TypeScript playback controls
are available through the engine-owned SDK. Timeline controls remain unfinished.
[Native GPU skinning](Skinning.md) now includes glTF/GLB import, scene/editor bindings,
isolated Play and owned exports. Native SceneAnimator cross-fades now have CPU and
actual skinned-GPU acceptance. Library/SDK/editor transition controls, visual character
authoring, morph targets, blend graphs/state machines, events/root motion and production animation streaming
remain unfinished; this does not establish full animated-model support.

Interpolation follows the [Khronos glTF animation specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#animations):
STEP holds the preceding key, LINEAR interpolates vectors and uses shortest-arc
SLERP for rotation, and CUBICSPLINE uses Hermite interpolation with tangents scaled
by interval seconds. Cubic quaternions are normalized after sampling; authored
quaternion signs and tangents are retained. Invalid zero quaternion/scale and
non-finite/overflow results fail explicitly. A channel clamps independently to its
own first/last key. Clip duration is the latest channel key, including leading
constant intervals and zero-duration static clips.

A clip owns 1..30000 unique node/path channels, node indices 0..9999, at most one
million aggregate keys and finite strictly increasing times in 0..86400 seconds.
Names are 1..256 UTF-8 bytes without NUL. Values are XYZW unit quaternions for
rotation and four-component vectors with w=0 for translation/scale; scale magnitudes
must be at least 1e-6. Cubic tracks require at least two keys and explicit in/out
tangents; non-cubic tracks do not accept nonzero tangents. Copies share immutable
storage, samples own their output and concurrent read-only sampling is supported.
Default/moved clip handles report errors. No device, file or hidden wall clock is
created by clip validation/sampling.

Native files use one strict JSON structure:

```json
{"name":"Sliding panel","tracks":[{"node":0,"path":"translation","interpolation":"linear","keys":[{"time":0,"value":[0,0,0,0]},{"time":2,"value":[4,0,0,0]}]}]}
```

Paths are translation/rotation/scale; interpolation is step/linear/cubic. Cubic
keys additionally require inTangent/outTangent arrays. Unknown/duplicate/missing
fields, wrong numeric types and invalid descriptions fail. Files are bounded to
64 MiB/depth 64 and project-root portable relative `.dotanimation` regular paths;
symlinks, special files and missing parents fail. Loading reads only the clip.
Saving encodes completely before atomic replacement and preserves the previous
file on validation failure. It does not create parent directories.

A SceneAnimator owns a clip and explicit node/entity bindings, not a scene address.
Bindings must be unique, live and cover every clip target in one scene scope.
All supplied bindings retain original rest values for later clip switches, even
when the current clip does not write them. Moving the scene retains valid handles;
changing its scope or removing any bound target fails. Animator operations require the creating thread;
moves preserve state, while moved-from operations return errors.

Playback uses explicit elapsed seconds (0..86400 per advance), configurable speed
in -16..16, forward/reverse looping, zero speed, pause/resume and checked seeks in
0..duration. A zero elapsed step preserves the exact seeked endpoint. Non-looping
playback holds its terminal sample and pauses. A zero-duration looping clip holds
its pose without division by zero. `play(restart)` changes clock/play state only;
`seek`/`advance` applies the pose. Pause freezes clock and scene. There is no hidden
system clock or automatic background mutation.

Each application samples all channels first, preserves current unanimated transform
fields and commits every node's local pose in one Scene::setTransforms transaction.
Animated parent/child transforms therefore combine through the real hierarchy once.
Invalid samples, removed bindings and final world/camera validation failures preserve
all node poses and the playback clock/state. Stop restores only animated fields to
the captured bind pose, preserving current unanimated fields, then stops at time
zero. `Scene::transform` returns an owned local pose without copying names/scripts
or unrelated components; animation uses this lightweight boundary. Playback currently
allocates owned sample/pose batches per application; allocation-free pose caches,
parallel evaluation and populated-scene performance are not established.

After a Debug build, run the focused tests and real offscreen acceptance:

```sh
ctest --preset debug -R '^(animation\.|runtime.animation$)'
build/debug/bin/DotRuntimeAcceptance --animation-test build/debug/tests/animation_acceptance
```

The acceptance saves a native clip and ten actual GPU PNG frames, exercises parent
translation/child rotation/scale, restores bind-pose pixels and verifies drawing
resource release. Offscreen tests additionally compare an animated sample against
an independently constructed native reference pose. CPU unit cases never create a
window/device; file tests are integration cases. Only arm64 macOS is locally
validated; Windows/Linux remain implementation targets.

## Native cross-fades

`SceneAnimator::crossFade(scene, clip, seconds)` transitions to the incoming clip's
directional start. Duration is finite 0..86400 seconds; zero uses the immediate
switch operation. Both active clips advance with the current speed/loop settings.
Their translation/scale channels blend linearly and rotations use normalized
shortest-arc SLERP. `PlaybackState.time` is incoming clip time; its optional
`transition` reports unscaled `elapsed` and `duration` seconds.

Starting retains the visible local pose. Missing source channels start there;
missing destination channels return to original bind values. Unaffected fields
continue using current scene values. A paused or never-applied source stays frozen.
Pause freezes both clip clocks and fade; speed zero freezes only clip clocks.
Nonlooping endpoints hold until transition completion. A new transition during an
existing one freezes the current composite instead of retaining an unbounded layer
chain. At most 30000 distinct channel values fit the existing 10000-node bindings.
Seek, stop and immediate switching restore outgoing-only channels and cancel the
transition only after a successful scene transaction. `play(restart)` changes only
incoming time/play state; it does not restart the fade clock. Restarted clock-only
state counts as unapplied until the next pose sample, so a subsequent fade starts
from the still-visible pose.

Every sample and final world transform must validate before publishing poses,
incoming/outgoing clocks or fade progress. Singular blended scale, invalid source/
target samples, missing bindings and world overflow preserve all prior state.
`Math::slerp` also exposes the normalized shortest-arc rotation operation for other
native consumers, including finite-weight and nonzero-input checks.

```sh
ctest --preset debug -R '^(animation\.fade_|runtime.animation_blend$|Math spherical)'
build/debug/bin/DotRuntimeAcceptance --animation-blend-test build/debug/tests/animation_acceptance
```

The runnable imports the actual eight-joint GLB, reloads a saved opposite-direction
clip, then cross-fades and interrupts it. Ten PBR/shadow/AO images compare against
independently calculated joint angles and CPU-deformed geometry. Stop restores bind
pixels and drawing resources return to baseline. The inspected ribbon images are
diagnostics, not character artwork. This stage is the lower-level native transition
service. ScenePlayers authority reservations, SDK exposure, isolated editor Play/
export transition acceptance and visual blend controls still need integration;
existing immediate library controls remain available. Layered/additive blending,
state machines, animation streaming and populated-game throughput remain unfinished.


## Imported model animation

`Dot.Assets.Model` reads glTF/GLB TRS channels and remaps source node indices to the
selected native hierarchy. Authored TRS signs/axes are retained for partial-channel
animation; matrix-target animation is rejected. Input/output accessors must be
nonnormalized floats without buffer-view stride; scalar input bounds must exactly
match finite increasing key times. Shared/sparse accessors use checked unpacking.
Rotation output is VEC4, other channels VEC3; cubic output contains in/value/out
triples per key. Existing clip validation also rejects duplicate targets, invalid
rotations and singular scale keys. Skinned models use the same joint-node channels
and native players; morph targets remain unsupported.

Import supports at most 1024 source clips, 30000 aggregate channels and one million
aggregate keys. Every channel is validated, including targets outside the selected
scene. Such targets are excluded with explicit warnings; empty resulting clips are
omitted. Names are retained, with stable source-index names for unnamed clips.

Native bundles contain `model.dotscene`, `animations/N.dotanimation` and
`model.dotmodel`. The descriptor records project-relative scene/ordered clip paths
and canonical string entity IDs for each logical node. Primitive entities do not
participate in the node index table. `Dot.Assets.ModelBundle::loadModelBundle`
returns an owned scene, immutable clips and live SceneAnimator node bindings; a
scene move retains those bindings. Files are independent of original glTF bytes.
No playback begins automatically. The descriptor is strict JSON <=1 MiB; encoded
clips total <=64 MiB with aggregate channel/key limits. References remain inside
the same model bundle; links, malformed/duplicate/missing IDs and wrong targets fail.
Import prepares the complete bundle in owned staging and publishes without replacing
an existing destination. This does not isolate hostile filesystem races.

The editor import command reports `model`, `animations` and its existing scene/
geometry fields. Native `.dotmodel`/`.dotanimation` assets can open externally.
Add model to scene remaps and retains the saved animation player and its node IDs.
GameSession/editor Play/native exports automatically play the selected clip. Editor
timeline tools remain unfinished.

```sh
ctest --preset debug -R '^(model.animation_|runtime.model_animation$)'
build/debug/bin/DotRuntimeAcceptance --model-animation-test tests/fixtures/models/animated.glb build/debug/tests/animation_acceptance
```

The runnable case imports a textured GLB, reloads its native bundle and saves nine
actual offscreen model-animation PNGs. An independent authored-pose GPU reference
checks conversion at the cubic midpoint, linear quaternion and exact STEP key;
stop restores initial pixels and GPU resources return to baseline. The diagnostic
fixture is test geometry, not production art or evidence of full character animation.


## Saved animation players

An entity may own an `animation` description: `clips` (unique assets paths), `targets`
(unique `{node, entity}` bindings with canonical string scene-local entity IDs),
`clip` (selected library index), `speed` (-16..16), `looping`, `playOnStart` and
`enabled`. `Dot.Animation.Source` validates/transports this strict description without
IO/devices. Scenes allow at most 128 players, aggregate 10000 bindings/1024 clip
paths. Targets must exist when authoring is published. Removing a referenced target
requires removing/rebinding its external player; a deletion containing the player
and all its targets is valid. Description snapshots copy no unrelated components.

Imported models attach a player to the first model node when clips exist. All nodes
are bound, including non-animated nodes required by other library clips. Insertion
and subtree duplication remap bindings with entity/parent IDs. Duplication requires
all of a copied player's targets to be included, ensuring independent instances.
Undo/redo/save and cloned editor Play preserve descriptions; Stop re-presents the
unmodified authoring scene. Inspector supports selected clip, speed, loop, auto-play,
enabled and clip-asset choices. Node bindings come from model import or native/
automation authoring; there is no visual binding editor/timeline yet.

`Dot.Animation.ScenePlayers` owns a shared scene and playback instances. Each
synchronize validates additions/changes as candidates before replacing its table;
unchanged descriptions retain clocks/rest poses, removed/disabled players release
playback, changed descriptions restart against the current local pose. Same-value
assignments retain playback. Disk clip edits require a changed description or a new
session. All library clips are validated, with 30000 aggregate channels/one million
keys per scene. All immutable library clips are retained within that aggregate budget for native switching.

Actual selected-channel writers must be disjoint across players. Rigid bodies on
an animated node or ancestor are rejected until explicit animation/physics authority
integration exists. These checks run before pose updates and include runtime edits.
Players reserve their selected writers even when paused; disabling releases them.
Each player's sampled pose/clock transaction is atomic; a later player's sample
failure does not roll back earlier players or completed scripts/physics callbacks.

GameSession preparation validates animation resources; start applies auto-play's
initial pose. Negative speed starts at the clip end, forward/zero at zero. Fixed
updates retain their script/physics order; variable update runs scripts, synchronizes
and advances animation once, then audio observes final poses. Native animation
failure faults the session until close, which releases playback while retaining the
inspectable final scene. No hidden timer, renderer or window is created. Scene
player synchronization still copies bounded authoring descriptions and checks node
ancestors; populated-scene frame times/streaming are not established.

`editor.scene_animation` runs actual import/Inspector/Play GPU pixels, stop, save,
copy/remapping, undo and invalid deletion. The standalone native export case removes
its authoring tree after relocating the game; its script observes native animation
motion and fails on stop if animation never advanced. This demonstrates the real
runtime path, not production skinning/blending/character-animation readiness.


## Runtime clip controls

ScenePlayers provides owned state snapshots and explicit play/pause/stop/seek/
configure controls. They synchronize descriptions without advancing time and leave
saved player descriptions unchanged. Playing another library index restores the
previous channels to original binding values and applies the new directional start
in one local-transform transaction. Original bindings include every supplied node,
so switching disjoint clips does not accumulate stale transforms. Speed/looping
remain live runtime settings; same-clip play resumes unless restart is requested.
Restart/first play uses the end for negative speed, zero otherwise. Stop restores
only the selected animated channels and pauses at time zero.

Clip selection validates the new actual writers against other players and rigid-body
authority before applying. Failed targets, samples or final world transforms retain
the old clip, complete pose and playback clock. Reads are observational snapshots
of the last synchronized description; changed descriptions require synchronization.
Controls hold no borrowed scene address and remain owner-thread APIs. The runnable
`runtime.animation` acceptance now saves a tenth image, animation-switch.png, and
verifies prior-channel restoration and exact bind-pose pixels after switch/stop.


## TypeScript playback

`dot:engine` exports `animation` and the owned `AnimationState` snapshot. Entity
provides `animationState`, `playAnimation`, `pauseAnimation`, `stopAnimation`,
`seekAnimation` and `configureAnimation` conveniences. Authored enabled players and
valid clip resources are required; no synthetic player is created for an entity.

| Service call | Behavior |
| --- | --- |
| `state(entity)` | Last synchronized clip index/asset/name, duration/time seconds, speed/looping/playing; returned values are owned |
| `play(entity, clip = null, restart = false)` | Resume current clip or atomically switch to a library index; explicit restart uses its directional start |
| `pause(entity)` | Hold current clock and pose |
| `stop(entity)` | Restore original bound animated fields and pause at zero |
| `seek(entity, seconds)` | Apply a finite [0,duration] sample without changing playing state |
| `configure(entity, speed, looping)` | Set finite [-16,16] runtime speed and loop mode without rewriting authoring |

A resumed nonlooping endpoint stays at its endpoint; request restart to begin again.
ReadOnly state is available during constructors/state migration; every control is
ReadWrite and denied there. `Dot.Script.AnimationSession` owns the native backend
and shared scene; retained host bindings survive wrapper moves/destruction without
borrowing it. Foreign/stale/missing/disabled players and malformed arguments become
catchable DotHostError values. Native scheduling alone advances time. GameSession
prepares resources/services before script construction, runs start hooks before its
zero-time animation step, and variable scripts before native animation/audio.

```ts
import {input, ScriptComponent} from 'dot:engine';

export class Panel extends ScriptComponent {
  onStart(): void {
    this.self.configureAnimation(1, false);
  }

  onUpdate(): void {
    if (input.key('E').pressed) {
      this.self.playAnimation(0, true);
    }
  }
}
```

Feature Lab demonstrates two saved clips, Digit 1/2 selection and Space pause/resume
with actual SDL input, native poses, editor Play and relocated standalone coverage.
It does not establish skinning/blending, timeline tooling or populated-game scale.

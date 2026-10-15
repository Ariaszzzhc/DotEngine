# Transform animation foundation

`Dot.Animation.Clip` provides immutable shared TRS channel data. `Dot.Animation.Files`
loads/saves native `.dotanimation` clips, and `Dot.Animation.SceneAnimator` binds
logical node indices to live scene-scoped entity handles. This foundation is usable
by native engine consumers and runnable acceptance. glTF/GLB node animations now
convert to these clips and native model bindings. GameSession components, the editor
timeline and TypeScript SDK playback remain unfinished. Skinning, morph targets, blending/state machines, events/root motion and production animation
streaming remain unfinished; this does not establish full animated-model support.

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
Unused bindings may describe the rest of an imported hierarchy, but are not stored
as animated targets. Moving the scene retains valid handles; changing its scope or
removing an animated target fails. Animator operations require the creating thread;
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

The acceptance saves a native clip and nine actual GPU PNG frames, exercises parent
translation/child rotation/scale, restores bind-pose pixels and verifies drawing
resource release. Offscreen tests additionally compare an animated sample against
an independently constructed native reference pose. CPU unit cases never create a
window/device; file tests are integration cases. Only arm64 macOS is locally
validated; Windows/Linux remain implementation targets.


## Imported model animation

`Dot.Assets.Model` reads glTF/GLB TRS channels and remaps source node indices to the
selected native hierarchy. Authored TRS signs/axes are retained for partial-channel
animation; matrix-target animation is rejected. Input/output accessors must be
nonnormalized floats without buffer-view stride; scalar input bounds must exactly
match finite increasing key times. Shared/sparse accessors use checked unpacking.
Rotation output is VEC4, other channels VEC3; cubic output contains in/value/out
triples per key. Existing clip validation also rejects duplicate targets, invalid
rotations and singular scale keys. Skinning/morphs remain unsupported.

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
**Add model to scene currently inserts only the geometry hierarchy.** Saved scene
animation components, automatic GameSession playback, editor timeline controls and
TypeScript animation APIs are the next consumer stage.

```sh
ctest --preset debug -R '^(model.animation_|runtime.model_animation$)'
build/debug/bin/DotRuntimeAcceptance --model-animation-test tests/fixtures/models/animated.glb build/debug/tests/animation_acceptance
```

The runnable case imports a textured GLB, reloads its native bundle and saves nine
actual offscreen model-animation PNGs. An independent authored-pose GPU reference
checks conversion at the cubic midpoint, linear quaternion and exact STEP key;
stop restores initial pixels and GPU resources return to baseline. The diagnostic
fixture is test geometry, not production art or evidence of full character animation.

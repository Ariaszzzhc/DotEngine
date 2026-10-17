# Feature Lab

An ordinary editable example project demonstrating engine components and the runtime SDK. Open this
folder directly with DotEditor; its scripts import the engine-owned `dot:engine` module. The build
also supplies `build/<preset>/examples/FeatureLab` without copying engine SDK sources.
The generated copy is refreshed by builds; copy it
elsewhere before persistent authoring changes.

The saved scene contains perspective/orthographic cameras, built-in cube/quad and
OBJ meshes, hierarchy, static/dynamic/kinematic bodies, sphere/box/capsule shapes,
a sensor, an audio listener/source and attached/disabled script descriptors.
Kinematic uses a saved copper PBR material; Quad uses an authored-tangent DotMesh and saved dielectric normal-map
material. The main camera carries a directional headlight. Their native scene
components and shared resource counters are visible in the editor;
material/light runtime script accessors are not yet exposed.
A hierarchy root uses an ID above JavaScript's safe integer range. D moves the
native dynamic body. The scene name/window title shows the completed check count.

`audit.ts` throws on failed behavior checks. Only successful assertions enter the
native Report entity's disabled script metadata. `coverage.json` lists the
current runtime SDK members, including getter/setter distinctions. The inventory
check reads the actual SDK independently, and execution tests compare it with the
report only after explicit stop. Additional checks exercise component removal,
failed-write/control preservation, owned snapshots, collider queries and exact
high-precision identity.

Focused regressions verify lifecycle phases, real contacts/PCM, owned snapshots,
native failures, reload without replaying start and Play isolation. Graphics checks
render offscreen; actual editor workflows use the product agent interface.
The report includes the last input, physics/contact and audio snapshots plus hook
counts. `character.virtualContact` records a real controller ground observation;
contact geometry distinguishes `manifold` from `character`, including native surface
point, normal, signed separation and observing participant. No proxy overlap is
required to observe a controller constraint. Opening the scene alone does not claim that runtime checks have executed.

This project does not enumerate every failure/input permutation. Focused subsystem
regressions provide the broader limits, lifetime, thread, format, device and
error-path coverage described in docs/Testing.md.
Local validation is arm64 macOS; Windows/Linux implementations are not locally
validated. No JavaScript scene/rendering/physics substitute is used.

## Lighting Studio scene

Use File → Open scene with `scenes/LightingStudio.dotscene`, or open it from the
Assets browser's Project → scenes folder. It is an ordinary editable scene in this
same project; `scenes/main.dotscene` remains the startup/API demonstration.

Lighting Studio contains a primary camera, a neutral PBR floor, copper/dielectric/
normal-mapped cubes, glass and alpha-cutout panels, HDRI IBL, SSAO, directional
cascades, a cool point light and a warm spot light with shadows. Use Game view/Play
for the saved camera, or Scene view to navigate and edit materials/lights. It has
a camera script: with Game input enabled in editor Play, L travels to the main
showcase; L there returns to the studio.
M toggles native mouse look in either scene. Each switch rebuilds the native game
services at the frame boundary without changing authoring data. The Play viewport's
Change game scene menu exposes the same operation to editor users and agents.
A missing/invalid target keeps the current scene running and reports its failure.
Save writes the active file; Save scene as retains
history while creating a new file. Workspace export starts at the currently open scene's own path and preserves the
other saved scene files, including the original project startup scene. The exported
game can make the same L-key round trip.

The studio and its floor material were authored through the actual editor's shared
agent operations. It is a persistent capability example, not a separate rendering
probe executable. Open `scenes/main.dotscene` to return to the runtime SDK showcase.

The saved **Material studies** container organizes the five surface examples.
Group/ungroup and hierarchy drag/drop preserve world poses; Edit provides Group
selection and Ungroup selection (Cmd/Ctrl+G and Cmd/Ctrl+Shift+G). The Inspector's
Parent (local pose) field explicitly retains local transforms instead. These
operations share the document transaction and strict agent interface described in
[editor hierarchy editing](../../docs/Editor.md#hierarchy-organization).
The runtime auditor also exercises the native `Entity.reparent`, `scene.group`
and `scene.ungroup` SDK operations without a scene/physics substitute.

`assets/MaterialStudies.dotscene` is editable reusable content saved from the studio
through Save selection as scene, then opened and edited as an ordinary scene. Its
Surfaces root retains the five models and shared project material references. In
Assets, select it and choose Add scene instance to place an independent copy;
the new root supports normal movement, hierarchy, undo/redo and saving. The asset
contains no camera/environment: use Scene view while editing it. This is scene
content reuse; Add linked scene separately enables explicit source refresh and
local overrides with field indicators and stored-baseline restore. See
[scene-content operations](../../docs/SceneContent.md).

## HDR environment

The saved scene uses Studio Small 09 HDRI for material illumination and the sky.
Environment settings can be edited in the editor's Environment pane, undone/redone
and saved; preview, Play and exported games consume the same scene configuration.
Intensity/yaw/background edits reuse the baked map. Changing quality or HDRI bakes
a new map; editing source bytes at the same path requires the existing asset reload
action. The demo keeps its HDRI local so copied projects and exports work offline.

`assets/studio_small_09_1k.hdr` is the unmodified [Studio Small 09](https://polyhaven.com/a/studio_small_09)
1K panorama by Sergej Majboroda, shared under [CC0](https://polyhaven.com/license).
SHA256: `e7cfda5f4e98e623db12b8bfd0184e048488e4855d9c83e2751fb44a32e80c45`.

The camera's directional headlight explicitly requests four 512-pixel shadow
cascades over 50 m with comparison filtering. Light/mesh inspectors can edit saved
shadow quality and casting/receiving. This demonstrates authored directional
shadows; Lighting Studio also demonstrates native point/spot shadows. Advanced
shadow tooling remains unfinished.

The saved scene also enables geometric SSAO with its local HDR environment and
directional shadows. Editor Rendering controls retain radius, bias, strength and
search/filter quality; preview, Play and relocated exports use the saved scene.
Native checks read the actual AO visibility and working storage rather than merely
checking the settings object. This is a capability laboratory, not production-game
art or a populated-scene performance benchmark.

Glass demonstrates sorted HDR alpha blending; Cutout uses an actual nearest-filtered
alpha texture and shared Mask coverage in color, shadows, SSAO and picking. Blend
materials currently contribute no shadow/SSAO geometry and use object-level sorting.


## Native animation

The saved Animated panel has a PBR material and two ordinary .dotanimation clips.
Digit 1 fades/restarts lift, Digit 2 fades to turn, Digit 3 toggles the masked turn
layer with native weight fades, and Space pauses/resumes the player.
Both clips advance during each 0.25-second fade; outgoing-only channels return to
their binding pose. Interruptions retain the visible composite pose; Stop
restores the original binding pose. The script uses the engine-owned `dot:engine`
module's animation service and Entity methods, not scripted transform simulation.
Base animation assertions run on the first update, masked layer assertions on the
next, following initialization checks, so
the full audit stays within the unchanged per-hook host-call limit. Fixed authored
entity names resolve once per script realm; getters still read actual native state.
Hot reload preserves completed checks and native playback without replaying start.
Use the actual editor's Play and agent controls to inspect selected-clip pose,
pause and resumed motion; computer use can exercise the keyboard bindings. This is
rigid-node animation, not skinning.

## Character controller

The saved Character controller owns a real Y-up physics capsule; its child box is
a simple capability visualization. Arrow keys move it at 3 m/s and J requests a
jump. Diagonal input is normalized. In editor Play, enable Game input first. The
expanded static platform supports the capsule, and its child follows native scene
writeback. Collision/gravity/stairs/grounding are native; the script supplies only
desired horizontal speed, jump edges and yaw. Character state and authoring are
available through dot:engine and the editor's shared component commands.

The audit exercises the new component/state/control methods on a separate update
from animation checks to respect the per-hook host-call bound. Headless behavior
regressions cover real input, jumping, source hot reload without resetting motion,
and landing. This is a component showcase; Prism Relay separately provides the
rigged humanoid and shoulder-camera template, with final realistic art unfinished.

The physics audit also sweeps a real sphere onto the saved platform and verifies
the obstacle identity, center, surface point and normal through physics.sphereCast.
The character audit also queries the real native capsule through ray/sphere casts
and excludes its own entity. Native proxies make characters visible to trigger
sensors and rigid-body contacts while controller motion stays in Jolt.

Enable Game input during Play, then press M to toggle native mouse look. Escape
or focus loss releases editor capture; re-enable Game input to resume. The Camera
script rotates only on acknowledged native frames and consumes motion once per
variable update. The standalone runtime also honors script capture requests while
focused. This is a free-look input demonstration; Prism Relay demonstrates the
shoulder camera and animated character, with finished realistic humanoid art pending.


## Continuous collision detection

The yellow CCD slider moves at 240m/s toward the blue 2cm-thick wall. Its saved
Collision detection is continuous, so the native engine stops it at the wall.
Enable Game input and press T to reset. While Play is stopped, change the slider's
Rigid Body / Collision detection to discrete and Play again: it crosses the wall
and stops at the display boundary. Undo restores continuous mode. Inspector and
agent component edits share the same saved property.

`collision.ts` handles reset edges and the fixed-update display boundary only;
Jolt handles the actual swept collision. Source reload retains the last consumed
input publication without replaying reset or recreating the native body. The
existing Dynamic showcase body also uses continuous mode, and its SDK state
reports the actual backend setting. Continuous mode is valid only for solid dynamic
bodies; it does not sweep rotation or sensor crossings.

## Camera checkpoints and game data

F6 explicitly saves the current game camera pose and scene into `camera-checkpoint`.
F7 reads/restores it, requesting the saved scene when necessary; a restored camera
applies the saved pose. Window → Game state exposes owned state/operation status and
slot controls through the same editor/agent implementation. Missing or invalid
checkpoints report their error in `cameraCheckpointStatus` without disabling camera
input. Enable Game input in editor Play before using these keys.

Play saves are disposable and disappear on Stop. Standalone games retain them in
SDL's per-user directory under the stable application ID, independently of project
name/location. Loading is a deliberate game action; engine storage does not itself
move cameras or switch scenes. The SDK auditor exercises state and filesystem APIs,
restores its initial state and deletes its own `feature-lab-audit` demonstration slot.
The demo's stored identity is `dotengine-feature-lab`; template instances receive new IDs.


## Native text

The ordinary SDK audit exercises `hud.label/snapshot/set/remove/clear` and leaves a
bilingual native label and clickable button in Play and exported games. The audit
also covers setButton/focus/activate/events/inputState and explicit text-input
request/acknowledgement. Actual demo button actions increment hudButtonCount;
the audit's own synthetic activation is excluded from that count. It shares the game text path
with Prism Relay; this is not an editor-only overlay or a visual layout designer.

The audit also exercises the native game clock and unscaled onFrame hook, preserving
the caller's current pause state. Pausing simulation keeps frame/UI work and audio
playback active; it does not freeze the editor or its controls.
The SDK audit explicitly checks that `gameClock.step()` refuses an unpaused request.
Use the editor's Step simulation button while paused to advance one real fixedHz
physics/gameplay/animation quantum; inspect its request status and native counters.


The existing Animated panel now has a saved PanelTurn layer using the turn clip,
rotation-only node-zero mask and half weight. It can turn while the lift clip still
owns translation. A separate audit phase exercises the generic/native entity layer
control, independent time/weight snapshots, pause, seek, speed/loop, weight, fade,
stop and failed control, retaining the per-hook host-call limit. Character/state
checks continue on the following phase. The inventory records the two new SDK
members rather than treating implementation-specific assertion totals as completion.

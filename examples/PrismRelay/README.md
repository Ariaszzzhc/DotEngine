# Prism Relay

A non-shooting over-the-shoulder character template in an industrial science-fiction
bay. The native Jolt capsule handles movement, gravity, support, stairs and jumping;
a textured maintenance character with 63 skinned joints uses the engine's animation
player, saved native state machine and cross-fades.
This replaces the earlier collection/timer prototype.

- WASD or arrows: camera-relative movement, with normalized diagonals and acceleration.
- Shift: sprint.
- Space: jump; short input buffering retains presses between fixed steps.
- E: trigger/cancel the masked inspection gesture while moving; native fades blend it in/out.
- M: pause/resume the checkpoint menu. While paused, N advances one native simulation
  step at fixedHz, including the airborne capsule, gameplay and animation layers;
  holding N does not repeat presses. Audio/frame UI remain unscaled.
- Mouse: orbit the shoulder camera; wheel: adjust distance.
- F6: save a grounded resume point. F7: explicitly restore that point, including
  after restarting the exported game. Airborne saves are refused. Loading clears
  velocity, queued jump and game acceleration, and restores feet, facing and orbit.
  The game also requests a new native audio motion baseline for its following
  listener, preventing teleport-induced Doppler without restarting voices.
- In editor Play, enable Game input. Escape releases it; focus loss requires enabling
  it again. In a standalone game, Escape exits.

For animation inspection, pause and disable **Show game UI** in the editor viewport
controls. This reveals the full character without changing the checkpoint menu or
simulation. Step simulation/N still advances one native quantum. Re-enable the view
to operate menu buttons; hidden buttons cannot consume native pointer/keyboard input.
This temporary view choice is not saved or exported.

Relative game control temporarily suspends SDL text input so Chinese input methods
do not intercept movement/jump keys. Releasing control restores editor text entry
without switching the system input method.

The camera uses native sphere sweeps rather than a center ray, pulls in immediately
at obstructions and recovers smoothly along the current clear path. Pitch and zoom
are bounded. Initial overlap uses native separation normals; impossible geometry
reports an error instead of silently publishing an intersecting camera pose.
A listener follows the avatar head with camera orientation, so orbiting does not
create artificial listener speed or Doppler.
The four authored steps exercise traversal; the final taller rise requires jumping.
Industrial utility cabinets and maintenance grilles are environmental props, without pickups,
health, rounds, timers or shooting logic.

Open this folder with DotEditor. Its ordinary scripts import the engine-owned
`dot:engine` module; relative project imports omit extensions. `player.ts` expresses
movement/animation intent and `camera.ts` defines orbit/follow policy. They do not
implement another physics solver or renderer in JavaScript.

The build supplies `build/<preset>/examples/PrismRelay`. Generated example copies
are replaced on every build, including deleted source files. Copy to a separate
folder before persistent editing, or create a writable project from the project
home. Use Play, edit scripts externally and export a native game through the normal
editor workflow. Hot reload retains movement intent, pending jump and orbit state
without restarting the native character, skeleton or animation clocks. During Play,
changed source snapshots compile in one background task while current code keeps
running. Invalid source keeps the prior generation active; Console reports the
error and Retry scripts can retry unchanged bytes. Current source is rechecked before
migration. This migration state is separate from the explicit persistent checkpoint.
The exported game can be operated through the optional [native product mailbox](../../docs/Export.md#opt-in-native-player-control),
including real movement/jump input and persistent game-data slots. A Prism-specific
checkpoint uses the `relay-checkpoint` slot and a strict `prism-relay-checkpoint`
schema for the saved main scene. It validates every field before moving the player;
missing, damaged or mismatched saves leave the current pose intact. F7 reads its
checkpoint without replacing unrelated live game-state keys. It does not resume
physics velocity, animation clocks, other actors or a previous running simulation.
Changed level geometry can obstruct a formerly valid point; normal native physics
resolves overlap on the next step. Native failures after pose mutation are reported,
without a rollback guarantee. Saving/loading use native background jobs. The status remains pending until a disk
receipt arrives; duplicate menu requests are disabled while busy. Pending tickets
survive source reload. Normal exit drains accepted writes and may wait for disk.

Editor Play saves are temporary and removed on Stop; they never overwrite standalone
progress. Exported games use their application ID's user directory, or an explicit
caller-owned `--data-directory`. Startup remains fresh until F7 is pressed. The
game displays controls and checkpoint success/failure through the native
[text layer](../../docs/GameUi.md). The scene binds `assets/interface.dotui`; use the editor UI panel to edit its
parent-local rectangles, text style and menu hierarchy. Full diagnostics remain available as
`relayProgressStatus` in Game state/product inspection. M opens the clickable/keyboard
checkpoint menu and pauses native simulation; F6/F7 remain
usable there, and M returns to the game. Relative pointer capture is released while
open. Audio playback continues independently. New scenes start unpaused; reload
retains the current menu. Click a button, use Tab/ShiftTab to select and Enter/Space
to activate, or keep using F6/F7/M. Holding a button/key does not repeat actions;
Space returning to gameplay does not also jump. Native pointer release and strict
HUD activate share the same queue. Scripts update text/visibility and handle actions;
widget construction and layout live in the saved native resource.

The environment uses real local PBR surfaces, Poly Haven CC0 textures and HDRI,
textured utility cabinets and modular wall ventilation with fitted static colliders.
Mechanical services are grouped in the hierarchy; the imported duct kit and reusable
straight/support scene assets remain editable in the project.
The twelve straight ducts and fourteen supports are linked instances. Open and save
their source scenes, then Refresh source on the desired placement roots to propagate
changes. Inspector field markers identify applied local overrides; Restore field
returns a whole component to the last refreshed baseline without changing siblings.
Apply field to source writes one overridden field to its saved source after conflict
checks, then rebases that instance. Other placements refresh explicitly; scene Undo
does not undo the shared source file.
Play and exported games consume the saved instances without source-file refresh.
Lighting uses directional cascades and three local point-light shadow sources, SSAO and HDR tone
mapping. Local maps use 512px faces within the shared depth-atlas budget.
Surveyor uses blue maintenance armor, a closed helmet and ordinary hands, with three
skinned parts sharing a real 63-joint rig and up to eight influences per vertex.
Four 2K images provide base color, tangent normals, packed occlusion/roughness/metallic
and emission. Idle, walk, jog, sprint, jump start, airborne and landing clips are
baked from the CC0 Quaternius locomotion donor, without gameplay root motion.
The editable source GLB, animation donor, native resources and licenses stay with
the project. [Sources and hashes](assets/Sources.md) record provenance.

The hard-surface maintenance character is an editable content step. Final realistic
art direction, environment polish and populated-game performance remain work;
this is not the final requested visual-quality claim.

To inspect the imported character without running gameplay, select Surveyor and use
Preview animation in the Inspector. The Scene controls provide Frame model, clip
playback, pause, time scrubbing, speed/loop settings and bind pose. End preview restores
the untouched authoring scene. These operations use the same native animation players
as Play; they do not run player/camera scripts, physics or audio. Agent commands and
viewport PNG provenance are documented in [editor operations](../../docs/Editor.md).

To author a separate action, select Surveyor while stopped and expand **New animation
from authoring pose** in Inspector. Create an unused `assets/*.dotanimation` with a
name and duration, then Apply the staged player. This appends a clip while preserving
the imported binding map and seven locomotion entries. Use Edit animation clip to
add keys, audition, undo/redo and save; the initial two keys hold the authored local
pose, rather than copying a sampled locomotion clip. Clip library and Node bindings
are editable Inspector lists with the same strict agent transport. Scene Undo keeps
the separately created resource. Player scripts select library indices, so keep the
seven locomotion entries in place or update the corresponding script indices.

For a saved action-only scene, save a copy of the scene, remove the Player movement
script in that copy and select the new auto-playing clip. The regular editor and
native exporter can then run the authored action without the locomotion script
selecting another clip. Keep the original gameplay scene to test movement, jumping,
checkpoints and camera behavior. The action-only scene is separate from the main scene's masked layer; retargeting is not implemented.

Use the Animation panel's **Dope sheet** to compare head, arm and other channel times
on a shared axis. Box-select a time/row region or Shift-click across tracks, then drag
a selected key to retime the whole group. Dense rows report actual key counts; zoom
or select a bounded range before editing. **Delete selected keys** removes the exact
group in one undoable edit and drops empty channels, while refusing an empty final
clip. Curves, clipboard and native audition share this selection and history. For an
inspection gesture, key the actual `head` and `upper_arm.L`/`upper_arm.R` joints from
the imported binding map; these are distinct from mesh-part roots such as Head1.
Keep the action-only scene and main locomotion scene separately playable.

The saved **Character action study** is available at `scenes/character-action.dotscene`.
Open it in the editor, select Surveyor and edit library slot 7, or press Play to see
the head/upper-arm action. Its peak keys share frame 24 at 30fps (0.8 seconds); its complete 201-channel
clip remains editable at `assets/actions/inspection.dotanimation`. The study removes
the Player movement script so native auto-play owns the action. Open `scenes/main.dotscene`
to return to movement, jumping, camera and checkpoints. The main scene's seven
locomotion slots are unchanged. The action is a small authoring example, not finished
cinematic animation or final character art.

The head rotation now uses native cubic easing, with an outgoing Y derivative of
0.15 per second at time zero and zero derivatives at the peak/return. Its authored
poses and times are unchanged; the arms remain linear. In Edit animation clip,
choose Curves, select the head rotation's first key and choose tangent component Y.
Pause and drag its outgoing square handle, or use the shared tangent transaction
commands. The thick candidate curve changes before the posed character does;
release/Apply tangent preflights and commits one undo step. Rotation handles edit
raw XYZW derivatives before normalization. Save/reopen and Play use the saved native
curve. Physical pointer acceptance remains unverified; this is an editable study,
not a final animation-polish claim.

For precise timing, open Frame options and choose 30/1, then enter frame 24 in
Frame target. Previous/Next frame pause and sample the native pose atomically. Enable
anchor snapping to move a group while preserving offsets, or Snap selected keys to
quantize each selected key with collision rejection. Grid/display choices are workspace
preferences, so choose the rate explicitly after opening a new editor process.


The main scene now appends the saved inspection action at library slot 7 and defines
one Inspection override layer. Its explicit mask contains only the real head and
left/right upper-arm rotation channels (logical nodes 8/10/33); legs, root, local
positions and scales remain driven by the selected locomotion clip. The seven
locomotion assets/indices stay in place. E triggers a native 0.12-second weight fade
while the native layer clock plays the action; a second E fades out and stops. At
natural completion the script requests a 0.15-second fade-out/stop. Script code only
chooses intent; clocks, quaternion/vector mixing, weights and atomic pose publication
belong to the native engine. The checkpoint excludes animation state and explicitly
stops the transient layer on restore. Menu pause freezes base/layer/fade clocks.
Source hot reload retains these native clocks while preserving script intent state.

In the stopped Inspector, expand Override layers to edit the saved clip/mask. During
Preview or Play, the Animation panel exposes actual independent clocks and strict
layer controls. Pause the game to inspect/stop/seek a layer without moving the legs
or advancing time. This is a masked override example, not additive/IK/state-machine
animation or a finished art/animation-quality claim.


## Saved locomotion state machine

`assets/locomotion.dotcontroller` contains Idle/Walk/Jog/Sprint/Jump/Air/Land and
30 ordered transitions. Inspector binds it to Surveyor; Edit animation controller
opens typed parameter/state/transition forms and an isolated native preview.
Applying validates the complete graph, clip library, writer authority and entry
pose before committing one undoable edit; it restarts the paused audition.
Save persists the resource; the scene binding has its own Inspector Apply/Save.
Individual clip audition detaches the controller in its clone only.

The external `player.ts` submits Speed, Grounded and Vertical intent. Native
simulation evaluates one outgoing edge per positive tick, owns state age, sampling,
fades and layer composition, and retains these across source reload. Inspection
remains an independent masked action layer. Pause freezes graph decisions/age;
a debugger step advances one simulation quantum. Restoring a grounded checkpoint
resets controller defaults/entry and stops Inspection, rather than persisting a
borrowed runtime state. This graph is a small locomotion controller, not blend trees,
root motion, animation events or a final character/art quality claim.

The floor, four boundary walls, eight imported cabinets, relay column and four
traversal steps now use native static triangle colliders authored through the real
Inspector/agent operations. Collision follows the saved geometry, including bevels
and nonuniform scale. The eight parent box approximations were removed; ventilation
runs still use their existing box proxies. Play and native exports use the same
geometry preparation and Jolt implementation.

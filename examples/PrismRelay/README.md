# Prism Relay

A non-shooting over-the-shoulder character template in an industrial science-fiction
bay. The native Jolt capsule handles movement, gravity, support, stairs and jumping;
a textured maintenance character with 63 skinned joints uses the engine's animation
player and cross-fades.
This replaces the earlier collection/timer prototype.

- WASD or arrows: camera-relative movement, with normalized diagonals and acceleration.
- Shift: sprint.
- Space: jump; short input buffering retains presses between fixed steps.
- Mouse: orbit the shoulder camera; wheel: adjust distance.
- F6: save a grounded resume point. F7: explicitly restore that point, including
  after restarting the exported game. Airborne saves are refused. Loading clears
  velocity, queued jump and game acceleration, and restores feet, facing and orbit.
  The game also requests a new native audio motion baseline for its following
  listener, preventing teleport-induced Doppler without restarting voices.
- In editor Play, enable Game input. Escape releases it; focus loss requires enabling
  it again. In a standalone game, Escape exits.

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
without a rollback guarantee. Saving is synchronous and may stall a frame.

Editor Play saves are temporary and removed on Stop; they never overwrite standalone
progress. Exported games use their application ID's user directory, or an explicit
caller-owned `--data-directory`. Startup remains fresh until F7 is pressed. The
game displays controls and checkpoint success/failure through the native
[text layer](../../docs/GameUi.md). The authored `uiFontSize` script property controls
its text size relative to viewport height. Full diagnostics remain available as
`relayProgressStatus` in Game state/product inspection. M opens the clickable/keyboard
checkpoint menu and pauses native simulation; F6/F7 remain
usable there, and M returns to the game. Relative pointer capture is released while
open. Audio playback continues independently. New scenes start unpaused; reload
retains the current menu. Click a button, use Tab/ShiftTab to select and Enter/Space
to activate, or keep using F6/F7/M. Holding a button/key does not repeat actions;
Space returning to gameplay does not also jump. Native pointer release and strict
HUD activate share the same queue. Visual UI layout authoring remains unfinished.

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
checkpoints and camera behavior. This does not add animation layers or retargeting.

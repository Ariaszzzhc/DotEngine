# Prism Relay

A non-shooting over-the-shoulder character template in an industrial science-fiction
bay. The native Jolt capsule handles movement, gravity, support, stairs and jumping;
a real 65-joint skinned mannequin uses the engine's animation player and cross-fades.
This replaces the earlier collection/timer prototype.

- WASD or arrows: camera-relative movement, with normalized diagonals and acceleration.
- Shift: sprint.
- Space: jump; short input buffering retains presses between fixed steps.
- Mouse: orbit the shoulder camera; wheel: adjust distance.
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
Power cells and maintenance grilles are environmental props, without pickups,
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
without restarting the native character, skeleton or animation clocks.

The environment uses real local PBR surfaces, Poly Haven CC0 textures and HDRI,
directional cascaded soft shadows, SSAO and HDR tone mapping. The adapted Quaternius
CC0 mannequin includes idle, walk, jog, sprint, jump start, airborne and landing
clips without root motion. The editable source GLB, native resources and license
stay with the project. [Sources and hashes](assets/Sources.md) record provenance.

The current mannequin is a functional animation/template asset. Finished realistic
character art, a cohesive sci-fi suit, environment polish and populated-game
performance remain work; this is not the final requested visual-quality claim.

To inspect the imported character without running gameplay, select Surveyor and use
Preview animation in the Inspector. The Scene controls provide Frame model, clip
playback, pause, time scrubbing, speed/loop settings and bind pose. End preview restores
the untouched authoring scene. These operations use the same native animation players
as Play; they do not run player/camera scripts, physics or audio. Agent commands and
viewport PNG provenance are documented in [editor operations](../../docs/Editor.md).

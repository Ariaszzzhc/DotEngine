# Example projects

## Prism Relay

Prism Relay is a non-shooting over-the-shoulder movement/jump template. Its former
collection/timer rules and drone are removed. A native Jolt character carries an
actual 65-joint skinned mannequin; authored idle, walk, jog, sprint, jump, airborne
and landing clips use native playback/cross-fades. Movement is camera-relative,
accelerated and normalized; buffered jump presses are consumed once rather than
repeating across fixed steps.

```text
cmake --preset debug
cmake --build --preset debug
build/vendor-debug/bin/DotEditor open build/vendor-debug/examples/PrismRelay
```

Enable Game input in Play. WASD/arrows move, Shift sprints, Space jumps, mouse
motion orbits and the wheel changes distance. Escape releases editor input;
focus loss requires re-enabling it. Native standalone Escape exits. A native sphere
sweep keeps the shoulder camera clear of volume/corner obstacles, tightens without
lag and damps outward recovery. The industrial bay includes physical walls, power
cells, a central relay and four traversal steps.

The scripts own game intent and camera policy. Jolt owns motion/support/collision;
native scene transforms, GPU skinning, animation players and rendering remain
engine systems. Source hot reload retains intent, jump buffering and camera state
without recreating the scene or replaying Start. Invalid checkpoints fail migration.

Generated copies under `build/vendor-<preset>/examples/` are replaced on builds. Use the
project home to create a writable template copy, or copy the generated folder before
persistent editing. Source projects open directly and resolve the engine-owned
`dot:engine` module without copied SDK sources. The portable editor includes the
same projects; the normal export workflow publishes a standalone native game.

The scene consumes local PBR assets, Poly Haven textures/HDRI, directional soft
shadows, SSAO and HDR tone mapping. The Quaternius CC0 source mannequin/locomotion
subset, native skin/mesh/animation files and license remain in the project; source
links and hashes are recorded in its asset provenance. No runtime downloads or
special game renderer are used. The mannequin establishes actual rigged movement,
not finished realistic character art or populated-scene performance.

Headless regressions exercise the real project scripts, native physics, camera
volume collisions/recovery/failure, jump scheduling, animation transitions, reload
and template ownership. Integrated visuals use the actual editor's AI operations
and computer use where useful. Local evidence covers arm64 macOS only.

## Feature Lab

[Feature Lab](Acceptance.md) is an ordinary editable project under
`examples/FeatureLab`. It inventories the current native component/runtime SDK and
provides interactive demonstrations, including native character motion and M-toggle
relative mouse look. It is separate from Prism Relay's character-template experience.

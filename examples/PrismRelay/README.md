# Prism Relay

Target: a realistic science-fiction, non-shooting over-the-shoulder character
template with movement, jumping, camera follow and physical collision. Character
controller/animation/camera integration is still required. The current startup
retains the earlier collection prototype during this environment stage; the
controls below describe that intermediate scene, not the final requested gameplay.

Collect all eight violet crystals within 75 seconds. Red zones cost one of three
health points; the centre obelisk and outer walls have real native collision.
The lower row shows eight collection indicators, three health markers and time.

- Enter or Space: start.
- WASD or arrows: move on the arena plane; diagonal speed is normalized.
- P: pause/resume the round.
- R: reset to the start screen, including pickups, health and timer.

The player and crystal animations are attached TypeScript components. The native
scene owns 173 entities: camera/listener, player, floor/walls, sensor pickups,
visual children, hazards, feedback audio and mesh indicators. The crystal OBJ with
explicit normals, hard-surface meshes and synthesized PCM sounds are original
sample assets.

Open this project folder with DotEditor; its scripts import the engine-owned `dot:engine` module.
The build also supplies `build/<preset>/examples/PrismRelay` without copying engine SDK sources.
Copy that generated project to a separate
folder before making persistent edits; it is refreshed from this source template.
Use Play and open scripts externally through Resources. External saves/reload
migrates the round checkpoint rather than recreating the native scene.
Native export includes the player and libraries and requires no editor/compiler
at playback. See the repository export guide for the CLI and constraints.

The window title describes controls; current scene name reports round status,
score, health and remaining time in the editor's inspector. Green player means
victory, red means damage/defeat. Runtime window titles follow scene status.


## Industrial bay environment

The authored game now uses actual physical materials throughout: an industrial
metal deck, concrete service apron, painted equipment, metallic drone hardware,
energy-cell docks, a relay column, intake assemblies and cooling cabinets. The
perspective camera retains the complete playable route and instrument console.
Native walls/sensors and their established gameplay route remain authoritative.

Metal Plate and Concrete Floor Worn 001 are unmodified Poly Haven CC0 1K texture
sets; diffuse uses sRGB, OpenGL normals and packed ARM use linear sampling, with
mipmaps and anisotropy. ARM is shared by baked AO and metallic/roughness slots.
Studio Small 09 supplies real HDRI reflections and diffuse illumination. One
warm directional source has two 1024-pixel soft-shadow cascades, supplemented by
local relay/service lights; geometric SSAO and the default HDR/ACES-fitted output
pipeline are used. Point-light shadow casting remains an engine limitation.

[Asset sources and SHA-256](assets/Sources.md) record the upstream authors/license.
The project and its export retain all inputs locally and work offline. Original
mesh geometry can be regenerated from the repository root with
`python3 tools/prism_relay/author_meshes.py`; this is an authoring tool and is not
needed by the editor or game. Assemblies group their mechanical pieces into owned
meshes instead of inflating the hierarchy with one entity per grille slat.

This is the first physical environment pass. Complete realistic art direction,
humanoid movement/jumping, animation and over-the-shoulder camera remain work;
it is not the final high-quality game or a large-scene performance claim.

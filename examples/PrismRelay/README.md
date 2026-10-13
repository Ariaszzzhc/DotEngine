# Prism Relay

Collect all eight violet crystals within 75 seconds. Red zones cost one of three
health points; the centre obelisk and outer walls have real native collision.
The lower row shows eight collection indicators, three health markers and time.

- Enter or Space: start.
- WASD or arrows: move on the arena plane; diagonal speed is normalized.
- P: pause/resume the round.
- R: reset to the start screen, including pickups, health and timer.

The player and crystal animations are attached TypeScript components. The native
scene owns 40 entities: camera/listener, player, floor/walls, sensor pickups,
visual children, hazards, feedback audio and mesh indicators. The OBJ crystal
and synthesized PCM sounds are original sample assets.

Build DotEngine, then open `build/<preset>/examples/PrismRelay` with DotEditor.
The build supplies the current SDK. Copy that generated project to a separate
folder before making persistent edits; it is refreshed from this source template.
Use Play and the Resources code page to edit scripts while running. Save/reload
migrates the round checkpoint rather than recreating the native scene.
Native export includes the player and libraries and requires no editor/compiler
at playback. See the repository export guide for the CLI and constraints.

The window title describes controls; current scene name reports round status,
score, health and remaining time in the editor's inspector. Green player means
victory, red means damage/defeat. Runtime window titles follow scene status.

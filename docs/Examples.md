# Example games

## Prism Relay

Prism Relay is a playable 3D collection game authored as a normal native project.
Collect eight violet crystals in 75 seconds, avoid red hazards and preserve three
health points. Native walls and the central obelisk block the player. Eight score
indicators, three health markers, a shrinking time bar and the runtime title show
progress. Victory and defeat stop player motion; restart restores the arena.

```text
cmake --preset debug
cmake --build --preset debug
build/debug/bin/DotEditor open build/debug/examples/PrismRelay
```

Enter/Space starts, WASD/arrows move, P pauses/resumes and R resets to the ready
screen. Diagonal speed is normalized. Player color changes on damage and victory;
crystals disappear after collection and their score indicators turn green.

The build copies `examples/PrismRelay` and the current script SDK to
`build/<preset>/examples/PrismRelay`. That output is generated and refreshed by
builds. Copy the project to a separate directory before persistent authoring
changes; editing the source template updates subsequent builds. Open the copied
project with DotEditor, use Play, edit scripts through the Resources code page,
and use [native export](Export.md) to deliver a standalone game.

The saved scene owns 40 native entities and their camera/listener, transforms,
hierarchy, meshes, rigid bodies, sensor pickups/hazards, audio and script
attachments. The OBJ crystal and synthesized mono PCM sounds are original sample
assets. Scripts attach to the player and visual crystal children; they do not
construct an alternative JavaScript scene/rendering/physics engine.

- `rules.ts` owns bounded round state, start/pause/reset, timed immunity, collection,
  deadline/health defeat and checkpoint validation. Its snapshots are independent.
- `player.ts` controls the real native body through input and physics bindings,
  tracks active sensor contacts until explicit end events, plays native feedback
  audio and updates authored meshes/time bar/scene status. Sleep does not silently
  remove a touching hazard or crystal.
- `spin.ts` animates visual children while native parent sensor poses remain stable.

Hot reload preserves all nine running component states: round, spawn, last input/
contact batches, active contacts and crystal rotation. It retains native scene and
body ownership rather than replaying start hooks. Failed checkpoint/configuration
validation prevents migration; it does not silently reset the round. Speed must be
finite (0,12], time limit (0,300] seconds and maximum health an integer 1..3.

Runtime windows follow the current native scene name, so attached scripts can
publish status through the existing scene API without application-specific game
bindings. Title updates are skipped when unchanged and native errors propagate.

Six focused acceptance cases cover owned/failed checkpoints, configuration bounds,
round/pause/win/health/deadline/reset behavior, a complete route through real Jolt
sensors, actual outer-wall collision, hazard sleep/continued damage, offline PCM,
nine-component hot reload, real SDL key events and GPU presentation/pixel changes,
a native audio endpoint, and relocated standalone startup after authoring deletion.
A GPU readback preview is generated under the test build's example_acceptance
folder. Local verification covers arm64 macOS; Windows/Linux remain unvalidated.

This sample exercises the listed systems. Dedicated exhaustive component/API
acceptance scenes are separate work; this game is not evidence of full API coverage.


The [Feature Lab acceptance project](Acceptance.md) separately inventories and
executes the complete current runtime SDK against native components and services.


The [project home](ProjectHome.md) can create a writable Prism Relay copy with the
current SDK directly from its packaged template; manual copying is optional.

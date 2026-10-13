# Feature Lab

An ordinary native project for component and runtime SDK acceptance. Open this
folder directly with DotEditor; it includes its versioned script SDK. The build
also supplies `build/<preset>/acceptance/FeatureLab` with the current SDK.
The generated copy is refreshed by builds; copy it
elsewhere before persistent authoring changes.

The saved scene contains perspective/orthographic cameras, built-in cube/quad and
OBJ meshes, hierarchy, static/dynamic/kinematic bodies, sphere/box/capsule shapes,
a sensor, an audio listener/source and attached/disabled script descriptors.
Kinematic uses a saved copper PBR material; Quad uses a saved dielectric normal-map
material. The main camera carries a directional headlight. Their native scene
components and shared resource counters are checked by the GPU/native acceptance;
material/light runtime script accessors are not yet exposed.
A hierarchy root uses an ID above JavaScript's safe integer range. D moves the
native dynamic body. The scene name/window title shows the completed check count.

`audit.ts` throws on failed behavior checks. Only successful assertions enter the
native Report entity's disabled script metadata. `coverage.json` lists the 81
current runtime SDK members, including getter/setter distinctions. The inventory
check reads the actual SDK independently, and execution tests compare it with the
report only after explicit stop. Five additional checks exercise component removal,
failed-write/control preservation and exact high-precision identity.

Tests verify all lifecycle phases, real contacts and PCM, owned snapshots/native
failures, reload without replaying start, actual GPU presentation/projections,
native audio callbacks, editor Play isolation and relocated standalone startup.
The report includes the last input, physics/contact and audio snapshots plus hook
counts. Opening the scene alone does not claim that runtime checks have executed.

This project does not enumerate every failure/input permutation. Focused subsystem
cases and command-line acceptance cases provide the broader limits, lifetime,
thread, format, device and error-path coverage described in docs/Acceptance.md.
Local validation is arm64 macOS; Windows/Linux implementations are not locally
validated. No JavaScript scene/rendering/physics substitute is used.

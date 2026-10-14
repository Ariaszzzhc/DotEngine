# Feature Lab

An ordinary native project for component and runtime SDK acceptance. Open this
folder directly with DotEditor; its scripts import the engine-owned `dot:engine` module. The build
also supplies `build/<preset>/acceptance/FeatureLab` without copying engine SDK sources.
The generated copy is refreshed by builds; copy it
elsewhere before persistent authoring changes.

The saved scene contains perspective/orthographic cameras, built-in cube/quad and
OBJ meshes, hierarchy, static/dynamic/kinematic bodies, sphere/box/capsule shapes,
a sensor, an audio listener/source and attached/disabled script descriptors.
Kinematic uses a saved copper PBR material; Quad uses an authored-tangent DotMesh and saved dielectric normal-map
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
shadows; point/spot casting and advanced shadow tooling remain planned.

The saved scene also enables geometric SSAO with its local HDR environment and
directional shadows. Editor Rendering controls retain radius, bias, strength and
search/filter quality; preview, Play and relocated exports use the saved scene.
Native checks read the actual AO visibility and working storage rather than merely
checking the settings object. This is a capability laboratory, not production-game
art or a populated-scene performance benchmark.

Glass demonstrates sorted HDR alpha blending; Cutout uses an actual nearest-filtered
alpha texture and shared Mask coverage in color, shadows, SSAO and picking. Blend
materials currently contribute no shadow/SSAO geometry and use object-level sorting.

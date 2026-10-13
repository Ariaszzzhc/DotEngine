# DotEngine

A C++26 game engine for arm64 macOS and x86_64 Windows/Linux, developed from tested core
modules toward a 3D runtime and editor.

## Build and test

Initialize the vcpkg submodule and provide LLVM Clang, CMake 3.31 or newer, and
Ninja on `PATH`. The build preserves compiler/linker flags from the environment,
including `CPPFLAGS`. No machine-specific toolchain paths are stored in the project.

```sh
git submodule update --init --recursive
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
build/debug/bin/DotRuntimeAcceptance --self-test
```

Acceptance implementations live in `tests/acceptance`. `DotRuntimeAcceptance` and
`DotEditorAcceptance` are built only with `BUILD_TESTING=ON` and are never installed
in editor/game distributions. Product executables retain playback and authoring/
automation commands. Use `-DBUILD_TESTING=OFF` when configuring a product-only build.

The `release` configure, build, and test presets provide the optimized configuration.
Executables use the `.exe` suffix on Windows.

## Start the editor

```sh
build/debug/bin/DotEditor
```

Choose an existing project folder or create a new project from Camera and cube,
Prism Relay, or Feature Lab. Creation validates the new project before publishing
and preserves existing destinations. The native workspace provides scene editing,
resource/script authoring, Play/hot reload and standalone native game export.

See [project home and automation](docs/ProjectHome.md), [editor workflow](docs/Editor.md),
[playable example](docs/Examples.md), [export](docs/Export.md) and
[acceptance coverage](docs/Acceptance.md). [Delivery evidence](docs/Delivery.md)
records the usable workflow and current feature boundaries. Local validation is arm64 macOS;
Windows/Linux implementation support is not a claim of local validation.

## Portable editor distribution

```sh
cmake --build --preset release --target DotEditorArchive
```

The platform-named zip under `build/release/distribution` contains the editor,
compiler, SDK, font, templates and native libraries. See
[distribution and validation](docs/Distribution.md) for usage and tested boundaries.
On macOS, double-click `DotEngine.app`. Complete projects in the archive's
`examples` directory can be opened directly or copied through project-home creation.

## Current state

The [capability roadmap](docs/EngineRoadmap.md) records the required large-3D-game
and mature-editor end state. Current rendering/editor foundations do not yet meet
that target; texture sampling and surface resource ownership are implemented; PBR materials,
HDRI IBL, soft shadows, SSAO, HDR tone mapping and editor production tools remain
in development. [Rendering](docs/Rendering.md) distinguishes current contracts
from the required rendering end state.

The static C++26 module library provides native scenes, WebGPU rendering, physics,
audio and entity-attached TypeScript scripts. Interfaces and implementation units
are separate. The editor includes validated project creation, scene/resource/script
editing, Play with hot reload, automation and standalone native export. Prism Relay
is a playable example; Feature Lab exercises every component and runtime SDK member.
The portable editor archive supports tested local deployment. Release signing,
notarization and installer publication have not been validated.

SDL3 windowing, input, and the application loop are available. Run
`build/debug/bin/DotRuntimeAcceptance --window-smoke` in a desktop session for a short native
window acceptance case. Automated window tests use SDL's dummy video driver.

The math module provides checked transforms, quaternions, camera matrices, and
WebGPU depth conventions. Run `build/debug/bin/DotRuntimeAcceptance --math-test` for its
camera projection and TRS decomposition acceptance case. Matrix decomposition
validates affine structure, shear, reflection, and reconstruction precision.
GLM stays private to the implementation.

Dawn rendering supports colored triangle lists, camera transforms, depth testing,
RGBA8 offscreen readback, and native window presentation with resize handling.
Run `build/debug/bin/DotRuntimeAcceptance --gpu-test` for pixel readback or
`build/debug/bin/DotRuntimeAcceptance --render-smoke` for a short visible triangle case.
GPU/native CTest cases require an available device and desktop session; they
report initialization failures instead of treating missing hardware as success.

Indexed meshes upload once and share GPU buffers across instances. Mesh draws
carry separate world transforms and tint colors; resource counters report live
ownership. Run `build/debug/bin/DotRuntimeAcceptance --mesh-test` for a perspective-camera
cube acceptance case.

Mesh assets support strict versioned `.dotmesh` JSON and bounded OBJ geometry
import, with explicit warnings for unsupported retained attributes. Run
`build/debug/bin/DotRuntimeAcceptance --asset-test <existing-directory>` for atomic save/load
acceptance. See `docs/Assets.md` for the format and importer contract.

EnTT scenes provide scoped entity handles, owned snapshots, validated transforms,
parent hierarchies, and subtree lifetime management. Run
`build/debug/bin/DotRuntimeAcceptance --scene-test` for hierarchy/world-transform acceptance.
Camera and mesh description components are available, with checked projection
matrices and primary-camera ownership. The scene render service loads shared
meshes within a byte budget, prunes unused resources, and reports missing assets
or importer warnings. `build/debug/bin/DotRuntimeAcceptance --scene-render-test` verifies
an actual entity/camera scene through Metal pixel readback. Versioned `.dotscene`
files preserve entity values, hierarchy, transforms, and camera/mesh components.
Run `build/debug/bin/DotRuntimeAcceptance --scene-file-test <existing-directory>` for
save/load acceptance; see `docs/Scenes.md` for the strict format contract.

Jolt physics provides independently owned worlds, scoped body handles, three
primitive shapes, static/kinematic/dynamic motion, sensors, impulses, and ray
queries. Run `build/debug/bin/DotRuntimeAcceptance --physics-test` for falling-body collision
and lifetime acceptance. See `docs/Physics.md` for limits and failure semantics;
optional rigid-body descriptions support validated editing, owned scene snapshots,
and scene persistence. The scene simulation service synchronizes component/pose
edits, retains unaffected motion, and writes world poses back through parent
hierarchies. `--scene-physics-test` exercises scene collision and cleanup;
`--scene-physics-render-test` also checks actual GPU pixels after the fall.
Owned contact events cover begin/stay/sleep/end, sensors, removal/reconfiguration,
and explicit queue limits; scene events retain entity values after destruction.

Audio resources decode bounded WAV/FLAC/MP3 into shared immutable float PCM.
Run `build/debug/bin/DotRuntimeAcceptance --audio-clip-test <existing-directory>` for
known sample values, ownership, and failures. See `docs/Audio.md` for limits
and format contracts. Offline mixing provides scoped voices, pause/seek/loop,
gain/pan/pitch, rate conversion, and allocation budgets. `--audio-mix-test` checks
actual summed PCM and cleanup. Native playback consumes a bounded PCM queue,
counts underruns/clipping, and retains explicit device failures. Run
`--audio-output-test` for a real, muted backend/callback acceptance case.
A dedicated playback worker executes voice controls and continuously fills the
queue while the client is idle. `--audio-playback-test` verifies this through a
real muted endpoint. Spatial mono voices use listener/source poses, distance
attenuation, directional cones, and optional Doppler; `--audio-spatial-test` checks
actual stereo PCM direction. Scenes store validated audio source/listener components
without opening audio resources. `--scene-audio-test <existing-directory>` checks
their file round trip, scoped identity, and failure isolation. Connecting these
components to live playback is provided by `Dot.Audio.SceneAudio` through an
owned bounded clip cache and one backend batch. `--scene-audio-play-test
<existing-directory>` loads a saved scene, verifies actual spatial PCM and
missing-asset retention, and releases destroyed entities' voices/assets.
Voice batches prepare every new resource before editing existing voices;
`--audio-batch-test` checks failed-edit retention and full-capacity replacement
using actual PCM. Playback runs each batch as one worker command.

See `docs/Development.md` for implementation milestones and actual validation.

`Dot.Script.Runtime` executes project-relative JavaScript ESM through a bounded
QuickJS-NG realm, with scoped modules, JSON export calls, Promise jobs, contextual
errors, and allocation/time limits. `--script-test <existing-directory>` exercises
the real VM and cleanup. See `docs/Scripting.md` for native TypeScript compilation,
scene/component/input/physics/audio APIs, entity lifecycle and hot-reload contracts.

`Dot.Core.Process` runs explicit tools without shell interpolation, captures
bounded stdout/stderr, preserves real exit status, and cleans up on timeout.
`--process-test <fixture-executable> <existing-directory>` verifies real children;
see `docs/Processes.md`. This supplies the execution boundary for the integrated
standalone Go TypeScript compiler, without a Node.js dependency.

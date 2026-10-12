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
build/debug/bin/DotRuntime --self-test
```

The `release` configure, build, and test presets provide the optimized configuration.
Executables use the `.exe` suffix on Windows.

## Current state

The core is a static library using C++ modules, with diagnostics, error results,
fixed-step timing, and memory statistics. The runtime currently exercises these
foundations, scoped events, and atomic project file access. Interfaces and
implementation units are separate. The graphical runtime and editor are under development.

SDL3 windowing, input, and the application loop are available. Run
`build/debug/bin/DotRuntime --window-smoke` in a desktop session for a short native
window acceptance case. Automated window tests use SDL's dummy video driver.

The math module provides checked transforms, quaternions, camera matrices, and
WebGPU depth conventions. Run `build/debug/bin/DotRuntime --math-test` for its
camera projection and TRS decomposition acceptance case. Matrix decomposition
validates affine structure, shear, reflection, and reconstruction precision.
GLM stays private to the implementation.

Dawn rendering supports colored triangle lists, camera transforms, depth testing,
RGBA8 offscreen readback, and native window presentation with resize handling.
Run `build/debug/bin/DotRuntime --gpu-test` for pixel readback or
`build/debug/bin/DotRuntime --render-smoke` for a short visible triangle case.
GPU/native CTest cases require an available device and desktop session; they
report initialization failures instead of treating missing hardware as success.

Indexed meshes upload once and share GPU buffers across instances. Mesh draws
carry separate world transforms and tint colors; resource counters report live
ownership. Run `build/debug/bin/DotRuntime --mesh-test` for a perspective-camera
cube acceptance case.

Mesh assets support strict versioned `.dotmesh` JSON and bounded OBJ geometry
import, with explicit warnings for unsupported retained attributes. Run
`build/debug/bin/DotRuntime --asset-test <existing-directory>` for atomic save/load
acceptance. See `docs/Assets.md` for the format and importer contract.

EnTT scenes provide scoped entity handles, owned snapshots, validated transforms,
parent hierarchies, and subtree lifetime management. Run
`build/debug/bin/DotRuntime --scene-test` for hierarchy/world-transform acceptance.
Camera and mesh description components are available, with checked projection
matrices and primary-camera ownership. The scene render service loads shared
meshes within a byte budget, prunes unused resources, and reports missing assets
or importer warnings. `build/debug/bin/DotRuntime --scene-render-test` verifies
an actual entity/camera scene through Metal pixel readback. Versioned `.dotscene`
files preserve entity values, hierarchy, transforms, and camera/mesh components.
Run `build/debug/bin/DotRuntime --scene-file-test <existing-directory>` for
save/load acceptance; see `docs/Scenes.md` for the strict format contract.

Jolt physics provides independently owned worlds, scoped body handles, three
primitive shapes, static/kinematic/dynamic motion, sensors, impulses, and ray
queries. Run `build/debug/bin/DotRuntime --physics-test` for falling-body collision
and lifetime acceptance. See `docs/Physics.md` for limits and failure semantics;
optional rigid-body descriptions support validated editing, owned scene snapshots,
and scene persistence. The scene simulation service synchronizes component/pose
edits, retains unaffected motion, and writes world poses back through parent
hierarchies. `--scene-physics-test` exercises scene collision and cleanup;
`--scene-physics-render-test` also checks actual GPU pixels after the fall.
Owned contact events cover begin/stay/sleep/end, sensors, removal/reconfiguration,
and explicit queue limits; scene events retain entity values after destruction.

Audio resources decode bounded WAV/FLAC/MP3 into shared immutable float PCM.
Run `build/debug/bin/DotRuntime --audio-clip-test <existing-directory>` for
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
the real VM and cleanup. See `docs/Scripting.md` for contracts; TypeScript compilation,
engine host APIs, script components, and hot reload remain under development.

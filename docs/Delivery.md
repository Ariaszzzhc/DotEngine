# DotEngine development evidence

DotEngine currently provides an authoring-to-native-game foundation. It does not
yet satisfy the large-3D-game, advanced rendering or mature-editor target in the
[capability roadmap](EngineRoadmap.md). The following links record existing behavior
and its evidence; passing these tests does not establish missing product capabilities.

| Delivered capability | Implementation and verification |
| --- | --- |
| Static C++26 engine, separate module interfaces/implementations | `DotEngine` static target and CXX_MODULES file set; runtime/editor consume modules without installed public headers |
| Required native stack | Pinned vcpkg manifest: SDL3, Dawn, GLM, miniaudio, Jolt, EnTT, Go TypeScript compiler and QuickJS-NG; actual GPU, audio, physics and compiler acceptance |
| Create/open projects and edit games | [Project home](ProjectHome.md) creates three validated templates; [workspace](Editor.md) edits native scene/hierarchy/components, resources and TypeScript, with undo/redo and conflict-aware saves |
| Surface image/texture resources | [Rendering](Rendering.md): bounded PNG/JPEG/Radiance import, explicit color spaces, shared GPU textures/samplers/mips and unlit material base-color sampling; native pixel/lifetime checks |
| Direct physical rendering | Metallic/roughness GGX, normal/emissive/data texture slots, directional/point/spot lights and perspective/orthographic view rays; saved material/light components, shared preview/Play/export, basic inspector/agent authoring and actual gallery/pixel/lifetime checks; dedicated material tooling remains planned |
| Environment preprocessing foundation | Real HDR panorama GPU conversion, diffuse convolution, GGX reflection mips, correlated-Smith BRDF LUT, shared residency/lifetime and actual Poly Haven input; surface/sky/authoring IBL consumption remains planned |
| HDR display pipeline | Linear RGBA16F scenes, emissive radiance, raw pre-map readback, exposure, Reinhard/ACES fitted mapping and single sRGB output across immediate/persistent/native scene rendering; camera/editor exposure authoring remains planned |
| Entity-attached ESM scripting | [Scripting](Scripting.md) compiles TypeScript, attaches lifecycle objects to native entities and exposes scene/components/input/physics/audio; hot reload retains compatible state |
| Engine-owned script modules | `dot:engine`/`dot:host` registry embedded in the static library; injected matching declarations, extensionless relative resolution, nonexecuting static graph validation and real reload/export/relocation coverage |
| External source editor | Resources/new-script/Script inspector/diagnostics share validated default-app opening; embedded source UI removed; agent file buffers/check/hot reload preserved; actual macOS TextEdit and Zed file-path verification |
| Agent control of authoring | GUI and strict JSON commands share implementation; CLI and live mailbox cover creation, inspection, scene/resource/script edits, type checks, Play, saving, export and close |
| Play and export | Play isolates the authoring scene; [native export](Export.md) includes player, compiled ESM, scene/assets and runtime libraries; relocated player runs after authoring deletion |
| Playable example | [Prism Relay](Examples.md): collection, collisions, hazards, audio, score, deadline, pause, victory/defeat, restart and reload; real native input/GPU/audio acceptance |
| Dedicated feature acceptance | [Feature Lab](Acceptance.md) covers every native component and all 81 runtime SDK members; focused subsystem suites add invalid input, boundaries, resource lifetime and failure recovery |
| Deployable editor | [Portable distribution](Distribution.md) includes compiler/embedded modules/font/templates/player and dependencies; relocated editor creates, checks, previews, runs and exports an independent game |

Development proceeded in reviewed, tested commits from core files/processes/events
through native devices, scenes, physics/audio, scripting, project services and the
editor. [Development log](Development.md) records actual stage results and earlier
limitations at the time of each entry. Local tests run with both Debug and optimized
Release/NDEBUG; acceptance includes real services rather than mocked success.

Latest arm64 macOS Debug and Release/NDEBUG suites each pass 848/848 CTest cases
(789 doctest cases and 59 executable checks), taking 131.60 and 35.32 seconds.
The [test layers](Testing.md) separate 189 pure CPU unit cases from files/compiler/
platform integration, 68 offscreen GPU cases and native/acceptance workflows.
The earlier 184-case unit selection passed with deliberately invalid SDL drivers;
the current 189-case selection adds pure URI/diagnostic and environment checks.
All 253 engine/application/test C/C++ files pass formatting checks; the three WGSL
sources are maintained separately with consistent indentation, spacing and blocks. The current 257-entry native zip
passes CRC/executable-permission checks; all 16 Mach-O files pass strict signatures
and relative/system loader-reference inspection. Its extracted macOS application
starts through LaunchServices, also after moving the application alone. Complete
source/package examples open and compile with embedded engine modules.

All former acceptance dispatch/implementations are now development-only targets
under `tests/acceptance`, compiled only with `BUILD_TESTING=ON`. An actual OFF build
has no test compilation entries, acceptance targets or former acceptance symbols
in the three product executables. Playback/authoring and agent commands remain.

The supported implementation targets are arm64 macOS and x86_64 Windows/Linux.
macOS uses Metal through Dawn, Windows D3D12, Linux Vulkan with X11/Wayland surface
paths. Native processes, files and dependency packaging have platform branches.
Only arm64 macOS has been built and run locally; no Windows/Linux result is claimed.
The caller's LLVM/compiler/linker environment is preserved.

The current renderer draws colored meshes with cameras and depth; asset import is
bounded OBJ/dotmesh retaining authored normals and UVs. Surface attributes are
persisted and uploaded; the shader samples base-color textures and multiplies
linear material/vertex/object colors and emission into HDR before exposure/tone
mapping/display encoding. Renderer API material draws additionally support direct
physical lighting with persisted material/light authoring and bounded shared asset caches. The editor uses
component inspectors, external source opening and bounded agent file drafts. These are the delivered feature boundaries, not promises of material/lighting
systems or a general IDE. Runtime-member coverage is not exhaustive driver/device
or argument-permutation coverage. macOS copies are ad-hoc signed and locally tested;
release identity signing/notarization and installer publication have not been performed.

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
| Environment preprocessing foundation | Real HDR panorama GPU conversion, diffuse convolution, GGX reflection mips, correlated-Smith BRDF LUT, shared residency/lifetime and actual Poly Haven input; authored IBL consumption is implemented; async bake/cache/streaming remains planned |
| HDRI renderer consumption | Explicit shared binding, single-scattering split-sum PBR, mapped normals, roughness reflections, yaw/intensity, camera sky, shared HDR/depth, scene-camera integration and actual HDRI-lit gallery; scene persistence, bounded HDRI caches, editor drafts/undo/Play/export and offline Feature Lab delivery now consume IBL; async baking/streaming remains planned |
| Authored geometric SSAO | Saved optional settings, depth/world-normal guides, bounded radial search and bilateral filtering, diffuse IBL composition, transactional Rendering pane, preview/Play/relocated exports, actual grayscale/HDR and owned working-map release; temporal quality and scalability remain planned |
| Directional soft shadows | Saved optional single directional source, stabilized 1..4 cascades, real depth passes, sixteen-tap/hardware PCF, receiver-plane correction, mesh cast/receive, overlap blending, inspector/agent/Play/export and actual hard/soft/disabled images; point/spot/contact-hardening/scalability remains planned |
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

Latest arm64 macOS Debug and Release/NDEBUG suites each pass 883/883 CTest cases
(822 doctest cases and 61 executable checks), taking 163.10 and 34.00 seconds.
The [test layers](Testing.md) separate 201 pure CPU unit cases from files/compiler/
platform integration, 87 offscreen GPU cases and native/acceptance workflows.
The earlier 184-case unit selection passed with deliberately invalid SDL drivers;
the current 201-case selection adds pure URI/diagnostic environment, shadow and occlusion checks.
All 271 engine/application/test C/C++ files pass formatting checks; the fourteen WGSL
sources are maintained separately with consistent indentation, spacing and blocks. The current 259-entry native zip
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


Independent editor view/navigation, actual opaque GPU picking and transformed mesh
framing now consume the same scene rendering path without persisting editor cameras
to game scenes. This stage passes 893/893 CTest cases in Debug (165.13s) and
Release/NDEBUG (37.69s) on arm64 macOS. Inventory is 831 doctest identities plus
62 executable checks, including 201 unit, 87 offscreen, 50 native and 100 acceptance
labels (overlapping). All 271 C/C++ files and 15 standalone WGSL files pass the
established formatting checks.

The refreshed macOS ZIP contains 259 entries and 46,013,826 bytes. CRC, executable
modes, exclusions, project/template AO and exact HDRI hashes pass; all 13 regular
Mach-O files and 3 relative library symlinks pass strict signatures and loader/rpath
checks. The actual bundled editor opens Feature Lab and changes real viewport pixels
when framing an entity. Gizmos, scalable scene
production and realistic Prism Relay polish remain planned. Windows/Linux have not
been validated locally.


The reference-guided editor now has actual docking panels, neutral dark/light themes,
bounded hierarchy/asset search and a real diagnostics console. Sarasa UI SC Regular
1.0.42 integrates upstream Inter Western glyphs; one 24,049,996-byte font and its
OFL license ship inside the app. No operating-system font installation is required.
GUI viewport rendering resolves current physical dimensions after layout, retaining
owned render targets until submission and propagating failures without fake frames.

This stage passes 898/898 CTest cases in Debug (177.40s) and Release/NDEBUG (42.59s)
on arm64 macOS: 836 doctest identities plus 62 executable checks. Labels overlap:
201 unit, 90 offscreen, 52 native and 102 acceptance. All 272 C/C++ files pass
clang-format and whitespace checks; the 15 standalone WGSL sources are unchanged.
The refreshed ZIP contains 259 entries and 47,092,128 bytes; CRC, executable modes,
exclusions, exact font/HDRI copies and Feature Lab AO scenes pass. All 13 regular
Mach-O files, 3 relative library symlinks and the app pass strict signature and
relative/system loader/rpath checks. The final bundled editor opens real Feature Lab
and displays the docked workspace. Windows/Linux have not been validated locally.
Persisted layout profiles, gizmos, asset thumbnails, large-scene production and the
realistic polished Prism Relay game remain unfinished.


Selected entities now have actual world/local translation handles and meter snapping.
A disposable preview scene renders live movement; release commits one history entry,
Escape/focus loss cancels, and Undo/Redo restores authored data and real pixels.
The UI and automation share revision-checked begin/update/finish operations. Changed
or cancelled drafts invalidate preview readiness until drawing completes. Rotation/scale
handles, plane movement and multi-selection remain planned.

This stage passes 904/904 Debug CTest cases (182.62s) and 904/904 Release/NDEBUG
cases (45.81s) on arm64 macOS. After final whitespace/interface-comment rebuilding,
7/7 focused Debug checks also pass (4.57s). Inventory: 842 doctest identities plus
62 executable checks; overlapping labels include 202 unit, 90 offscreen, 54 native
and 104 acceptance. All 277 C/C++ sources pass clang-format and whitespace checks.
Latest ZIP: 259 entries, 47,110,227 bytes, verified CRC/font/license/HDRI copies,
permissions, signatures and portable loader/rpath references (13 regular Mach-O files
and 3 relative library symlinks). Inspected actual XYZ overlay in bundled Feature Lab
and closed without discarding authored changes; the source project is unchanged.
Only macOS was validated. Large-game and mature-editor completion remains unproven.


Selected entities now support translation, rotation rings and axis scale endpoints
with world/local coordinates and explicit meter/degree/factor snapping. Rotation
uses the actual affine operation frame, including reflected/nonuniform parents;
world operations that require shear fail instead of overwriting approximate TRS.
All modes share disposable preview, one commit, cancel and Undo/Redo. Exact agent
values now remain isolated from native pointer motion across frames.

This stage passes 909/909 CTest cases in Debug (172.22s) and Release/NDEBUG (41.16s)
on arm64 macOS. Focused checks pass 12/12 (6.79s). Inventory: 847 doctest identities
plus 62 executable checks; overlapping labels: 204 unit, 90 offscreen, 56 native,
106 acceptance. All 277 C/C++ files pass formatting/whitespace checks; 15 standalone
WGSL sources are unchanged. Refreshed ZIP: 259 entries, 47,120,689 bytes; strict
signatures, portable loader/rpath references, CRC/permissions and exact font/license/
HDRI copies pass. Native Feature Lab shows real rotation rings and scale endpoints;
the inspected source project remains unchanged. Plane/uniform-scale handles,
multi-selection, scalable production scenes and polished realistic game content
remain unfinished. Windows/Linux have not been validated locally.

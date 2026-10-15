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


Conservative homogeneous frustum tests now skip main HDR and SSAO geometry draws
outside the camera while preserving off-camera shadow casters. Pixel-cropped picking
retains original IDs. Real encoding-loop counts are available through Renderer and
the editor Rendering panel/agent inspection. A 1024-input case submits one main mesh
draw with identical pixels; a 513-object scene reuses one mesh and recomputes visibility
when the camera changes. Input validation remains complete even for culled objects.

The engine/editor suite passes 916/916 Debug (177.18s) and Release/NDEBUG (40.53s),
plus the subsequently added archive regression 1/1 in both configurations (0.27s /
0.12s). Focused rendering checks pass 8/8 (5.30s). Final inventory is 917 identities:
854 doctest plus 63 executable/script checks; overlapping labels: 206 unit, 95
offscreen, 56 native, 106 acceptance. All 281 C/C++ files pass formatting checks.

Archive generation now snapshots the distribution into an owned directory with
Finder metadata excluded, retains source contents/permissions/relative symlinks and
publishes only after archive creation succeeds. Final ZIP: 259 entries, 47,132,050
bytes. All 228 files/links equal the signed source package payload; CRC/executable
permissions, exact font/license/HDRI data and strict signatures/portable loader/rpath
checks pass (13 regular Mach-O files, 3 relative symlinks). Native Feature Lab
statistics change from 7/7 visible/input to 0/7 when the editor camera faces away,
with SSAO draws dropping to zero and conservative shadow draws remaining 28; scene
data stays unmodified. Only arm64 macOS is validated. Preparation remains linear
with a 4096-input limit; indexing, instancing, streaming and polished game production
remain unfinished.


Stable consecutive instancing now batches compatible mesh/texture runs while preserving
per-instance transform/tint/emission/PBR/backface attributes and original integer pick
IDs. Main/SSAO share one compact storage upload; shadow cascades use indexed matrix
storage. Full validation/culling remains and scene/renderer/caster inputs accept
10000 instances. Counters explicitly distinguish instances from actual GPU commands.

A 16-instance varying-material case matches independent-resource reference pixels
with one color/SSAO command each. Four-cascade shadows match reference HDR pixels
while reducing 12 commands to 8. A complete 10000-entity scene (9999 meshes + camera)
uses one color command. Full Debug: 922/922 (184.64s); Release/NDEBUG: 922/922 (40.17s).
After contract-comment rebuilding, focused Debug passes 16/16 (13.00s). Inventory:
859 doctest plus 63 executable/script checks; overlapping labels: 208 unit, 98
offscreen, 56 native, 106 acceptance. All 285 C/C++ and 15 standalone WGSL files
pass established formatting checks; real pipeline creation is verified.

Latest ZIP: 259 entries, 47,143,881 bytes; CRC/permissions, exact font/license/HDRI,
all 228 source file/link payloads, strict signatures and portable loader/rpath checks
pass (13 regular Mach-O files and 3 relative links). The bundled Feature Lab reports
7 visible instances -> 4 color/SSAO commands and 28 cascade instances -> 16 shadow
commands, ready=true/dirty=false. Additional screenshot review was unavailable because
macOS was locked; native acceptance and actual control-interface checks passed.
Preparation remains linear and grouping consecutive. Spatial indexing, streaming,
production import/animation/editor tools and realistic polished Prism Relay remain
unimplemented. Only arm64 macOS was validated.


Refit BVH visibility now rejects outside subtrees and accepts strictly inside
subtrees, retaining exact tests for boundary candidates and explicit homogeneous
tests for projective/float-unrepresentable bounds. Full input validation, stable
batch/picking identity and off-camera shadows remain. Camera changes reuse the
index; motion refits branches, count changes rebuild. Dense 1024-input visibility
keeps identical pixels with fewer than 64 classification tests; this is not an FPS
or fully sublinear scene-traversal claim.

Full Debug: 926/926 (180.80s); Release/NDEBUG: 926/926 (44.83s). All 288 C/C++ files
pass formatting. Latest ZIP: 259 entries, 47,172,615 bytes, 228 matching file/link
payloads; CRC/modes/font/license/HDRI and strict signatures/loader/rpath checks pass
(13 regular Mach-O, 3 relative links). Bundled Feature Lab camera movement reuses
7 indexed objects with one node test and no individual tests/refits; ready=true,
dirty=false, normal close/control cleanup verified. No extra screenshot review.
Only arm64 macOS validated. Preparation remains linear; adaptive indexing,
streaming, production import/animation/editor tools and polished realistic Prism
Relay remain unfinished.


Authored tangent frames now survive current DotMesh persistence and GPU physical
shading. Complete/absent/orthogonal/unit/seam contracts fail malformed inputs;
reflection, nonuniform scaling and double-sided back-face orientation retain actual
pixel coverage. Feature Lab's saved normal-map panel uses its real authored frame
asset, exercised by native editor/Play/export workflows. This is a glTF import
prerequisite; no glTF/GLB importer or animation completion is claimed.

Full Debug: 929/929 (190.96s); subsequent strengthened authored GPU fixture: 1/1
(0.44s), with no runtime change. Full Release/NDEBUG: 929/929 (43.31s) after that
fixture rebuild. All 288 C/C++ and 15 WGSL files pass established formatting checks.
Latest ZIP: 261 entries, 47,174,666 bytes, 230 matching file/link payloads and two
exact tangent assets. CRC/modes/font/license/HDRI, strict signatures and relative
loader/rpath checks pass (13 regular Mach-O files, 3 relative links). No extra
screenshot review. Only arm64 macOS validated; the full engine/editor/game objective
remains active.


Static glTF/GLB import now publishes reusable native meshes/images/five-slot PBR
materials and a hierarchical model scene without replacing existing resources.
Editor and agent instantiate it with fresh parent/entity IDs in one undo transaction;
real rendered HDR matches an independent native reference. Opaque alpha, tangent
basis, reflected hierarchy, color spaces and indirect-only baked occlusion retain
coverage. Animation/transparency/compression and broader production asset tooling
remain unfinished; diagnostic geometry is not realistic-game content.

Final focused: 10/10 (3.54s). Full Debug: 939/939 (191.67s); Release/NDEBUG: 939/939
(48.13s). Native import/viewport/undo/redo/save-reopen acceptance passed. All 296
C/C++ and 15 WGSL files pass established formatting/pipeline checks. Latest ZIP:
263 entries, 47,317,150 bytes, 232 source-matching file/link payloads; cgltf notices,
font/license/HDRI, CRC/modes, strict signatures and portable loader/rpath checks pass
(13 regular Mach-O files, 3 relative links). No extra screenshot review. Only arm64
macOS validated. Full engine/editor/game objective remains active.


Material coverage now spans native persistence, static glTF/GLB import, actual HDR
composition/depth, masked shadow/SSAO guides and original-identity integer picking.
Feature Lab ships real Glass and textured Cutout demonstrations and exposes actual
solid/blended pass counters. Sarasa UI SC Regular (Inter Western glyphs) and its OFL
license remain bundled. Object-center transparency sorting is approximate; no
refraction, OIT or transparent-shadow completion is claimed.

Final Debug: 947/947 (197.14s); Release/NDEBUG: 947/947 (48.41s), after both builds.
Native Feature Lab editor/Play/export acceptance and added coverage pixel tests pass.
All 300 C/C++ and 16 WGSL files pass established formatting/pipeline checks. Latest
ZIP: 269 entries, 47,353,213 bytes, 238 source-matching file/link payloads. Alpha
assets, cgltf notices, exact font/license/HDRI, CRC/modes/exclusions, strict signatures
and portable loader/rpath checks pass (13 regular Mach-O files, 3 relative links).
No extra screenshot review. Only arm64 macOS validated. The full engine/editor/game
objective remains active; realistic polished Prism Relay is not complete.


The structured material Inspector now edits the actual PBR, five-slot texture and
coverage contract. Unsaved fields preview matching scene objects without writes;
Apply creates one independent material history entry. Undo/redo, exact disk-conflict
checks, save/reopen, dirty close/save-close and saved-asset Play/authoring-preview
restoration are covered. Real release-directory UI inspection caught/fixed narrow
Inspector label clipping. Scripts continue to open externally.

Full Debug: 953/953 (206.63s). After test-only Play/pointer strengthening, final Debug
focused: 7/7 (4.14s); final Release/NDEBUG full: 953/953 (37.56s). An intermittent
native pointer failure in the first Release run was investigated and its window/
event preconditions hardened with unchanged assertions; ten focused repetitions pass.
All 303 C/C++ and 16 WGSL files pass established formatting checks. Latest ZIP:
269 entries, 47,386,765 bytes; all 238 file/link payloads match signed source. Exact
font/license/HDRI/material assets, CRC/modes/exclusions, strict signatures and portable
loader/rpath checks pass (13 regular Mach-O files, 3 relative links). QA instances
and owned staging/request scratch are closed/removed. Only arm64 macOS validated.
Material graphs/thumbnails, mature production authoring and realistic polished
Prism Relay remain incomplete; the full engine/editor/game objective stays active.


The Assets browser now has actual image/mesh/material thumbnails, grid/list,
folder breadcrumbs and project-wide search. Material Inspector renders the current
unsaved form on an independent studio sphere. Preview rendering uses an isolated
device and the packaged CC0 HDRI; main scene pixels/camera/draw counters and source
files remain unchanged. Errors and pending/stale states are explicit. Caches/queue
are bounded and one synchronous job runs per frame; async/zero-copy previews and
broader scene/model/audio thumbnails/material graphs remain unfinished.

Actual release-directory UI QA recorded eight ready thumbnails plus the material
sphere, nine completed jobs, zero pending and no errors. All final builds precede
serial validation: Debug 960/960 (211.45s), Release/NDEBUG 960/960 (38.93s). All 307
C/C++ and 16 WGSL files pass established formatting checks. Latest ZIP: 272 entries,
48,604,643 bytes; all 240 file/link payloads match signed source. Exact font/license/
three HDRI copies/credit/material assets, CRC/modes/exclusions, strict signatures
and portable loader/rpath checks pass (13 regular Mach-O files, 3 relative links).
QA instances/request scratch and owned staging are closed/removed. Only arm64 macOS
validated. Mature production authoring and realistic polished Prism Relay remain
incomplete; the full engine/editor/game objective stays active.


The 2026-10-15 editor stage fixes real Hierarchy mouse-selection snapshot
invalidation and adds owned asynchronous asset/material previews. Native mouse
regression fails on the old inline action and passes with deferred selection;
scene/history semantics remain intact. Preview source/HDRI/GPU work and destruction
run on an owned thread with bounded queues, current-ticket publication, explicit
errors, pause/reset and joined close. GUI uploads and model imports remain
synchronous; this does not establish a general streaming system.

The combined stage passes 967/967 Debug (219.82s) and 967/967 Release/NDEBUG
(48.18s) on arm64 macOS: 903 doctest identities and 64 executable/script checks.
All 310 C/C++ and 16 WGSL sources pass established formatting checks. Actual release
UI inspection shows eight thumbnails/current copper sphere, nine completed jobs
and no pending/error state; normal close returns exit 0. Mouse-selection proof
comes from the native SDL regression, not the inconclusive CUA coordinate clicks.
The refreshed ZIP contains 272 entries/48,635,402 bytes, with 240 exact payloads,
CRC/modes/exclusions, font/HDRI/license checks, 13 regular Mach-O files/three links,
strict signatures and relative/system loader references verified. Windows/Linux,
production streaming and the full large-game/mature-editor target remain unvalidated
or unfinished as documented in the roadmap.

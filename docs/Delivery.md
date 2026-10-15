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
| Direct physical rendering | Metallic/roughness GGX, normal/emissive/data texture slots, directional/point/spot lights and perspective/orthographic view rays; saved material/light components, shared preview/Play/export, structured material Inspector, isolated studio/scene preview, asset thumbnails and actual gallery/pixel/lifetime checks; material graphs remain planned |
| Environment preprocessing foundation | Real HDR panorama GPU conversion, diffuse convolution, GGX reflection mips, correlated-Smith BRDF LUT, shared residency/lifetime and actual Poly Haven input; authored IBL consumption is implemented; async bake/cache/streaming remains planned |
| HDRI renderer consumption | Explicit shared binding, single-scattering split-sum PBR, mapped normals, roughness reflections, yaw/intensity, camera sky, shared HDR/depth, scene-camera integration and actual HDRI-lit gallery; scene persistence, bounded HDRI caches, editor drafts/undo/Play/export and offline Feature Lab delivery now consume IBL; async baking/streaming remains planned |
| Authored geometric SSAO | Saved optional settings, depth/world-normal guides, bounded radial search and bilateral filtering, diffuse IBL composition, transactional Rendering pane, preview/Play/relocated exports, actual grayscale/HDR and owned working-map release; temporal quality and scalability remain planned |
| Directional soft shadows | Saved optional single directional source, stabilized 1..4 cascades, real depth passes, sixteen-tap/hardware PCF, receiver-plane correction, mesh cast/receive, overlap blending, inspector/agent/Play/export and actual hard/soft/disabled images; point/spot/contact-hardening/scalability remains planned |
| HDR display pipeline | Linear RGBA16F scenes, emissive radiance, raw pre-map readback, exposure, Reinhard/ACES fitted mapping and single sRGB output across immediate/persistent/native scene rendering; camera/editor exposure authoring remains planned |
| Native GPU skinning | [Skinning](Skinning.md): immutable eight-slot influences, inverse-bind palettes, shared static inputs, transactional GPU vertex/normal/tangent updates and exact bounds; physical/shadow/AO/pick reference tests; imported-skin assets and editor binding remain unfinished |
| Entity-attached ESM scripting | [Scripting](Scripting.md) compiles TypeScript, attaches lifecycle objects to native entities and exposes scene/components/input/physics/audio/animation; hot reload retains compatible state |
| Engine-owned script modules | `dot:engine`/`dot:host` registry embedded in the static library; injected matching declarations, extensionless relative resolution, nonexecuting static graph validation and real reload/export/relocation coverage |
| External source editor | Resources/new-script/Script inspector/diagnostics share validated default-app opening; embedded source UI removed; agent file buffers/check/hot reload preserved; actual macOS TextEdit and Zed file-path verification |
| Agent control of authoring | GUI and strict JSON commands share implementation; CLI and live mailbox cover creation, inspection, scene/resource/script edits, type checks, Play, saving, export and close |
| Play and export | Play isolates the authoring scene; [native export](Export.md) includes player, compiled ESM, scene/assets and runtime libraries; relocated player runs after authoring deletion |
| Playable example | [Prism Relay](Examples.md): collection, collisions, hazards, audio, score, deadline, pause, victory/defeat, restart and reload; real native input/GPU/audio acceptance |
| Dedicated feature acceptance | [Feature Lab](Acceptance.md) covers every native component and all 93 runtime SDK members; focused subsystem suites add invalid input, boundaries, resource lifetime and failure recovery |
| Deployable editor | [Portable distribution](Distribution.md) includes compiler/embedded modules/font/templates/player and dependencies; relocated editor creates, checks, previews, runs and exports an independent game |

Development proceeded in reviewed, tested commits from core files/processes/events
through native devices, scenes, physics/audio, scripting, project services and the
editor. [Development log](Development.md) records actual stage results and earlier
limitations at the time of each entry. Local tests run with both Debug and optimized
Release/NDEBUG; acceptance includes real services rather than mocked success.

Latest arm64 macOS Debug and Release/NDEBUG each pass 1032/1032 CTest identities
(964 doctest cases and 68 executable/script checks), taking 238.56s and 49.65s.
The [test layers](Testing.md) separate 235 pure CPU unit cases from 674 integration,
124 offscreen GPU, 70 native and 123 acceptance cases; labels overlap. All 341
current engine/application/test C/C++ files and 17 WGSL files pass formatting.
The native animation SDK and interactive Feature Lab are covered through actual
TypeScript, SDL input, GPU/audio, isolated editor Play and relocated game exports.
Final package evidence for these stages is recorded at the end of this document.

All former acceptance dispatch/implementations are now development-only targets
under `tests/acceptance`, compiled only with `BUILD_TESTING=ON`. An actual OFF build
has no test compilation entries, acceptance targets or former acceptance symbols
in the three product executables. Playback/authoring and agent commands remain.

The supported implementation targets are arm64 macOS and x86_64 Windows/Linux.
macOS uses Metal through Dawn, Windows D3D12, Linux Vulkan with X11/Wayland surface
paths. Native processes, files and dependency packaging have platform branches.
Only arm64 macOS has been built and run locally; no Windows/Linux result is claimed.
The caller's LLVM/compiler/linker environment is preserved.

The renderer retains authored normals/UVs/tangents and imports bounded OBJ, DotMesh
and static/rigid-animated glTF/GLB content into native PBR meshes/materials/images.
It consumes direct lights, HDRI IBL, directional soft shadows and geometric SSAO,
composes HDR radiance and tone maps for display. Scene authoring and shared caches
span editor preview, Play and exports. The editor has docked panels, an independent
camera, picking, single/group gizmos, multiselection/history, material/asset tools,
external source opening and bounded agent drafts. Saved node-animation players and
script controls now share the native GameSession path. Native GPU skin controllers
are available; imported-skin resources/editor binding, blending, visual animation
authoring, streaming, populated-game performance, broader production
editor tools and polished realistic Prism Relay remain unfinished. macOS copies
are ad-hoc signed and locally tested; release identity signing/notarization and
installer publication have not been performed.

## Earlier delivery stages


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


The 2026-10-15 scene-production stage adds ordered document multiselection,
Hierarchy modifier/range input and transactional subtree Duplicate/Delete with
selection-aware undo/redo. Copies use fresh IDs/remapped parents, preserve native
components and demote copied primary camera/listener flags. Inspector properties
remain active-only; group gizmos/framing/bulk properties are unfinished and
single-object gestures explicitly reject multiple selections.

Full arm64 macOS Debug passes 972/972 (223.16s), Release/NDEBUG 972/972 (56.52s):
908 doctest identities plus 64 executable/script checks. Native input tests cover
Command/Ctrl toggles, Shift ranges, keyboard duplication/deletion and undo; runnable
document acceptance covers group copy/restore. The actual release UI shows three
selected originals, three selected copies and exact scene/selection restoration on
one undo, followed by normal clean close. All 310 C/C++ and 16 WGSL files pass
formatting checks. The latest ZIP has 272 entries/48,646,602 bytes and 240 matching
payloads; CRC/modes/licenses, strict signatures and portable linkage checks pass
for 13 regular Mach-O files/three relative links. Windows/Linux remain implemented
without local validation. Large-game production and mature editor completion are
still open requirements.


The 2026-10-15 group-transform stage makes Move/Rotate/Scale operate on frozen
selections around the active pivot. Topmost selected roots change once; descendants
inherit, invalid later-root TRS samples roll back and release commits one typed
Document transform/history transaction. Shared GUI/agent input, native saved scenes,
actual GPU preview/commit/undo/redo and 301-root commits have focused coverage.
Local group operations use the active affine frame; median/individual origins and
group framing remain unfinished. This does not establish large-game production
readiness or populated-scene frame-time performance.

Full arm64 macOS Debug passes 978/978 (227.37s), Release/NDEBUG 978/978 (47.24s),
with 914 doctest identities and 64 executable/script checks. A final inline-label
row-spacing correction was rebuilt and checked with the eight affected native/
GPU/viewport cases in Debug 8/8 (11.45s) and Release 8/8 (4.02s), after those full
suites. Actual release UI checks show a three-root 3.5-meter draft, one commit/undo
restoring scene/selection, corrected tool layout and normal cancellation/close.
All 310 C/C++ and 16 WGSL sources pass established formatting checks. The latest
ZIP has 272 entries/48,655,590 bytes and 240 exact payloads; CRC/modes/licenses,
strict signatures and portable loader references pass for 13 regular Mach-O files/
three relative links. Windows/Linux remain implementation targets without local
validation; mature-editor, large-game and realistic polished Prism Relay work
remains active.


The 2026-10-15 native animation foundation adds immutable bounded TRS clips,
Step/Linear/Cubic sampling, strict .dotanimation files and an explicit-clock scene
animator with atomic pose/state updates, reverse/loop/nonloop playback and rest
restoration. CPU cases run with invalid SDL drivers; independent-pose GPU checks
and nine inspected acceptance frames establish actual native hierarchical playback.
Model-import, GameSession, editor timeline/Play and TypeScript consumers are still
unfinished, as are skinning, morphs, blending, events and production streaming.

Full arm64 macOS Debug passes 993/993 (224.56s), Release/NDEBUG 993/993 (45.96s):
928 doctest identities plus 65 executable/script cases. All 321 C/C++ and 16 WGSL
files pass formatting checks. The current ZIP has 272 entries/48,656,310 bytes,
240 exact payloads, passing CRC/modes/licenses, strict signatures and portable
loader references for 13 regular Mach-O files/three relative links. Native hierarchy
click regressions additionally pass three consecutive runs each in both builds.
Windows/Linux remain implemented without local validation. The large-game,
mature-editor and polished realistic Prism Relay requirements remain active.


The 2026-10-15 animated-model import stage converts glTF/GLB TRS channels into
native clips while retaining authored transform axes/signs. Selected-scene remapping,
validated/explained exclusions, shared inputs and strict malformed-data rejection
have file integration coverage. Native model descriptors persist scene/clip paths
and exact node/entity bindings, including interleaved primitive entity IDs. Owned
bundle reload, scene moves, original-source removal, explicit playback/restoration,
exact catalog capacity and failed-publication preservation are verified.

An independent authored pose matches actual imported-animation GPU pixels; the
runnable acceptance generates nine actual frames and releases drawing resources.
The real editor import command reports native model descriptor/animation count and
recognizes externally editable model/clip resources. Add model to scene currently
inserts geometry only. Saved animation components, automatic GameSession/editor/
TypeScript playback, skinning/morphs/blending/events/streaming and realistic game
content are unfinished. This stage does not establish production animation or the
full large-game/mature-editor objective.

Final arm64 macOS Debug passes 1001/1001 (215.74s), Release/NDEBUG 1001/1001
(38.71s): 935 doctest identities plus 66 executable/script cases. All 325 C/C++ and
16 WGSL sources pass formatting checks. Latest ZIP is 48,680,122 bytes/272 entries,
240 matching payloads with passing CRC/modes/licenses, strict signatures and portable
loader references for 13 regular Mach-O files/three relative links. Windows/Linux
remain implementation targets without local validation; the full goal remains active.


The latest 2026-10-15 stages add saved animation players through model insertion,
independent duplication/history/save, Inspector, editor Play/GameSession and native
relocated games. Clips/bindings and runtime writer/physics authority are validated;
failed rebinds preserve prior players. Native acceptance checks actual animated
transforms/pixels, Stop restoration and an exported game after authoring removal.
Visual binding/timeline tools, TypeScript playback controls, skinning/morphs/blending/
events, streaming, production scale and realistic polished Prism Relay remain open.

The project launcher now separates Open/Create pages with a coherent graphite
style, aligned identity fields, template explanations and destination preview. SDL
native folder pickers open projects or choose creation locations, retain forms on
cancel and report errors. The callback owns copied results without borrowing Home.
Manual Release-app verification clicked Browse, used/cancelled the real macOS
picker, selected a parent, created a project and reopened it into its editor workspace.
Parent-picker invocation used the shared command; project-picker invocation used
the GUI. Pending-picker full window destruction is not manually verified.

Both builds succeed. Full arm64 macOS Debug passes 1011/1011 (234.55s),
Release/NDEBUG 1011/1011 (47.78s): 944 doctest identities plus 67 executable/script
cases. All 334 C/C++ and 16 WGSL sources pass formatting. The current ZIP is
48,839,304 bytes/272 entries/240 exact payloads, with passing CRC/modes/licenses,
original font/HDRI payloads, strict signatures and portable loader references for
13 regular Mach-O files/three relative links. No private input or shader sources
are shipped; archive staging is removed. Windows/Linux have implementation support
without local validation. These stages do not complete the full active goal.


The latest native animation control stages add atomic clip replacement using
original node binding poses, transient play/resume/restart, pause, stop, seek and
speed/loop configuration. dot:engine exports typed animation state/services and
Entity conveniences. Script preparation/migration reads are observational; controls
remain prohibited there. Owned callbacks retain scene/backend lifetime and reject
foreign/stale/missing/disabled owners, wrong threads and malformed inputs.

Feature Lab includes a saved PBR panel and two native clips: Digit 1/2 switch and
Space pauses/resumes. Native input checks actual pose changes and retained authoring
descriptions; editor Play and relocated standalone tests use the same project.
Its independent inventory verifies 93 SDK members and six extra behavior checks.
Checks span initialization/first update within unchanged host-call limits, and
fixed-name resolution is cached per realm without caching native property values.
The standalone TRS acceptance saves a tenth actual switch image and verifies atomic
old-channel restoration and final bind-pose pixels. Skinning, blending/state
machines/events, visual animation tools, streaming/populated-game scale and polished
realistic Prism Relay remain unfinished; the full objective stays active.

Final Debug passes 1019/1019 (228.83s), Release/NDEBUG 1019/1019 (45.93s), after
updating the template's entity expectation and adding actual copied-clip checks.
The first Debug full run's lone stale expectation is recorded in Development.md.

The owned archive target succeeds. The latest ZIP is 48,883,115 bytes, 276 entries
and 244 exact payloads. Both shipped Feature Lab copies contain the exact two
source clip files and the saved panel binding. CRC/executable modes, original
font/HDRI/licenses, private/source exclusions, strict signatures and portable loader
references pass for 13 regular Mach-O files/three relative links. Private archive
staging is removed. Only arm64 macOS execution is validated.


The latest skinning stages add immutable inverse binds and eight joint influences
per vertex, a bounded CPU reference and native GPU controllers. Static source/index
resources are shared. GPU compute updates positions, inverse-transpose normals,
orthogonal/reflected tangents, triangle validation and exact signed bounds, then
publishes the complete batch after compact status validation. Prior views survive
failed requests/samples and controller retirement. PBR, shadow, SSAO-guide and
picking passes use the same deformed buffer; culling uses current bounds.

The runnable acceptance animates a real native joint hierarchy and saves nine
reference-compared physical/shadow/AO frames plus bind/reference/GPU geometry.
Focused tests also cover indexed/unindexed eight-weight data, float overflow,
reflected seams, exact byte/count limits, dynamic normal-map frames and shared/
foreign/moved/thread/device lifetimes. Final Debug passes 1032/1032 (238.56s),
Release/NDEBUG 1032/1032 (49.65s); all 341 C/C++ and 17 WGSL files pass formatting.
Status readback is synchronous. Automatic imported-skin resources/scene bindings,
editor skin workflows, animation mixing, streaming and populated-game performance
remain unfinished. This does not complete the requested engine/editor/game goal.

The owned archive target succeeds. Final ZIP is 48,935,380 bytes/276 entries/244
exact payloads with passing CRC, executable modes, original font/HDRI/licenses and
private/source exclusions. Strict signatures and portable loader/rpath references
pass for 13 regular Mach-O files/three relative links. Private archive staging is
removed. Shader sources stay private; the new skin shader is embedded normally.
Windows/Linux remain implementation targets without local execution evidence.

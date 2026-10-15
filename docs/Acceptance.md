# Acceptance coverage

Daily CPU unit checks use `ctest --preset debug-unit` after building `debug-unit`.
[Test layers](Testing.md) distinguish pure unit, integration, offscreen GPU and
native/acceptance workflows. Run the complete `ctest --preset debug` and
`ctest --preset release` suites after building both configurations before delivery.
Release enables NDEBUG; behavioral checks use doctest/CTest rather than assertions.
Native GPU/audio checks are serialized where required. Only arm64 macOS is locally
validated; Windows/Linux implementation support is not cross-platform evidence.

The development-only `DotRuntimeAcceptance` and `DotEditorAcceptance` executables
own all subsystem acceptance dispatch and implementations under `tests/acceptance`.
They exist in the build graph only with `BUILD_TESTING=ON`; they are not installed.
The CTest case names, assertions and native services remain the same. Product CLI
checks still invoke the actual DotRuntime/DotEditor/DotGame, and exported-game
coverage starts the shipped player before the separate deterministic native driver
checks the relocated package through the shared player lifecycle and callbacks.

## Dedicated component and SDK scene

[Feature Lab](../acceptance/FeatureLab/README.md) is an ordinary editable native
project at `build/<preset>/acceptance/FeatureLab`. Its saved scene includes both
camera projections, cube/quad/OBJ meshes, parented transforms, all three body
motions/shapes, a sensor, listener/source and active/disabled script descriptors.
An imported ID above 2^53 tests exact identity through scene and script operations.

`coverage.json` records all 81 current runtime SDK members. The independent C++
inventory reads method/getter/setter declarations and readonly fields from the
actual SDK, checks exported constants/functions and notices class additions. The
runtime component records a member only after a relevant behavior assertion passes.
Execution compares the expected inventory with the native report after explicit
stop. Added members or missing checks fail rather than being silently called covered.

| SDK group | Members | Runtime evidence |
| --- | ---: | --- |
| Entity | 34 | Owned/frozen IDs, info, names, local/world transforms, hierarchy, all component getters/setters/removal, body/audio aliases and destruction |
| Scene | 6 | Metadata/counts, rename, deterministic entity views, creation and primary camera/listener |
| Components | 6 | Native-default builders, complete descriptor round trips and independent removal |
| Input | 8 | Every declared key/button against owned frame data, pointer/delta/wheel/text/index getters |
| Physics | 8 | Real native body/stats/ray hit, contacts, velocity/angular velocity, impulse and kinematic controls |
| Audio | 8 | Loaded clip/voice/stats/listener, play/pause/seek/stop and volume |
| ScriptComponent | 7 | Constructor, self/properties and start/fixed/update/stop hooks |
| Constants, transport, error | 4 | keys/mouseButtons, actual invoke and contextual DotHostError |

Five additional assertions check high-precision IDs, removal and preserving prior
state after failed component/physics/audio writes. The scene reports last input,
contacts, physics/audio statistics and lifecycle counts through disabled native
script metadata, which does not inflate running hook counts. C++ tests independently
check injected key/button edges, nonzero pointer/delta/wheel, Unicode text, native
sensor events, actual PCM, byte/allocation/generation cleanup and unchanged start
counts after hot reload. Native tests additionally inspect audio callback progress,
actual GPU pixels and both projections, editor Play/source isolation and relocated
standalone startup after deleting authoring data.

This inventory is runtime-member coverage, not proof of every argument, interface
field permutation, device/driver or unsupported platform. Types are compiled through
the real native TypeScript compiler; the subsystem suites below cover additional
ranges, lifetime, invalid fields and async/fault behavior. The laboratory does not
replace those suites or claim completion merely because a name appears in JSON.

## Engine subsystem evidence

| Feature family | Acceptance cases and focused suites |
| --- | --- |
| Results, text, IDs, logging, clocks, scheduling, memory and event dispatch | runtime.self_test; Core/Text/Path/Event tests |
| Rooted files, exclusive directories, process arguments/output/timeouts | runtime.files/runtime.process; File/StagedDirectory/Process tests |
| Main-thread SDL window/input/application lifecycle | runtime.window; Platform/Input/Application tests; editor.workspace native events |
| Math, geometry, GPU buffers/targets/surfaces, presentation and cache lifetime | runtime.math/geometry/gpu/mesh/render; Math/Geometry/Graphics tests |
| Mesh codecs/import limits and native hierarchy/cameras/scene persistence | runtime.assets/scene/scene_files/scene_render; Asset/Scene/SceneFile tests; Feature Lab |
| Real Jolt bodies, scale/hierarchy binding, contacts, queries and native controls | runtime.physics/scene_physics/script_physics/render; Physics/Body/Contact/ScenePhysics/PhysicsSdk tests; Feature Lab |
| WAV/FLAC/MP3 validation, mixing/resampling/seek, batching/spatial audio, real output queues | runtime.audio_clips/audio_mix/audio_batch/audio_spatial/audio_output/audio_playback/scene_audio/playback; Audio/Mixer/Spatial/Queue/Output/SceneAudio/AudioSdk suites; Feature Lab |
| TypeScript capture/compilation, owned ESM, QuickJS objects/JSON/promises/fault budgets and host transport | runtime.typescript/script/script_host/script_instance; Compiler/Runtime/Host/Instance/ScriptSdk suites |
| Native component attachment, authoring commands, script dispatch/input/physics/audio, hot-reload state and failures | runtime.script_components/authoring/input/physics/audio/reload and render variants; SceneScripts/Reload/GameSession suites; Feature Lab |
| Project creation, documents/history/selection, resource/code editing, mailbox and native GUI/Play | editor.project/document/gui/workspace and render variants; Project/Document/Resource/EditorCommand/Workspace/Play suites |
| Portable data and native executable/library export, strict trees and ownership | runtime.exported_game; PublishedProgram/GamePackage/GameExport cases; relocated Feature Lab/Prism Relay startup |
| Complete playable authored game | Prism Relay rules/contact-route/hazard/restart/reload cases plus native window/GPU/audio/export acceptance |

The CTest cases above are actual configured targets; their implementations and
failure/lifetime tests remain the authoritative evidence. Planned features are
listed separately in the development log. The native project home additionally
covers validated creation/open and mailbox handoff. Release signing/installer
workflows and further editor refinement remain work; no manual desktop, other-host
or exhaustive driver coverage is claimed here.

## Ambient occlusion renderer foundation

OcclusionTests supplies three pure CPU cases; OcclusionGpuTests supplies four real
offscreen cases. runtime.occlusion writes actual enabled/disabled/visibility images
from a combined IBL/cascaded-shadow scene. These validate the explicit Renderer API. Scene/codec/document/workspace cases now
cover saved settings, transaction rollback/undo, native controls and draft guards.
Scene-material pixel checks verify actual saved HDR attenuation and shared binding
ownership; Play, relocated packages and Feature Lab native checks retain settings
and render real AO. Temporal/scalable rendering remains planned.


## Independent editor viewport

Three ViewTests cases exercise CPU pose/lens validation, framebuffer pixel-center
projection, transformed mesh bounds and navigation/focus transactions. Two
PickingGpuTests cases exercise actual opaque coverage/depth/backface/mirror policies,
background hits, view-target preservation, thread boundaries and moved ownership.
SceneRenderTests adds independent views without scene cameras, full-size entity IDs
and hierarchical/mirrored bounds. Three workspace cases cover commands, drafts and
render revision guards plus real SDL mouse/keyboard/release/focus-loss navigation.
The existing missing-camera case now verifies independent Editor availability and
explicit Game failure/undo recovery.

`editor.viewport` runs `DotEditorAcceptance --viewport-test <existing-parent>` against
a saved camera-free ordinary project. It draws actual preview pixels, selects a
visible cube, clears selection on background, frames/orbits, checks immutable scene/
history, reports missing Game camera and saves without inserting an editor camera.
The driver and dispatcher compile only with BUILD_TESTING. These checks establish
opaque single-click selection and navigation; gizmos/docking and polished content
remain subsequent work.


## Docked editor, mixed text and GUI viewport phases

WorkspaceLayoutTests covers real initial hierarchy/viewport/inspector/assets geometry,
deep/light palette changes with unchanged scene pixels, queued docking and reset,
actual filtered asset paths and case-insensitive hierarchy names, Chinese child
search with expanded ancestors, 800x480 bounds, malformed commands and Play isolation.
Existing native shortcut/material/environment/AO/home/laboratory cases retain their
first-frame, undo, draft, Play/Stop and lifetime coverage. editor.viewport additionally
checks theme pixel retention, native queued docking and layout reset in a saved
camera-free project.

GuiTests loads the pinned Sarasa UI SC font from a staged file, deletes the file, moves
the renderer and reads actual mixed Latin/CJK glyphs and pixels before releasing its
GUI context. Two viewport-phase cases validate current DPI/resize pixels, retained
targets, foreign/unrendered/wrong-size failure, explicit absence, exceptions and
non-replay, GUI reentry/close guards and finite size/sixteen-slot boundaries.
These are real service/native checks; profiles, gizmos and thumbnail tools remain
planned rather than inferred from the concept art.

Move-tool validation separates pure ray/axis math, document-preview transactions and
native UI/GPU behavior. AxisDragTests covers perspective/orthographic rays, top-left
UV, absolute sampling/snap and parallel/invalid boundaries without devices. The
TransformToolTests draft ownership/move/commit/conflict cases run in filesystem
integration, checking one history entry, parent reflection, rejected shear, stale or
foreign documents, owner-thread enforcement, no-op recovery and cancellation.
WorkspaceMoveTests checks real changed/restored scene pixels, unpublished draft data,
stale-ready invalidation, save/Undo/Redo and native pointer release/Escape/focus loss.
`DotEditorAcceptance --viewport-test <existing-parent>` now includes a full live
move-preview/commit/Undo workflow in addition to camera-independent selection.

Rotation/scale coverage adds continuous shortest-arc angle unwrapping, affine-frame
reflection/stretch, perspective/orthographic sampling and parallel/pivot/singular
boundaries. Transactions verify pivot and local TRS preservation, world-shear rejection
and positive scale limits. Native tests hit actual ring segments/scale endpoints,
preview real changed pixels, release a transaction and Undo once. Agent tests assert
exact degrees, factors and movement across frames so native pointer input cannot
silently overwrite commands. Viewport acceptance runs all three transform modes.

FrustumTests runs device-free clip-plane/contact/reflection/shear/behind-eye/extreme
checks. VisibilityGpuTests checks 1024 inputs -> 1 encoded main draw with identical
pixels, rejects malformed off-camera data, retains reports on failure/move and checks
owner-thread rules. Pixel-cropped picking retains original indices and independent
mesh reports. Shadow/AO fixtures verify off-camera shadow contribution and unchanged
visible-only AO pixels. A 513-object scene reuses one mesh cache, submits one color
draw and recomputes after camera changes. Runnable native viewport acceptance includes
an outside object and checks actual visible/input/culled statistics.

Archive regression creates root/nested Finder metadata alongside normal payload,
empty directories, an executable and a relative symlink where permitted. It checks
actual ZIP extraction/permissions, source preservation and rejection of unsafe
staging paths without altering the previous archive. This runs only in BUILD_TESTING.

DrawBatchTests checks stable consecutive runs, skipped entries, binding identity,
owned indices and full/invalid boundaries. InstancingGpuTests compares varying PBR
attributes, reflection and SSAO against independent same-geometry resources that
prevent batching; texture/equal-depth cases verify run isolation and original pick
IDs. Shadow fixtures compare four-cascade instanced results against independent
resource references. Full-scene acceptance now renders 9999 meshes plus its camera
in one color command while keeping invalid-clear and cache-lifetime checks. Native
viewport acceptance includes two visible shared meshes and one culled mesh, requiring
one color command and unchanged authoring/Undo/export behavior.


SpatialIndexTests compares ordered BVH queries against linear classification for
moving/reordered perspective/orthographic bounds, checks owned data/copies, unchanged
reuse, partial refit, rebuild/clear, 10000 bounds and invalid update preservation.
VisibilityGpuTests verifies dense subtree pruning with unchanged pixels, camera-only
reuse, motion, deletion, original pixel-pick IDs, projective/extreme homogeneous tests,
and rotated/reflected/sheared/boundary candidates against exact classification.
Native viewport acceptance requires actual index reuse statistics in inspection.


Authored tangent coverage checks orthogonal unit frames, complete/absent attributes,
handedness seams, malformed/nonfinite frames and flat-normal clearing with unchanged
source data. DotMesh round trips require/preserve the tangent attribute. Real PBR
pixel tests compare authored frames with UV-derived references under directional
lighting, reflection/nonuniform scaling and double-sided back faces; rotating the
authored frame changes lighting. Feature Lab contains the actual saved frame asset
used by its existing editor/Play/export native acceptance flows.


ModelTests checks GLB/internal and glTF/external buffers/images, base64/sparse data,
normalized quantized positions, frame/material/factor/color-space retention, reflected
hierarchy, opaque alpha canonicalization, malformed types/ranges/URIs/cycles/extensions,
unsupported content, publication collision/capacity/thread/symlink boundaries and
single-history insertion/undo/redo/stale/save. ModelGpuTests compares imported native
scene HDR pixels with independently built geometry/material/texture references,
checks picking identities and cache release. Baked-occlusion GPU coverage verifies
indirect attenuation with direct light/emission retained. PNG encode/decode retains
owned exact pixels and rejects malformed storage.

`DotEditorAcceptance --model-import-test <existing-parent>` imports the actual GLB
through the live workspace protocol, inserts its hierarchy/materials and an authored
light, checks changed/restored real viewport pixels, one undo/redo, save/reopen and
normal close. CTest `editor.model_import` is a native acceptance case compiled only
when BUILD_TESTING is enabled.


RenderQueueTests checks stable solid ordering, blended depth/tie ordering, original
indices, owned results, disabled validation and 10000-input boundaries. AlphaGpuTests
checks actual Opaque/Mask threshold and texture coverage, sorted straight HDR blending,
opaque depth occlusion, original pick IDs, zero-alpha rejection, and blend-excluded /
mask-clipped SSAO geometry. ShadowGpuTests checks texture holes/cutoff and blend's
no-shadow policy. Model/Material tests retain factor/vertex/texture alpha and mode /
cutoff through import/current persistence. Existing native Feature Lab editor/Play/
export coverage now uses real Glass and Cutout assets.


MaterialDocumentTests exercises real filesystem drafts, no-op revisions, stale
requests, branching bounded history, save/undo/redo, exact-byte disk conflict and
restoration, owner-thread/move behavior and destruction without save. These are
integration tests without a window/device. SceneMaterialTests independently checks
real HDR pixels for owned overrides, invalid descriptions, missing images, cache
reload/restoration, unchanged disk/scene and texture cleanup. Native WorkspaceTests
material cases drive the same GUI commands through staging/reset/apply/undo/redo,
save/reopen, Play mutation lock/Stop preview restoration, dirty close/save-close and
external disk conflicts. CTest names begin `material_document.`,
`scene_material.owned_preview` and `workspace.material_`.


GUI texture GPU acceptance checks exact pixels and ownership through submission,
error cleanup and foreign-device rejection. Independent material resolution checks
shared texture validation/cache lifetime. AssetPreviewTests checks real image pixels,
color-space/edge clamping, aspect, material-sphere draft changes, source preservation,
HDR/normal errors, framed geometry, thread/move and release boundaries. Native
AssetBrowserTests drives thumbnail requests, folder/search/list commands, independent
material form previews/errors, cache/queue limits and explicit failed-source status.
It compares main viewport pixels, camera and drawing counters before/after previews.
These native/GPU checks remain compiled only with BUILD_TESTING.


PreviewWorkerTests uses controlled provider gates without a GPU/window to establish
that submit/poll continue while work is blocked, same-slot/priority ordering, late
active discard, 64-request/8-result bounds, completion backpressure, initialization
failure/reset, pause, exceptions/output validation, owner/move and joined stop with
provider destruction on its worker thread. AssetPreviewTests additionally creates,
renders and destroys the real offscreen renderer on that thread and checks actual
pixels/missing studio errors. Native asset browser cases await actual completion
with deadlines, retain pixel/source/isolation assertions, and exercise rapid form
changes, refresh/close/reopen generations, joined close and post-close rejection.
Workspace Play tests assert pause/resume state. Provider-gated integration tests do
not substitute for those real GPU/native acceptance cases.


Document multiselection tests cover ordered/scoped handles, no-op/stale/foreign/
duplicate rejection, owner/move and history budgets, subtree union/remapping,
external parents, primary camera/listener demotion, UTF-8 names, opaque script
properties, exhausted IDs, rollback and single-entry undo/redo/branching. The
runnable document acceptance additionally selects/duplicates/undoes two entities.
Native Hierarchy tests use actual SDL mouse and modifier/key events for arrow
expansion, Command/Ctrl toggles, Shift ranges, Duplicate/Delete and undo selection
restoration. Agent cases cover reversed/additive ranges and stale requests.
Group transform acceptance below now covers actual multi-object gizmos.


Group transform foundation cases verify topmost roots, selected ancestor/active
child overlap, shared pivot/orbits, mirrored nonuniform parents, local frames,
absolute/reset samples, last-valid rollback on a later root's shear, scoped/owner/
stale checks and one typed commit for 301 roots. They are filesystem integration
tests without a device/window. Native/GPU cases retain all single-object checks
and add group move/rotate/scale pointer drags, actual draft/committed/undo/redo
pixels, unchanged descendant local transforms, cancellation/focus loss and saved
group scene data. Runnable viewport acceptance additionally moves two roots and
restores data/selection/pixels with one undo. No bulk-entity test is presented as
populated-game performance evidence.


Animation foundation acceptance separates immutable clip/unit sampling from actual
file IO and offscreen rendering. Curve cases cover endpoint/independent times,
interval-scaled cubic tangents, shortest-arc SLERP and normalized authored cubic
rotations, invalid/singular/overflow results and immutable concurrent ownership.
Scene playback cases cover forward/reverse looping, endpoints/zero elapsed/speed,
pause/seek/stop, unanimated fields, parent/child batching, stale/foreign/moved/owner
contracts and atomic rollback of poses plus clock after sample/world failure.
Native codec tests exercise strict schema, rooted regular paths, symlink rejection
and failed-save preservation. GPU samples match an independent native reference;
`DotRuntimeAcceptance --animation-test <existing-output-directory>` writes nine
real PNG frames/native clip, restores bind-pose pixels and releases resources.
This acceptance isolates the native TRS animation foundation; consumer coverage is described below.


Model animation acceptance imports GLB TRS channels, publishes/reloads the actual
native scene/clip/binding bundle, samples nine offscreen textured frames and verifies
rest pixels/resource release. CPU/file integration checks cover glTF and GLB remapping,
scene selection with warnings, shared sampler inputs, binary malformed/nonfinite/
unordered/singular data, unsupported targets, strict descriptor paths/IDs, moves,
source independence and failed-publication preservation. The native editor model
acceptance also checks animation import report fields.

`editor.scene_animation` imports an animated model through the real workspace,
remaps saved player bindings, edits Inspector properties, and verifies actual Play
pixels/native transforms. Stop restores authored pixels; save/reopen, independent
duplication, undo and rejected bound-target deletion retain scene contracts. A
relocated native exported game removes its authoring tree and observes native
animation movement from its script without writing the animated transform. Missing
clips, overlapping writers, physics authority, clock retention/reverse/pause and
session faults have focused CPU/file integration coverage. Timeline, skinning and
TypeScript playback controls remain unfinished.


Project-home acceptance now separates Open/Create pages and retains staged form
values across real SDL pointer navigation. Pure CPU callback-mailbox cases verify
borrowed path/error copying, cancellation, one-shot replies, and worker completion
after caller retirement without borrowing GUI state. Manual macOS Release-app
verification additionally clicked Browse, cancelled the real system picker, selected
a creation parent, created a project, then selected that existing project through
the picker and observed its workspace title. Parent-picker invocation used the
shared browse-folder command; project Browse used an actual mouse click. Final
Create-page columns were visually inspected. Windows/Linux native pickers and
full window destruction while a picker is pending are not locally validated.

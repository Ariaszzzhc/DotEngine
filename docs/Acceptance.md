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

# Goal boundaries and implementation

The user has resumed engine development after project cleanup. Completion is not
defined around features already built or the number of tests. The original
requirements remain:

- Capability to make large 3D games, including usable rendering, content production,
  resource/scene management and evidence from populated real projects.
- A mature editor workflow guided by the supplied visual references and actual
  engine behavior. Every operation must be fully accessible to AI agents through
  real product interfaces, without human intervention being required.
- External source editing rather than an embedded code editor.
- Engine-owned modules under the dot: namespace and extensionless relative project
  imports. The engine is unpublished: replace current structures cleanly rather
  than adding format versions or compatibility implementations.
- Feature Lab as an editable capability showcase and Prism Relay as a realistic
  science-fiction, non-shooting over-the-shoulder movement/jump template. Both
  projects belong in examples/.

| Area | Current implementation | Evidence still needed or unfinished work |
| --- | --- | --- |
| Rendering | PBR material/texture slots, HDRI IBL, mixed directional/point/spot soft shadows with a bounded atlas, local-face culling and retained unchanged depth pages, SSAO, HDR/tone mapping and opt-in renderer CPU/GPU sampling | Temporal quality, pooled submission, resource streaming and whole-application populated-scene frame costs |
| Content/animation | Native resources, OBJ/glTF/GLB import, GPU skinning, animation cross-fades, isolated audition, live skeleton inspection, pose-seeded native clip creation, staged clip libraries/node bindings, key editing/save/undo, shared multitrack dope sheet/range selection/density views, rational frame grids/stepping/anchor and per-key snapping, and atomic bulk deletion, sampled curves and cross-track group key selection/retiming/relative TRS value adjustment and owned copy/paste across a player's clips shared with agents | Native pointer/rectangle acceptance, graphical tangent handles, skeleton and retargeting authoring, layered/state-machine workflows and scalability |
| Physics/gameplay | Rigid bodies with linear CCD, queries/contacts, saved Y-up character components, scene movement/jumping/stairs/slopes/platforms, native character query/contact proxies, unified virtual ground/wall/peer events and script/Inspector/agent controls | Populated-game character scalability |
| Game state | Shared JSON state/revision checks, atomic slots, stable app IDs/user directories, built-in script services, isolated Play/editor-agent controls and Prism Relay's explicit grounded position/facing/orbit checkpoints; see [storage contracts](GameState.md) | Background I/O, crash recovery/cloud persistence, menus and broader game-specific restoration |
| Editor | Docked views, single-active scene-file create/open/save-as with shared agent forms, scene/resource/material authoring, picking/multiselection/gizmos, world-preserving hierarchy grouping/ungrouping/reparenting, reusable scene-content extraction/insertion, recursive linked source refresh with graph/cycle budgets, paged override inspection/selective or full baseline revert/unlink, inline component override counts, whole-field baseline restore and conflict-checked field application to saved sources, persisted entity allocation across reopen and layered source identities independent of working IDs, undo/redo, isolated Play, background scene graphics/skin preflight and initial animation decoding with old-game continuity, bounded asynchronous old-audio teardown, and export | Consistent production workflows, variants/whole-instance and topology apply-to-source, source retargeting, multi-scene runtime composition/streaming, remaining owner-thread startup/service preparation and non-audio retirement/resource I/O and performance/debugging tools |
| AI control | Product CLI, strict JSON operations and shared bounded local mailbox; document/resource/Play operations and opt-in standalone inspection/input/game-data/scene/shutdown control, native focus requests and read-only native/GUI pointer diagnostics | Audit complete human/agent operation parity as workflows evolve; macOS virtual mouse evidence is limited by SDL global-state handling; no test-only substitutes |
| Game UI | Scene-local native text/buttons with focus, pointer/keyboard activation and bounded action history, pinned fonts, bounded layout/cache, shared script/product controls and UI-revision-aware capture; native simulation pause and unscaled frame callbacks support Prism Relay’s clickable/keyboard checkpoint menu; see [text contracts](GameUi.md) | Visual layout authoring, full shaping/bidi and asynchronous preparation |
| Example experience | Feature Lab and Prism Relay's textured maintenance character, native traversal, sphere-swept shoulder camera and imported industrial PBR equipment/ventilation | Finished realistic sci-fi character/environment art, broader workflow and performance evidence |
| Production evidence | Focused headless behavior/GPU tests and past macOS product checks | Real project workflow, stress/performance and shipping evidence beyond test counts |

Native application CPU diagnostics now expose actual event/prepare/fixed/update/render
step envelopes through the editor and player `--profile` output. Retained slowest
steps, longest supplied intervals and independent phase peaks preserve startup/hitch
evidence after history eviction without conflating costs from different frames. These measurements exclude process initialization/teardown and idle waits;
they do not complete the remaining performance, streaming or background-work goals.
See [profiling scope](Profiling.md).

Prioritize complete usable
editor/game workflows and clear engine contracts. Do not accumulate one-off visual
acceptance executables, procedural example-generation scripts or staged test counts
as substitutes for those workflows. Unit tests never initialize windows/devices;
automatic integration does not open desktop windows. Durable offscreen GPU checks
remain separate. Use the real editor's agent interface for integrated checks, with
computer use for human-style interaction and unexpected UI problems.

Implementation targets remain arm64 macOS and x86_64 Windows/Linux. Only macOS is
locally validated; Windows/Linux execution is outside the requested local scope.

Initial visible skin poses now join incremental loading with owned GPU transactions,
zero-wait polling, cancellation and atomic validation/publication. Dynamic editor
Play/native frames now overlap skin completion with simulation using bounded owned
snapshots and actual completed-frame provenance. Explicit drawing/readback, owner
resource uploads and some driver operations remain synchronous; resource streaming
and broader frame-latency work remain open. See [skinning contracts](Skinning.md).

Visible mesh/skin CPU reads, decode and surface analysis now run in a bounded owned
geometry worker. Incremental scene readiness includes all visible GPU sources, with
at most one mesh/controller residency publication per poll. Independent material
drafts preserve progress, and cancellation/clear/source changes prevent late data.
GPU uploads/binding, other asset services and broader streaming/pacing remain open.
See [prepared geometry](Rendering.md#prepared-geometry-residency).

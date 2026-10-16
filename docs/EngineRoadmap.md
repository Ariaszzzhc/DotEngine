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
| Rendering | PBR material/texture slots, HDRI IBL, mixed directional/point/spot soft shadows with a bounded atlas and local-face culling, SSAO and HDR/tone mapping | Temporal quality, cached/pooled shadow work, resource streaming and populated-scene performance |
| Content/animation | Native resources, OBJ/glTF/GLB import, GPU skinning, animation cross-fades, isolated audition, live skeleton inspection, existing-clip key editing/save/undo, sampled curves and cross-track group key selection/retiming and owned copy/paste across a player's clips shared with agents | Native curve-drag acceptance, dope-sheet/multi-key value/tangent-handle and skeleton/binding authoring, layered/state-machine workflows and scalability |
| Physics/gameplay | Rigid bodies with linear CCD, queries/contacts, saved Y-up character components, scene movement/jumping/stairs/slopes/platforms, native character query/contact proxies and script/Inspector/agent controls | Virtual-only character contact events and populated-game character scalability |
| Game state | Shared JSON state/revision checks, atomic slots, stable app IDs/user directories, built-in script services and isolated Play/editor-agent controls; see [storage contracts](GameState.md) | Background I/O, crash recovery/cloud persistence and complete game-specific checkpoint workflows |
| Editor | Docked views, single-active scene-file create/open/save-as with shared agent forms, scene/resource/material authoring, picking/multiselection/gizmos, undo/redo, isolated Play and export | Consistent production workflows, multi-scene runtime composition/streaming and complex scene organization, background compilation and performance/debugging tools |
| AI control | Product CLI, strict JSON operations and live mailbox; shared document/resource/Play operations, bounded gameplay input, native focus requests and read-only native/GUI pointer diagnostics | Audit complete human/agent operation parity as workflows evolve; macOS virtual mouse evidence is limited by SDL global-state handling; no test-only substitutes |
| Example experience | Feature Lab and Prism Relay's rigged character, native traversal and sphere-swept shoulder camera | Finished realistic sci-fi character/environment art, broader workflow and performance evidence |
| Production evidence | Focused headless behavior/GPU tests and past macOS product checks | Real project workflow, stress/performance and shipping evidence beyond test counts |

Prioritize complete usable
editor/game workflows and clear engine contracts. Do not accumulate one-off visual
acceptance executables, procedural example-generation scripts or staged test counts
as substitutes for those workflows. Unit tests never initialize windows/devices;
automatic integration does not open desktop windows. Durable offscreen GPU checks
remain separate. Use the real editor's agent interface for integrated checks, with
computer use for human-style interaction and unexpected UI problems.

Implementation targets remain arm64 macOS and x86_64 Windows/Linux. Only macOS is
locally validated; Windows/Linux execution is outside the requested local scope.

# Current implementation and evidence

DotEngine has real rendering and authoring foundations. Large-game production
readiness, a mature editor and the requested finished Prism Relay experience remain
open goals. Tests establish specific contracts, not a completion percentage.

| Implemented foundation | Current boundary |
| --- | --- |
| Static C++26 library, separate module interfaces/implementations, pinned dependencies | Preserve caller compiler/linker environment; no installed public header tree |
| PBR textures/materials, HDRI IBL, directional/point/spot soft shadows, retained unchanged shadow pages, SSAO and HDR/tone mapping | Temporal quality, submission reuse, resource streaming and populated-scene performance remain unfinished |
| Opt-in renderer CPU/GPU intervals, bounded histories and explicit unsupported/drop/error states | Excludes scripts, physics, separate skin work, GUI and other application costs; not game FPS |
| Independent native application-step CPU wall sampling in editor and player, bounded history, retained slowest/input-interval records and failed callback phases | Includes work inside step; excludes process initialization, teardown, idle delays and background worker CPU; populated-game costs remain to be established |
| Asynchronous shared environment/shadow/SSAO/skin pipelines, bounded HDR/material image/mip preparation and zero-wait environment GPU completion with editor status and native startup gating | Material metadata, GPU uploads, other assets/pipelines and skin status synchronization remain synchronous; first-frame stalls are not resolved |
| OBJ/glTF/GLB import, binary native meshes, GPU skinning/cross-fades, isolated audition, skeleton inspection and existing-clip key editing/retiming/copy-paste | Full curve/binding/animation production tools and scale remain unfinished |
| Jolt rigid bodies/linear CCD, character traversal, ray/sphere queries and virtual/proxy contacts, native audio | Populated-game character scalability and long-running behavior need broader evidence |
| TypeScript/QuickJS-NG, hot reload, dot:engine/dot:host and extensionless project imports | Compilation is synchronous; new native capabilities need explicit SDK exposure |
| Shared game state, atomic save slots, stable application IDs and isolated editor Play storage | Background I/O, crash recovery and complete game-specific checkpoint workflows remain unfinished |
| Docked editor, scene/resource/material authoring, picking/gizmos/multiselection/history, isolated Play and export | Consistent content production, background work and application performance/debugging tools remain unfinished |
| Frozen viewport PNG capture with owned background encoding, explicit saving/completion/cancellation and unchanged-destination commit guards | GPU readback, destination checks/atomic write and catalog refresh still run on the owner; not general background filesystem I/O |
| Recursive linked sources with persistent layered identities, three-way refresh, paged override inspection/revert, inline field counts and whole-field restore | Variants, apply-to-source, retargeting and runtime scene composition/streaming remain unfinished |
| Strict CLI/JSON/live-mailbox operations sharing product implementations; external script editing | Every future user operation needs equivalent agent control; native simulated pointer acceptance has specific unresolved limits |
| Editable Feature Lab and Prism Relay under examples/ | Textured maintenance armor, shoulder camera and industrial linked assemblies exist; final realistic sci-fi character/environment art and game-scale evidence remain unfinished |

## Verification scope

Unit tests initialize no windows or graphics/audio devices. Automatic integration
opens no desktop windows. Durable offscreen GPU regressions remain separate from
unit tests. Native editor/game workflows use actual products and their strict
agent operations, supplemented by computer use for visual interaction and failure
investigation. Temporary helpers remain in ignored build storage.

The latest full results and concrete product evidence are recorded at the end of
[Development.md](Development.md). Older milestones retain historical results;
this summary does not treat their test counts or unfinished-feature lists as the
current state. See [testing](Testing.md), [editor operations](Editor.md),
[scene reuse](SceneContent.md) and [goal boundaries](EngineRoadmap.md).

Prism Relay's twelve straight ducts and fourteen supports use shared scene sources.
Field indicators report complete applied differences independently of pagination;
restoring a component uses the stored remapped baseline with ordinary atomic
history and source-independent failure handling. Play and exports use saved
materializations. This does not imply automatic runtime source refresh or finished
prefab/variant tooling.

Local validation covers macOS arm64 only. Windows/Linux have implementation
support without local execution evidence. Portable packaging remains an explicit
capability; ordinary stages do not produce new distribution archives. Release
identity signing/notarization and installer publication remain unfinished.

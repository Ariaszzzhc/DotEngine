# Skinning

The native skin foundation is separate from node-animation playback.
`Dot.Animation.Skin` owns immutable inverse-bind matrices and eight joint/weight
slots per vertex. Joint order follows binding order, not scene traversal. Limits
are 1024 joints and one million vertices. All indices (including unused slots)
must be in range; weights are finite [0,1], positive in total, and a joint may
appear only once with positive weight. Creation normalizes weights once. Copies
share immutable storage; empty/moved handles return errors.

These conventions follow [Khronos skin attributes](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#skinned-mesh-attributes).
The model importer converts glTF/GLB skins into these same native assets and bindings.

A mesh-local palette is `inverse(meshWorld) * jointWorld * inverseBind`, in column
major order. Rendering with meshWorld cancels the mesh node's transform, leaving
the posed joint transforms authoritative. Joint-world inputs include their complete
ancestor hierarchy. Results are owned and leave source data untouched. Input and
bind matrices must be finite, invertible affine; evaluated palette entries have
absolute magnitude at most 1e8.

`Dot.Graphics.Skinning::deformMesh` is an owned CPU reference, without a device or
file operation. It computes the weighted matrix per vertex, applies its inverse
transpose to normals, re-orthogonalizes tangents and flips handedness on reflection.
A normalized linear determinant magnitude <=1e-6 fails, as do overflow, invalid
inputs and a triangle crossing a tangent-handedness seam. A primitive must retain
one blended-matrix orientation; disconnected reflected islands require separate
primitives. Uniform reflection preserves the authored front-face convention across
PBR, shadows, AO and picking. Tangent handedness combines skin reflection once with
the separate world-transform sign. Color/UV/indices remain
unchanged. It does not mutate the original mesh or cache a partly evaluated pose.

## Saved assets and scene bindings

`Dot.Animation.SkinFiles` reads/writes one strict little-endian `.dotskin` layout.
The 16-byte header holds `DOTSKIN` plus NUL and uint32 joint/vertex counts. Column
major float32 inverse binds follow, then eight uint32 indices and eight float32
weights per vertex. Exact length is `16 + 64 * joints + 64 * vertices`, bounded by
64 MiB. Counts and length are checked before allocation, all payloads use the same
native skin validation, and no structure padding or native endianness is assumed.
Portable paths reject links/special files. Save encodes completely before atomic
replacement; parents must exist. Loaded immutable data outlives its input file.

The optional scene `skin` component holds `asset`, ordered canonical decimal-string
`joints` and `enabled`. Paths start with `assets/` and end with `.dotskin`; joints
are 1..1024 unique, positive local entity IDs. At most 128 owners are allowed. A
mesh is required; consumers verify actual files and exact geometry/joint counts.
Disabling deformation does not permit dangling references. Delete/rebind external
owners before deleting joints; remove skin before its mesh. Whole-subtree removal
is allowed when every affected owner is also removed. Serialization resolves refs
after constructing all entities, so source order does not affect correctness.

`Dot.Animation.SceneSkin` owns immutable bind data and copied component values.
It evaluates current joint-world transforms in binding order, including complete
ancestor hierarchies and mesh-world cancellation. It borrows no scene address;
scene moves preserve identity, while foreign/deleted/changed bindings fail. It is
an owner-thread CPU service; no rendering or scene mutation is hidden in sampling.

## GPU deformation

Renderer::createSkin shares a same-device static mesh and an immutable skin. It
checks vertex counts and residency before allocating owned weights, palette, two
posed vertex buffers, parameters and status. RendererConfig permits 1..1024 skin
controllers and 1 byte..512 MiB of extra skin storage; source/current posed vertices
remain in ordinary mesh counters. A new controller exposes bind geometry with zero
updates. Dynamic/foreign sources are rejected; no recursive skin chain is created.

A SkinnedMesh copy shares one controller. mesh() returns the posed Mesh view; all
color, depth/shadow, SSAO-guide and picking paths use its same current vertex buffer.
A retained view freezes the last successful geometry when the controller retires,
and retains the static input/index buffers. Queries and updates require the creating
thread and live device; quiescent destruction can outlive the renderer.

Renderer::updateSkins accepts up to 128 different controllers/four million vertices.
It validates the complete CPU request before queue writes, computes packed vertices
and tangent frames on GPU, checks triangle handedness, and reduces signed float
bounds through monotonic integer atomic keys. A compact status batch is mapped once;
only after every result succeeds are all buffers, bounds and counters published.
The explicit synchronous path uses the same transaction described below.
Invalid palettes, blended singularity, float overflow or handedness seams retain
all previous views. Source geometry is always used, so poses do not accumulate.

The WGSL source lives separately in Skinning.wgsl and is embedded privately by the
normal shader target. It rescales positions before arithmetic to detect overflow
without depending on nonfinite propagation. The blended inverse-transpose direction
uses cofactors and determinant sign; tangents are orthogonalized and reflected W
is retained. Deformation can remove/recover a normal-map frame; eligibility combines
static normals/UVs with current GPU triangle geometry.

`beginSkinUpdate` submits a move-only SkinBatch with prepared pipelines; it owns
controller handles and driver-copied palette data before return, borrowing no caller
spans. An empty batch is complete. A controller allows one unpublished job at a time;
overlapping async or synchronous updates reject. Queries and readSkin retain the
last published pose. `pollSkinUpdate` checks completion with zero GPU status wait,
validates the complete report and atomically publishes all views/bounds/counters.
No callback publishes scene state. Completed polls are idempotent; errors persist.
`finishSkinUpdate` explicitly waits within the same ten-second deadline;
`updateSkins` prepares pipelines as necessary, then uses begin/finish on that same
transaction. Async begin requires already prepared pipelines and never hides a
pipeline compilation wait. Submission still includes owner-thread allocation,
queue writes and ordinary GPU validation-scope processing.

Destroying/move-replacing an unpublished job cancels publication and unlocks its
controllers without waiting for GPU completion. Queued resource references and
callback-owned completion data retain their own lifetimes; cancelled work cannot
later mutate views. Jobs can be destroyed after renderer shutdown once quiescent.
Operations require the originating live renderer/owner thread and reject GUI
reentry. Failure/abandonment unlocks retry without changing previous geometry.
Owned logical readback storage is 32 bytes per pending controller beyond resident
skin bytes; controller exclusivity bounds it to maxSkins * 32 (<=32 KiB). This
excludes backend-retained in-flight allocations after handle destruction, as with
other logical GPU resource counters. Cancellation destroys the map and reaps its
already-signalled WaitAnyOnly callback with zero timeout, avoiding accumulation of
unobserved completion records until instance teardown.
Caller-retained completed job objects own their small completion/error records.

readSkin remains explicit synchronous owned GPU vertex readback for diagnostics.
Explicit exact-frame drawing uses updateSkins; editor Play and the native player
use the bounded frame pipeline described below. No populated-game frame-time
guarantee follows from a zero-wait completion poll.

## Scene, editor and export consumption

SceneRenderer's incremental `prepare` now adds an `initial-skin-poses` stage after
pipelines/materials. New visible/rebound controllers load their ordinary assets,
submit one owned GPU transaction and poll without a completion wait; readiness
queries remain pure. Native initial startup stays in its loading loop until this
stage completes; the editor shares that preparation implementation. Hidden/deleted/
rebound owners, scene replacement, clearCache and destruction cancel unpublished
work. Missing assets or invalid GPU deformation report errors while target pixels
remain untouched. Loading source geometry/controller buffers still uses existing
synchronous resource operations.

A submitted first pose is retained while joints keep advancing, avoiding perpetual
cancel/resubmit starvation. The cache records the actual submitted palette; every
subsequent frame compares its captured palette and validates any necessary update
before publishing that frame. Explicit drawing/bounds queries supersede owned
unpublished preparation and use their synchronous transaction. No stale cached pose
is presented as current. Ready concerns prepared resources, not completed rendering.
Identical failed initial requests retain their errors; a corrected palette permits
a new transaction. Later native resource changes use the same incremental preparation,
with ordinary geometry uploads and resource operations still on the owner thread.

SceneRenderer loads enabled saved skin components automatically. Immutable file
data and static source meshes are shared; each mesh owner retains one GPU skin
controller between frames. A visible frame gathers ordered joint palettes
and validates them as one atomic batch before drawing. The same posed view supplies
color, physical materials, shadows, SSAO and picking. bounds() evaluates a current
pose even before the first frame. Both posed buffers, influences, palettes and
control bytes count toward the scene cache budget; static inputs/indices count once.
stats() exposes skin owner count and its logical GPU bytes. Retiring an owner,
changing scene scope/mesh/binding, hiding or disabling a skin releases its controller
before pruning the shared input. clearCache explicitly reloads source assets.
As with other scene caches, failed preparation can prune caches; it never renders
fake bind geometry as a successful enabled pose. Failed updates preserve existing
GPU views and persistent render-target pixels. Physics colliders retain their
authored shapes; mesh deformation does not silently change collision geometry.

GameSession validates real skin/mesh assets, exact counts and the initial CPU
deformation before constructing scripts or native game services. All enabled skins
(including hidden owners) are checked, with a four-million influenced-vertex limit;
later runtime poses use renderer validation. GamePackage publication uses that same
session preparation and copies skins through the ordinary owned asset snapshot.
Relocated package tests delete the authoring project before loading the copied
skin and sampling its scene joints.

The Inspector offers an asset picker, enabled flag and ordered live-entity joint
pickers. Adding a binding requires a mesh. Draft application uses the same component
edit/history transaction as agent commands; missing assets/count mismatches produce
explicit rendering/Play/export errors. Duplicate requires every referenced joint
inside the copied subtree union; both duplicate and model-scene instantiation remap
joint IDs. Undo/redo/save preserve them. Offscreen GPU regressions compare saved
scene deformation with independent CPU mesh references. Visual skeleton/timeline/
blend tools remain unfinished.

## glTF/GLB import

The importer retains four/eight weights, byte/short joints, float or normalized
byte/short weights, sparse/strided accessors and optional float inverse binds.
Absent inverse binds use identity. Ordered joint IDs are remapped after every node
and primitive entity exists; the mesh-node transform cancels through the palette.
Referenced joints and their common hierarchy root must belong to the selected scene.
Non-unit float weights produce a warning and normalize; quantized sets require an
exact combined integer sum. Out-of-range indices fail even at zero weight. More
than eight influences and malformed pairings fail explicitly.

Shared primitive/skin pairs reuse immutable native files; a mesh instantiated with
different source skins retains distinct bindings. Flat-normal vertex expansion
copies each triangle corner's original influences. Import permits 128 skin owners,
four million instance-weighted vertices and 256 MiB encoded skin data. Prepared
models validate bindings and initial CPU deformation before writing owned staging.
Publication remains exclusive. Reload verifies bundle-local referenced assets and
initial deformation without a GPU. Resource reports include `skins` and `skinnedMeshes`.

Diagnostic ribbon images do not establish realistic character production or
populated-game throughput. Explicit GPU status/readback waits remain synchronous.
Validate integrated import/edit/Play/export through the real editor's agent interface and ordinary
example projects; use computer use for human-style interaction when needed.

## Bounded dynamic frames

SceneRenderer::renderIntoAsync/presentAsync own one pending and one completed frame.
A frame captures transforms, camera, physical material handles, lighting, environment,
SSAO, post process and screen textures together with its skin palette. New skin jobs
are first polled on a later host frame with zero timeout; simulation/input continues
while the last valid target remains visible. A successful submission is not a draw.
Receipts identify the actual completed tag, scene, size and view-projection matrix.

Renderer::freezeSkin returns an immutable view of a validated output, including
exact bounds and pose orientation. A shared lease pins that controller output;
repeated freezes of the same pose retain mesh identity for shadow dependencies.
The existing two output buffers remain bounded: an update whose spare output is
still leased fails with Unavailable. Releasing the last view unlocks reuse; it also
retains its controller/resources until that release. No per-frame GPU vertex copy
or blocking vertex readback is added. pickAsync requires the completed receipt
serial and uses that frame's meshes/camera, never a newer live pose.

Changing scene/resource layout, caller epoch, render target identity or dimensions
cancels obsolete work. Cache clearing, material preview and explicit synchronous
drawing/bounds queries also release the pipeline. Failed deformation preserves prior
target pixels and publishes no invalid pose; correcting the input permits retry.
If a prior frame really draws before capture of the next fails, its receipt and
the later error are returned together. This is bounded overlap, with possible
visual latency, not resource streaming or a guarantee that all driver calls,
allocation, uploads, text preparation or surface acquisition are nonblocking.

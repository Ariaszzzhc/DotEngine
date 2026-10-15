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

```sh
ctest --preset debug -R '^(skin\.|runtime.skinning$)'
build/debug/bin/DotRuntimeAcceptance --skinning-test build/debug/tests/animation_acceptance
```

The development-only acceptance reloads a saved skin/scene, uses actual scene joints
and node animation to drive GPU skinning, writes bind/reference/GPU DotMesh data plus nine PNG frames
and compares positions/normals and actual physical/shadow/AO pixels. Unit data/
reference cases still create no device. Animation blending,
retargeting and populated-game scale remain unfinished.

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
Invalid palettes, blended singularity, float overflow or handedness seams retain
all previous views. Source geometry is always used, so poses do not accumulate.

The WGSL source lives separately in Skinning.wgsl and is embedded privately by the
normal shader target. It rescales positions before arithmetic to detect overflow
without depending on nonfinite propagation. The blended inverse-transpose direction
uses cofactors and determinant sign; tangents are orthogonalized and reflected W
is retained. Deformation can remove/recover a normal-map frame; eligibility combines
static normals/UVs with current GPU triangle geometry.

readSkin is explicit owned GPU vertex readback for diagnostics. Normal updates
read only a compact status/bounds report, but that wait is synchronous. These APIs
are not an asynchronous skin pipeline or populated-game frame-time guarantee.
## Scene, editor and export consumption

SceneRenderer loads enabled saved skin components automatically. Immutable file
data and static source meshes are shared; each mesh owner retains one GPU skin
controller between frames. A visible frame gathers current ordered joint palettes
and updates them as one atomic batch before drawing. The same posed view supplies
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
joint IDs. Undo/redo/save preserve them. Native workspace tests compare actual
authoring/undo/isolated-Play pixels and prove that stop restores the authoring pose.

The runnable also compares automatic saved-scene PBR/shadow/AO output to its
independent CPU mesh and writes skin-scene.png. This is a diagnostic deformation
scene, not realistic game artwork. Visual skeleton/timeline/blend tools remain unfinished.

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

```sh
ctest --preset debug -R '^(model\.skin_|runtime.model_skin$|editor.model_skin$)'
build/debug/bin/DotRuntimeAcceptance --model-skin-test \
  build/debug/bin/resources/typescript-native/tsc build/debug/tests/animation_acceptance
```

The runtime acceptance imports the owned eight-joint GLB, saves/reloads native
assets, advances actual GameSession animation and compares nine PBR/shadow/AO
frames with independent CPU-deformed geometry. It checks picking/bounds, resource
release and export pixels after both authoring and relocated input trees are deleted.
The editor acceptance uses actual import/Inspector/duplicate/undo/Play/save/export
commands and runs the relocated native game after removing its source project.
Diagnostic ribbon images do not establish realistic character production or
populated-game throughput. GPU status waits remain synchronous.

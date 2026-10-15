# Skinning

The native skin foundation is separate from node-animation playback.
`Dot.Animation.Skin` owns immutable inverse-bind matrices and eight joint/weight
slots per vertex. Joint order follows binding order, not scene traversal. Limits
are 1024 joints and one million vertices. All indices (including unused slots)
must be in range; weights are finite [0,1], positive in total, and a joint may
appear only once with positive weight. Creation normalizes weights once. Copies
share immutable storage; empty/moved handles return errors.

These conventions follow [Khronos skin attributes](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#skinned-mesh-attributes).
This module does not itself add glTF skin import.

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
inputs and a triangle crossing a tangent-handedness seam. Color/UV/indices remain
unchanged. It does not mutate the original mesh or cache a partly evaluated pose.

```sh
ctest --preset debug -R '^(skin\.|runtime.skinning$)'
build/debug/bin/DotRuntimeAcceptance --skinning-test build/debug/tests/animation_acceptance
```

The development-only acceptance reloads a saved skin/scene, uses actual scene joints
and node animation to drive GPU skinning, writes bind/reference/GPU DotMesh data plus nine PNG frames
and compares positions/normals and actual physical/shadow/AO pixels. Unit data/
reference cases still create no device. glTF skin import, automatic scene rendering,
editor skin authoring and populated-game scale remain unfinished.

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
Automatic scene-renderer consumption and glTF import must still connect these
native resources before editor projects can use the controllers automatically.

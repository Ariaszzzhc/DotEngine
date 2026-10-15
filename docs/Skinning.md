# Skinning

The native skin foundation is separate from node-animation playback.
`Dot.Animation.Skin` owns immutable inverse-bind matrices and eight joint/weight
slots per vertex. Joint order follows binding order, not scene traversal. Limits
are 1024 joints and one million vertices. All indices (including unused slots)
must be in range; weights are finite [0,1], positive in total, and a joint may
appear only once with positive weight. Creation normalizes weights once. Copies
share immutable storage; empty/moved handles return errors.

These conventions follow [Khronos skin attributes](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#skinned-mesh-attributes).
This module does not itself add glTF skin import or a persisted scene component.

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

The development-only acceptance writes bind and independently deformed DotMesh
artifacts. Unit cases cover eight influences, quantization normalization, bind
identity, mesh-local order, nonuniform/reflected normal and tangent frames, invalid
weights/indices/affine matrices, exact capacities, source preservation and shared
lifetime. GPU skinning, scene binding/persistence, glTF skin import, editor workflows
and populated-game scale remain unfinished at this foundation stage.

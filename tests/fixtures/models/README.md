# Model import fixtures

Owned deterministic diagnostic geometry, not production art. `surface.glb` embeds
one strided indexed quad, authored normals/tangents/colors, three PNG images and an
opaque five-slot PBR material in a reflected two-node hierarchy. `surface.gltf` and
`surface.bin` use the same geometry with external images. Base/vertex alpha varies
so import must retain authored alpha and rendering must apply coverage explicitly.

`sparse.gltf` exercises base64 buffer bytes and sparse positions with absent normals.
`quantized.gltf` declares required mesh quantization and normalized uint16 positions.
All fixtures are created for this repository. Tests compare converted values and
actual rendered pixels against independently authored native geometry/materials;
malformed variants must fail without publishing a partial asset bundle.


`animated.gltf` and self-contained `animated.glb` reuse the diagnostic PBR surface,
with source node order differing from hierarchy traversal. A selected parent has
cubic translation and its reflected child has linear rotation/STEP scale; another
scene owns an unnamed clip. Tests check filtering/warnings, source-index names,
actual tangent timing, authored scale signs, strict malformed variants, saved node
binding IDs and source-independent native replay. These are deterministic repository
fixtures, not realistic game assets.

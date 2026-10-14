# Model import fixtures

Owned deterministic diagnostic geometry, not production art. `surface.glb` embeds
one strided indexed quad, authored normals/tangents/colors, three PNG images and an
opaque five-slot PBR material in a reflected two-node hierarchy. `surface.gltf` and
`surface.bin` use the same geometry with external images. Base/vertex alpha varies
so import must implement opaque alpha semantics without changing RGB.

`sparse.gltf` exercises base64 buffer bytes and sparse positions with absent normals.
`quantized.gltf` declares required mesh quantization and normalized uint16 positions.
All fixtures are created for this repository. Tests compare converted values and
actual rendered pixels against independently authored native geometry/materials;
malformed variants must fail without publishing a partial asset bundle.

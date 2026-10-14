# Mesh assets

`Dot.Assets.Mesh` decodes CPU geometry; upload it through the renderer to obtain
shared GPU mesh resources. `loadMesh` reads relative to a `FileSystem` project
root, and `saveMesh` atomically replaces a `.dotmesh` file in an existing parent.
File extensions are case-insensitive. Source/output size is limited to 64 MiB.

DotMesh requires exactly `vertices` and `indices`. Every vertex requires
`position` (three finite float-range numbers), `color` (four linear numbers in
[0,1]), `normal` (a unit three-vector or zero to explicitly denote absence),
`texCoord` (two finite floats, including negative/outside-unit coordinates for
tiling), and `tangent` (an orthogonal unit XYZ and handedness W=+1/-1, or
all zero for absence). Tangents must be complete or absent for the whole mesh;
triangle corners must share handedness, with seams split into separate vertices.
There is one current structure, with no old-format loader or version
dispatch. Indices are unsigned 32-bit integers referring to vertices. An empty
index list selects non-indexed triangle geometry. Mesh validation limits counts
to one million vertices and three million indices.
Duplicate fields, unknown fields, and nesting deeper than
64 are errors.

```json
{
  "vertices": [
    {"position": [0, 0, 0], "color": [1, 0, 0, 1], "normal": [0, 0, 1], "texCoord": [0, 0], "tangent": [1, 0, 0, 1]},
    {"position": [1, 0, 0], "color": [0, 1, 0, 1], "normal": [0, 0, 1], "texCoord": [1, 0], "tangent": [1, 0, 0, 1]},
    {"position": [0, 1, 0], "color": [0, 0, 1, 1], "normal": [0, 0, 1], "texCoord": [0, 1], "tangent": [1, 0, 0, 1]}
  ],
  "indices": [0, 1, 2]
}
```

OBJ import accepts finite positions, RGB vertex colors, triangles/polygons of
3..64 vertices, positive/negative attribute references, and object/group/smoothing
metadata. Polygons are triangulated, all shapes merge into one colored mesh, and
missing vertex colors default to white. Non-unit homogeneous position weights,
materials, line/point primitives, and free-form geometry return errors. Referenced
material files are never opened. Authored normals are normalized and retained;
zero normals fail. Two-dimensional UVs are retained, and face-corner tuples split
hard-normal/UV seams. Meaningful third UV coordinates are rejected explicitly.
Object/group/smoothing metadata loss is reported in `warnings`; supplied normals
carry their authored smooth/hard distinctions. OBJ lines
are limited to 1 MiB; malformed numbers cannot fall back to zero/default values.

Run `DotRuntimeAcceptance --asset-test <existing-directory>` to save and reload a cube at
`assets/cube.dotmesh`. This acceptance case reports file/codec errors and returns
a non-zero exit status when its checks fail.

`Dot.Graphics.Geometry::withFlatNormals` explicitly derives winding-based flat
normals into a new non-indexed mesh without modifying the source. It retains colors
and UVs, uses double intermediates for extreme finite geometry and rejects
degenerate faces or expansion beyond one million vertices. Primitive/unattributed
vertices otherwise keep missing normals; rendering does not pretend lighting exists.

`DotRuntimeAcceptance --surface-mesh-test <existing-directory>` exercises surface
persistence and an owned normal-color visualization through actual GPU upload and
pixel readback. Material/texture sampling and light/shadow rendering are subsequent
capabilities in the [engine roadmap](EngineRoadmap.md).

## Images and surface textures

PNG/JPEG and Radiance RGBE import, explicit color spaces, immutable GPU texture
ownership, mips, samplers and material base-color sampling are documented in
[rendering development](Rendering.md). Physical PBR/HDRI/soft-shadow/SSAO/HDR
rendering remains planned; retaining HDR pixels does not yet provide IBL.


## Static glTF/GLB models

`Dot.Assets.Model` imports triangle lists/strips/fans, indexed/nonindexed geometry,
strided and sparse accessors, normalized attributes and required mesh quantization.
Normals/tangents/UV0/colors are retained; absent normals are generated flat. Selected
nodes retain names and decomposable local TRS, including reflection. Shared mesh
references reuse generated primitive assets. Default scene (otherwise first) is
selected unless the caller supplies a scene index. Mesh/material/image names, extras/unused UV/color channels
are reported as unpersisted metadata.

Metallic/roughness materials preserve base, packed MR, normal, emission and
baked occlusion textures, their factors, sidedness and supported sampler settings.
Color slots are sRGB, data slots linear. PNG/JPEG are losslessly canonicalized to
RGBA8 PNG after decoding; RGBA is retained and coverage is explicit at rendering
without implicit gamma conversion. Emissive strength and unlit materials with
compatible native slots are supported. Animated/skinned/morph content, compressed
geometry/textures, cameras/lights, UV transforms/UV sets other
than zero and mixed sampler filtering currently return explicit errors. They are
not imported with missing visual semantics. This is static model authoring, not a
complete animation/content pipeline.

Source buffers/images can be GLB views, strict base64 data URIs or percent-escaped
portable paths under the source-file directory. Remote/absolute/traversal/symlink
references fail. Files are at most 64 MiB; loaded bytes 128 MiB, decoded pixels
256 MiB, parser allocations 128 MiB, aggregate geometry one million vertices/three
million indices. At most 10000 generated entities; folder publication also checks
remaining project catalog capacity. Accessor arithmetic/alignment and sparse index
ordering are checked before library reads. Callers serialize source/filesystem edits;
this is not hostile-race isolation or crash-durable publishing.

Importer prepares native `meshes/*.dotmesh`, `materials/*.dotmaterial`, `images/*.png`
and `model.dotscene` in an owned sibling directory, then publishes without replacing
an existing destination. Failures before publication preserve source/project and
remove staging. Asset import and scene insertion are separate operations: insertion
remaps entity/parent IDs and commits one document history transaction. Undo retains
the reusable asset directory. Imported physical models need authored scene lighting
or an HDR environment to be visible; no substitute illumination is persisted.

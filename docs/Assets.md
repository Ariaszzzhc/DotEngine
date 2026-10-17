# Mesh assets

`Dot.Assets.Mesh` decodes CPU geometry; upload it through the renderer to obtain
shared GPU mesh resources. `loadMesh` reads relative to a `FileSystem` project
root, and `saveMesh` atomically replaces a `.dotmesh` file in an existing parent.
File extensions are case-insensitive. Source/output size is limited to 64 MiB.

DotMesh is a bounded, fixed little-endian binary layout:

| Byte range | Content |
| --- | --- |
| 0..7 | Eight magic bytes `DOTMESH\0` |
| 8..11 | uint32 vertex count |
| 12..15 | uint32 index count |
| From 16 | Sixteen IEEE float32 values per vertex: position XYZ, linear color RGBA, normal XYZ, UV, tangent XYZW |
| After vertices | uint32 triangle indices |

The exact byte length is `16 + 64 * vertexCount + 4 * indexCount`, at most 64 MiB.
There is no padding, native structure dump, optional attribute block or layout
selection. Attribute and index order are retained exactly, with finite float bits
(including signed zero/subnormals) preserved. Normal zero denotes absence;
otherwise it must be unit length. Colors remain in [0,1], UVs may tile outside [0,1].
Tangents are orthogonal unit XYZ and W=+1/-1, or all zero for absence; they must be
complete or absent, and triangle corners share handedness. Counts are bounded by
one million vertices and three million indices, subject to the byte limit. Empty
indices select non-indexed triangle geometry. Wrong headers/counts, truncation,
trailing bytes, invalid values/indices/topology reject before publication. Counts
and exact length are checked before allocating decoded geometry.

`decodeMesh` consumes a byte span and returns independently owned geometry;
`encodeMesh` returns owned bytes. File loading/saving uses binary/atomic file access.
Native meshes are previewable binary resources; edit their OBJ/glTF source or
reimport models rather than opening native bytes as text. Every repository native
mesh uses this layout.

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

`Dot.Graphics.Geometry::withFlatNormals` explicitly derives winding-based flat
normals into a new non-indexed mesh without modifying the source. It retains colors
and UVs, uses double intermediates for extreme finite geometry and rejects
degenerate faces or expansion beyond one million vertices. Primitive/unattributed
vertices otherwise keep missing normals; rendering does not pretend lighting exists.

## Images and surface textures

PNG/JPEG and Radiance RGBE import, explicit color spaces, immutable GPU texture
ownership, mips, samplers and material base-color sampling are documented in
[rendering development](Rendering.md), including the implemented PBR/HDRI IBL,
soft shadows, SSAO and HDR/tonemapping pipeline and its current limits.

## glTF/GLB models

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
compatible native slots are supported. Morph content, compressed
geometry/textures, cameras/lights, UV transforms/UV sets other
than zero and mixed sampler filtering currently return explicit errors. They are
not imported with missing visual semantics. Node TRS animation is supported through
native clips/bindings and saved scene players as described in [Animation](Animation.md).
Skinned glTF/GLB meshes now retain native inverse binds, ordered joints and all
four/eight influences, as described in [Skinning](Skinning.md). A complete character
animation pipeline, including blends, retargeting and visual skeleton tools,
remains unfinished.

Source buffers/images can be GLB views, strict base64 data URIs or percent-escaped
portable paths under the source-file directory. Remote/absolute/traversal/symlink
references fail. Files are at most 64 MiB; loaded bytes 128 MiB, decoded pixels
256 MiB, parser allocations 128 MiB, aggregate geometry one million vertices/three
million indices. At most 10000 generated entities; folder publication also checks
remaining project catalog capacity. Accessor arithmetic/alignment and sparse index
ordering are checked before library reads. Callers serialize source/filesystem edits;
this is not hostile-race isolation or crash-durable publishing.

Importer prepares native `meshes/*.dotmesh`, `materials/*.dotmaterial`, `images/*.png`
and `model.dotscene`, plus `model.dotmodel` node bindings, any `skins/*.dotskin`
and native animation clips in an owned sibling directory, then publishes without replacing
an existing destination. Failures before publication preserve source/project and
remove staging. Asset import and scene insertion are separate operations: insertion
remaps entity/parent/joint/animation-target IDs and commits one document history
transaction. Undo retains the reusable asset directory. Imported physical models
need authored scene lighting
or an HDR environment to be visible; no substitute illumination is persisted.

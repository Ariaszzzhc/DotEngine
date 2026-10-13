# Mesh assets

`Dot.Assets.Mesh` decodes CPU geometry; upload it through the renderer to obtain
shared GPU mesh resources. `loadMesh` reads relative to a `FileSystem` project
root, and `saveMesh` atomically replaces a `.dotmesh` file in an existing parent.
File extensions are case-insensitive. Source/output size is limited to 64 MiB.

DotMesh requires exactly `vertices` and `indices`. Every vertex requires
`position` (three finite float-range numbers), `color` (four linear numbers in
[0,1]), `normal` (a unit three-vector or zero to explicitly denote absence), and
`texCoord` (two finite floats, including negative/outside-unit coordinates for
tiling). There is one current structure, with no old-format loader or version
dispatch. Indices are unsigned 32-bit integers referring to vertices. An empty
index list selects non-indexed triangle geometry. Mesh validation limits counts
to one million vertices and three million indices.
Duplicate fields, unknown fields, and nesting deeper than
64 are errors.

```json
{
  "vertices": [
    {"position": [0, 0, 0], "color": [1, 0, 0, 1], "normal": [0, 0, 1], "texCoord": [0, 0]},
    {"position": [1, 0, 0], "color": [0, 1, 0, 1], "normal": [0, 0, 1], "texCoord": [1, 0]},
    {"position": [0, 1, 0], "color": [0, 0, 1, 1], "normal": [0, 0, 1], "texCoord": [0, 1]}
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

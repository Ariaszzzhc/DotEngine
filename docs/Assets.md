# Mesh assets

`Dot.Assets.Mesh` decodes CPU geometry; upload it through the renderer to obtain
shared GPU mesh resources. `loadMesh` reads relative to a `FileSystem` project
root, and `saveMesh` atomically replaces a `.dotmesh` file in an existing parent.
File extensions are case-insensitive. Source/output size is limited to 64 MiB.

DotMesh version 1 requires exactly `version`, `vertices`, and `indices`. Every
vertex requires exactly `position` (three finite float-range numbers) and `color`
(four linear numbers in [0, 1]). Indices are unsigned 32-bit integers referring
to vertices. An empty index list selects non-indexed triangle geometry. Mesh
validation limits counts to one million vertices and three million indices.
Duplicate fields, unknown fields, incompatible versions, and nesting deeper than
64 are errors.

```json
{
  "version": 1,
  "vertices": [
    {"position": [0, 0, 0], "color": [1, 0, 0, 1]},
    {"position": [1, 0, 0], "color": [0, 1, 0, 1]},
    {"position": [0, 1, 0], "color": [0, 0, 1, 1]}
  ],
  "indices": [0, 1, 2]
}
```

OBJ import accepts finite positions, RGB vertex colors, triangles/polygons of
3..64 vertices, positive/negative attribute references, and object/group/smoothing
metadata. Polygons are triangulated, all shapes merge into one colored mesh, and
missing vertex colors default to white. Non-unit homogeneous position weights,
materials, line/point primitives, and free-form geometry return errors. Referenced
material files are never opened. Normal/UV/group data that the colored mesh cannot
retain is explicitly reported in the returned asset's `warnings` field. OBJ lines
are limited to 1 MiB; malformed numbers cannot fall back to zero/default values.

Run `DotRuntimeAcceptance --asset-test <existing-directory>` to save and reload a cube at
`assets/cube.dotmesh`. This acceptance case reports file/codec errors and returns
a non-zero exit status when its checks fail.

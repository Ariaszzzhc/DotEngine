# Scene files

`Dot.World.SceneFiles` reads and writes version 1 `.dotscene` JSON. Extensions are
case-insensitive; file access stays relative to the supplied `FileSystem` root.
Saving atomically replaces the destination and requires an existing parent.
Decoding builds an independent scene, preserving entity values while assigning a
fresh scene identity. Existing live scenes and their handles remain independent.

The root requires exactly `version` (integer 1), `name`, and `entities`. Every
entity requires `id`, `name`, `parent`, and `transform`; `camera`, `mesh`, and
`rigidBody` are optional and may be null. Unknown or duplicate fields are rejected.
Documents are limited to 64 MiB, 10000 entities, and JSON nesting depth 64. Hierarchies must be
acyclic with depth at most 256.

Entity IDs and non-null parent references are canonical positive decimal strings
below the uint64 maximum, preserving precision for JavaScript and automation.
Records may arrive in any order; duplicate IDs and missing parent targets fail.
The encoder emits records in creation-value order.

```json
{
  "version": 1,
  "name": "Example",
  "entities": [
    {
      "id": "1",
      "name": "Cube",
      "parent": null,
      "transform": {
        "position": [0, 0, 0],
        "rotation": [0, 0, 0, 1],
        "scale": [1, 1, 1]
      },
      "camera": null,
      "mesh": {"kind": "cube", "asset": "", "tint": [1, 1, 1, 1], "visible": true}
    }
  ]
}
```

Rotations use XYZW quaternions and are normalized on import. Transforms must be
finite, have nonzero scale, and produce supported world matrices. Cameras require
`projection` (`perspective`/`orthographic`), `verticalFovDegrees`,
`orthographicHeight`, `nearPlane`, `farPlane`, and boolean `primary`. At most one
camera may be primary, and camera world matrices must support float inversion.

Meshes require `kind` (`cube`/`quad`/`asset`), `asset`, `tint`, and boolean `visible`.
Built-ins require an empty asset path. Asset paths use relative UTF-8 forward-slash
paths without drive prefixes or parent traversal, ending in `.dotmesh` or `.obj`.
Asset existence is checked when rendering, rather than substituting missing data.

Rigid bodies require `motion` (`static`/`kinematic`/`dynamic`), `shape`
(`box`/`sphere`/`capsule`), `halfExtents`, `radius`, `halfHeight`, `velocity`, `mass`,
`friction`, `restitution`, `gravityFactor`, and boolean `sensor`. They store
pose-independent authoring properties, with world-space initial velocity. Pose
comes from the entity transform. All properties use the same finite/range checks
as actual body creation; see `Physics.md`. Physics descriptions coexist with
hierarchy, camera, and mesh components. Importing them creates no simulation
resources. The scene simulation service checks transform/scale suitability when
binding, advances bodies, and atomically writes world poses back as local TRS.
See `Physics.md` for the binding and failure contracts.

```json
{
  "motion": "dynamic",
  "shape": "box",
  "halfExtents": [0.5, 0.5, 0.5],
  "radius": 0.5,
  "halfHeight": 0.5,
  "velocity": [0, 0, 0],
  "mass": 1,
  "friction": 0.5,
  "restitution": 0,
  "gravityFactor": 1,
  "sensor": false
}
```

Run `DotRuntime --scene-file-test <existing-directory>` for save/load acceptance
at `scenes/main.dotscene`. Renderable scenes also need a camera; the file codec
permits scenes without one so editing and persistence can occur independently.

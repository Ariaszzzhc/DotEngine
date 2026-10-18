# Scene files

`Dot.World.SceneFiles` reads and writes version 1 `.dotscene` JSON. Extensions are
case-insensitive; file access stays relative to the supplied `FileSystem` root.
Saving atomically replaces the destination and requires an existing parent.
Decoding builds an independent scene, preserving entity values while assigning a
fresh scene identity. Existing live scenes and their handles remain independent.

The root requires `version` (integer 1), `name`, `nextEntity`, and `entities`, and allows optional
`environment`, `occlusion` and `links` (null/absent environment means no environment).
Unknown fields are rejected. Every
entity requires `id`, `name`, `parent`, and `transform`; `camera`, `mesh`, and
`rigidBody`, `character`, `audioSource`, `audioListener`, `script`, `material`, `light`,
`animation` and `skin` are optional and may be null.
Unknown or duplicate fields are rejected.
Documents are limited to 64 MiB, 10000 entities, and JSON nesting depth 64. Hierarchies must be
acyclic with depth at most 256.

Entity IDs and non-null parent references are canonical positive decimal strings
below the uint64 maximum, preserving precision for JavaScript and automation.
Records may arrive in any order; duplicate IDs and missing parent targets fail.
The encoder emits records in creation-value order.

`nextEntity` is the canonical positive decimal string for the next unreserved value,
strictly greater than every live entity value and below uint64 maximum. Null means
permanent allocation exhaustion, including after deleting every entity. Missing,
noncanonical, numeric or regressing values fail; the decoder never reconstructs the
floor from surviving entities. Save/load preserves retired and explicitly reserved
values. Allocation metadata does not identify a scene scope or create an entity.
Native `nextEntityValue` observes it; `reserveEntityValuesBefore` advances it or seals
allocation with null, without changing live handles. It cannot rewind or unseal.

Optional `links` is an array of authoring records with exactly `root`, `asset`,
`baseline`, `mapping` and `namespaces`. Root is a live placement value; asset is a portable
scenes/assets `.dotscene` path. Baseline is a native source scene object without
links. Product insertion/refresh orders nodes by logical key with compact values
and nextEntity, independently of source/working allocation. Mapping entries contain
canonical string `source`/`target` values and a `key` array of 1..32 canonical source values. Source
indexes the canonical baseline; key is the persistent logical identity. Mappings
cover every baseline node, including locally deleted targets. Roots and mapped
target values have disjoint ownership; targets must already be reserved. Link and
mapping counts are bounded by 10000, combined baseline text by 64 MiB, baseline
JSON depth by 61 to fit within the scene depth limit of 64. Empty sources can remain
linked after refresh. The complete encoded scene must still fit 64 MiB. Scene
validates replacement metadata atomically; loading does not read source assets.
Each namespace contains exactly `prefix`, `asset`, `nextEntity` and `entities`.
The unique empty-prefix root matches the link asset; nested prefixes append persistent
instance-root source values. Prefixes have at most 31 values and require live parent
anchors. nextEntity is the original source allocation floor, or null for exhaustion.
Entities are canonical source-local live physical allocation strings, including saved
inherited allocations and values hidden by outer overrides. Node keys must belong
to these namespaces. At most 10000 namespaces and 100000 observed allocation values
are retained across a scene's links. Replacement normalizes namespace/mapping order.
See [linked content](SceneContent.md#linked-instances-and-local-overrides) for merging
and runtime behavior.

```json
{
  "version": 1,
  "name": "Example",
  "nextEntity": "2",
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

Rotations use XYZW quaternions. Already unit-length float components are retained
exactly (squared length within two float epsilons of one); other nonzero finite
rotations are normalized on import/edit. Matrix evaluation still normalizes.
This prevents rotation drift across scene loads, saves and undo/redo without
rounding away genuine external edits. Transforms must be finite, have nonzero scale,
and produce supported world matrices. Cameras require
`projection` (`perspective`/`orthographic`), `verticalFovDegrees`,
`orthographicHeight`, `nearPlane`, `farPlane`, and boolean `primary`. At most one
camera may be primary, and camera world matrices must support float inversion.

Meshes require `kind` (`cube`/`quad`/`asset`), `asset`, `tint`, and boolean `visible`.
Built-ins require an empty asset path. Asset paths use relative UTF-8 forward-slash
paths without drive prefixes or parent traversal, ending in `.dotmesh` or `.obj`.
Asset existence is checked when rendering, rather than substituting missing data.

Material bindings require `asset`, a portable project-root relative `.dotmaterial`
path. Missing bindings use the mesh's deliberate unlit tint. Light descriptions
require `kind` (`directional`/`point`/`spot`), RGB `color`, `intensity`, `range`,
`innerConeDegrees`, `outerConeDegrees` and boolean `enabled`. Entity transforms
provide the world position and local -Z ray direction; ranges are world meters.
Descriptions are validated without opening a GPU or reading material/texture
files. See [rendering contracts](Rendering.md) for factor/unit/resource limits.

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

The optional `character` component requires exactly radius, halfHeight,
maxSlopeDegrees, stepHeight, stickDistance, mass, maxStrength, jumpSpeed and
maxContacts. All numeric values must satisfy the native controller's finite bounds;
maxContacts is an integer in [1,4096], without narrowing/wrapping large integers.
It cannot coexist with rigidBody. Pose remains in the entity transform; decoding
creates no physics backend. See [physics](Physics.md) for binding and scale rules.

Renderable scenes need a camera; the file codec permits camera-free scenes so
editing and persistence can occur independently.

Audio sources require exactly `asset`, `voice`, `spatial`, and boolean `playOnStart`.
Voice settings require `volume`, `pan`, `pitch`, and boolean `looping`. Spatial
properties require boolean `enabled`, `attenuation` (`inverse`/`linear`/`exponential`),
`minDistance`, `maxDistance`, `rolloff`, `dopplerFactor`, `innerConeDegrees`,
`outerConeDegrees`, and `outerConeGain`. They use the same pure validation as voice
and spatial controls; enabled spatial sources require zero manual pan. Pose is
not duplicated in this description; it belongs to the entity transform.

Audio listeners require only boolean `primary`. At most one listener can be
primary, independently of the camera. Multiple primary listeners fail loading
the whole candidate scene. Missing/null audio fields are optional. No audio asset
is opened or native voice/device created by component editing or scene loading;
`Dot.Audio.SceneAudio` resolves them in an explicit binding step. See `Audio.md`
for parameter/path contracts.

Script components are authoring data and initialize no VM while editing/loading.
A non-null `script` requires exactly `source`, `className`, `properties`, and
boolean `enabled`. Source is a portable canonical UTF-8 .ts/.mts path relative to
the compiler's source directory, excluding .d.ts/.d.mts declarations. The exported
class is a 1..128-byte ASCII identifier (including default/_/$). Properties are a
finite scalar-Unicode JSON object of at most 64 KiB/depth 32 with no duplicate
fields; the native description retains a canonical compact JSON representation.
Invalid edits preserve the old description. One script description attaches to
an entity, independently of camera/mesh/body/audio. Missing/null fields preserve
older version-1 documents. See `Scripting.md` for compiled lifecycle binding.

```json
{
  "source": "actors/player.ts",
  "className": "Player",
  "properties": {"speed": 3},
  "enabled": true
}
```

## Scene environment

A non-null root `environment` requires `asset`, `intensity`, `rotationDegrees`,
`background`, `radianceSize`, `diffuseSize`, `specularSize`, `brdfSize` and `samples`.
The asset is a portable project-relative `.hdr` path; intensity is finite [0,64],
yaw finite [-360,360], and background is boolean. Work values are integers: powers
of two for radiance/specular 16..512, diffuse 4..128, BRDF 16..256; samples 32..1024.
World/codec validation needs no device or filesystem. Actual opaque 2:1 HDR pixels,
source accessibility and GPU/cache budgets are validated by render/export consumers.
See [environment authoring](Editor.md) and [rendering contracts](Rendering.md).

Lights optionally own `shadows`: null/absence disables it; an object requires
resolution/cascades/distance/splitLambda/depthBias/slopeBias/normalBias/filterRadius.
Only directional shadow requests are supported; invalid types/work fail scene
validation. Meshes optionally store boolean castsShadow/receivesShadow (default
true). Encoding writes these fields explicitly. [Shadow contracts](Rendering.md)
describe bounds, finite frustums and the frame's single directional-source limit.


## Native hierarchy editing

`Scene::setParent`/`setParents` default to `ParentSpace::Local`, preserving local
TRS. `ParentSpace::World` preserves each original world matrix, including when
multiple parents change in one batch. The final hierarchy is validated once;
cycles, depth overflow, foreign/stale/duplicate handles and unrepresentable local
TRS fail without publishing changes. Setting the same parent retains the exact
local pose. Float decomposition can introduce rounding and rejects shear; it does
not approximate an unrepresentable transform.

`groupEntities` accepts 1..10000 unique live entities and creates one plain
container. Only minimal selected roots move: selecting both an ancestor and its
child does not transform the child twice. A shared immediate parent is retained;
otherwise the group is placed at scene root. Its origin is the last selected
entity's world origin, with identity local rotation/scale. Grouping under a shared
parent retains the children's original local basis, including an outer sheared
world matrix. Mixed-parent grouping must admit local TRS. Capacity, validation or
allocation failure leaves the original graph and external ID allocator intact.

`ungroupEntities` removes only containers without functional components; it never
implicitly deletes a mesh, camera, light, body, character, script, audio, animation
player or skin binding. Nested container selections are supported. Surviving
children retain world poses and attach to their nearest surviving ancestor. Nodes
referenced by native skin joints or animation targets cannot be removed. Opaque
script properties are not interpreted as entity references. Returned affected
children are owned and sorted by entity value. Live handles retain their scene
scope; committed removed IDs are never reused. See [editor operations](Editor.md#hierarchy-organization)
for selection/history/save behavior and [SDK operations](Scripting.md#hierarchy-operations).

## Owned native subsystem observations

`Scene::entities()` remains the complete, deterministic owned snapshot used by
scene codecs/editor inspection. It sorts compact identities before creating full
component descriptions. Subsystem loops use narrower owned observations:

| Native query | Result |
| --- | --- |
| `transforms()` | All live IDs, local poses and parents, in ID order |
| `colliders()` | Only rigid-body/character owners, with their physical description |
| `audioSources()` | Only source owners and their authored audio properties |
| `primaryAudioListener()` | Current primary listener identity, or explicit NotFound |

These queries neither borrow EnTT/component storage nor initialize backends. Values
remain independent after component edits, subtree removal, scene moves or destruction;
only the owned values can cross threads. Query calls retain the scene's creating-
thread/moved-state checks. Physical membership still enforces rigid-body/character
exclusion. They do not cache an obsolete set of owners or skip failed properties.

SceneSimulation synchronizes from collider descriptions and prepares writeback from
all topology/pose records. SceneAudio observes sources and the listener directly.
This avoids cloning unrelated names, scripts, material paths, skin joints and
animation libraries on each native tick. Parent-before-child writeback, full world-
transform validation, atomic failed edits, voice/PCM budgets, source retention and
listener motion baselines retain their existing behavior.

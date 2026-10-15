# Physics world

`Dot.Physics.World` owns a Jolt 5.6 rigid-body simulation independently of scenes
and rendering. It uses engine math types and expected error results; vendor types
remain private to its implementation unit.

`Dot.Physics.Body` supplies CPU-only `BodyProperties` and shared validation.
`BodyConfig` extends those properties with an initial world-space pose. Scenes
store the properties as their optional `RigidBody` component and persist them in
`.dotscene` files. Authoring creates no Jolt bodies, and editing a snapshot cannot
change the scene. Failed replacements preserve the previous component.

## Ownership and operations

Create a move-only `Dot::Physics::World` on its owner thread. All operations must
run on that thread. Each step waits for its jobs to finish; destruction after
quiescence removes and destroys every body before releasing the simulation,
workers, and temporary allocator. Independent worlds may run on different owner
threads. Jolt registration remains shared until process exit. A pre-existing
factory owned by another Jolt integration is reported as an initialization error.

Public `BodyId` values have a world scope and a monotonic body value. Moving a
world preserves its handles. Destroyed, foreign, and moved-from handles fail;
native Jolt body-slot/sequence reuse cannot make an old public handle valid.
Capacity is reusable after destruction, but public ID values are never reused.

`applyChanges` creates, destroys, and replaces configurations in one batch of at
most 8192 unique changes. A configuration alone creates a body; a handle alone
destroys it; both replace it while retaining the public handle. Results follow
input order, with null entries for deletions. All configurations and final
capacity are checked first, and new shapes/bodies are prepared outside the body
slot pool before old bodies are released. Full-capacity replacement is supported.
Invalid input or preparation failure preserves existing bodies. Unexpected slot
assignment failure can partially apply a batch and faults the world. Existing
bodies outside a batch retain their native objects, sleep state, and motion.
Moving-body replacements may preserve native linear/angular velocity through
explicit flags. Authored configurations remain validated before preparation;
trusted solver snapshots avoid being rejected because of limit-rounding drift.

World configuration allows 1..4096 bodies and 0..8 worker threads. Zero workers
executes synchronously on the owner thread. Gravity must be finite with magnitude
at most 1000 m/s². Pair and contact capacities are each sixteen times the body
capacity. Temporary simulation storage starts at 16 MiB and falls back to heap
storage when necessary.

## Bodies and time

Bodies support static, kinematic, and dynamic motion and box, sphere, and capsule
shapes. Box dimensions are half-extents; a capsule's `halfHeight` describes half
the cylinder height, excluding its two hemispheres. Units are metres, kilograms,
and seconds. Positions and rotations describe the shape origin in world space.
The backend uses single precision and its default contact/sleep tolerances.

Simulation configuration additionally accepts initial angular velocity with
magnitude at most 15*pi rad/s and `startActive` (default true). Static angular
velocity must be zero. Inactive moving bodies remain asleep until another
operation wakes them. Snapshots include angular velocity; explicit setters use
the same limit and reject static, invalid, stale, foreign, or faulted bodies.
These motion-state fields belong to simulation configuration rather than the
pose-independent scene authoring description.

Every body field is checked, including dimensions for inactive shape variants:

| Field | Accepted range |
| --- | --- |
| Half-extents, radius, capsule half-height | [0.001,10000] |
| Mass | [0.01,1000000] |
| Friction and restitution | [0,1] |
| Gravity factor | [0,10] |
| Initial/explicit position components | [-1000000,1000000] |
| Initial/explicit linear speed | [0,500] |

All values must be finite. Nonzero quaternions are normalized. Static bodies must
start with zero velocity; setting their velocity or adding an impulse fails.
Impulses apply at the centre of mass to dynamic bodies and fail before mutation
when the resulting speed would exceed 500 m/s. Linear/angular damping is zero;
the backend limits simulation velocities to 500 m/s and 15*pi rad/s. Sensors
participate in queries but do not produce collision response. Static/static
pairs are excluded; other pairs follow Jolt motion/sensor rules.

`setPose` teleports the body and updates the broad phase, retaining velocity and
waking moving bodies. `moveKinematic` sets velocity toward a target over a valid
duration, with the same speed limits. It changes the pose during subsequent
steps and does not automatically stop when the target is reached; call it again
with the current pose to stop linear and angular movement.

`step` accepts finite durations from one microsecond through 250 milliseconds.
It subdivides the duration into `ceil(seconds * 60)` collision steps. Invalid
input leaves simulation state intact. A backend capacity error may be returned
after time has advanced; the world then faults and rejects further mutation.
Body snapshots, statistics, ray queries, and body destruction remain available.
Recreate the world to resume simulation. In Debug, only Jolt's final assertion
for these returned update-error flags is allowed to continue; other assertions
still report their origin and request a breakpoint.

## Ray queries

`rayCast` takes an origin and displacement, with endpoints within position
limits and displacement length at least 0.001. It returns the closest hit,
including sensors, or a successful empty optional for a miss. Invalid inputs
return an error. A hit contains the public body handle, segment fraction, and
point. Fraction-zero hits can start inside a shape and have no surface normal;
positive-fraction hits include a normalized world-space normal.

## Scene simulation

`Dot.Physics.SceneSimulation` owns a world and entity bindings without retaining
a scene reference. All operations require the creating thread. Scene moves keep
their identity and bindings; a service can outlive its source scene for diagnostic
queries. Queries describe the last successful synchronization, so authoring
deletions become visible on `synchronize` or `step`.

`synchronize` checks every desired body before the batch is applied. Removed
components/entities release bodies; new components create them. Shape/material/
motion edits replace only the affected bodies, including at full capacity.
Unchanged bodies keep native contact state, velocity, angular velocity, and sleep.
Changing an initial velocity applies that new value; other edits preserve current
motion. Editing a transform teleports the corresponding body and wakes it.

Body positions/orientations use world space. Box half-extents multiply absolute
world scale. Sphere/capsule shapes require uniform magnitude within the documented
1e-5 relative tolerance; shear and out-of-range scaled geometry fail. Reflection
is supported because these centred primitive shapes are symmetric. Collider
dimensions are authoring values, with mass remaining independent of scale.

`step` validates duration, synchronizes, advances physics, and writes poses back
in one scene transform transaction. It processes parents before children even
when entity IDs were imported in another order. Body world poses remain
independent when a physical parent moves; non-physical children follow the updated
hierarchy. Reflected local-scale signs are preserved. Kinematic target operations
use world coordinates. Scene changes and physics are not a rollbackable single
transaction: a post-advance failure faults the service, while failed scene batch
writeback leaves its previous transforms intact.

A rotating body under a nonuniformly scaled parent can require a sheared local
matrix that a TRS scene cannot represent. Such writeback fails explicitly rather
than approximating the physical pose. The fault also appears in statistics; body
snapshots/rays remain diagnostic operations. `reset` prepares an independent
candidate world and replaces bindings only on success. It restarts motion from
authoring descriptions and can bind a different scene. It is required to recover
from writeback or backend faults.

## Contact events

Both world and scene services deliver owned events through `drainContactEvents`.
Callbacks from Jolt workers only copy body/manifold values into a synchronized
collector. No game code runs inside Jolt's locked callback context. The owner
reconciles contacts after the synchronous step, using current backend contact and
activity state. Entering sleep also checks actual geometry within Jolt's
speculative contact distance; inactive status alone does not establish overlap.

Events describe one logical body pair. Multiple collision substeps coalesce to
the last observed manifold for each pair. `Begin` starts a pair, `Stay` carries an
observation of an existing pair, `Sleep` marks a dormant pair, and `End` closes it.
Dormant pairs remain tracked; waking observations produce `Stay`, with separation
closing the pair. A transient observed contact may produce Begin/Stay followed by
End within the same engine step. Delivery follows operation order, with public
pair values sorted within each step/lifetime operation. The step field denotes
the most recent physics step; lifecycle edits retain that step number.

Begin/observed Stay geometry owns its contact points and uses the pre-solver
manifold. Normals point from the smaller public body value to the larger, and
contact point sides follow that ordering. Negative depth is speculative contact;
these events do not claim a resolved impulse. Sleep, End, and an unobserved wake
have no geometry. End reasons distinguish separation, pose change, destruction,
and reconfiguration. Direct pose edits invalidate affected contacts; identical
position/equivalent quaternion edits retain them.

Runtime `BodyConfig::userTag` is an opaque uint64 association copied into events.
It is never dereferenced and survives body destruction in already captured data.
Scene binding uses entity values for this association, so scene events remain
identifiable after entity/component removal without retaining scene storage.
Such removal IDs can be stale when queried against the scene. Reset discards the
old world and its contact stream.

World configuration permits 1..262144 queued events (default 65536), with tracked
and per-step captured pairs limited to sixteen times body capacity. Drain events
regularly, including after lifecycle edits. Queue-full lifecycle/pose operations
fail before mutation and can be retried after draining. Capture, reconciliation,
or queue failure after a step faults the world/service and rejects incomplete
event delivery. Faulted bodies remain available for diagnostic reads/cleanup;
recreate or reset to resume. There is no silent truncation of event streams.

## Sphere volume sweeps

`World::sphereCast(origin, radius, displacement, filter)` sweeps a real Jolt sphere
against current rigid bodies. `SceneSimulation::sphereCast` maps contacts and the
optional ignored body to scene-scoped entities. Radius is [0.001,10000] metres,
center endpoints are finite within +/-1e6, and displacement length is at least
0.001. Sensors are excluded by default; includeSensors explicitly enables them.
ignoreBody must resolve to a current synchronized rigid body, not a virtual capsule.
Invalid/stale/foreign filters fail instead of silently changing query meaning.

The closest hit includes fraction, sphere center, obstacle surface point, outward
normal and penetrationDepth. Contact coordinates are world-space. Unlike a center
ray, the sphere catches nearby corners and narrow obstacles that can clip a camera.
Initial overlap returns fraction zero and nonnegative depth; a geometrically
degenerate overlap can have null normal. Later contacts report zero depth. Queries
do not move/depenetrate/wake bodies, advance physics or publish contacts. A query
describes the current synchronized world, not future movement of dynamic obstacles.

The backend uses an origin-relative contact offset to retain local precision when
translating returned surface points. This does not make the single-precision world
a large-world streaming/origin-rebasing system. Headless regressions check corners,
overlap, sensor/body filtering, owner/thread/lifetime contracts, curved colliders
and translated scenes. Virtual capsules retain their documented lack of an inner
rigid-body proxy and do not participate in these queries.

## Native character controller foundation

`Dot.Physics.Character` separates CPU-only capsule/configuration/input validation
from `Dot.Physics.World`'s Jolt implementation. `createCharacter` owns a feet-origin
Y-up virtual capsule in the same body world, with independent capacity (default 64,
0..256), monotonic world-scoped handles and owner-thread operations. World moves
preserve handles; destruction releases all capsules before native physics storage.
Characters require vertical nonpositive gravity. Configuration/input errors preserve
existing state; contact-query preparation failures do not publish a character.

The capsule has configurable radius/cylinder half-height, walkable slope, stair
height, floor-stick distance, mass, push strength, jump speed and native query hit
budget. Horizontal world-space input is limited to 50 m/s. Its velocity persists;
jump edges merge until the next simulation substep, then are consumed whether or
not the capsule can jump. Air requests do not become buffered landing jumps.
`setCharacterYaw` changes facing without clearing intent. `teleportCharacter`
clears velocity/input, refreshes contacts without advancing time, and leaves any
initial overlap recovery to the next step. A failed query after pose mutation faults
the world; diagnostic reads/destruction remain available.

Character and rigid-body simulation advance together at the existing 60 Hz
substep bound. Current support is refreshed before using it, so removed bodies or
capsules cannot supply stale jumping support. Actual native capsule sweeps handle
wall sliding, ceilings, stairs, walkable/steep ramps, dynamic-body pushing and
moving-platform velocity. Ground state, ground normal/velocity and an optional
live body handle are returned. Controller velocity includes the native downward
support probe; it is not a difference of corrected feet positions. Native reported
contact-budget failure is an error, rather than a successful truncated result.

This foundation intentionally has no inner rigid-body proxy. Virtual capsules
collide with solid bodies and one another, but ordinary ray casts, rigid-body
sensor/contact events and fast incoming bodies do not see a capsule in the broad
phase. Body statistics count rigid bodies; `characters` counts owned capsules.
Scene persistence, Inspector/agent authoring and shared GameSession simulation now
use the controller. TypeScript controls use the same service. Humanoid content and the requested
over-the-shoulder camera remain unfinished. This does not establish a finished
character template or large-crowd scalability.

## Scene character controllers

`Scene::setCharacterController` stores validated CPU-only properties. A controller
and rigid body cannot coexist on one entity; explicitly remove the old component
before adding the other. Scene loading, document edits/history/duplication, Inspector
drafts, Play cloning and game packages use the same optional `character` description.
Editing does not allocate a native controller or world.

`SceneSimulation` binds feet-origin capsules to entity world poses. World scale must
be positive and uniform within 1e-5 relative tolerance; it scales radius and cylinder
half-height. Step/stick distances remain world metres. Shear, reflections and tilt
are binding errors. World yaw is supported, including rotated/scaled parents. Body
and character poses write back together in hierarchy order through one scene
transform transaction. A changed authored pose clears velocity and queued input;
property-only replacement retains native motion, pending jump and explicitly set yaw.

The native scene service exposes `character`, `setCharacterInput` and
`setCharacterYaw`. State includes world feet/rotation/velocity, ground state/normal/
velocity and optional scene-scoped `groundEntity`. Queries describe the latest
synchronization; removed/foreign/moved handles and wrong-thread calls fail explicitly.
Support lookup currently scans synchronized bodies. It is not a crowd-performance
claim. Capsule contacts still do not enter rigid-body events or ray queries.

The mixed `World::applyChanges(bodies, characters)` prepares both sets before
mutation. Invalid descriptions, duplicate/stale/foreign handles and final-capacity
failures retain both sets and do not consume new IDs. Replacements work at full
capacity and preserve public IDs. Input-order results use nullopt for deletions.
Final native contact queries occur after publication, so a query/slot failure then
faults the world rather than reporting fake atomic rollback. Diagnostic reads and
destruction remain available; scene reset independently prepares a replacement world.

Animation may drive bones/meshes below a character root. It cannot write that root
or any ancestor, including when switching/cross-fading to another clip. This prevents
an animation channel from fighting capsule ownership without blocking child animation.

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

## Ray queries and acceptance

`rayCast` takes an origin and displacement, with endpoints within position
limits and displacement length at least 0.001. It returns the closest hit,
including sensors, or a successful empty optional for a miss. Invalid inputs
return an error. A hit contains the public body handle, segment fraction, and
point. Fraction-zero hits can start inside a shape and have no surface normal;
positive-fraction hits include a normalized world-space normal.

Run `build/debug/bin/DotRuntimeAcceptance --physics-test` to drop a sphere onto a static
box, verify its resting height and a ray hit, then destroy it and reject its old
handle. Focused tests also cover restitution, gravity scaling, all three shapes,
kinematic motion, sensors, limits, transactional input failures, native sequence
wrap, worker execution, moves, and actual contact-capacity failure.

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

Run `DotRuntimeAcceptance --scene-physics-test` for a falling scene cube, entity-scoped ray
query, and synchronized cleanup. `--scene-physics-render-test` additionally checks
the actual GPU pixels at its resting position. GPU tests also compare the initial
and final pixels to prove that scene writeback changes rendering.

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

`--scene-physics-test` and its GPU variant verify contact begin and destruction
alongside collision poses, entity ray queries, and synchronized cleanup.

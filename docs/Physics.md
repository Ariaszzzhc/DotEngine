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

Run `build/debug/bin/DotRuntime --physics-test` to drop a sphere onto a static
box, verify its resting height and a ray hit, then destroy it and reject its old
handle. Focused tests also cover restitution, gravity scaling, all three shapes,
kinematic motion, sensors, limits, transactional input failures, native sequence
wrap, worker execution, moves, and actual contact-capacity failure.

Contact events and scene synchronization are subsequent stages.

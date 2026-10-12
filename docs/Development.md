# Development milestones

DotEngine is developed in small, independently verifiable stages. The intended
dependency order is:

1. Build targets, tests, diagnostics, time, memory, and filesystem foundations.
2. SDL window lifecycle, events, input, and the application loop.
3. Dawn device/surface lifecycle, rendering, GPU resources, and 3D math.
4. Assets, ECS scenes, transforms, cameras, and scene persistence.
5. Physics, audio, TypeScript modules, and script hot reload.
6. Project tooling, a graphical editor with automation commands, and export.
7. Example game and acceptance scenes for components and scripting APIs.

Target platforms are arm64 macOS and x86_64 Windows/Linux. Other architectures
are outside the development scope; platform validation is limited to macOS.

## Verification log

### Core foundation

Implemented a static C++26 module library and a runtime acceptance executable,
structured results with error origin, configurable concurrent logging, a monotonic
clock, bounded fixed-step scheduling, and an upstream-backed memory tracker.

- macOS with LLVM Clang 23.1.3: Debug and Release each pass all 31 CTest cases
  (28 doctest cases and three executable acceptance checks).
- The standalone standard-stream probe succeeds with the configured environment.
- The macOS runtime links the LLVM libc++ and libunwind selected by the caller.
- Formatting and whitespace checks pass. Review covered source-location capture,
  reentrant logging sinks, failed allocations, counter lifetimes, and duration
  overflow and interpolation boundaries.
- Sanitizers are excluded by request. Windows/Linux code paths are unverified.

### Files, events, and implementation units

Added root-relative file access, bounded binary/text reads, native atomic file
replacement, and scoped synchronous event subscriptions. Windows file reads share
delete access to allow destination replacement while readers hold an existing
snapshot; macOS/Linux use same-directory temporary files and rename.

Non-template definitions now live in `.cpp` module implementation units. Platform
headers and filesystem implementation details are excluded from the interfaces.
Template event dispatch and compile-time formatting remain in interfaces.

- macOS Debug and Release each pass all 56 CTest cases (52 doctest cases and four
  executable checks), including Unicode paths, read limits, failed-write cleanup,
  permission preservation, complete concurrent snapshots, and event reentrancy.
- Review covered temporary-file ownership, OS error propagation, Windows handle
  sharing, invalid paths, subscription mutation, and moved connection lifetimes.
- Incremental compilation of a touched filesystem implementation rebuilds that
  implementation and links executables without recompiling interface consumers.
- Formatting and whitespace checks pass. Windows/Linux remain unverified.

### Window, input, and application loop

Added SDL3 window ownership, main-thread checks, typed event translation,
frame-scoped input edges and text, and an application loop with fixed simulation,
per-frame updates, rendering callbacks, focus pause, and stop/error propagation.
The runtime's `--window-smoke` case opens a window and executes a bounded loop.

- macOS Debug and Release each pass all 88 CTest cases (83 doctest cases and five
  executable checks). Window tests use SDL's dummy driver, including 50 repeated
  initialization cycles and cleanup after failed window creation.
- Both Debug and Release pass the real Cocoa window acceptance case, executing
  20 frames and releasing the window in an ordinary desktop process. A restricted
  sandbox cannot access the display service, so native checks run outside it.
- Reviewed video reference ownership, moves, foreign-window events, worker-thread
  rejection, callback failures, focus pause, repeat keys, and input overflow.
- vcpkg dependencies use the LLVM compiler on the caller's PATH and inherit the
  caller's CPPFLAGS/CFLAGS/CXXFLAGS/LDFLAGS through the maintained target triplets.
- Formatting and whitespace checks pass. Windows/Linux remain unverified.

### Mathematics and GPU dependency preparation

Added a separate mathematics interface and implementation using private GLM
operations, engine-owned vector/quaternion/matrix data, checked normalization,
transform composition/inversion, and right-handed camera projections with [0, 1]
depth. Double intermediates preserve finite float extremes; unrepresentable
results and degenerate inputs return errors. The pinned manifest prepares Dawn
with Metal on macOS, D3D12 on Windows, and Vulkan/X11/Wayland on Linux.

- macOS Debug and Release each pass all 102 CTest cases (96 doctest cases and six
  executable checks), including camera projection acceptance, extreme values,
  parent/child transform order, inverse round trips, and invalid camera inputs.
- Reviewed matrix storage/order, quaternion normalization, homogeneous division,
  overflow reporting, and projection depth. Formatting/whitespace checks pass.
- GLM 1.0.3 and Dawn 20260911.162847#1 build with the configured LLVM environment.
  Dawn rendering is not implemented yet. Windows/Linux remain unverified.

### Block spacing

Function/type definitions and control-flow blocks now have blank lines between
adjacent statements. Block interiors stay unpadded and `else`/`catch` remain
attached. The formatting configuration and development instructions record this
convention. Reviewed existing core/platform/math changes as whitespace-only;
their Debug/Release test suites still pass.

### Dawn rendering foundation

Added a private Dawn implementation with Metal/D3D12/Vulkan adapter selection,
checked device creation, asynchronous callback lifetime management, scoped GPU
errors, colored triangle pipelines, camera uniforms, depth testing, and tightly
packed RGBA8 readback. Native surfaces use SDL Metal views, Win32 handles, or
X11/Wayland handles; window size changes reconfigure the surface. Acquisition
timeouts and recoverable surface changes explicitly defer a frame.

- macOS Apple M4 Pro with LLVM 23.1.3: Debug and Release each pass all 114 CTest
  cases (106 doctest cases and eight executable checks), including real Metal
  readback, clipping/occlusion, odd row widths, transforms, invalid inputs, moves,
  worker-thread rejection, 12 device lifecycles, and four native surface lifecycles.
- Both configurations pass the visible 30-frame triangle acceptance case with
  a resize after frame ten. GPU/native checks ran outside the restricted sandbox.
- Reviewed callback cancellation ownership, exception containment across the C
  callback boundary, balanced error scopes, readback padding, window borrowing,
  Metal view release before window destruction, and surface acquisition statuses.
- Formatting/whitespace checks pass. macOS executable linkage preserves the
  caller-selected LLVM libc++/libunwind and uses system Metal/QuartzCore frameworks.
  Windows/Linux implementation paths remain unverified.

### CPU geometry

Added a separate geometry module with indexed/non-indexed triangle validation,
finite positions and color ranges, bounded vertex/index counts, axis-aligned
bounds, and centered cube/quad generators. Vertex data now belongs to this module
and remains re-exported by the renderer for existing consumers.

- macOS Debug and Release each pass all 122 CTest cases (113 doctest cases and
  nine executable checks). Geometry tests cover outward cube winding, quad
  winding, bounds, invalid topology/indices/colors, resource limits, and primitive
  underflow/overflow boundaries; `--geometry-test` exercises the public API.
- Reviewed primitive indices, scale boundaries, allocation limits, and existing
  renderer compatibility. Formatting/whitespace checks pass.
- Windows/Linux remain unverified.

### Shared GPU meshes and draw lists

Added immutable indexed/non-indexed GPU mesh uploads with shared ownership,
device identity checks, resource statistics, and draw lists with independent
world transforms and tint uniforms. Mesh resources can be released after their
renderer is destroyed, while attempts to query/draw them report invalid state.
Readback, pass setup, and surface presentation are shared by both drawing APIs.

- macOS Debug and Release each pass all 131 CTest cases (121 doctest cases and
  ten executable checks), including uploaded-data independence, multi-instance
  transform/tint isolation, shared copy/move accounting, worker-thread release,
  cross-device rejection, renderer moves, invalid draws, and resource shutdown.
- A real Metal indexed cube passes perspective-camera readback and native surface
  presentation. `--mesh-test` provides the standalone 3D acceptance case.
- Reviewed uniform layout/alignment, queue write ordering, command retention,
  device/mesh lifetimes, validation before submission, and quiescent counters.
  Formatting/whitespace checks pass. Windows/Linux remain unverified.

### Mesh asset files

Added bounded strict DotMesh JSON serialization and geometry-only OBJ import,
using private nlohmann-json and TinyObj implementations. JSON rejects duplicate
or unknown fields, invalid versions/types, deep nesting, and invalid geometry.
OBJ preflight rejects numeric fallback, malformed references, unbounded faces,
unsupported materials/primitives, and weighted positions; attribute/group loss
is returned as warnings. Root-relative asset saves use atomic file replacement.

- macOS Debug and Release each pass all 144 CTest cases (133 doctest cases and
  eleven executable checks), including float-extreme round trips, strict schema
  failures, duplicate/depth limits, OBJ polygons/negative references/colors,
  unsupported commands, Unicode paths, missing files, and failed-save preservation.
- `--asset-test` saves and reloads an actual cube asset in an existing directory.
- Reviewed parser bounds, explicit attribute loss, third-party numeric defaults,
  index validation, path conversion errors, and replacement semantics.
  Formatting/whitespace checks pass. Windows/Linux remain unverified.

### UTF-8 boundary validation

Added allocation-free UTF-8 scalar validation and checked code-point counting for
upcoming scene/entity text boundaries. NUL is a valid scalar; subsystems define
their own NUL policy. The validator performs no normalization or grapheme counting.

- macOS Debug and Release each pass all 151 CTest cases (140 doctest cases and
  eleven executable checks). Text tests cover every single byte, legal scalar
  boundaries, combining characters, Chinese/emoji text, malformed continuations,
  truncation, overlong encodings, surrogate values, and out-of-range scalars.
- The runtime core self-test checks multilingual text and invalid encodings.
  Reviewed byte indexing, signed-byte conversion, range/overlong checks, and
  error offsets. Formatting/whitespace checks pass. Windows/Linux remain unverified.

### Scene and entity foundation

Added a private EnTT scene implementation with separate identity, transform, and
hierarchy components. Public handles have scene identity and monotonic entity
values; moves preserve them, and destroyed values are never reused. Owned
snapshots are deterministic by creation ID. Hierarchy/world matrices are cached,
and transform/parent mutations validate the whole resulting scene before success,
rolling back on cycle, depth, numeric, or allocation failures.

- macOS Debug and Release each pass all 168 CTest cases (156 doctest cases and
  twelve executable checks), including Unicode name limits, scoped/stale handles,
  move semantics, subtree destruction, normalized rotations, parent composition,
  cache invalidation, overflow rollback, 256-depth/10000-entity limits, worker
  rejection, independent concurrent scenes, and 100 registry lifecycles.
- `--scene-test` verifies an actual parent/child world transform and subtree removal.
- Reviewed partial entity construction cleanup, rollback reference stability,
  counter saturation, cached depth validation, component ownership, and move
  invariants. EnTT global type counters use atomics for independent scene creation;
  each individual scene remains confined to its creating thread for operations.
- Formatting/whitespace checks pass. Windows/Linux remain unverified.

### Camera and mesh components

Added optional perspective/orthographic cameras and built-in/asset mesh
descriptions to entity snapshots and EnTT storage. Primary cameras are unique;
views use the full world inverse. Transform and hierarchy changes also validate
camera invertibility and roll back invalid poses. Mesh descriptions validate tint,
kind, and portable relative UTF-8 asset paths before storage; file existence is
left to the upcoming render service.

- macOS Debug and Release each pass all 174 CTest cases (162 doctest cases and
  twelve executable checks), including projection/clipping, primary lifetime,
  owned component snapshots, invalid camera parameters, camera/ancestor rollback,
  mesh path/tint validation, component removal, and foreign/stale/worker rejection.
- The runtime scene acceptance also sets camera/mesh components, resolves the
  primary camera, and computes its view-projection matrix.
- Reviewed optional component ownership, primary replacement failures, inverse
  range checks, UTF-8/native path conversion, and unchanged-state failure paths.
  Formatting/whitespace checks pass. Windows/Linux remain unverified.

### Scene rendering and mesh cache

Connected camera/mesh components to actual offscreen and native rendering through
a separate scene render service. The service borrows the renderer, owns an
optional asset-root descriptor, shares uploaded meshes, and bounds cache bytes.
Unused visible-scene resources are pruned before new loads; importer warnings are
available separately. Missing camera/assets, invalid input, excessive draws, and
budget failures return errors. Cache reload is explicit through `clearCache`.

- macOS Debug and Release each pass all 183 CTest cases (170 doctest cases and
  thirteen executable checks), including instance pixels, shared-cache release,
  hidden meshes, camera override, unresolved assets, imported warning propagation,
  reload failure, byte/draw limits, moves, worker rejection, and native scene resize.
- `--scene-render-test` renders an actual entity/camera cube scene and verifies
  its Metal pixel readback and shared mesh count.
- Reviewed frame-owned mesh pointer stability, move-only frame transport, cache
  pruning/budget accounting, asset-root ownership, renderer borrowing, and failure
  paths. Formatting/whitespace checks pass. Windows/Linux remain unverified.

### Scene transactions and imported IDs

Added monotonic explicit entity-ID import and batch transform/parent transactions.
Inputs and duplicate handles are validated before mutation; the final scene is
checked once, with complete rollback on failure. Single-item setters now use the
same transaction path. This permits valid hierarchy changes whose intermediate
individual steps would temporarily contain a cycle.

- macOS Debug and Release each pass all 190 CTest cases (177 doctest cases and
  thirteen executable checks), including imported-ID gaps/exhaustion, final-state
  hierarchy validation, complete transform/parent rollback, invalid/duplicate/
  stale/foreign handles, empty/oversized batches, worker rejection, camera inverse
  failures, and a 1000-entity batch.
- The runtime scene acceptance imports IDs and applies its setup in batches.
- Reviewed mutation preparation, stable component references, rollback ownership,
  monotonic counter updates, and no-op/error paths. Formatting/whitespace checks
  pass. Windows/Linux remain unverified.

### Scene persistence

Added strict versioned scene JSON with decimal-string IDs, deterministic encoding,
optional camera/mesh components, unordered input records, and batch construction
of an independent candidate scene. Failed decoding leaves live scenes untouched;
file saves use root-relative atomic replacement. A private implementation helper
shares duplicate/depth validation between mesh and scene codecs.

- macOS Debug and Release each pass all 203 CTest cases (189 doctest cases and
  fourteen executable checks), including complete component/hierarchy round trips,
  full uint64 precision, fresh handle scope, unordered records, schema/type/version
  failures, duplicate IDs/fields, missing parents/cycles/primary conflicts, source
  limits, repeated failure cleanup, Unicode atomic replacement, and 1000-entity
  batch import.
- `--scene-file-test` saves and reloads an actual scene file. A deserialized scene
  also produces the expected real Metal pixels through the scene render service.
- Reviewed candidate ownership, strict typed conversion, ID precision/order,
  validation sequencing, common parser bounds, and failed-save preservation.
  Formatting/whitespace checks pass. Windows/Linux remain unverified.

### Physics world foundation

Added independently owned Jolt worlds with primitive shapes, three motion types,
sensors, mass-based impulses, pose/velocity operations, nearest ray queries,
synchronous substeps, optional job workers, and observable body/active counts.
Public IDs remain valid across moves and cannot alias reused native slots.
Invalid inputs fail before mutation; real contact/pair capacity errors fault the
world while preserving diagnostic reads and cleanup operations.

- macOS Debug and Release each pass all 227 CTest cases (212 doctest cases and
  fifteen executable checks), including actual sphere/box/capsule contacts,
  restitution, gravity scaling, sensor response, kinematic motion, short-duration
  quaternion precision, ray hit/miss/inside semantics, finite/range limits,
  unchanged-state failures, capacity recovery, 3600 native body reuse cycles,
  moves, wrong-thread rejection, concurrent independent worlds/job workers, and
  actual contact-capacity exhaustion.
- `--physics-test` drops a sphere, verifies resting height and its ray hit, and
  destroys it before checking stale-handle rejection. It accounts for Jolt's
  default 0.02-metre contact penetration tolerance.
- Jolt's exported compiler threading options propagate to module consumers;
  otherwise Clang rejects their BMI threading-mode mismatch. RTTI is enabled in
  the pinned dependency to match the engine's compilation mode. No compiler,
  linker environment, or sanitizer setting was changed.
- Jolt 5.6 asserts immediately before returning recoverable update-capacity flags
  in Debug. The runtime allows only that known final diagnostic during its own
  update call; other assertions retain reporting and breakpoint behavior.
- Reviewed global registration/diagnostic lifetime, partial initialization,
  body/shape cleanup, worker quiescence, non-reused handle scope, mutation bounds,
  and post-advancement failure semantics. Formatting/whitespace checks pass;
  `otool` confirms the caller-selected LLVM libc++ and libunwind. Windows/Linux
  remain unverified.
- An implementation-only touch rebuilds the physics object and relinks consumers
  without rebuilding module interfaces or their BMI consumers.

### Rigid-body descriptions and persistence

Extracted pose-independent physics properties and CPU-only validation into a
separate module. Actual body creation and scene authoring share the checks;
world-space position/rotation remain part of simulation configuration. Scenes now
store optional rigid-body descriptions without initializing Jolt, and strict
scene JSON persists every property while accepting older documents without the
component. Component replacement validates before mutation.

- macOS Debug and Release each pass all 240 CTest cases (225 doctest cases and
  fifteen executable checks), including all motion/shape combinations, returned
  quaternion normalization, physical boundaries/non-finite fields, inactive shape
  dimensions, static velocity, snapshot independence, component removal, failed
  replacements, subtree lifetime, stale/foreign/moved handles, worker rejection,
  complete JSON round trips, old/null components, exact fields/types/enums,
  duplicate fields, and repeated failed imports preserving live scenes.
- The scene-file runtime acceptance saves and reloads an actual rigid-body
  description alongside its mesh component. Existing physical collision and
  Metal rendering acceptance cases also pass after the module extraction.
- Reviewed module dependencies, value ownership, shared validation equivalence,
  initial velocity versus runtime pose, candidate-scene failure cleanup, and
  strict schema compatibility. Formatting/whitespace checks pass. Windows/Linux
  remain unverified.

Physics scene synchronization/contact events, audio, scripting,
project/editor/export tooling, and the example game remain to be implemented.

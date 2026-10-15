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

### Checked TRS decomposition

Added affine matrix decomposition needed for hierarchy-aware simulation pose
transfer and editor transform operations. It uses double intermediate column
lengths and orientation calculations, assigns reflection to X scale, normalizes
the quaternion, and verifies reconstructed matrix columns. Perspective,
singularity, shear beyond the documented tolerance, and unrepresentable scales
return errors.

- macOS Debug and Release each pass all 249 CTest cases (234 doctest cases and
  fifteen executable checks), including identity and nonuniform transforms, all
  eight scale-sign combinations, half-turn rotations, uniform parent hierarchies,
  sheared hierarchy rejection, perspective/singular/non-finite matrices, tiny
  non-affine elements, extreme/subnormal scales, unrepresentable column lengths,
  tolerance boundaries, and 128 seeded transformed-point round trips.
- `--math-test` verifies affine TRS decomposition alongside its camera projection
  and rejects decomposition of a perspective projection.
- Reviewed column-major indexing, reflection equivalence, quaternion sign/range,
  zero/non-finite handling, and relative reconstruction checks. Formatting and
  whitespace checks pass. Windows/Linux remain unverified.

### Batched body configuration and motion state

Added batch create/destroy/reconfigure operations that validate all inputs and
prepare bodies outside Jolt's slot pool before mutation. Replacements work at
full capacity and retain public handles; unaffected native bodies keep their
motion and sleep state. Simulation configuration and snapshots now carry angular
velocity, initial activation is explicit, and statistics expose fault state.

- macOS Debug and Release each pass all 257 CTest cases (242 doctest cases and
  fifteen executable checks), including full-capacity replacement, mixed input
  order, real mass/ray verification after replacement, later-input rollback,
  unchanged ID allocation on rejected batches, duplicate/foreign/stale/empty/
  oversized/capacity failures, sleeping bodies, angular movement and limits,
  static angular rejection, 300 repeated motion/shape replacements, moves,
  owner-thread enforcement, and faulted batch/angular rejection.
- `--physics-test` now creates its actual floor and falling sphere in one batch.
- Reviewed prepared-body RAII, shape ownership, map-node transfer without commit
  allocations, slot counts, non-reused IDs, unchanged-body lifetime, and partial
  backend failure cleanup. Formatting/whitespace checks pass. Windows/Linux
  remain unverified.

### Scene physics synchronization

Connected rigid-body descriptions to an independently owned scene simulation
service. It validates all bindings before applying batched changes, scales
supported primitive geometry, retains unaffected native state, and transfers
physical world poses back through the hierarchy in one scene transaction.
Creation-order-independent parent processing preserves reflected local signs.
Explicit reset prepares a candidate before replacing bindings and recovers faults.

- macOS Debug and Release each pass all 281 CTest cases (264 doctest cases and
  seventeen executable checks), including falling scene boxes/rays, uniform and
  reflected parent transforms, independently moving physical parents/children,
  component addition/removal, full-capacity reconfiguration, retained angular/
  linear/sleep state, explicit initial velocity changes, authoring teleport,
  scaled/sheared/oversized geometry failures, capacity and duration boundaries,
  kinematic targets, post-advance writeback failure/reset, failed-reset retention,
  scene rebind/move/persistence, imported child-before-parent IDs, service lifetime
  beyond scene lifetime, empty scenes, scope/thread checks, and actual backend
  contact-capacity failure preserving authoring data.
- Additional normal-path cases verify a non-physical camera child following its
  simulated parent and actual reflected/scaled sphere/capsule ray geometry.
- `--scene-physics-test` drops an actual scene cube, checks its resting pose and
  entity-scoped ray hit, then verifies synchronized cleanup.
  `--scene-physics-render-test` also checks GPU pixels at the resting position.
  A Metal test compares initial/final green pixels and prunes GPU mesh ownership
  after entity destruction.
- Moving-body replacements can preserve trusted native motion instead of
  rejecting solver rounding at speed limits. Dedicated backend and scene tests
  verify this path and reject invalid preservation flags; authoring inputs still
  receive normal validation.
- Reviewed value/scene ownership, two-phase binding changes, world/local matrix
  conversion, reflected signs, normalization when predicting scene writes,
  pose-batch rollback, last-synchronized query semantics, fault diagnostics,
  candidate reset cleanup, and private backend handle lifetime. Formatting and
  whitespace checks pass. Windows/Linux remain unverified.
- An implementation-only touch rebuilds the simulation object and relinks
  consumers without rebuilding module interfaces or their BMI consumers.

### Owned contact events

Added worker-safe contact capture and owner-side logical pair reconciliation.
Events carry owned pre-solver manifold geometry and opaque runtime associations.
They distinguish Begin/Stay/Sleep/End and retain dormant contacts instead of
reporting sleep as separation. Sleep entry checks actual geometry within backend
contact tolerance; wake observations retain pair identity. Lifecycle edits end
affected pairs with an explicit reason, and scene events preserve removed entity
values without retaining scene references or historical body maps.

- macOS Debug and Release each pass all 300 CTest cases (283 doctest cases and
  seventeen executable checks), including real begin/stay geometry and tags,
  sleep/wake/separation, sensors, destroyed/reconfigured/teleported bodies,
  equivalent-pose no-ops, queue-full lifecycle rollback/retry, post-step overflow
  faulting and rejected incomplete streams, owner-thread/move checks, native ID
  churn, canonical normal/point orientation despite native/public order differences,
  deterministic multi-pair worker capture, and scoped scene delivery after
  entity/component removal, reconfiguration, reset, and failed-step recovery.
- Both scene-physics runtime acceptance commands now verify begin and destruction
  events alongside existing pose/ray/Metal pixel validation.
- Contact queues are explicitly bounded, with no silent truncation. Capture/queue
  failures after advancement fault the service; lifecycle capacity failures happen
  before body changes and can be retried after draining.
- Reviewed callback lock restrictions, copied geometry/tag lifetime, native slot
  reuse, sleep-transition geometry queries, sorted pair/operation ordering,
  pre-mutation queue reservation, failed-stream handling, listener detachment,
  and transactional scene event mapping. Formatting/whitespace checks pass.
  Windows/Linux remain unverified.

### Owned audio clips

Added a pinned miniaudio backend and separate module interface/implementation for
shared immutable PCM resources. Bounded WAV/FLAC/MP3 decoding retains source
channels/rate, validates WAV containers before backend startup, checks complete
frame reads, and reports malformed inputs rather than substituting silence.
Root-relative asset loading validates extension/container agreement.

- macOS Debug and Release each pass all 315 CTest cases (297 doctest cases and
  eighteen executable checks), including PCM copying/sharing/move invalidation,
  minimum/maximum format and exact byte limits, signed/float/extensible/unsigned
  padded WAV, generated FLAC/MP3, malformed chunks and frame alignment, duplicate
  or missing chunks, truncated/corrupt compressed input, non-finite/out-of-range
  samples, repeated decoder cleanup after post-initialization failures, concurrent
  independent decoding, Unicode/uppercase paths, missing/escaped assets, mismatched
  extensions, and retained samples after source replacement.
- `--audio-clip-test <directory>` writes and loads known WAV samples, then checks
  sharing, moved-source rejection, PCM limits, and missing-asset failures. Both
  configurations pass this executable acceptance alongside existing native
  window/Metal acceptance cases.
- Reviewed decoder initialization/uninitialization, immutable shared lifetime,
  integer bounds before allocation, pre-backend WAV format validation, strict
  file/container checks, and private C11 backend linkage. Formatting/whitespace
  checks pass. `otool` confirms both runtime executables retain the caller's LLVM
  libc++/libunwind and link native audio frameworks; neither has an LC_RPATH.
- Generated test media requires no installed encoder or external media rights.
  Tests/documentation preserve the backend's unsigned 8-bit normalization convention.
  Windows/Linux backend/link branches are implemented but remain unverified.

### Offline voice mixing

Added an independently owned miniaudio graph and scoped voice IDs. Voices retain
immutable Clips through pause, looping, and completion; explicit destruction
releases resources. Controls provide volume, pan, pitch, source-frame seek, and
master gain. Output is actual unclamped float PCM with mono/stereo conversion
and source-rate resampling. No playback device is opened in this stage.

- macOS Debug and Release each pass all 336 CTest cases (317 doctest cases and
  nineteen executable checks). Twenty new mixer tests cover known summed PCM,
  positive/negative stereo pan, voice/master gain, pause/resume position, looping
  changes preserving unconsumed samples, ended-voice restart/seek, rate/pitch
  frequency changes, resampled seek matching a fresh graph, format/settings and
  frame-buffer boundaries, exact maximum frame blocks, stale/foreign/non-reused
  handles, moved sources/services, retained Clip ownership, voice capacity,
  initialization/voice-preparation allocation failures, owner-thread checks,
  quiescent foreign destruction, independent threaded graphs, and repeated
  initialization/voice cleanup with allocation counters returning to baseline.
- `--audio-mix-test` checks two summed voices, pause/seek/master gain, stale ID
  rejection, final silence, and complete backend cleanup. Existing native
  window/Metal acceptance still passes; offline output does not validate a
  playback device or audible output.
- A private bridge manages the pinned backend's exposed input cache and resampler
  state at quiescent control boundaries. It prevents old cached audio after seek,
  preserves unread position during loop changes, and bypasses needless rate-1/
  pitch-1 resampling. The graph clock also advances through natural silence.
- Backend allocation reservations enforce per-mixer payload budgets. Preparation
  failure leaves the graph usable and releases partial native resources. Vendor
  file/resource management is disabled in favor of FileSystem/Clip asset loading.
- Reviewed private bridge/cache invariants, immutable sample lifetime, sound-before-
  buffer-before-engine teardown, partial initialization cleanup, budget reservation
  rollback, handle/thread identity, bounded output, diagnostic short-circuiting on
  backend failure, and callback ABI/configuration agreement. Formatting/whitespace
  checks pass. `otool` confirms caller-selected LLVM libc++/libunwind and native
  frameworks, with no LC_RPATH in either runtime. Windows/Linux remain unverified.
- Implementation-only edits rebuilt the mixer object and relinked executables;
  module interfaces and BMI consumers did not recompile. A final native-voice
  copy/move prohibition rebuilt cleanly, then all 34 audio unit cases and both
  audio runtime acceptance commands passed again in each configuration.

### Native queued audio output

Connected real native playback endpoints to bounded PCM queues, keeping mixing on
the owner thread and sample consumption on the device callback. Default backends
are CoreAudio/WASAPI/PulseAudio-ALSA-JACK, with explicit unavailable-backend errors
and no null-device fallback. Source format and owned device metadata are checked.

- macOS Debug and Release each pass all 352 CTest cases (332 doctest cases and
  twenty executable checks). New coverage includes five queue cases, nine native
  output cases, and an owned mixer-configuration query. Queue tests verify copied
  interleaved data, full/invalid writes without changes, silence on underrun,
  wrap/reset ordering, capacity boundaries, and 100000 concurrent ordered frames.
- Real CoreAudio tests cover start/stop/restart, actual PCM consumption, underrun
  accounting, explicit endpoint clipping, queue-full mix preflight without graph
  advancement, invalid PCM/settings/configuration, discarded-frame accounting,
  context budget/unsupported-backend failures, copied/moved ownership, owner-thread
  rejection, active-device destruction, foreign quiescent cleanup, and mono client
  rates of 8000/44100/192000 Hz. Native tests are serial and use endpoint software
  volume zero, independent of system/global volume.
- `--audio-output-test` consumes actual Mixer PCM through a real default endpoint,
  observes underruns, verifies stopped callback totals, and releases all tracked
  backend allocations. It stays muted and establishes native backend/callback
  delivery; it does not claim physical speaker audibility.
- Shared backend allocation callbacks preserve separate mixer/output counters.
  The output's preallocated SPSC queue uses release/acquire publication; callbacks
  allocate no memory, take no locks, and run no application code. Stop/uninitialize
  waits for backend callbacks before freeing their queue/context owner.
- Reviewed owner/consumer roles, wrapped frame indices, complete-buffer checks,
  pre-render queue/format validation, fault races, initializer rollback, callback
  userdata lifetime, stopped-only clearing, native gain, and teardown ordering.
  Unexpected stop/reroute/interruption faults are implemented and reviewed, but
  physical hot-plug/interruption has not been triggered in automated tests.
- Formatting/whitespace checks pass. Both runtimes retain the caller-selected LLVM
  libc++/libunwind and native audio frameworks, with no LC_RPATH. Implementation-only
  edits rebuild the output object and relink without rebuilding BMI consumers.
  Windows/Linux code paths remain unverified.

### Continuous audio playback

Added a dedicated worker that owns the tested Mixer/Output, prefills real PCM,
and continuously refills it independently of the client/game thread. Client
controls synchronously return executed worker results through a single owned
command slot. SDK creation, mutation, mixing, and teardown stay on the worker;
the native callback remains an allocation-free queue consumer.

- macOS Debug and Release each pass all 365 CTest cases (344 doctest cases and
  twenty-one executable checks). Twelve new playback cases cover actual PCM
  delivery during 250 ms client idle, retained clips and executed controls,
  invalid controls/capacity without worker faults, finished-voice restart,
  idempotent joined closure, terminal metadata/counters, copied/moved/foreign
  handles, client thread checks, foreign quiescent destruction, failed mixer/device
  initialization cleanup, scheduler/format boundaries, repeated worker/voice
  lifetimes, recoverable voice allocation failure, and small 192 kHz client buffers.
- `--audio-playback-test` observes continuing mixed/device frames and endpoint
  clipping while the client sleeps, then verifies joined cleanup and discarded
  queue accounting. It uses real CoreAudio with endpoint software gain zero;
  physical speaker audibility is not claimed.
- Refill work is bounded by block/batch limits, wakes for pending controls, and
  adapts waits to queued duration. A refill opportunity follows each command so
  control bursts cannot skip all mixing. Native underruns remain observable;
  scheduling is not a hard real-time guarantee.
- Shutdown joins device/mixing work before destroying native owners, resolves
  pending commands with owned errors, and retains fault/terminal snapshots.
  Queue data discarded by teardown is counted; lifecycle ownership is cleared
  only after actual destruction. Source controls affect future mixed blocks;
  already-queued audio retains its previous content.
- Reviewed startup publication, owned command arguments/promises, producer/client/
  callback thread roles, condition-variable predicates, queue capacity, command
  fairness, terminal snapshot races, fault propagation, move identity, and teardown
  ordering. Formatting/whitespace checks pass. Implementation edits rebuilt only
  the playback object and relinked consumers without recompiling module interfaces.
- The installed libc++ lacks move_only_function, so private owned polymorphic
  command objects carry packaged tasks instead; no toolchain/environment changes
  were needed. Both runtimes retain LLVM libc++/libunwind and native frameworks,
  with no LC_RPATH. Windows/Linux implementations remain unverified, and physical
  hot-plug/interruption remains outside automated coverage.

### Spatial audio voices and listener

Added bounded spatial descriptions and a single listener using the same right-
handed metre coordinates as the engine. Normalized source/listener rotations derive
direction and up; mono spatial voices support inverse/linear/exponential distance
attenuation, directional cones, and optional Doppler. Playback executes these
controls through its existing worker path, retaining owned query descriptions.

- macOS Debug and Release each pass all 382 CTest cases (360 doctest cases and
  twenty-two executable checks). Fifteen new spatial unit cases verify pose/
  velocity/rotation/attenuation/cone boundaries, actual stereo source-side changes,
  listener translation/yaw/roll, distance-model gain ratios and range clamps,
  directional cone orientation/outer gain, spatial disable restoring ordinary PCM,
  mono/pan conflicts and invalid-edit retention, normalized owned snapshots, actual
  Doppler frequency changes and disable, scope/thread/move checks, finite extreme/
  coincident output, seek/pitch/lifetime interaction, and independent mono-output
  listeners. A native case verifies executed Playback spatial/listener controls.
- `--audio-spatial-test` checks actual left/right PCM, listener yaw, and stale-voice
  rejection. Existing real CoreAudio and native window/Metal acceptance remains
  passing; tests do not claim physical hearing/direction or HRTF rendering.
- A regression caught an incorrect backend directional-attenuation parameter
  disabling channel direction gains. Correct spatial directional gains are now
  covered by source-side, listener-rotation, and mono-output PCM tests.
- Doppler refresh uses the pinned backend's calculation on a quiescent graph;
  pitch bypass stays enabled only when source/output rates match, pitch is one,
  and Doppler is unnecessary. Disabling Doppler resets its stale ratio. Input
  velocity bounds keep its denominator nonsingular; positions are not integrated
  implicitly by audio. Native gain smoothing remains in effect.
- Reviewed complete validation before mutation, checked matrix/basis derivation,
  listener/source coordinate agreement, mono/pan boundary contracts, copied
  descriptors, backend update faulting, Doppler/bypass transitions, and worker
  ownership. Formatting/whitespace checks pass. Both runtimes retain caller-selected
  LLVM libc++/libunwind and native frameworks, with no LC_RPATH. Windows/Linux
  remain unverified; physical device hot-plug/interruption remains outside automated
  coverage.

### Scene audio authoring and persistence

Added backend-independent VoiceSettings/SpatialProperties validation and an owned
AudioSource description with bounded UTF-8 relative asset paths. Scenes store
audio sources and independently selected primary listeners without opening files,
creating native voices, or initializing devices. Strict scene JSON now preserves
all these properties while accepting missing/null audio fields from older files.

- macOS Debug and Release each pass all 394 CTest cases (371 doctest cases and
  twenty-three executable checks). Eleven new unit cases cover owned snapshots
  with unchanged backend allocation counters, path/parameter boundaries, failed
  replacement retention, unique primary listener independent of camera, idempotent
  removal and subtree lifetime, foreign/stale/moved/thread handles, complete JSON
  round trips, old/null fields, malformed nested fields/types, multiple-primary
  rejection, and failed decode isolation.
- `--scene-audio-test <directory>` saves/loads UTF-8 asset descriptions, checks
  primary listener and parent/world-transform restoration with fresh scoped
  handles, rejects a malformed file while keeping existing scenes, and destroys
  an imported subtree without affecting the original. It tests descriptions and
  persistence; it does not claim automatic clip loading or live scene playback.
- Existing real muted CoreAudio, Cocoa window, and Metal pixel checks continue
  passing in both configurations. Release runs with assertions disabled. No
  sanitizers or environment/toolchain changes were introduced.
- Reviewed pure-data module dependencies, shared validator compatibility with
  Mixer, owned component snapshots, pre-mutation validation, primary uniqueness,
  thread/scope checks, entity cleanup, exact nested JSON fields, and independent
  candidate-scene construction. Formatting/whitespace checks pass. Both runtimes
  retain caller-selected LLVM libc++/libunwind and native frameworks, with no
  LC_RPATH. Windows/Linux implementations remain unverified.

### Prepared audio voice batches

Added a single batch path for voice creation, optional parameter/spatial/play
updates, removal, and listener edits. This supplies the resource transaction
needed by subsequent scene audio binding: validation and preparation complete
before existing graph state is changed, including replacement at full capacity.
Playback owns the batch arguments and executes it as one worker command.

- macOS Debug and Release each pass all 409 CTest cases (385 doctest cases and
  twenty-four executable checks). Twelve new unit cases cover ordered owned
  creations and actual summed PCM, unchanged graph time, cursor/handle retention,
  omitted properties and pause/resume, full-capacity replacement and retired IDs,
  combined pan/spatial transitions, actual stereo direction and listener edits,
  late invalid-input retention, duplicate/overlapping/foreign/stale handles,
  malformed clips/listeners/parameters, final capacity and empty/removal batches,
  partial-preparation allocation failure and exact PCM cursor continuation,
  finished-voice restart, and move/thread/repeated lifetime checks.
- The allocation failure case first proves that one temporary voice fits the
  transient budget, then exhausts it on the second. Eight failed batches restore
  live backend bytes/allocations, preserve the listener and old playing voice,
  and continue its known ramp samples without skipping/resetting the cursor.
  A later successful replacement confirms failed preparation consumed no IDs.
- Two native cases verify executed owned batches, full-capacity replacement,
  recoverable input/resource failures, retained voice/listener state, continued
  real CoreAudio frame delivery, and closure. They keep endpoint software volume
  zero and do not claim physical hearing. `--audio-batch-test` checks actual PCM,
  rejected-edit isolation, replacement, graph time, stale handles, and cleanup.
- Reviewed combined-state validation before allocation, unique/scoped handles,
  identity/capacity arithmetic, partial initialization RAII, preallocated map-node
  transfer, shared voice preparation/spatial code paths, graph quiescence, worker
  ownership, and explicit commit faulting. Unexpected backend commit failures are
  implemented/reviewed but not induced by the tests. Long batches may cause
  observable native queue underruns; hard real-time scheduling is not claimed.
- Formatting/whitespace checks pass. Release executes with assertions disabled;
  existing native audio/window/Metal checks remain passing. Both runtimes retain
  caller-selected LLVM libc++/libunwind and native frameworks with no LC_RPATH.
  No sanitizers or environment changes were introduced. Windows/Linux remain
  unverified.

### Scene audio runtime binding

Added an owned SceneAudio service for offline PCM and continuous native playback.
It loads real root-relative audio assets, shares immutable clips by authored path,
checks cache budgets, and synchronizes entity voices/listener through one prepared
backend batch. Component removal evicts unreferenced voices/assets after successful
synchronization; queries retain owned binding data without a scene reference.

- macOS Debug and Release each pass all 425 CTest cases (400 doctest cases and
  twenty-five executable checks). Thirteen new unit cases cover UTF-8 file loading,
  shared PCM cache accounting/output, component/subtree cleanup, cursor and manual
  pause/end retention, missing/corrupt-file isolation with entity/path diagnostics,
  stable cache and successful/failed reload, full-capacity asset replacement and
  playOnStart, inherited world position/listener rotation with actual stereo PCM,
  primary/mono/reflection/shear/pose rejection, velocity derivation from successful
  positions, failed-motion retention and teleport reset, tiny/invalid timesteps,
  asset/PCM/voice/source limits, listener switching, backend preparation rollback,
  move/scope/thread/reset checks, repeated resource lifetimes, and independently
  owned bindings after source scene/filesystem destruction.
- Two real muted CoreAudio cases verify continuous scene playback, failed asset
  edits, failed/successful native reset, rejected offline rendering with unchanged
  output, synchronized removal, failed asset initialization after native startup,
  joined worker teardown, and restored backend allocation counters.
- `--scene-audio-play-test <directory>` creates a real WAV and saved scene, loads
  that scene into the service, checks actual left/right PCM after pose edits,
  retains old playback on a missing asset, and verifies voice/cache cleanup.
  Existing native audio, Cocoa window, and Metal acceptance continue passing.
- An EOF regression exposed a retained backend start flag after a finished voice;
  voice queries and playing counts now exclude ended voices. PCM/end tests verify
  synchronization does not restart the finished sound.
- Reviewed bounded candidate decoding/cache ownership, shared path references,
  UTF-8 filesystem construction, complete preflight before backend mutation,
  preallocated binding publication, world TRS/velocity constraints, snapshot/thread/
  scope rules, worker execution, factory failure cleanup, and reset replacement.
  Native reset may briefly overlap old/new endpoints and is not a seamless
  transport transition. Unexpected backend commit faults remain explicit and
  reviewed; they are not induced by automated tests.
- Formatting/whitespace checks pass. Release runs with assertions disabled;
  caller-selected LLVM libc++/libunwind and native frameworks remain linked with
  no LC_RPATH. No environment changes or sanitizers were introduced. Windows/Linux
  implementations remain unverified.

### Bounded ESM script runtime

Added an owned QuickJS-NG realm with project-relative ESM imports, scoped module
handles, cached live bindings, JSON export calls, Promise job execution, and
contextual errors. Interfaces and implementation remain separate; vendor values
stay private and explicit RAII releases them before the context/runtime.

- macOS Debug and Release each pass all 452 CTest cases (426 doctest cases and
  twenty-six executable checks). Twenty-six new unit cases verify file-backed
  relative/parent/cyclic/dynamic imports, imported-entry cache reuse and shared
  mutable state, UTF-8 paths/data, undefined/null distinction, top-level await,
  real async results, contextual recoverable exceptions with retained mutations,
  malformed/syntax/missing-source failures, path/attribute/NUL rejection, foreign/
  stale/moved/thread handles, JSON argument/count/result boundaries, strict
  non-JSON/cyclic/nested-promise/symbol-key rejection, copied unterminated argument
  views, empty/NUL buffer rejection, infinite-call/initializer interruption,
  diagnostic getters, stack overflow, bounded allocation failure and cleanup,
  pending promises/unhandled rejections/infinite jobs, handled rejections,
  shared execution/serialization job budget, no implicit host I/O bindings,
  repeated and foreign quiescent destruction, and failed partial initialization.
- `--script-test <directory>` writes and loads an actual ESM graph, calls async
  exports with JSON, observes shared counters across calls, recovers from a clean
  exception, interrupts an infinite function, and restores live allocator counters.
  Existing real muted CoreAudio/Cocoa/Metal acceptance remains passing.
- The existing baseline pins QuickJS-NG 0.16.2; a versioned overlay revision adds
  only full-length NUL checks for static/dynamic import specifiers before the
  upstream C-string resolver can alias another filename. Both paths are exercised
  by regression tests. Project-wide vcpkg configuration selects the overlay;
  Debug configuration succeeds without a cached overlay-port setting. The core
  library is linked privately without its libc/OS module feature.
- VM allocator payload, stack, source/JSON, module/job, and cooperative execution
  limits are explicit. SDK preflight out-of-memory errors are classified even when
  they occur before the custom allocator callback. Ordinary clean exceptions do
  not roll back script mutations; loader/resource/job faults require a new realm.
  These bounds are not a hostile-code process sandbox or a guarantee that native
  filesystem/compilation/destruction completes within a real-time deadline.
- Reviewed allocator overflow/alignment/realloc accounting, C ABI exception
  containment, reference lifetimes and destruction order, module cache identity,
  root/UTF-8/terminated-buffer boundaries, Promise/rejection ownership, cooperative
  deadlines covering diagnostic getters, shared job limits, all SDK exception
  results, strict JSON conversion, scope/thread/reentry guards, and fault recovery.
- Formatting/whitespace checks pass. Release runs with assertions disabled.
  Caller-selected LLVM libc++/libunwind/native frameworks remain linked, with no
  LC_RPATH. No sanitizers or environment changes were introduced. Windows/Linux
  implementations remain unverified.

- Normalized blank context prefixes in the dependency diff file after Git reported
  them as trailing whitespace during staging. Both original/normalized patch files
  pass `git apply --check` against the pinned source and produce byte-identical
  patched C source. No functional SDK/engine changes result; Git whitespace checks
  now include the added patch file and pass.

### Native tool process execution

Added a synchronous explicit-executable process boundary for compiler/tool clients,
with literal UTF-8 arguments, child-only cwd, inherited environment, EOF stdin,
bounded separate raw output, actual normal exit status, and contextual failures.
POSIX process groups and Windows jobs own cancellation/lifetime; no shell is used.

- macOS Debug and Release each pass all 467 CTest cases (440 doctest cases and
  twenty-seven executable checks). Fourteen new focused cases verify literal
  empty/space/quote/backslash/newline/Unicode/shell arguments, equivalent child cwd
  with unchanged parent cwd, nonzero status and distinct diagnostics, EOF stdin,
  binary output, concurrent large streams beyond pipe capacity, combined/exact
  output limits, timeout/reaping, descendants retaining pipes after root exit,
  invalid/missing paths/encoding/count/budgets, repeated concurrent independent
  calls, Unicode executable paths, signals, unrelated descriptor closure, closed
  parent stdin, ignored SIGCHLD rejection, and inherited unchanged environment.
- `--process-test <fixture> <directory>` runs actual child programs and checks
  status, raw streams, EOF, large capture, timeout and restored ownership counters.
  Tests use a dedicated target to isolate fixture-path definitions from other unit
  consumers. Existing real CoreAudio/Cocoa/Metal and ESM checks remain passing.
- POSIX waits preserve the unreaped leader through group teardown to prevent PID
  reuse; pipe descriptors are normalized above stdio and streams drained fairly.
  macOS spawn actions support the current standard chdir API plus a narrow older
  API fallback. Current glibc Linux closes unrelated descriptors with closefrom.
  Windows code supplies an explicit application, writable quoted UTF-16 arguments,
  a stdio-only handle list, suspended job assignment, native capture and job cleanup.
  Windows/Linux branches have been reviewed but not compiled/executed locally.
- Reviewed validation/size arithmetic, literal argument separation, immutable
  parent state, EOF/capture fairness, spawn/action/attribute/descriptor ownership,
  partial startup cleanup, child/group identity, signal ownership, timeout/limit
  teardown and concurrent lifetime. POSIX descendants must not detach, application
  handlers must not reap owned children, and kernel startup/teardown is not a hard
  real-time guarantee. No partial output is returned on runner failure.
- Formatting/whitespace checks pass. Release runs with assertions disabled;
  runtimes retain caller-selected LLVM libc++/libunwind with no LC_RPATH. No
  environment overrides or sanitizers were introduced.

### Native TypeScript compilation

Added `Dot.Script.Compiler` with the official standalone TypeScript 7.0.2 Go
compiler, bounded source capture and independent owned ESM/source-map generations.
The module interface and implementation are separate; applications provide an
explicit tool path and retain a CompiledProgram owner while a Runtime uses it.

- macOS Debug and Release each pass all 484 CTest cases (456 doctest cases and
  twenty-eight executable checks). Sixteen new focused cases cover actual strict
  typed ESM imports/promises, Unicode paths/mts/declarations, source-map mappings
  and embedded original sources, type diagnostics with unchanged old bundle and
  continuing runtime state, copy/move/last-owner/foreign destruction cleanup, lazy
  imports after authoring removal, ignored project configuration and unavailable
  Node globals, missing/external/bare/side-effect imports, declaration-only/empty
  projects, malformed UTF-8/NUL/paths/extensions/symlinks, exact source/output
  bytes and file/entry/depth limits, tool version/launch failures, timeout, bounded
  diagnostics, zero/partial/unexpected output, and concurrent real compilations.
- `--typescript-test <absolute-compiler> <directory>` performs real type checking,
  ESM/source-map emission, QuickJS execution, deliberate TS2322 diagnostics,
  continued previous-runtime execution, generation deletion and restored native
  process/VM ownership counters. Test-only tool fixtures exercise abnormal tool
  behavior; the normal path and CLI acceptance use the actual official compiler.
- The host vcpkg dependency pins platform-specific arm64 macOS and x86_64
  Windows/Linux archives with independent SHA512 values and retains adjacent
  standard declarations, Apache license and NOTICE. No Node/npm runtime is used.
  Package copying preserves executable permissions. Automatic vcpkg rpath
  rewriting is disabled for this standalone tool because it modified the signed
  macOS binary and caused exit 137. Final Debug/Release binaries are byte-identical
  to the verified official archive; signature verification passes outside the
  restricted sandbox (the sandbox reports failure for the original binary too).
- Compilation creates its own strict ES2022/ESNext configuration and explicit
  source list, with noResolve/no ambient package types, forced modules, isolated
  modules, verbatim module syntax, noEmitOnError and checked side-effect imports.
  No authoring tsconfig is executed. Expected actual output names, regular types,
  UTF-8 and bytes are inspected before returning success. Failed candidates do not
  mutate successful generations or authoring sources; all invocations own their
  temporary source/config/output tree and child process independently.
- Reviewed path/encoding and symlink boundaries, source/output arithmetic,
  deterministic relative names, diagnostics and version-probe handling, generation
  creation/partial failure/destruction, ownership while lazy imports remain,
  concurrency, compiler configuration and cross-platform tool selection. Capture
  prohibits concurrent source edits; limits do not provide OS memory/disk quotas
  or hostile filesystem isolation. Output size is checked after compilation;
  last-owner directory cleanup is best effort. Tests allow normal macOS initial
  tool startup before exercising a deterministic sleeping-child timeout.
- Formatting/whitespace checks pass. After the final license/format-only updates,
  the seventeen compiler/CLI checks pass again in Debug/Release. Full native
  CoreAudio/Cocoa/Metal checks remain passing. Release uses `NDEBUG`; caller LLVM
  libc++/libunwind linkage and no LC_RPATH are retained. No sanitizers or compiler/
  linker environment edits were introduced. Windows/Linux code and packages have
  been reviewed but not compiled/executed locally.

### Registered script services and core scene SDK

Added an owned native ESM host bridge and the first real engine script services,
with typed Entity/scene wrappers compiled by the official TypeScript tool. This
stage connects scripts to native scene state and actual rendered pixels; it does
not yet attach script instances as components or implement lifecycle/hot reload.

- Final macOS Debug and Release each pass all 513 CTest cases (483 doctest cases
  and thirty executable checks). Twenty-seven new focused cases cover real native
  ESM/static/dynamic/top-level-await transport, owned callbacks, missing/bounded
  modules, all Core error codes/context, contained standard/nonstandard C++ throws,
  strict JSON/Unicode/finite results and undefined/null, invalid arguments before
  side effects, method/registration limits, shared host budgets through jobs and
  serialization, recursive argument conversion, native deadline observation,
  reentry rejection, prototype-setter-independent errors, bounded diagnostics,
  deep valid JSON rejected by VM stack with preserved method context, and callback/
  VM cleanup after moves, partial initialization and foreign quiescent destruction.
- Scene cases cover all eleven methods, owned snapshots, quaternion normalization,
  unchanged state after invalid transforms/arguments, exact ID/object shapes,
  foreign/stale IDs, values above JavaScript integer precision, parent/unparent/
  cycle/subtree semantics, shared scene lifetime, moved-scene invalidation and
  native thread restrictions. Four SDK cases use actual TypeScript compilation
  and QuickJS execution to verify native edits, snapshot assignment, recoverable
  failures, continued state, compile-time ID/transform constraints and contextual
  uncaught failures retaining earlier successful native operations.
- `--script-host-test <compiler> <sdk-root> <directory>` compiles the actual source
  SDK, creates/removes a child, edits hierarchy/names, rejects an invalid transform
  and applies sixty typed ticks to a native cube. `--script-host-render-test` adds
  real Metal readback before/after that movement, verifying changed pixels and the
  retained native mesh/cache. Both verify compiler generation/process/VM cleanup;
  existing actual CoreAudio/Cocoa/Metal acceptance checks remain passing.
- Runtime copies an immutable registry of up to 256 canonical named callbacks.
  Only registered realms expose the reserved `dot:host` module; no host globals or
  implicit OS services appear. The native module counts toward the module quota.
  Arrays/results are checked through bounded finite scalar-Unicode JSON. Catchable
  DotHostError objects carry stable Core code names and method context, with owned
  properties that bypass script Error prototype setters. C callbacks contain C++
  exceptions and correctly transfer/free SDK values and native module exports.
- Host-call budgets default to 256 per public operation (range 1..65536), shared
  across initializers/jobs/serialization. Catching budget failures cannot hide
  realm failure. Argument getters/toJSON can recurse, so limits are checked again
  before native side effects. Native calls are synchronous and not preempted;
  observed deadlines fault after return. Callback allocations and JSON transport
  containers remain outside the VM payload quota. Earlier successful operations
  and candidate module-initializer native side effects are not rolled back.
- Core scene bindings retain the original shared scene identity and invoke the
  same native operations as C++ consumers. IDs use scoped canonical decimal string
  pairs; complete finite float-range TRS shapes are validated before mutation.
  SDK Entity wrappers own copied IDs/snapshots, and getters only commit changes
  upon assignment. Component-specific camera/mesh/physics/audio APIs remain future
  work rather than being implied by the core entity snapshot.
- Reviewed registration/context/move/destruction ownership, every SDK exceptional
  result and consumed export/property value, loader budgets/reserved specifiers,
  JSON bounds/encoding/depth failures, native callback exception containment,
  deadline/recursive/reentry guards, ID precision/scope, transaction boundaries,
  native thread rules, typed SDK contracts and real GPU acceptance lifetimes.
  Formatting/whitespace checks pass. Module interfaces and implementations remain
  separate; vendor headers stay private. Release runs with NDEBUG; LLVM libc++/
  libunwind remain caller-selected with no LC_RPATH. No sanitizers or environment
  edits were introduced. Windows/Linux use the common implementations but remain
  uncompiled/unexecuted locally.

### Persistent script instances

Added runtime-owned JavaScript objects with scoped monotonic InstanceIds, native
class construction, inherited/private-state method calls, optional hook discovery
and explicit reference release. Shared argument/result helpers keep exported
function and instance operations under the same established transport/job budgets.

- macOS Debug and Release each pass all 527 CTest cases (496 doctest cases and
  thirty-one executable checks). Thirteen focused cases verify independent private
  fields/inheritance/this, deliberate factory aliasing, async methods, capacity
  rejection before constructor side effects, nonreused released handles, partial
  constructor failures and prior mutations, nonconstructable exports and handled
  rejected Promise constructor results, bounded/terminated argument views, invalid
  arguments before getter side effects, optional/missing/noncallable hooks and
  drained getter jobs, contextual ordinary/async failures and undefined/null,
  foreign/moved/thread-scoped handles, foreign quiescent destruction, constructor/
  getter deadlines, cleanup after realm fault, shared jobs/strict serialization,
  failed allocation/cycles/repeated initialization and active-object reentry guards.
- `--script-instance-test <compiler> <sdk-root> <directory>` compiles real typed
  classes using the scene SDK, creates two instances with independent private
  counters and inherited start/stop hooks, applies sixty async updates to each
  native entity with different speeds, checks clean error recovery/context,
  explicitly stops/releases objects and verifies stale handles and compiler/
  process/VM cleanup. The caller invokes hooks explicitly; this acceptance does
  not imply a component authoring model or automatic lifecycle scheduler.
- Default live capacity is 1024, configurable to 1..10000. Handle values are never
  reused within a realm and moves preserve identity. Constructors are synchronous;
  returned promises are marked handled and bounded jobs drained before rejection.
  Standard JS constructor semantics, including deliberately shared return objects,
  remain intact. Ordinary failures do not undo earlier script/native mutations.
- Method argument validation precedes property lookup. Optional hook discovery is
  observable script execution: getters may enqueue jobs, present nonfunctions fail,
  and invocation performs a fresh lookup. Release removes only host ownership and
  never runs script callbacks; other JS references/cycles may retain the object.
  Release remains available after realm failure, on the owner thread without
  reentry. Instance roots release before namespaces/context/runtime destruction.
- Reviewed SDK constructor/method result ownership, argument roots and native argv
  lifetime, receiver/private-field preservation, monotonic scope/capacity checks,
  partial-construction cleanup, Promise/rejection ownership, getter/job behavior,
  deadlines/resource faults, active map lifetime/reentry rejection, moves/thread
  restrictions, shared helper parity and cleanup of cyclic graphs. Full native
  CoreAudio/Cocoa/Metal and previous SDK/host checks remain passing. Formatting and
  whitespace checks pass; Release uses NDEBUG. Caller LLVM libc++/libunwind and no
  LC_RPATH remain unchanged. No sanitizers/environment changes were introduced;
  Windows/Linux common code remains uncompiled/unexecuted locally.

### Authored script components and lifecycle dispatch

Added owned script authoring to native entities and version-1 scene persistence,
then connected the compiled generation and persistent object layer through a
deterministic SceneScripts lifecycle service and typed source SDK base component.

- Final macOS Debug and Release each pass all 553 CTest cases (520 doctest cases
  and thirty-three executable checks). Eight authoring cases verify canonical
  JSON/module mapping, portable paths/export names, finite scalar Unicode objects,
  duplicate/depth/exact-byte boundaries, no VM initialization while editing,
  owned snapshots and unchanged invalid edits, idempotent removal, scene roundtrip/
  fresh scope/older absent-null fields, strict schema failures, stale/moved/foreign
  and thread-bound edits. Sixteen real TypeScript lifecycle cases cover independent
  state, start/update/fixed/stop, once-only starts/stops, disabled missing sources,
  enable/disable/property replacement, native mutation rejection during initializers/
  constructors/getter discovery, unchanged old bindings on ordinary preparation
  failure, isolated callback/cleanup failures, all starts before updates, immediate
  self-deletion pruning, full-capacity staged replacement and time boundaries,
  realm-fault cleanup, nonvoid/noncallable hooks, moves/thread/destruction behavior,
  caller-released generation/scene lifetime, stop-induced owner deletion and
  preparation loader faults releasing previous active handles and replacement-stop
  resource faults releasing newly prepared objects.
- `--script-components-test <compiler> <sdk-root> <directory>` saves an authored
  native scene, reloads it with fresh scope, compiles actual typed classes and
  automatically starts/updates/fixed-updates/stops two attached components. Sixty
  async updates preserve independent native transform state and cleanup counters.
  `--script-components-render-test` also verifies actual Metal pixel changes from
  that lifecycle-driven movement and the shared native mesh cache. Existing native
  CoreAudio/Cocoa/Metal and earlier compiler/host/instance checks remain passing.
- Component descriptions store a source-root-relative .ts/.mts path, exported
  class identifier, canonical property object and enabled flag. Validation reads
  no code/initializes no runtime. Property JSON is at most 64 KiB/depth 32, finite
  scalar Unicode without duplicates. SceneFiles adds an optional/null script field
  without dropping older version-1 documents; snapshots and loaded data are owned.
- SceneScripts owns a CompiledProgram copy/realm and original shared scene identity.
  Preparation loads and constructs changed candidates and discovers optional hooks
  before old objects are retired. Native scene writes are denied while preparing;
  ordinary preparation failure keeps existing objects, while loader/resource faults
  release handles. JS module-global mutations are not rolled back. File edits do
  not reload cached definitions; source hot reload remains separate work.
- All pending starts run in creation-ID order before update/fixed-update callbacks.
  Hooks return void/Promise<void>; other results and noncallable fields fail. Ordinary
  callback failures are explicit report entries, attempt one cleanup and isolate
  that description while other components continue. Missing owners are pruned;
  stopping old scripts may remove candidate owners, which are rejected instead of
  activated. Realm faults stop further dispatch and release handles. Explicit stop
  is terminal/idempotent; quiescent destruction releases storage without callbacks.
- Default authoring capacity is 512 (range 1..5000), with at least twice that many
  Runtime slots for full-capacity replacement. Invalid budgets and finite seconds
  outside [0,1] fail before lifecycle changes. Reports distinguish completed dispatch
  from script success through contextual entity/phase failures and realm state.
  Unexpected native exceptions fault the service and retain cleanup/statistics.
- Reviewed schema/UTF-8/numeric/depth/duplicate limits and compatibility, module
  dependency direction, source mapping, captured ownership and destruction order,
  staged object cleanup/node transfer, constructor mutation guards, callback order,
  stale-owner handling, bounded handle/report state, explicit stop/fault recovery,
  typed SDK ABI and actual native/GPU acceptance. Formatting/whitespace checks pass;
  interfaces and implementations remain separate. Release uses NDEBUG; caller LLVM
  libc++/libunwind linkage and no LC_RPATH remain unchanged. No sanitizers/environment
  edits were introduced; Windows/Linux common implementations remain unverified.

### Source hot reload and state migration

Added independent-generation/realm replacement with optional state migration,
immutable compiler source snapshots and a portable content-polling LiveScripts
service. Failed compilation or candidate preparation keeps old code available;
successful switching changes behavior while retaining compatible running state.

- macOS Debug and Release each pass all 568 CTest cases (533 doctest cases and
  thirty-five executable checks). Thirteen focused cases cover exact source/path
  comparison and timestamp-only changes, compiling captured bytes after authoring
  changes, moved snapshots and rechecked limits, private state migration without
  duplicate startup/old cleanup, constructor/initializer/import failure with
  continuing old updates, native-write denial during save/load, missing/undefined/
  nonvoid/oversized state contracts, faulted-realm recovery and new pending entities,
  actual compiler-error retention and no unchanged failed-source recompilation,
  poll intervals/force retries/delta validation, source removal/new imports and
  repaired identical capture, moves/thread/lifetime/resource cleanup, component
  disable/removal/type changes and committed cleanup failures pruning lost owners.
- `--script-reload-test <compiler> <sdk-root> <directory>` exercises real source
  editing and automatic capture/compilation: thirty old-code updates, a deliberate
  TS2322 failure while old code continues, a new realm restoring a private counter,
  thirty updates with changed native movement, explicit stop and compiler/process/
  VM teardown. `--script-reload-render-test` additionally verifies actual Metal
  pixels before/after reloaded behavior. Previous CoreAudio/Cocoa/Metal, lifecycle,
  compiler/host/instance and persistence checks remain passing.
- SourceSnapshot shares immutable bounded captures. Exact source path/content
  comparisons avoid timestamp-only recompilation and hash collisions. Emission
  uses the captured bytes, with source count/byte limits rechecked; enumeration/
  depth limits apply during capture. Original compileProject uses the same path.
- Reload completes candidate initialization before any old state export. Compatible
  running objects can export finite scalar-Unicode JSON through onSaveState and
  restore it through a required onLoadState, bounded by Runtime transport and an
  aggregate budget (default 8 MiB, 1 byte..64 MiB, codec depth 64). Native writes are
  denied during candidate preparation/export/import. Save hooks must be observational;
  arbitrary JS mutations cannot be rolled back and resource failures may fault the
  old realm. Faulted realms can be replaced without exporting state.
- Complete private owners are swapped so borrowed Runtime addresses remain stable.
  Compatible running components skip old stops/new starts; new descriptions start
  normally. Removed/disabled/changed types stop after commit and report cleanup
  errors explicitly. Cleanup may remove candidate owners, which are pruned. Native
  exceptions after commit identify that commit and fault the new service. Old
  realm/program destruction follows migration/access/active scopes; candidate
  failure releases all temporary objects/output generations.
- LiveScripts polls exact contents at 100 ms by default (0..60000 ms), compiles
  new snapshots and dispatches the old or committed generation. It remembers failed
  contents to avoid repeated compiler invocations; forced polling retries unchanged
  contents. Capture errors clear on identical valid restoration without recompiling.
  Status/stats retain actual diagnostics, checks/changes/commit, cleanup reports and
  compiler/reload counts. Compilation is synchronous and not a hard frame-time
  guarantee; native captures/transport allocations are outside the VM quota.
- Reviewed snapshot immutability/limits, generation lifetime, complete-owner swap/
  destruction order, observational export and isolated candidate imports, matching
  scope/type/start status, native mutation guards, aggregate/transport validation,
  failed attempts/source restoration, cleanup after commit, thread/reentry/moves,
  reporting distinctions and actual GPU acceptance. Formatting/whitespace checks
  pass. Release uses NDEBUG; caller LLVM libc++/libunwind and no LC_RPATH remain
  unchanged. No sanitizers/environment changes were introduced. Windows/Linux
  common C++ implementations remain uncompiled/unexecuted locally.

### Component authoring script APIs

Extended the native scene bridge and typed SDK with complete camera, mesh, rigid
body, audio source, audio listener and script descriptions, primary lookups and
entity existence. Lifecycle scripts can author and edit these native components;
preparation/state migration remains read-only.

- macOS Debug and Release each pass all 582 CTest cases (545 doctest cases and
  thirty-seven executable checks). Twelve focused behavioral cases cover all six
  descriptions, null removal, owned snapshots, native defaults and independent
  nested factory values, variants/numeric/asset/property constraints, strict
  argument and nested schemas, duplicate/deep JSON, primary uniqueness, foreign/
  stale IDs, real TypeScript type errors and recovery, lifecycle edits/self-disable,
  constructor/save/load read access and write denial, and thirty repeated shared
  scene ownership/release cycles without VM/mixer/output backend allocation.
  After embedded TypeScript formatting, all eight affected Debug compiler/lifecycle/
  executable checks passed again; Release full checks used the final formatted sources.
- `--script-authoring-test <compiler> <sdk-root> <directory>` creates six component
  descriptions through actual compiled TypeScript, checks validation/primary IDs,
  saves/loads the authored scene with fresh scope and executes lifecycle edits/
  removal. `--script-authoring-render-test` also verifies actual Metal pixels for
  the script-created camera/blue mesh, green tint assignment and hidden geometry,
  including eviction of the last hidden mesh from the renderer cache. Compiler
  generations, process handles/children and VM allocations return to baseline.
- SceneHost now has sixteen methods. Component reads and entity snapshots are
  owned nullable descriptions; complete assignments delegate to native validation
  before changes. Unknown/missing fields, enum values, wrong scalar types and
  float overflow fail. Script properties cross as an object and retain native
  depth-32/64-KiB limits; transport parsing rejects duplicates/depth beyond 64.
  Existence is false only for missing same-scene IDs; primary lookups map the two
  native absence codes to null. Foreign IDs and moved/thread-invalid scenes fail.
- SDK factories return native-default descriptions with independent default nested
  objects. Camera/mesh/body overrides replace whole fields; caller override objects
  remain ordinary values until assignment copies them. Authoring does not read
  assets or initialize physics/audio/script backends. Simulation, ray/impulse and
  audio playback controls remain separate script API work. Self-disable is consumed
  when the next lifecycle dispatch synchronizes authoring.
- Reviewed schema conversion/native mapping, finite/scalar/depth/size limits,
  shared scene identity/lifetime, primary uniqueness, failure-before-mutation,
  nullable snapshots, factory ownership/default parity, read-only guard coverage,
  deferred description synchronization and real GPU visibility/cache behavior.
  Formatting/whitespace checks pass; non-template module implementations remain
  separate from interfaces. Release uses NDEBUG and the caller's LLVM libc++/
  libunwind; no LC_RPATH, sanitizers or environment changes were introduced.
  Windows/Linux common C++ implementations remain uncompiled/unexecuted locally.

### Published input and native service composition

Added an owned InputSource and typed input SDK, a frame preparation callback in
the application loop, and explicit native host access classification/composition
for entity-attached and live-reloaded script services.

- macOS Debug and Release each pass all 597 CTest cases (559 doctest cases and
  thirty-eight executable checks). Fourteen focused cases cover all 65 native
  key names and five mouse buttons, owned point/text/button snapshots, non-consuming
  edges, same-frame press/release, repeats and focus loss, unavailable initial reads,
  malformed/unsupported arguments and UTF-8/text limits with old-frame retention,
  moves/thread/publisher/binding lifetime, actual typed SDK names/type failures,
  multiple fixed steps sharing input, continued old input after failed compilation,
  migrated private state with a moved live publisher, read/write guards in
  constructors/save/load, invalid/duplicate/over-capacity service registries before
  VM allocation, and prepareFrame ordering/focus pause/failure/exception/stop/quit.
- `--script-input-test <compiler> <sdk-root> <directory>` injects deterministic SDL
  key/button/motion/wheel/Unicode text/focus events into the application event pump.
  The preparation callback publishes before fixed/update dispatch; actual compiled
  classes edit native transforms and preserve private counters through changed source
  and realm replacement. Repeat does not re-press, focus loss releases held input,
  and compiler generations/process/VM resources return to baseline after explicit
  stop. CTest uses the dummy video driver for this input acceptance; physical
  keyboard delivery is not asserted. Existing Cocoa/Metal/CoreAudio cases pass.
- InputSource retains an owned snapshot independently of window/scene/realm lifetime.
  Successful publication increments a decimal-string uint64 index; validation and
  allocation happen before nonthrowing JSON replacement. Default text capacity is
  64 KiB (1 byte..1 MiB); custom limits still need compatible Runtime transport
  budgets. Reads never consume frame edges; the application owns event/reset/
  publication timing, and shared fixed steps see the same edges. Moves preserve
  bindings; after publisher destruction, callbacks retain the last published frame.
- prepareFrame runs after events, including paused frames, and before any fixed/
  per-frame update. Quit skips it; failure/exception/stop blocks later callbacks.
  The input reference is borrowed for the call only. Unicode delivery uses the
  existing explicit window text-input switch; no window/event pumping is created
  by a script input publisher.
- HostAccess is trusted native metadata. Scene/input reads are marked ReadOnly;
  default ReadWrite methods are denied during preparation and state save/load.
  Direct Runtime calls do not impose this lifecycle policy or verify a native
  callback's observational promise. SceneScriptsConfig owns additional callbacks
  across reloads; names/callbacks/access/total capacity are validated without
  overriding existing scene methods. Empty callbacks remain invalid before guards
  are created. LiveScripts retains the same dependencies across source failures
  and successful replacements.
- Reviewed native name completeness/uniqueness, owned snapshots/publication commit,
  text/JSON/finite-value boundaries, index exhaustion, moved/thread/dependency
  lifetime, callback scheduling/early stops, default-deny classifications, registry
  preflight and original callbacks retained across fresh guards/realms. Formatting
  and whitespace checks pass; module interfaces and implementations remain separate.
  Release uses NDEBUG and caller LLVM libc++/libunwind, without LC_RPATH or
  sanitizers/environment changes. Windows/Linux common implementations remain
  uncompiled/unexecuted locally.

### Runtime physics script APIs and contact publication

Added an owned PhysicsSource around the real scene simulation, eight native
methods and typed global/entity conveniences for runtime body state, velocity,
angular velocity, impulses, kinematic targets, rays and non-consuming contact batches.

- macOS Debug and Release each pass all 612 CTest cases (572 doctest cases and
  forty executable checks). Thirteen focused behavioral cases cover real Jolt
  motion/pose writeback, owned snapshots, kinematic targets and ray hit/inside/miss
  geometry, malformed/numeric/shape/transport limits with retained motion, foreign/
  missing/removed owners and stale ray bindings, owned repeated contact reads and
  removed IDs, publication-budget fault/diagnostic/reset recovery, moves/thread/
  scene/binding lifetime, failed synchronization/reset retaining motion/generation/
  batch, actual TypeScript controls and type errors, catchable failures without
  fake bodies, read access/write denial during constructors and state migration,
  moved publishers/unchanged simulation through reload, and two components reading
  the same contact batch independently.
- `--script-physics-test <compiler> <sdk-root> <directory>` compiles real typed
  classes controlling linear/angular velocity, impulses and kinematic movement.
  Thirty fixed frames verify actual native poses, rays and sensor batches. Changed
  source halfway through migrates a private counter without resetting native motion;
  explicit stop releases VM handles and compiler generations/process resources
  return to baseline. `--script-physics-render-test` also verifies actual Metal
  pixels changed by script-controlled physical movement. Existing native Cocoa/
  Metal/CoreAudio and earlier input/lifecycle/compiler cases remain passing.
- PhysicsSource owns the original shared scene, native SceneSimulation and last
  encoded contact batch. Moves preserve callbacks; retained bindings keep native
  dependencies alive. Moving/replacing the captured Scene invalidates the source.
  Queries show last synchronized state; unsynchronized/new/missing/foreign bodies
  fail explicitly. Rays on removed owners require synchronization instead of
  silently becoming misses. Authored poses/components apply on native sync/step;
  controls apply to the real synchronized bodies without implicit advancement.
- Native sync/step drains events once for a published batch shared by all scripts.
  Reads do not consume and return owned data, including historical removed IDs,
  nullable geometry/end reasons and decimal-string step/batch/generation values.
  Default contact JSON capacity is 1 MiB (1 byte..16 MiB, must hold an empty batch)
  and must fit Runtime transport. Capture/serialization/budget failures after
  physical mutation explicitly fault the source, reject stale batches and require
  reset; body/stat diagnostics remain available. Reset prepares its encoded empty
  batch before world replacement and retains old state on preparation failure.
- Four queries are ReadOnly; four controls are ReadWrite and denied during script
  preparation/export/import. Script reload retains native dependencies and motion;
  scripts cannot directly advance or reset the world. Native scalar/vector,
  canonical ID, UTF-8, duplicate/depth/byte and existing motion/speed/pose constraints
  remain checked at the bridge/subsystem boundaries.
- Reviewed native state mapping and finite backend guarantees, thread/scene scope,
  original dependency/destruction order, prepared resets, post-advance publication
  fault semantics, diagnostic access, contact ownership/IDs/generation, serialization
  and transport budgets, guard classification and actual CPU/GPU acceptance.
  Formatting/whitespace checks pass; interfaces and implementations remain separate.
  Release uses NDEBUG and caller LLVM libc++/libunwind, without LC_RPATH,
  sanitizers or environment changes. Windows/Linux common implementations remain
  uncompiled/unexecuted locally.

### Runtime audio script APIs and stop/cursor controls

Added native stop/rewind and source-cursor queries, an owned AudioSession for real
offline/native scene audio, eight host methods and typed global/entity controls.
Script hot reload keeps existing voices, paused cursor and master gain.

- macOS Debug and Release each pass all 627 CTest cases (585 doctest cases and
  forty-two executable checks). Thirteen focused behavioral cases cover native
  stop/pause/seek/restart/end state and independent voices, real PCM gain/silence,
  owned voice/clip snapshots, strict canonical frame/argument/volume/transport
  boundaries, asset/sync/reset failure preserving cursor/generation/gain, foreign/
  missing/removed owners, spatial listener/source world pose/derived velocity,
  moves/thread/scene/binding/backend lifetime and failed initialization cleanup,
  actual TypeScript controls/type errors and catchable missing/invalid voices,
  preparation/save/load reads and write denial, cursor preserved across reload/
  moved publisher, synchronized settings retaining manual pause, and one-command
  native worker stop with stable zero cursor afterward.
- `--script-audio-test <compiler> <sdk-root> <directory>` compiles real typed classes,
  checks invalid seek, verifies play/seek/master gain through PCM and pause through
  silence, changes source and migrates private state while keeping the same paused
  voice/cursor/gain, then stops and releases resources. `--script-audio-play-test`
  exercises the same scripts on actual CoreAudio with endpoint software gain zero,
  confirms delivered frames and native rejection of offline rendering. Compiler
  generations, process/VM handles and mixer/output backend allocations return to
  baseline. Earlier Metal/Cocoa, physics/input/lifecycle checks remain passing.
- Mixer/Playback/SceneAudio stop pauses and rewinds in one graph operation; native
  playback performs it as one worker command without interleaved mixing. Backend
  failures fault the mixer explicitly. VoiceState now reports miniaudio's source
  read cursor, including possible prefetch/resampling; it does not represent
  audible device presentation. Frames/cursors/lifetime counters cross the script
  boundary as unsigned decimal uint64 strings, including zero.
- AudioSession owns original scene/clip/backend dependencies; moves retain
  callbacks, and final release joins native playback. Captured scene moves/
  replacement and foreign threads fail. Queries describe synchronized state;
  unsynchronized/foreign/missing owners fail, without fake voices. Native scheduling
  loads assets and derives poses, preserving manual state for unchanged sources.
  Reset prepares from authoring, increments generation/restores gain only on success;
  failed file/preparation edits retain old state. Native mode has no silent-device
  fallback and keeps already queued/driver PCM unchanged after controls.
- Three queries are ReadOnly; five controls are ReadWrite and denied during
  preparation/export/import. Scripts cannot open/advance/reset/destroy the backend.
  Per-voice settings use existing authoring/synchronization; master mixer gain is
  independent of endpoint/system volume. Actual mode/playback errors and output
  frame/callback counters are exposed, with null playback in offline mode.
- Reviewed stop/cursor semantics and backend failure handling, source-frame
  canonical/range checks, shared ownership/destruction order, scene/thread scope,
  native worker sequencing, prepared sync/reset retention, owned state/spatial
  mappings, gain/counter reporting, guard coverage and actual PCM/endpoint delivery.
  Formatting/whitespace checks pass; interfaces and implementations remain separate.
  Release uses NDEBUG and caller LLVM libc++/libunwind, without LC_RPATH,
  sanitizers or environment changes. Windows/Linux common implementations remain
  uncompiled/unexecuted locally.

### Project templates, persistence and authoring commands

Added a strict rooted Project descriptor, exclusively published complete project
templates and a DotEditor executable with JSON create/inspect/configure/save-scene/
check commands. The graphical editor/runtime/export remain subsequent work.

- macOS Debug and Release each pass all 646 CTest cases (601 doctest cases and
  forty-five executable checks). Sixteen focused cases cover staging cleanup/
  moves, complete no-replace publication/retry, existing empty directories/files/
  symlinks and concurrent publication preserving the winner, invalid names,
  strict version/schema/type/byte/portable metadata, Unicode templates with actual
  SDK/attached script/camera/mesh, owned descriptions and fresh scene scopes,
  native config/scene persistence and failed edits retaining memory/files, missing/
  bad/symlink resources, failed creation cleanup and preserved existing destinations,
  relocation/copies/moves, manifest-link save refusing to overwrite another resource,
  every public JSON command through literal-argument child processes, actual
  TypeScript errors and relocated recompilation. Existing subsystem checks pass.
- `DotEditor --project-test <compiler> <sdk-root> <parent>` creates a complete owned
  template using the shared project operations, compiles the actual SDK/Mover class,
  drives its attached cube through published input, saves native scene/config,
  moves the project and reopens/recompiles with fresh scene scope. Compiler
  generations/process/VM resources return to baseline. `--project-render-test`
  also verifies actual Metal pixel changes from the template's typed movement.
- Core StagedDirectory uses native atomic no-replace directory rename: macOS
  renamex_np(RENAME_EXCL), Linux renameat2(RENAME_NOREPLACE), Windows MoveFileExW
  without replacement. Private unpublished storage is removed by RAII; native
  publication failure retains staging for retry. Existing destinations are never
  replaced, and post-publication open failures explicitly retain/report the
  published directory. This is not crash durability or hostile race isolation.
- Project manifests are strict version-1 JSON <=64 KiB/depth 8. Names, portable
  relative paths, window dimensions and fixed rate are validated; unknown/missing/
  duplicate fields and non-integer/wrong-type values fail. Open validates startup
  scene/SDK files and rejects symlink components. Save prepares state before atomic
  file replacement and changes memory only afterward. Startup saves use SceneFiles.
  Scene loads use fresh identity while preserving entity values. Relative project
  paths survive relocation; descriptor copies do not own filesystem storage.
- The template owns a native scene and attached TypeScript behavior, with local
  SDK, Mover source, scenes/scripts/assets folders and guidance. Creation prepares
  all files in staging, validates resources, then publishes. No compiler/window/
  backend is started by project open. DotEditor commands return JSON success/error
  with real statuses and compiler diagnostics using these same module APIs;
  no graphical editor or full project runtime/export is claimed at this stage.
- Reviewed path/schema/UTF-8/numeric boundaries, root/symlink/manifest protections,
  exclusive platform primitives, staging ownership and nonthrowing transfers,
  publication/post-publication failure distinctions, prepared save state, scene
  identity/relocation, JSON command/error behavior and actual template compilation/
  execution/GPU acceptance. Formatting/whitespace checks pass; module interfaces
  and implementations remain separate. Release uses NDEBUG and caller LLVM libc++/
  libunwind, without LC_RPATH, sanitizers or environment changes. Windows/Linux
  implementations remain uncompiled/unexecuted locally.

Remaining project runtime, graphical editor/export tooling, expanded rendering,
complete acceptance scenes and the example game remain to be implemented.

### Process cancellation test startup budget

The descendant/pipe cancellation test now allows two seconds for executable
startup and heartbeat publication before cancellation. It still requires a real
timeout, a stopped descendant heartbeat and zero owned child resources.

- During the project-runtime stage, initial Debug and Release full runs each
  passed 662/663 cases; this existing test timed out at 150 ms before its marker
  existed. Debug isolated repetition passed three times at the original budget.
  The final Debug full run passes 663/663 with the revised budget. This changes
  the test's scheduling tolerance, without changing process implementation or
  swallowing timeout/resource assertions. Reviewed the diff and formatting.

### Script scheduling and aggregate physics advancement

Added explicit LiveScripts startup and native PhysicsSource advancement for the
project scheduler, without extending script authority over native scheduling.

- Four focused behavioral cases pass in macOS Debug and Release. The complete
  project-runtime working tree passes 663/663 CTest cases in both configurations.
  Tests verify start without source polling, idempotent start/terminal stop,
  preserved old scripts after actual compiler failure, real motion at duration
  bounds and across slice boundaries, Begin/End contacts from different slices
  retained in one non-consuming batch, invalid durations preserving motion/batch,
  publication-budget faults rejecting stale data, reset retaining callbacks,
  moved services and foreign-thread rejection.
- Physics advancement accepts finite 1 microsecond..1 second, divides into equal
  <=250 ms slices and publishes once. Ordered contacts from every slice are
  accumulated under the JSON budget before further mutation. Capture/serialization
  or later-slice failures after advancement require reset without rollback;
  first-step preparation failures retain existing native state.
- Reviewed owner/scene scope, prevalidation, contact ordering/capacity, single
  publication index increments, native fault/stale-read behavior, reset recovery
  and explicit script-start lifecycle guards. Formatting/whitespace checks pass;
  module interfaces and implementations remain separate. No environment changes
  or sanitizers; Windows/Linux common code remains unvalidated locally.

### Project game sessions and native player

Added a reusable window-independent GameSession and a DotRuntime project entry
that runs configured native scenes/attached scripts with actual rendering/audio.
Graphical editing, game export and the example game remain subsequent work.

- macOS Debug and Release each pass all 663 CTest cases (616 doctest cases and
  forty-seven executable checks), including the new native project-player
  acceptance. Ten session cases cover published input and explicit startup,
  real physics/audio ordering and PCM, fixed-rate limits, invalid deltas without
  effects, disabled source reload, migrated script state with preserved native
  motion/audio, actual compiler failures retaining old dispatch, ordinary script
  failure isolation, realm timeout and native failure becoming terminal, moves/
  foreign-thread/scene-identity checks, repeated close and retained scene access,
  destructor avoiding hooks, failed creation and compiler/VM/audio cleanup.
  Spatial audio velocity is verified after multiple fixed steps and one frame
  synchronization, with zero-delta teleportation clearing velocity afterward.
- GameSession loads its own fresh scene scope, publishes initial empty input,
  creates physics/audio before preparing script services, and selects live/static
  compilation from project metadata. Fixed dispatch reads prior contacts, runs
  controls and advances real physics at 1/fixedHz. Frame dispatch synchronizes
  audio once after all fixed steps, preserving full-frame Doppler motion. Input
  publication, start, update, fixed update, offline PCM and terminal close are
  explicit. Thread/reentry/scene scope are checked; stable heap ownership survives
  moves. Close runs stop hooks and releases native/realm/compiler services even
  after faults; destruction runs no script, and shared scenes remain inspectable.
- `DotRuntime --project <root> <absolute-compiler> [frames]` opens the configured
  native window, WebGPU surface and real playback endpoint without fallback.
  It publishes consumed input before updates, renders the native startup scene,
  reports isolated script/reload/retirement errors and stops on realm/backend
  failure. Escape/window close exits. Budgets are strict non-negative integers;
  child-process tests check malformed/overflow budgets, exact statuses/diagnostics,
  missing resources and process ownership. Long variable frames are capped at
  one second with a diagnostic; fixed catch-up drops are reported explicitly.
- `--project-player-test <compiler> <sdk> <parent>` owns a complete temporary
  template and uses the same player loop with deterministic input. It verifies
  attached TypeScript movement, actual Cocoa/Metal surface presentation and
  changed pixels, muted real CoreAudio endpoint opening and compiler/VM/audio
  cleanup after shutdown. Endpoint delivery counters/PCM remain verified by the
  existing native audio acceptance cases; this case does not claim audible output.
- Reviewed service construction/destruction order, stable window/renderer borrows,
  candidate failures, callback reports, fixed/contact/audio timing, terminal states,
  resource release after partial initialization and loop failures, strict command
  parsing and real native behavior. Formatting/whitespace checks pass. Release
  uses NDEBUG and caller LLVM libc++/libunwind, without LC_RPATH, sanitizers or
  environment changes. Windows/Linux common implementations remain uncompiled/
  unexecuted locally. Live source polling/compilation remains synchronous.

Remaining graphical editor/export tooling, expanded rendering, complete acceptance
scenes and the example game remain to be implemented.

### Editor document transactions and history

Added a window-independent authoring Document with native scene validation,
selection, bounded undo/redo, explicit saves and a shared JSON request protocol.

- Fourteen focused document cases pass on macOS Debug and Release, including
  Release with NDEBUG. The full working tree with the command consumers passes
  682/682 CTest cases in each configuration. Tests cover owned/read-only snapshots,
  selection and stale revisions/handles, every component and hierarchy edits,
  final primary-camera validation, component removal, invalid late operations
  rolling back the whole batch, subtree removal/selection restoration, no-op
  scope/revision/history retention, repeated undo/redo and branch ID non-reuse,
  entry/byte eviction and invalid capacities, dirty state around saved baselines,
  external metadata/scene conflicts and missing-file failures retaining edits,
  moves/stable borrows/foreign-thread rejection, uint64 entity exhaustion and
  strict transport/schema/canonical IDs/UTF-8/duplicate/depth/count/byte limits.
- Edit batches prepare the final scene through the native SceneFiles codec,
  canonicalize it, prepare history/selection and then commit with nonthrowing
  transfers. Successful edits/undo/redo get fresh scene scopes; published entity
  values are never reused within the document. Histories share immutable payloads,
  retain a configured entry/byte budget and evict old entries as needed. The saved
  baseline may retain one extra native snapshot. No-op batches preserve redo.
- Mutations require the current document revision. Saves revalidate project
  metadata and the saved semantic scene baseline before atomic replacement;
  conflicts preserve unsaved state. This is not OS compare-and-swap and filesystem
  edits still require caller serialization. The model opens no runtime backends.
- Reviewed transactional allocation/commit boundaries, ID/revision exhaustion,
  scope replacement and stable-borrow lifetime, selection/history consistency,
  subtree traversal, final native validation, save conflicts/failure retention,
  response failures after mutation and bounded ownership. Formatting/whitespace
  checks pass; module interface and implementation remain separate. Caller LLVM
  environment is unchanged; Windows/Linux common code remains unvalidated locally.

### Editor document commands and rendering acceptance

Added DotEditor document command sequences and executable model/rendering
acceptances using the same Document operations that graphical clients can consume.
The graphical editor window remains subsequent work.

- macOS Debug and Release each pass all 682 CTest cases (633 doctest cases and
  forty-nine executable checks). Three new real child-process cases verify every
  document request, owned per-request JSON lines, literal paths, exact revisions/
  statuses, selection and undo/redo, native persisted saves, independent requests
  preserving earlier explicit saves after a later failure, stopping the remaining
  sequence, no implicit save on document destruction and strict duplicate fields.
- `DotEditor document <root> <requests-json-file>` accepts one request or 1..256
  requests within a 1 MiB/depth-64 strict UTF-8 JSON file. It owns one document
  for the sequence, returns one JSON line per request and stops with a structured
  error/exit one on failure. Requests commit independently; there is no implicit
  save. The native protocol reports complete snapshots plus created values.
- `--document-test <sdk> <parent>` owns a complete temporary project, selects/
  moves its attached cube, checks stale-handle/dirty behavior, undoes/redoes and
  saves/reopens the actual native scene. `--document-render-test` verifies real
  Metal pixel changes and exact restoration of initial/edited pixels after undo/
  redo. It tests the model/renderer and does not claim a graphical editor window.
- Reviewed strict parsing before command execution, independent sequence effects,
  failure statuses and honest responses, scene/native renderer borrowing and
  temporary storage lifetime, explicit persistence, protocol documentation and
  actual pixel restoration. Formatting/whitespace checks pass. Release uses
  NDEBUG and caller LLVM libc++/libunwind without LC_RPATH, sanitizers or environment
  changes. Windows/Linux common implementations remain uncompiled/unexecuted locally.

Remaining graphical editor/export tooling, expanded rendering, complete acceptance
scenes and the example game remain to be implemented.

### macOS main-thread detection before initialization

Replaced macOS SDL_IsMainThread with Darwin pthread_main_np for accurate checks
before any SDL subsystem initializes. SDL 3.4.18 reports true for every thread
while its main/events/video thread IDs are unset, permitting worker GUI creation.

- A focused test verifies the real main thread and rejects a worker before video
  initialization without starting video. GUI worker creation now fails while
  normal main-thread offscreen/native GUI works. The final GUI-stage working tree
  passes all 695 CTest cases in both macOS Debug and Release/NDEBUG configurations.
- Reviewed the actual SDL implementation, Darwin guard/header and unchanged
  Windows/Linux checks. Formatting/whitespace checks pass; no environment or
  library initialization is introduced by the query. Other platforms remain
  uncompiled/unexecuted locally.

### Pinned native GUI dependencies

Added Dear ImGui 1.92.9b docking/wchar32, official SDL3/WebGPU bindings and FreeType
through the pinned manifest. The imgui port-version-1 overlay reuses the checked-in
baseline's source hashes/build recipe and sets Dawn dependency defaults off, keeping
the intended Metal/D3D12/Vulkan backend feature selection.

- macOS Debug and Release configurations install successfully without changing
  caller compiler/linker settings. The complete GUI-stage working tree passes all
  695 CTest cases in both configurations. Release/NDEBUG executables retain caller
  LLVM libc++/libunwind and have no LC_RPATH or sanitizer linkage.
- Reviewed feature/dependency propagation, fixed baseline/source recipe reuse,
  engine-private vendor linkage and editor frontend linkage. Windows/Linux features
  are represented in code/manifests but remain uncompiled/unexecuted locally.

### Persistent GPU previews and native GUI rendering

Added shared persistent color/depth targets, direct scene-to-target rendering and
an isolated native GUI layer using the official SDL3/WebGPU and FreeType backends.
This enables the editor frontend; the complete project editor remains subsequent work.

- macOS Debug and Release each pass all 695 CTest cases (645 doctest cases and
  fifty executable checks). Eleven focused GPU cases verify actual target pixels/
  readback equivalence, repeated drawing into shared targets, invalid-input frame
  retention, count-budget exhaustion/retry, cross-device/thread/move/destruction
  rules, actual GUI colors/DPI and direct GPU image sampling, retaining an image
  whose caller releases it inside the callback, restoring foreign GUI contexts,
  repeated initialization/close and context counts, callback failures/standard/
  nonstandard exceptions/unbalanced stacks and subsequent recovery, renderer
  reentry rejection, real button and UTF-8 text input, native Cmd/Ctrl/focus rules,
  invalid/missing fonts, geometry/delta/size budgets and pending input saturation/
  frame-processing recovery. The separate platform case covers pre-init main-thread
  detection. Existing GPU/audio/application/script/project checks also pass.
- RenderTarget owns reusable RGBA8/depth textures with shared content and scoped
  device identity. SceneRenderer::renderInto performs no CPU readback/upload;
  guiImage directly samples that view. Dimensions are 1..4096/device limits;
  per-renderer targets cap at 128/256 MiB logical color+depth bytes. New targets
  have no readable frame, invalid draws retain prior content and drawing submission
  errors fault the target. Logical ownership counters include shared targets once;
  driver/queued GPU allocations are separate. Readback handles row padding and
  BGRA-to-RGBA conversion, retaining the public top-to-bottom RGBA contract.
- GUI ownership is restricted to the process main thread. Context switches restore
  prior context even after failure; private backends are destroyed before the device.
  Explicit fonts are bounded/read/FreeType-validated without fallback. Callbacks
  execute widgets within the owned context, cannot reenter renderer operations,
  and may display targets retained through submission. ImGui recovery diagnostics
  become expected errors, including unbalanced stacks, without claiming repaired
  frames succeeded. Widget/native side effects are not rolled back. Geometry,
  delta/DPI/image sizes, text transport and pending input are bounded; shutdown is
  explicit/idempotent and no ini/log/detached viewport is created implicitly.
- `DotEditor --gui-test <sdk> <parent>` creates an owned temporary project and real
  Cocoa window, receives Tab/space through SDL/Platform/Application, activates an
  actual ImGui button, edits Document and renders changed GPU preview pixels using
  one reused target. It verifies Metal surface presentation and context release.
  It is a GUI-layer acceptance, not the complete graphical editor product.
- Reviewed native device/target/GUI/font ownership, partial initialization cleanup,
  typed input/scaling/modifier/focus handling, context restoration and reentry,
  callback diagnostics/recovery, target prevalidation/fault/lifetime/counters,
  persistent depth reuse, color format/readback behavior and real native interaction.
  Formatting/whitespace checks pass; module interfaces and implementations remain
  separate, and vendor headers stay private. Release uses NDEBUG and caller LLVM
  libc++/libunwind without LC_RPATH, sanitizers or environment changes. Windows/Linux
  common implementations remain uncompiled/unexecuted locally.

Remaining editor navigation keys/Unicode font packaging, graphical editor frontend/
export tooling, expanded rendering, complete acceptance scenes and example game
remain to be implemented.


### Editor navigation input and deferred close

Appended Delete/Insert/Home/End/PageUp/PageDown/KeypadEnter without renumbering
existing keys, with exact SDL, native input, GUI and TypeScript SDK mappings.
Application close/quit can now be deferred by an expected<bool> handler; absent
handlers retain normal closure and handler failures stop later frame callbacks.

- Five focused cases pass on macOS Debug and Release/NDEBUG: native scancode
  press/release, host edges, real compiled TypeScript names/readback, exact GUI
  key states and deferred close/failure dispatch. Existing input/application
  acceptance cases also pass. The final workspace working tree passes all 708
  CTest cases in each configuration (657 doctest cases and 51 executable checks).
- Reviewed preserved key values, complete constexpr host coverage, SDK/native
  agreement, close ownership/event ordering and expected failure propagation.
  Formatting/whitespace checks pass. Other platforms remain unvalidated locally.


### Packaged Unicode font and Windows entry

Pinned Noto Sans SC 2.004 to an immutable Google Fonts revision and SHA512 hashes
in a local vcpkg manifest port. DotEditor receives the font and OFL license beside
its executable. Windows editor/runtime entry points use SDL_main's UTF-8 argv
wrapper; this common source path has not been compiled or executed locally.

- Debug and Release configuration/install/build succeed with the caller LLVM
  environment. Both packaged font hashes match the pin and both contain the OFL
  license. The final workspace acceptance actually opens the packaged font; a
  missing explicit font fails without fallback and releases its partial context.
  The final workspace working tree passes all 708 CTest cases in each macOS
  configuration, including Release/NDEBUG.
- Reviewed immutable source/license hashes, manifest override, resource location,
  UTF-8 Windows entry integration, private dependency ownership and partial-init
  cleanup. Formatting/whitespace checks pass. Release DotEditor retains caller
  LLVM libc++/libunwind without LC_RPATH or sanitizer linkage. No compiler/linker
  environment setting was changed; other platforms remain unvalidated locally.


### Native project editing workspace

Added Dot.Editor.Workspace with separate interface/implementation and stable RAII
ownership of the authoring document, application/window, renderer/GUI, scene cache,
GPU preview and private temporary command directory. DotEditor open/send expose
real project editing and serialized local automation through shared operations.

- macOS Debug and Release/NDEBUG each pass all 708 CTest cases (657 doctest cases,
  51 executable checks). Six focused workspace cases cover strict malformed/
  duplicate fields, inspector apply/reset revisions, dirty close and actual native
  save, absent-camera error/undo preview recovery, bad commands/invalid UTF-8
  receipts followed by successful requests, request ID/size/NUL boundaries,
  response-I/O failure after actual effects with terminal/no-replay behavior and
  explicit save recovery, pending overflow before effects, invalid initialization,
  moved owners/negative timing and repeated initialization/context release.
  The CLI case checks invalid budgets and malformed send requests before native
  initialization and confirms process ownership is released.
- The executable workspace acceptance opens the packaged font and real Cocoa
  window, applies online document edits with actual Metal pixel changes, verifies
  identical-ID retries and stale revision failures, activates GUI Undo through
  native SDL Cmd/Ctrl+Z and checks exact initial pixel restoration. It exercises
  missing-font partial cleanup, worker/move guards and command-directory/context
  lifetime. Native mouse injection did not activate Undo while the Mac was locked;
  computer-use visual/manual review was unavailable. No mouse/visual-review result
  is claimed; existing native Tab/space GUI button acceptance also passes.
- Scene tree/inspector authoring supports names, local transforms, parents and all
  six native components, including strict JSON script properties. Buffered edits
  cannot be silently replaced by selection/history/automation. Apply/Reset share
  automation operations; Save applies buffered changes first. Applied edits send
  only changed fields as one atomic history entry, including primary-camera
  replacement. Actual preview errors remain visible and recoverable; native
  renderer faults stop the loop. Close/quit confirms unsaved document/draft changes.
- Requests are <=1 MiB strict UTF-8 objects with canonical revisions and ASCII IDs;
  processing is lexical, sixteen per frame with at most 256 pending/4096 retained
  IDs. Submission reports queued status only. Responses preserve the source when
  UTF-8-readable and contain actual success/error outcomes. IDs cannot change
  content. Possible-effects response failures permanently fault processing before
  a retry, while owner inspection/save remain available. Callers serialize local
  filesystem activity; readiness is not a liveness heartbeat or hostile-race/
  crash-durability guarantee. The control directory is removed with its owner.
- Reviewed destruction order/event disconnection, partial opening cleanup, stable
  renderer/window/scene borrows and moves, main-thread/reentry rules, input/focus/
  close behavior, inspector field validation/primary switching/history boundaries,
  frame/dimension/DPI limits, expected preview and mailbox failures, invalid-UTF-8
  response serialization, retry/no-replay semantics, request limits and honest CLI
  outcomes. Formatting/whitespace/private-reference checks pass. Release uses
  caller LLVM libc++/libunwind without LC_RPATH or sanitizers; no environment setting
  was changed. Windows/Linux common code remains uncompiled/unexecuted locally.

Remaining workspace play preview, project resources/script-code authoring and
export tooling, expanded rendering, complete acceptance scenes and example game
remain to be implemented. The full engine goal is still active.


### Independent game-session scene cloning

GameSession::createFromScene validates/encodes/clones current authoring into a
fresh scope, with no source borrow or save. Both factories share backend/script
preparation and cleanup; normal project runtime still loads its startup scene.

- Twelve focused GameSession cases pass on macOS Debug and Release/NDEBUG,
  including two new cases for unsaved state, independent script mutation, unchanged
  authoring/disk files, replacing the source owner, malformed scripts/invalid
  configuration and compiled-generation release. Existing native project-player
  acceptance also passes. The final Play workspace working tree passes all 720
  CTest cases in each configuration (669 doctest cases, 51 executable checks).
- Reviewed validated fresh scope/explicit ownership, shared preparation ordering,
  expected failure propagation, no persistence and partial backend/realm cleanup.
  Formatting/whitespace checks pass. Windows/Linux remain unvalidated locally.


### Independent editor game timing and lifecycle

Added Dot.Editor.Play with separate interface/implementation. PlaySession clones/
starts GameSession, owns independent project-rate fixed timing and exposes actual
reports/read-only runtime scene; it has no window or document dependency.

- Five focused Play cases pass on macOS Debug and Release/NDEBUG: unsaved scene
  isolation with real input-driven physics and stop hooks, 1..64 update-budget/
  negative/zero/maximum-duration timing bounds and dropped catch-up time, invalid
  input preserving frame/timing state, hot reload failure retaining old code and
  successful source recovery, ordinary component reports versus terminal realm
  timeout, moves/source destruction/worker rejection and repeated/failed creation
  releasing generations. The native workspace acceptance consumes this service
  with real audio, physics, input and GPU presentation. The final workspace working
  tree passes all 720 CTest cases in each configuration.
- Variable script/audio update runs once after fixed steps and caps delta at one
  second. Counters describe attempted frames and completed fixed steps; reports
  retain current-frame lifecycle/reload failures rather than claiming script
  success from dispatch success. Stop closes services even after faults and retains
  the final scene; destruction releases resources without substituting stop hooks.
  No pause/single-step control is implemented by this stage.
- Reviewed initialization/partial failure cleanup, independent timing/remainder/
  catch-up bounds, input publication before effects, partial-update accounting,
  report ownership, reentry/thread/move rules and explicit stop/resource lifetime.
  Formatting/whitespace checks pass; other platforms remain unvalidated locally.


### Native workspace game preview and shared close actions

Integrated PlaySession into Workspace with shared Play/Stop/F5, input capture,
actual runtime inspection and diagnostics. Current unsaved authoring is cloned;
editing/history/save are locked until Stop. Editor state and files are preserved
while the game runs and when returning to authoring preview.

- macOS Debug and Release/NDEBUG each pass all 720 CTest cases (669 doctest cases
  and 51 executable checks). Five added workspace cases cover unsaved runtime/
  authoring separation with actual changed/restored GPU pixels and stop hooks,
  factory failure cleanup and realm-fault editor recovery, direct command script
  reentry rejection, strict capture/close/revision input and cancel/save-close
  while playing, external save conflicts stopping the game but preserving the
  open/dirty editor, repair/retry, and compiled-generation release.
- The extended native workspace acceptance uses the packaged compiler/font and a
  real Cocoa/Metal window, SDL F5 activating actual GUI shortcuts, captured D input
  driving a Jolt rigid body and a script querying its real native-playback voice,
  Escape releasing input and F5 Stop invoking hooks/releasing generations. It
  checks runtime GPU pixel changes, exact authoring pixel restoration and unchanged
  authoring snapshot/files. Playback is a real endpoint with instance software
  volume zero; no silent-device fallback is used. Existing native GPU/audio,
  project/runtime/script cases also pass. Manual mouse/visual review remains
  unavailable from the previously observed locked desktop; no such result is claimed.
- Capture starts disabled, routes keyboard/text to game and uses viewport-local
  logical pointer coordinates. Escape/outside click releases capture and publishes
  released edges before empty input; F5 stays an editor control. Focus loss clears
  native held keys. Capture does not pause simulation/audio. Historical diagnostics
  retain up to 64 entries; current game faults remain observable while the editor
  can stop/recover rather than reporting game success from editor-frame success.
- Play/Stop/inspect-play/capture-input/save-close/cancel-close share GUI/automation
  implementations. Save-close stops the game, applies pending inspector values,
  saves and closes only on success; completed stop/apply effects are not rolled
  back on later save conflict. Close/error/ownership paths stop hooks and release
  native services; destructor diagnostics use stderr. Direct dispatch/destruction
  now guard against reentry from trusted native script services.
- Packaged the full pinned native TypeScript tool directory plus combined license/
  notice. A first binary-only copy failed its version probe because lib.d.ts was
  absent; this was a packaging omission, not a caller environment problem. Both
  final compiler probes report 7.0.2 and all 109 packaged tool/standard-library files
  match the installed pinned files byte-for-byte. Runtime lookup uses executable-
  relative resources and supports custom compiler configuration without hardcoded
  development paths, Node/npm or compiler/linker environment changes.
- Reviewed scope/ownership and destruction order, partial creation/explicit stop,
  expected/report error handling and terminal game recovery, fixed/input timing,
  GUI capture/shortcut/reentry behavior, mutation locks, shared close/save failure
  ordering, persistent GPU target/cache borrows, relocated compiler data/license
  completeness and actual native/Release evidence. Formatting/whitespace/private-
  reference checks pass. Release uses caller LLVM libc++/libunwind, without
  LC_RPATH or sanitizer linkage. Windows/Linux code remains unvalidated locally.

Remaining resource/script-code authoring, project export, expanded rendering,
complete acceptance scenes and example game remain to be implemented. Full engine
completion has not been claimed; the goal remains active.


### Shared portable project path rules

Added Dot.Core.Paths with separate interface/implementation and allocation-free
UTF-8 lexical validation. Project metadata/folder validation now uses this shared
foundation. Windows device names include superscript COM/LPT digits, according to
[Microsoft naming rules](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file).

- The focused path case and eight project cases pass on macOS Debug and Release/
  NDEBUG, including Unicode, reserved names/characters, traversal/NUL/invalid UTF-8,
  exact 240-byte component/4096-byte path limits and preserving existing project
  behavior. The final resource workspace working tree passes all 731 CTest cases
  in each configuration (680 doctest cases, 51 executable checks).
- Reviewed no-allocation/noexcept validation, ASCII folding without locale,
  reserved-device rules, byte boundaries and shared project behavior. Formatting/
  whitespace checks pass. These are lexical rules, not a promise every filesystem
  accepts a path. Windows/Linux remain unvalidated locally.


### Rooted resource authoring and text documents

Added Dot.Editor.Resources with separate interface/implementation. ResourceStore
lists actual asset/script files, creates one-level directories/text and imports
bounded bytes without replacing existing destinations or removing source data.
TextDocument owns a UTF-8 draft and exact disk baseline with local revisions.

- Six focused resource cases pass on macOS Debug and Release/NDEBUG: real Unicode
  files/sorted catalog/SDK read-only protection, exact-byte/no-op/revision behavior,
  external changes retaining dirty drafts and repair/save, binary import/source
  preservation/no overwrite/invalid text, path/text/binary size/NUL/UTF-8/metadata/
  parent boundaries, symlink/deleted-file/depth/capacity failures, store/text moves,
  shared descriptor lifetime and worker rejection. The final workspace working
  tree passes all 731 CTest cases in each configuration.
- Paths stay in assets/configured scripts. SDK protection includes directory
  identity; symlinks/special files and ASCII case aliases/collisions are rejected.
  Defaults bound text at 1 MiB, binary imports at 64 MiB, catalog/lookup entries at
  8192 and depth at 64. Changed metadata requires reopening. Save verifies exact
  baseline bytes before atomic replacement and prepares the next baseline before
  effects. No-op replace/save preserve revision; destruction never implicitly saves.
- Reviewed rooted/source path boundaries, SDK/alias protection, finite traversal,
  source preservation, expected I/O versus argument errors, baseline conflict and
  allocation-before-write ordering, thread/move/shared ownership and partial errors.
  Formatting/whitespace checks pass. Caller-serialized checks/replacement are not
  OS compare-and-swap, hostile-race isolation or crash-durability guarantees. Other
  platforms remain unvalidated locally.


### Workspace resource browser and script editing

Added the Resources tab, single-buffer UTF-8 code page, nested Behavior script
creation, directory creation/import, save/check/discard controls and shared live
workspace commands. File drafts participate in close confirmation and save-close;
resources may be edited while Play runs for real TypeScript hot reload.

- macOS Debug and Release/NDEBUG each pass all 731 CTest cases (680 doctest cases,
  51 executable checks). Four focused workspace cases cover nested-template real
  compilation, draft/open/check protection and identities rejecting old-buffer
  writes, scene snapshot preservation, file conflict/save-close repair, actual
  saved OBJ changes invalidating cached geometry and changing GPU pixels, binary
  import/source preservation, SDK read-only protection and strict command fields.
- Extended native workspace acceptance opens the actual code page, modifies the
  shared text buffer, receives native Cmd/Ctrl+S through SDL/ImGui and verifies
  exact persisted source. The running native audio/physics game hot reloads that
  source and its real rigid body moves faster from captured D input. It still
  verifies authoring isolation, GPU pixel change/restoration and stop/generation
  lifetime. Existing GPU/audio/project/script cases pass. Manual mouse/visual
  review remains unavailable from the previously observed locked desktop; no
  such result is claimed.
- Resource commands expose actual catalogs and per-open buffer identities/local
  revisions; creation/import write immediately, draft replacement does not.
  Existing destinations/SDK writes/stale requests fail. Reopening requires explicit
  discard when dirty. Save checks disk/metadata conflicts, retains drafts on failure
  and refreshes the catalog; post-write refresh failures explicitly report committed
  files. Source checks compile saved data and reject pending drafts rather than
  labeling old code valid. Prior check messages clear on file changes.
- Code creation computes relative SDK imports for nested .ts/.mts paths and creates
  an export named Behavior; declarations/non-script paths are rejected. Source save
  does not implicitly attach components or claim compilation succeeded. Ctrl/Cmd+S
  belongs to the visible code page, scene shortcuts are inactive there, and game
  capture switches back to preview. Scene cache clearing is deferred outside GUI
  rendering callbacks after resource writes. File drafts and invalid widget edits
  participate in close/play protection; completed earlier save-close actions are
  not rolled back if a later save fails.
- Reviewed service initialization before native resources, descriptor/buffer lifetime,
  identity/revision and argument validation, SDK protection/import preservation,
  pending draft guards, compiler diagnostics/generation cleanup, GUI action parity/
  shortcut/capture behavior, save-close ordering, asset cache/GPU borrow lifetime,
  formatting/blank-line/private-reference checks and actual native/Release evidence.
  Release retains caller LLVM libc++/libunwind without LC_RPATH or sanitizer linkage;
  no environment setting changed. Windows/Linux remain unvalidated locally.

Remaining resource rename/delete, richer code editing, project export, expanded
rendering, complete acceptance scenes and example game remain subsequent work.
The full engine goal is still active and has not been marked complete.


### Published ESM program foundation

Added separate CompiledProgram publication/loading implementations. A new strict
version-1 manifest delivers JS/MJS only; trailing source map directives are
removed. Loading captures validated code into independently owned generations
without invoking a compiler. Publication satisfies default load bounds before
exclusive rename and preserves existing destinations.

- Four focused cases pass on arm64 macOS Debug and Release/NDEBUG: real imported
  ESM execution after source removal, immutable snapshots after package changes,
  map omission, moves and generation/process cleanup; strict fields/counts/limits,
  portable and ASCII directory aliases, missing/extra/symlink files, invalid UTF-8/
  NUL and oversized module rejection, no overwrite and staging cleanup.
- The integrated export working tree passes all 743 CTest cases in both builds.
  This includes later consumers being committed in subsequent cohesive stages;
  it is not an isolated first-stage suite count. Initial map stripping was corrected
  for compiler output without a trailing newline before the passing final checks.
- Reviewed exclusive publication, bounded traversal/byte arithmetic, exact tree
  membership, default load compatibility, expected error propagation, shared
  generation destruction, portable names and interface/implementation separation.
  clang-format and diff whitespace checks pass. No toolchain environment changed.
  Manifest shape/count/size validation does not authenticate code or isolate hostile
  filesystem races. Windows/Linux remain unvalidated locally.


### Precompiled game session foundation

Added GameSession::createCompiled and a shared internal session preparation path.
Precompiled sessions clone native scene values into fresh scope, retain owned ESM,
require hotReload=false and borrow the resource root. Existing authoring factories
continue using real compilation/live reload.

- Two new focused cases and all 14 GameSession cases pass on arm64 macOS Debug
  and Release/NDEBUG. They verify actual attached scripts, published input, rigid
  body motion, offline PCM and stop hooks after deleting TypeScript sources;
  hot-reload/moved-owner/missing-class rejection and VM/mixer cleanup on failed
  preparation. Integrated export working-tree suites pass 743/743 in both builds.
- Reviewed fresh scene ownership, borrowed asset lifetime, compiler/precompiled
  branch invariants, setup failures/realm faults, native service destruction and
  unchanged update/close semantics. Formatting and whitespace checks pass; no
  sanitizer or environment override was introduced. Other platforms unvalidated.


### Portable game data and editor export

Added Dot.Project.GamePackage with separate interface/implementation, strict
versioned manifests, bounded native-scene/resource/ESM publication and independently
owned loaded snapshots. CLI and the workspace Export data modal share publication;
workspace export includes unsaved native scene values without changing authoring
revision/dirty state. DotRuntime --game plays captured code without a compiler.

- arm64 macOS Debug and Release/NDEBUG each pass 743/743 CTest cases: 691 doctest
  cases and 52 executable checks. Three package cases verify relocation, source/
  published-directory removal with surviving captured resources/code, binary byte
  preservation, moves/copies, strict metadata/tree/limits and cleanup, invalid
  scripts/missing audio, no overwrite/path escape and symlink failures. CLI/workspace
  cases verify actual publication, invalid budgets, unsaved scene preservation and
  stale revision/Play/pending resource draft protection.
- runtime.exported_game passes in both configurations: relocates package, deletes
  authoring data, then exercises real native window, published input, rendered GPU
  pixel change, audio endpoint and explicit shutdown with generation/VM/audio
  counters returning to baseline. Existing project/player/workspace acceptance
  and all GPU/audio suites also pass. Debug took 50.87 seconds; Release 21.46 seconds.
  Initial supplemental tests had two expectation mistakes (canonical temporary
  paths and the stop-play command), corrected before the final passing suites.
- Before exclusive publication, staged data is reopened, static mesh/audio assets
  are decoded and the compiled session is prepared with offline audio. Gameplay
  hooks and dynamically chosen assets are not predicted. Strict tree membership,
  per-file/total bytes, portable names and directory ASCII aliases are checked;
  no TypeScript/maps/source SDK/metadata/owned staging directories are delivered.
- Reviewed rooted traversal, finite limits and byte arithmetic, source preservation,
  validation-before-publication and partial cleanup, snapshot/borrowed asset lifetime,
  script preparation, default load contract, revision/draft guards, GUI/automation
  parity, contextual failures, platform-independent paths and module separation.
  clang-format and whitespace/private-reference checks pass. Release DotRuntime
  retains caller-selected LLVM libc++/libunwind, with no LC_RPATH or sanitizer
  linkage. No environment settings changed. Windows/Linux remain unvalidated.
- No manual mouse/visual acceptance is claimed. This stage delivers game data for
  an existing matching runtime, not a standalone executable distribution. Native
  executable/dynamic-library bundling, richer rendering/editor tools, exhaustive
  component/API acceptance scenes and the example game remain subsequent work.
  The full engine goal remains active.


### Native runtime and distribution foundation

Added a dedicated DotGame player and generated runtime-kit installation component,
plus Dot.Project.GameExport with separate module interface/implementation. RuntimeKit
validates a bounded native tree and publishes permission/notice-preserving copies;
exportGame prepares native files and the current scene/data package in owned private
staging before exclusive whole-directory publication. Playback resolves adjacent
game data from the executable directory, independent of working directory.

- Four focused distribution cases pass on arm64 macOS Debug and Release/NDEBUG:
  strict platform/version/path/count/size/permission/tree validation, normal copies,
  moved owners/manifest changes/source preservation/no overwrite, actual relocated
  standalone execution after deleting authoring/intermediate kit data, and compiler
  failure after native copying with staging cleanup and compiler/process counters
  returning to baseline. Native playback verifies real input, GPU pixel movement,
  audio endpoint and explicit stop hooks. Integration working trees pass 749/749
  CTest cases in each configuration; this includes subsequent editor consumers.
- CMake macOS dependency fixup copies caller-selected LLVM libc++/libc++abi/libunwind,
  rewrites their references and signs copied binaries ad hoc. All seven delivered
  binaries in Debug and Release pass codesign --verify --strict; otool -L finds no
  non-system absolute dependency and DotGame has no LC_RPATH. Release kit occupies
  22 MiB. Windows CRT/non-system DLL and Linux relative-search/dependency-copy paths
  are implemented, with post-copy external-resolution failures, but unvalidated.
- Kit notices include installed vcpkg copyrights and upstream LLVM runtime licenses
  recorded at fda66c5ab9b49af68d2205f1127b21567a42b9ef. Caller build environment is
  preserved; only distributed copies receive loader-reference/signature changes.
- Initial verification exposed an untracked regular .DS_Store in the generated
  kit. RuntimeKit now ignores only macOS ordinary Finder files up to 64 KiB and
  omits them from copying; symlink metadata and other extra files still fail. A
  bounded visited-entry limit remains in force. An unused helper and a shadowed
  test variable were corrected under warnings-as-errors before final passing builds.
- Reviewed native dependency closure/system exclusions, relative loader behavior,
  signed-copy preservation, path/size bounds and aliases, borrowed source ownership,
  manifest revalidation, permission preservation, failure cleanup, exclusive rename,
  expected diagnostics, platform branches and interface/implementation separation.
  Formatting/whitespace/private-reference checks pass. No manual visual, other-host,
  release signing/notarization or installer validation is claimed. Format checks
  do not authenticate arbitrary executable bytes; input changes must be serialized.


### Workspace and CLI native game export

Added DotEditor export and the workspace Export game modal/command using the same
exportGame implementation. Native export is the GUI default; the data-only checkbox
retains export-data. WorkspaceConfig may supply a runtime kit explicitly or resolve
the editor's packaged kit lazily. The current native scene is exported without
implicitly saving or changing authoring revision/dirty state.

- arm64 macOS Debug and Release/NDEBUG pass all 749 CTest cases (697 doctest cases,
  52 executable checks). New CLI and workspace cases verify complete player/data/
  runtime-manifest delivery, strict frame budget errors before native setup, missing
  kit errors leaving no destination, current unsaved scene values, saved authoring
  preservation and stale revision rejection. Existing shared export-data cases
  still verify stopped Play and pending file draft guards. Debug suite took 52.33
  seconds, Release 22.01 seconds. Native relocated distribution and all existing
  GPU/audio/project/editor cases also pass.
- Reviewed shared GUI/protocol/CLI behavior, strict request fields and canonical
  revision guards, Play/inspector/text boundaries, lazy tool/kit resolution, expected
  failures and source-preserving publication. Formatting and whitespace checks
  pass. Only arm64 macOS is locally validated; no manual mouse/visual result is
  claimed. Native export requires a compatible runtime kit for this platform.
- Standalone directory export is implemented. Native installers/release signing,
  fuller rendering/editor tools, comprehensive component/API acceptance scenes
  and a playable example game remain subsequent work. The overall goal is active.


### Playable Prism Relay example

Added an ordinary authored 3D project with 40 native entities, original crystal OBJ
and synthesized PCM feedback assets, an attached player controller, eight visual
crystal animations and bounded pure round rules. The game provides start, normalized
WASD/arrow movement, eight pickups, three health points, timed hazard immunity,
75-second deadline, pause, win/lose and complete restart. Native components retain
scene, mesh, physics and audio responsibilities. Runtime titles follow scene names
through the existing native window API, with unchanged-title writes skipped.

- arm64 macOS Debug and Release/NDEBUG full suites pass 755/755 (703 doctest cases,
  52 executable checks). Debug took 58.13 seconds, Release 23.12 seconds. After final
  TypeScript formatting and additional configuration/outer-wall boundary assertions,
  all six affected cases were rebuilt and pass in both configurations (9.55/3.46
  seconds respectively). Engine/runtime implementation bodies were unchanged by
  those final source-format and test-only edits.
- Focused rules cases verify ready/start/pause, collection deduplication, health/
  deadline defeat, victory, reset, owned snapshots, failed checkpoint restoration,
  invalid ticks/targets and rejected speed/time/health configuration with a reusable
  VM. Native Jolt gameplay completes the eight-crystal route, restores all pickups
  and player pose, and stops against the authored outer wall. Other cases verify
  hazard damage/immunity/pause/defeat and actual mixed PCM, plus migration of all
  nine running components without changing scene identity or collected progress.
- An initial hazard case exposed sleeping contact behavior: repeated stay events
  do not continue indefinitely. The player now retains begin/stay/sleep contacts
  until native end events, preserves that set through reload, and clears it on
  restart. The corrected damage case passes; no substitute geometric pickup test
  replaces the real physics events.
- Native acceptance consumes actual SDL key events, presents the real window,
  checks player motion/collection and changed GPU pixels, opens a real muted audio
  endpoint and explicitly shuts down scripts/services. It exports the sample,
  relocates it, deletes authoring data and launches the delivered player from a
  different working directory. Actual 192x144 GPU readback was inspected; generated
  preview data stays in build output. No manual mouse/desktop review is claimed.
- Reviewed native versus script responsibilities, authored hierarchy/body scales,
  timer/phase/ownership invariants, contact lifetime across sleep/end/reset/reload,
  source authoring preservation, metadata/resource validity, stable window/renderer
  borrows, expected title/backend errors, exclusive export and platform assumptions.
  C++/TypeScript formatting and whitespace/private-reference checks pass. Strict
  type-only/direct-module/standard-comparison imports were corrected before passing
  builds; the configured LLVM environment remains unchanged. Windows/Linux are
  implemented through existing native modules but unvalidated locally.
- Builds generate the complete example plus current SDK under build/<preset>/examples.
  That output is refreshed from the source template; users must copy it before
  persistent authoring edits. The sample is playable and exportable, but dedicated
  exhaustive component/API scenes, further editor usability and distribution work
  remain subsequent stages. The full goal remains active.


### Dedicated component and runtime SDK acceptance

Added an ordinary Feature Lab project with 13 authored native entities, both camera
projections, cube/quad/OBJ geometry, hierarchy, all three body motions/shapes, a
sensor, spatial audio source/listener and active/disabled script descriptors.
The hierarchy includes an ID above 2^53. The attached auditor records successful
behavior checks into disabled native report metadata, which does not run hooks.

- arm64 macOS Debug and Release/NDEBUG each pass all 759 CTest cases (707 doctest
  cases, 52 executable checks), taking 57.81/22.61 seconds respectively. Four new
  focused cases pass: independent SDK inventory, actual services/lifecycle/failure
  behavior, checkpoint migration and real graphics/audio/workspace/export acceptance.
- The inventory contains all 81 current runtime SDK members with getter/setter
  distinctions. It is independently compared with actual SDK declarations, readonly
  fields, constants/functions and class names, then with checks actually completed
  after explicit shutdown. All members execute through the native compiler/runtime.
  Five additional checks cover high-precision identity, component removal and failed
  component/physics/audio changes preserving prior state. Checks are not inferred
  from names alone and no JavaScript scene/rendering/physics replacement is used.
- Real service tests verify scoped/frozen/owned IDs and transforms, native parent/
  world matrix behavior, component description round trips/removal, every declared
  key/button against published snapshots, nonzero pointer/delta/wheel and Unicode
  text, Jolt ray/body/control/contact results, actual PCM, native HostError context,
  start/fixed/update/stop counts and compiler/VM/mixer cleanup. Compatible reload
  migrates the one active auditor and keeps verified checks/start counts intact.
- Native acceptance verifies actual GPU pixels/presentation and different camera
  projections, body movement from SDL key events, real muted audio callback progress,
  native editor preview/Play/Stop with unchanged authoring snapshot, and standalone
  relocated startup after deleting the authoring directory. All CTest cases pass;
  no manual desktop/mouse or Windows/Linux validation is claimed.
- Reviewed float tolerance versus exact IDs/booleans, metadata-only report ownership,
  physical effects versus authored descriptions, lifecycle ordering, invalid-write
  preservation, source inventory drift, reload semantics, stable window/renderer
  borrows, private-source exclusion and native/export boundaries. C++/TypeScript
  formatting and whitespace checks pass. The caller LLVM environment is unchanged.
- docs/Acceptance.md indexes this scene and the existing engine subsystem unit/
  command/native evidence, distinguishing runtime member coverage from exhaustive
  argument/device/driver permutations. Core low-level features remain covered by
  focused suites and executable acceptance rather than unsupported script bindings.
  GUI project creation/launch ergonomics and further distribution work remain
  subsequent stages; the full goal remains active.


### Validated project template foundation

Added Dot.Project.Templates with separate interface/implementation. It copies a
bounded authored template, replaces the source SDK with the current SDK, applies
the requested project name and validates scene, real compilation, static resources
and script preparation before exclusive publication. Validation output belongs to
a separate private owner, so ordinary template paths cannot be deleted as scratch.

- Two focused cases pass on arm64 macOS Debug and Release/NDEBUG: complete 40-entity
  game copy, binary resource equality, replacement of obsolete SDK while preserving
  source bytes/metadata, compiler generation cleanup/no validation artifacts, no
  overwrite, traversal/source-directory rejection, actual compile failure leaving
  no destination, symlink and private staging rejection with cleanup.
- Final integrated working trees pass all 766 CTest cases in both configurations.
  This includes subsequent home consumers and the separately committed native
  acceptance focus correction; it is not an isolated foundation suite count.
- Reviewed portable names/aliases, traversal/depth/count/byte arithmetic, exclusive
  publication and partial ownership, replacing rather than using obsolete SDK,
  current descriptor/resource validation, external errors and source preservation.
  Formatting/whitespace/private-reference checks pass. Input changes require caller
  serialization; gameplay hooks, hostile races and crash durability are not promised.
  Windows/Linux code remains unvalidated locally. The LLVM environment is unchanged.


### Native project home and automation handoff

DotEditor now starts a native project home with no arguments. It opens an existing
project or creates the starter, Prism Relay and Feature Lab through shared strict
project actions. The packaged resources include the current SDK, compiler, font and
source templates. Selection releases GUI/video before opening the editor workspace,
while retaining the home response owner for the workspace lifetime.

- arm64 macOS Debug and Release/NDEBUG each pass 766/766 CTest cases (713 doctest
  cases, 53 executable checks). Seven focused template/home/CLI/startup checks pass.
  Final full suites took 66.19/31.33 seconds. Native home tests render its GUI,
  select a real game through the mailbox, read retained responses, release GUI
  contexts, enter/render the workspace and run Play/Stop. All three project choices
  compile and validate; real CLI arguments, no overwrite, malformed requests and
  invalid budgets before native setup are covered.
- Tests cover main-thread/dimension requirements, worker rejection, moves and GUI
  lifetime, failed open retaining the home, session selection rejecting new submits,
  and response-write failure after actual committed creation. Such failures remain
  terminal across dispatch/step/run, retain explicit partial-effect context and do
  not replay requests. The created project survives. An early failure test incorrectly
  used an already-existing response directory, which was treated as completed; the
  test now blocks the response parent to exercise the actual write failure. Existing
  non-regular response entries are rejected explicitly.
- Reviewed source-preserving project actions, SDK/compiler/template resolution,
  shared GUI/CLI/live commands, strict fields/UTF-8/paths, exact ownership handoff,
  event subscription/renderer/window order, thread/reentry RAII, bounded request
  processing and error persistence. Formatting and whitespace checks pass. Direct
  module/standard-type imports were corrected before passing builds. No environment
  settings changed; Windows/Linux remain unvalidated locally and no manual desktop/
  mouse result is claimed. Further editor/distribution refinement remains work;
  the overall engine goal stays active.


### Deterministic synthetic native key holds

The initial full home working-tree suite passed 765/766: Prism Relay native input
acceptance moved only 0.25 units because actual SDL focus events released its
synthetic held key while switching native windows. The acceptance helpers now
queue focus gained followed by the intended key state before each driven frame,
matching the established workspace acceptance approach. Runtime focus-loss/input
release behavior is unchanged. Both Prism Relay and Feature Lab continue consuming
real SDL events and checking native movement/GPU/audio rather than substituting
an input service.

Reviewed that the correction is limited to synthetic test setup and retains actual
window events, body motion, gameplay, pixel and audio assertions. Final Debug and
Release/NDEBUG full suites both pass 766/766; formatting/whitespace checks pass.
No Windows/Linux or manual desktop result is claimed.


### Portable complete editor distribution

Added EditorDistribution installation and DotEditorDistribution/DotEditorArchive
build targets. The portable directory includes editor/runtime, current native
compiler/standard libraries, SDK, CJK font, source templates, native player kit
and license notices. Runtime/editor now share one native dependency relocation
implementation, preserving caller compiler/linker settings. Linux installs relative
library lookup; Windows copies detected CRT/non-system DLLs; macOS relocates and
ad-hoc signs distributed binaries.

- arm64 macOS Debug and Release/NDEBUG each pass all 767 CTest cases (714 doctest,
  53 executable checks), taking 74.44/29.08 seconds. The new acceptance copies the
  complete editor to a path containing spaces, starts actual native home, creates
  Prism Relay with packaged SDK/compiler/templates, checks real TypeScript, opens
  editor preview and runs the native project runtime, exports a standalone game,
  deletes both authoring project and editor copy, then runs the delivered player.
  Process ownership returns to baseline. Existing native/GUI/audio/API suites pass.
- The Release zip contains 219 entries and occupies 43,122,457 bytes. CRC, expected
  resources, executable bits and private-document exclusion pass. The actual zip
  was extracted with CMake and its native project home starts successfully. Every
  delivered Mach-O executable/library passes strict signature verification and
  loader inspection finds only relative bundle references and system dependencies.
  Nested player manifest byte counts remain valid after packaging.
- Initial package inspection caught the bundled Go TypeScript compiler's signature
  being invalidated during loader fixup. Final preparation now signs/verifies all
  four delivered executables after dependency relocation, plus all copied dylibs.
  Only distributed copies change. A doctest diagnostic expression was parenthesized
  before passing compilation. No toolchain environment workaround was applied.
- Reviewed common fixup behavior, native dependency closure/system exclusions,
  permission-preserving resources, compiler/SDK/template location independence,
  notices, final signature order, installed relative RPATH on Linux and generated
  directory/archive ownership. Formatting/whitespace/private-reference checks pass.
  Windows/Linux are implemented but unvalidated locally. No manual desktop, release
  signing/notarization or installer-publication result is claimed; the native zip
  supports tested local deployment without a developer checkout/toolchain path.
- The earlier three project-home commits were pushed after a definitive GitHub SSL
  timeout and a bounded HTTP/1.1 retry; remote main was verified at 841ad7b. The full
  engine objective remains active pending final delivery audit of all requirements.


### Final workflow delivery audit and resource refresh

The final audit traced the authoring-to-native-game workflow through implementation
and runnable acceptance: required native stack/static module library, project home,
native scene/components, resource and attached ESM authoring, Play/hot reload, shared
agent commands, independent native export, playable Prism Relay, Feature Lab and
portable editor deployment. docs/Delivery.md records the evidence and current
feature boundaries. Earlier milestone limitations remain historical entries;
stale current README/editor/script/GUI/example claims were corrected.

The audit found editor SDK/templates were copied only after relinking. They now
have an explicit resource dependency refreshed on every editor/distribution build,
removing only owned generated SDK/template trees so deleted files do not linger.
Debug and Release probes inserted obsolete generated files, rebuilt resources
without relinking and verified exact source/copy byte inventories and probe removal.
Authoring projects and native player-kit ownership remain separate from these trees.

- Final arm64 macOS Debug and Release/NDEBUG pass all 767/767 CTest cases (714
  doctest and 53 executable checks), taking 76.44/32.81 seconds. Both builds pass;
  the actual Release build uses -O3 -DNDEBUG. Native graphics/audio, compiler,
  lifetime/failure, full SDK/component, example and relocated distribution cases
  pass. No assertions-only validation or fake hardware success was substituted.
- All 211 C/C++ files pass clang-format; diff whitespace and private/reference/
  toolchain-path scans pass. Delivery-related local document links resolve.
  The private local design document remains untracked and is absent from the archive.
- Regenerated Release archive: 219 entries, 43,122,457 bytes. CRC, executable
  permissions and private/stale-resource exclusion pass. All 16 delivered Mach-O
  files pass strict codesign verification and relative/system loader-reference
  inspection. Actual final archive extraction and native home startup pass.
- Reviewed generated-directory ownership, dependency ordering/no target cycle,
  rebuilds without code relinking, removed source cleanup, shared installation
  inputs, platform-independent CMake paths and evidence accuracy. No public header
  installation or local LLVM path was introduced. Caller environment is preserved.
  Windows/Linux implementation remains unvalidated locally; release identity
  signing/notarization, installer publication and manual mouse acceptance are not
  claimed. Current renderer/import/editor feature boundaries are explicit in delivery
  documentation rather than hidden by a blanket production or exhaustive claim.


### Desktop application entry and complete examples

User desktop use exposed two delivery gaps: Finder opened the naked macOS editor
executable through Terminal, and opening the repository Prism Relay source failed
because its scripts/dot SDK existed only in generated projects. The previous
distribution acceptance checked direct process startup/template creation, which
did not establish a proper Finder application entry or complete source examples.

macOS distribution now contains DotEngine.app with an APPL Info.plist, executables
and relative libraries in Contents/MacOS, and compiler/SDK/font/templates/player/
notices in Contents/Resources. Resource lookup uses SDL's standard bundle base;
the distribution manifest records executable paths. Library movement is optional
when the toolchain needs no bundled libraries. Nested binaries are signed before
the final application signature. Windows editor uses the GUI subsystem and existing
SDL main wrapper; that branch remains unvalidated locally. CLI/mailbox support stays
available through the executable inside the macOS app.

Prism Relay and Feature Lab source projects now contain versioned SDK snapshots.
A regression opens both source projects, compares SDK bytes against the canonical
source and compiles actual scripts without build-generated SDK injection. The
distribution includes complete top-level examples, and native acceptance directly
opens/type-checks its Prism Relay in addition to template creation/Play/export.

- arm64 macOS Debug and Release/NDEBUG each pass 768/768 CTest cases (715 doctest,
  53 executable checks), taking 72.81/30.74 seconds. Focused source-example and
  distribution checks pass. The latter starts the relocated app through actual
  LaunchServices and reads its native project-home response, then runs existing
  independent native game export/lifetime acceptance. This is programmatic desktop
  application startup evidence, not a claim of manual mouse testing.
- Release ZIP: 263 entries, 43,255,944 bytes. CRC, executable permissions, APPL
  metadata, complete example SDKs and absence of the exposed bare editor/private
  document pass. Actual extraction verifies the whole application signature and
  all 16 Mach-O signatures/relative-system loader references. Moving only the app
  to another directory still starts the home through LaunchServices, opens the
  extracted example and type-checks it with the compiler inside that moved app.
- Reviewed complete source/package inventories, shared GUI/CLI entry, SDL bundle
  resource lookup, executable-relative dependencies, signing order, ownership of
  generated paths, platform guards and SDK snapshot consistency. Changed C++ files
  pass clang-format; diff whitespace and private-reference checks pass. Caller
  environment remains unchanged. Windows/Linux and release identity signing/
  notarization remain unvalidated. The local private document remains untracked.


### Isolated acceptance targets and temporary-output cleanup

Moved the runtime/editor acceptance dispatchers and implementations into
tests/acceptance. Its two executable targets are reachable only through the
BUILD_TESTING-gated tests subdirectory and are never installed. Product entry points
now contain only playback, authoring/automation and normal argument handling.
The runtime main is 55 lines; all test-prefixed implementations left product sources.
The standalone player no longer implements --verify-native.

The shared native player retains session start/close, input/script/render callbacks,
error precedence and stable local resource ownership. A private synchronous loop
driver lets the test side supply deterministic input/steps and inspect actual
presentations/pixels without copying the product loop. Tests verify returned driver
errors and thrown exceptions keep context and release compiler/VM/audio ownership.
An explicit default empty callback fixed a warnings-as-errors initializer diagnostic;
no compiler/environment settings or warning policies were weakened.

- The before/after CTest inventory retains all 768 original cases and adds one
  product-CLI rejection/no-side-effects case. Debug full suite passes 769/769 in
  77.58 seconds; after adding driver-failure assertions its focused native player
  check passes in 2.35 seconds. Final Release/NDEBUG passes 769/769 in 35.23 seconds
  (716 doctest, 53 executable checks). Four initial focused playback/export/CLI cases
  pass. Export coverage runs the real shipped player after removing authoring/tools,
  then uses the separate native driver to preserve input/pixel/lifetime assertions.
- Built Release with BUILD_TESTING=OFF using existing caches: all 115 compilation
  entries are product sources, the graph has no acceptance targets, and nm finds
  none of the former runtime/editor test symbols in DotRuntime/DotEditor/DotGame.
  Restored ON afterwards. All 215 C/C++ files pass clang-format; whitespace checks
  pass. Acceptance command documentation now names development executables.
- Updated latest native ZIP remains at its existing path: 263 entries, 43,072,411
  bytes. CRC, absence of acceptance executables/private data, complete app signature,
  all native binary signatures and relative/system loader references pass. Product
  editor/runtime symbols contain no former acceptance functions; native relocated
  editor/example/export coverage passes in the full suite. Caller environment stays
  unchanged; Windows/Linux implementation remains locally unvalidated.
- Removed the three owned distribution verification copies (about 425 MiB), obsolete
  sanitize output (about 55 MiB), 99 historical diagnostics (4,482,098 bytes) and 12
  Finder metadata files. Removed this cleanup's temporary inventory files after
  verification. Active Debug/Release/vcpkg/clangd caches and the latest distribution/
  archive are preserved; the local private document's content hash is unchanged and
  it remains untracked. New validation logs retain this stage's actual evidence.
- Reviewed build gating/source inventories, preserved CTest names, normal product
  command behavior, callback borrow lifetime, close on driver failures, native
  export independence, installed contents and narrowly scoped cleanup paths.

Planned next: Feature Lab remains a separate editable capability-showcase project.
Prism Relay's requested realistic science-fiction presentation and game-quality
upgrade requires further rendering/content/interaction work; that upgrade is not
claimed as implemented by this acceptance-code refactor.


### Surface geometry foundation

Replaced the current DotMesh structure directly: its root contains only vertices
and indices, and every vertex requires position, color, normal and texCoord.
There is no format number, old-format loader or compatibility dispatch. The engine
is unpublished; callers, generated fixtures and documentation use the same current
structure. No tracked DotMesh asset files required migration.

Vertex payloads retain unit/explicitly absent normals and finite UVs, including
coordinates outside the unit square. OBJ import remaps face-corner position/normal/UV
tuples to preserve hard-normal and texture seams, normalizes authored normals,
rejects zero normals and volumetric texture coordinates, and reports only metadata
it actually discards. Position-only OBJ input without faces now fails instead of
being interpreted as an implicit triangle. Existing unsupported materials/free-form
geometry and resource bounds remain explicit failures.

Added independently owned flat-normal generation with winding-derived normals,
colors/UV preservation, double intermediates for extreme finite coordinates,
degenerate-face errors and expansion bounds checked before allocation. Direct GPU
input and mesh uploads enforce the surface-attribute contract. The native surface
acceptance persists/reloads attributes and renders a CPU-derived normal-color
visualization, checking pixels and complete vertex allocation size. The current
shader still reads position/color: this does not establish normal-based lighting
or texture sampling. Materials, lighting, shadows, production editor tools and
the requested game-quality upgrade remain planned in the capability roadmap.

The first full Debug run passed 773/775: an obsolete test expected normal-loss
warnings, and the workspace reload acceptance lost its synthetic held-key motion.
The warning fixture now tests actual object metadata loss. The acceptance driver
reasserts its intended SDL focus/key state per movement tick, consistent with the
existing native game drivers; product focus handling is unchanged. No assertions
or warning policy were disabled.

Review covered strict field validation, seam identity, normal/UV finite ranges,
triangle winding, source ownership, expansion limits, GPU payload layout and
resource recovery after rejected inputs. The caller toolchain environment remains
unchanged. Local validation is arm64 macOS; Windows/Linux results are not claimed.

- Final Debug and Release/NDEBUG pass 775/775 CTest cases (721 doctest,
  54 executable checks), taking 77.96 and 35.90 seconds. Focused geometry,
  asset, warning/cache, workspace and native surface checks also passed. Tests cover
  malformed/missing attributes, seam preservation, non-unit/tiny/non-finite normals,
  tiling/non-finite UVs, extreme scales, source ownership, degenerate faces, limits
  and rejected GPU input without resource allocation/device poisoning.
- All 216 C/C++ files pass clang-format; diff whitespace and private/reference
  checks pass. Acceptance code remains under BUILD_TESTING-gated tests/acceptance.
- Refreshed latest macOS ZIP: 263 entries, 43,082,684 bytes; CRC and absence of
  acceptance binaries/private data pass. Staged application and all 16 native
  signatures/relative-system loader references pass. Full suites retain relocated
  editor/example/export coverage. Active build caches and the private untracked
  document are preserved. No publication or new release version is claimed.


### Owned image and texture surfaces

Added separate Graphics.Image and Assets.Image module interfaces/implementations
for owned pixel data and bounded PNG/JPEG/Radiance decoding. The fixed manifest
baseline now includes stb; its implementation is an isolated static C backend,
while engine boundaries retain contextual expected results and RAII. Native image
fixtures verify exact PNG row order/alpha, JPEG output, HDR radiance above one,
malformed sources, precision rejection, input/dimension limits, independent
parallel decoding and project-path context. No global flip/gamma state is changed.

Replaced the current mesh shader's fragment path with actual texture sampling.
Every draw carries its material base-color factor and optional shared texture;
base color multiplies vertex color and object tint. A missing texture deliberately
represents a constant white texel; an explicitly empty or foreign texture fails.
The same pipeline serves immediate meshes, persistent targets and native surfaces.
This stage is unlit, and existing RGBA8 output is linear: it does not deliver PBR,
HDR render targets, tone mapping, IBL, shadows or SSAO. Those requested capabilities
and their quality/evidence gates are explicit in the rendering development document
and capability roadmap; no production-rendering completion claim is made.

Owned GPU textures include explicit sRGB/linear formats, nearest/linear samplers,
clamp/repeat/mirror addressing, anisotropy and complete optional mip chains. Mips
average linear light with linear alpha and preserve odd edges. Texture queries
require the creating thread; copies share ownership and count once, while quiescent
destruction may occur after device shutdown. Configurable count/logical-byte
residency budgets are enforced before upload/mip allocation; allocation/scope
failures retain expected error paths and do not increment resource counters.

The native development-only texture acceptance loads actual PNG/HDR fixtures,
samples a material into a persistent GPU target, checks RGBA pixels and verifies
mesh/target/texture release. Focused tests additionally exercise sRGB conversion,
alpha, linear filtering, negative/outside-unit UV addressing, odd-edge mip
minification, source independence, sharing/moves/orphans, thread rejection,
invalid material/foreign textures retaining the last frame, exact budgets and
recovery after release. Reviewed ABI/layout, per-draw uniform/texture ownership,
queue write lifetime, mip color math, bounded counts and platform-independent
native decoder/binding APIs. Caller compiler/linker environment is preserved.

Initial builds exposed missing standard declarations in implementation global
fragments, an omitted structured-error origin initializer, and unparenthesized
conditional messages in doctest macros. These were corrected without weakening
warnings/assertions. The final focused Debug run passes 45/45 in 14.07 seconds.

The first full Debug run passed 788/789; its failing existing native audio test
compared consumed-frame counters after a fixed 30 ms sleep and observed 0/0.
That test now uses the existing two-second bounded wait helper for actual healthy
worker/device progress, preserving the allocation-failure, state, voice, memory
and frame-consumption assertions. The audio implementation is unchanged.

A subsequent full Debug run exposed six export/distribution failures with
"Invalid runtime file record". Inspection found Finder's .DS_Store had entered
the generated runtime manifest. Runtime tree validation already permits bounded
regular Finder metadata on macOS, but manifest generation incorrectly inventoried
it as runtime content. The shared CMake writer now removes metadata from its owned
output and excludes it from records; a regression seeds root/nested metadata, runs
the actual writer and verifies the unchanged strict manifest opens. General tree
and manifest validation remain strict.

- Final arm64 macOS Debug and Release/NDEBUG pass 790/790 CTest cases
  (735 doctest, 55 executable checks), taking 86.40 and 33.42 seconds.
  Focused export/distribution regressions pass 10/10 in 23.70 seconds; the audio
  progress regression passes ten consecutive runs in 1.29 seconds. Actual native
  surface, GUI, editor/example and independent exported-player checks remain.
- All 224 C/C++ files pass clang-format; diff whitespace/private-reference checks
  pass. The 1,076-byte image fixture set is authored test data, not external art;
  tests do not require platform image-conversion tools. Acceptance implementation
  remains gated by BUILD_TESTING and excluded from products/install contents.
- Latest macOS ZIP is refreshed at its existing path: 265 entries, 43,108,375 bytes.
  CRC, included stb notices, absence of acceptance/private/Finder files and runtime
  metadata exclusion pass. Staged application and all 16 native signatures and
  relative/system loader references pass. Caller environment, active caches and
  the untracked private document are preserved. Windows/Linux remain implemented
  through the common Dawn/decoder paths but locally unvalidated.


### Linear HDR scenes and display mapping

Replaced the current immediate/mesh/native/viewport drawing path with RGBA16F
scene rendering followed by a full-screen exposure/tone-mapping/display pass.
No legacy eight-bit scene renderer is retained. Persistent targets own separate
HDR, mapped display and depth attachments; raw pre-map float readback and mapped
RGBA8 readback refer to the same completed frame. HDR clear and emissive material
limits keep half-float storage finite; exposure is not baked into materials.

Added PostProcess interface/implementation units with validated exposure stops,
LinearClamp/Reinhard/AcesFitted and output encoding. Default scene display uses
ACES fitted/sRGB. The Stephen Hill RRT/ODT fit includes its color matrices and
packaged MIT attribution; full ACES processing is not claimed. Shader/device
sRGB attachment handling applies final conversion once. Alpha bypasses exposure,
mapping and gamma. A review caught a hardware-sRGB linear-output branch applying
its inverse transfer after an unintended forward transfer; the shader now applies
the forward transfer only for manual-sRGB output. Windows/Linux branches use the
same Dawn APIs but remain locally unvalidated.

Native and immediate frames reuse owned internal HDR/depth working attachments;
resize prepares and validates replacement allocation before publishing it. Working
logical bytes are reported separately from persistent target residency. Target
accounting now includes HDR+display+depth (16 bytes per pixel); working storage is
12 bytes per pixel. GUI widgets bypass scene tone mapping and viewport images use
the completed mapped texture. Existing targets retain their pixels when settings
change until redrawn; bad input/settings leave prior state intact.

The new native HDR acceptance renders increasing-emission cubes, inspects actual
pre-map radiance above one, saves the GPU display PPM, presents/resizes a real native
window and verifies shared mesh/target release. The captured image was visually
inspected after lossless PNG format conversion. Face colors are CPU normal
visualization, not physical lighting or production game art. PBR/IBL/shadows/SSAO
and scene/editor/agent exposure controls remain planned, alongside the larger
editor/game requirements.

Initial test builds exposed missing standard declaration visibility in module
consumers (vector comparisons and expected), corrected with explicit includes.
Focused Debug GPU/post-process/native checks pass 45/45 in 18.46 seconds. The first
full Debug run passed 796/798: script tint and physics position acceptance retained
old ungraded pure-color assumptions (green mapped to RGB 90/207/47). Those semantic
checks now explicitly select linear diagnostics through the same HDR path, keeping
all tint/visibility/pixel/cache/physics assertions. New default-mapping checks retain
actual ACES/HDR/native coverage; no assertion was disabled.

- Final arm64 macOS Debug and Release/NDEBUG pass 798/798 CTest cases
  (742 doctest, 56 executable checks), taking 88.69 and 35.35 seconds.
  Focused semantic script/physics GPU checks pass 2/2 in 1.55 seconds. Full suites
  retain native window/GUI/editor/example/audio and independent exported playback.
- All 230 C/C++ files pass clang-format; diff whitespace and private-reference
  checks pass. Reviewed uniform offsets/sizes, exposure/curve ranges, half-float
  decoding, padded copies/unmap on failure, per-draw emission, render/pass resource
  retention, frame/target budgets, resize replacement and native format handling.
  No compiler/linker environment or assertion/warning policy changed.
- Latest macOS ZIP is refreshed at its existing path: 267 entries, 43,133,434 bytes.
  CRC, included ACES attribution, absence of acceptance/private/Finder content and
  runtime manifest metadata exclusion pass. Staged application and all 16 native
  signatures/relative-system loader references pass. Active caches and the private
  untracked document remain preserved. Windows/Linux are not locally validated.


### Direct physical materials and lights

Added a separate Lighting interface/implementation with owned perspective or
orthographic camera data and bounded directional/point/spot descriptions.
Validation normalizes applicable ray directions, checks all numeric fields/counts
and retains contextual expected errors. Renderer initialization and setters own
validated copies; invalid changes preserve previous state and completed targets.

The current surface shader now supports explicit unlit or metallic/roughness
materials through the same HDR pipeline. Physical shading uses GGX, correlated
Smith visibility, Schlick Fresnel and energy-split Lambert diffuse, with dielectric
F0=0.04, metallic base-color F0 and a stable minimum effective roughness. Linear
packed G/B roughness/metallic data, tangent normal maps and color emissive maps
affect actual lighting; normal maps require forward-hemisphere linear source
data. Base color/alpha continue to multiply authored factors/vertex/object color.
No IBL, invented ambient light, occlusion texture effect or shadows are supplied.

Physical draws require complete normals, invertible affine world transforms and
bounded coordinates/normal precision. Normals use a direction-preserving scaled
inverse transpose; reflected transforms adjust front winding, and double-sided
materials face the observer. Derivative position/UV frames preserve mirrored
normal-map handedness; unusable/degenerate UV faces fail before submission.
Perspective view rays depend on position, while orthographic rays remain parallel.
Directional lights use lux; point/spot lights use candela, inverse-square falloff
with a 1-cm distance floor and smooth range/cone edges. Accumulated scene radiance
explicitly saturates at the finite half-float ceiling.

Added a bounded UV sphere with radial normals, explicit seam/pole vertices and
outward triangles; degenerate finite-position precision fails. The native PBR
acceptance renders dielectric/gold/copper roughness rows and a normal-mapped sphere
under two direct lights, verifies actual HDR highlights, saves a GPU image, presents
and resizes a native window and checks shared mesh/target/texture release. Its
rendered image was visually inspected after lossless PNG format conversion.
This is a renderer API gallery, not persisted scene/material/editor authoring,
IBL/shadow/SSAO integration, large-scene performance or final realistic game art.
Those broader requirements remain planned.

The initial shader check failed because WGSL reserves "meta"; renaming the field
to "counts" restored actual shader creation without relaxing validation. The first
focused normal/material/light/HDR regressions pass 18/18 in 6.02 seconds. Final
focused material/light/native tests pass 12/12 in 5.40 seconds, including the full
64-light GPU array and orthographic reflections. Reviewed CPU/GPU struct offsets,
per-draw texture/uniform retention, world/normal transforms, UV/normal-map boundaries,
light units/falloff, finite HDR saturation, typed validation, resource release and
platform-neutral Dawn paths. Caller toolchain settings remain unchanged.

- Final arm64 macOS Debug and Release/NDEBUG suites pass 812/812 CTest cases
  (755 doctest, 57 executable checks), taking 107.26 and 37.48 seconds.
  All 236 C/C++ files pass clang-format and diff/private-reference checks pass.
- Refreshed latest macOS ZIP: 267 entries, 43,165,492 bytes. CRC, executable
  modes, attribution, private/test/Finder exclusions and runtime manifest checks
  pass. Staged app and all 16 native signatures/relative-system loader references
  pass. Build caches and the untracked private document remain preserved.
  Windows/Linux are implemented but not locally validated.


### Separate unit, integration and native checks

Audited the previously broad "unit" label against actual dependencies. Compiler,
filesystem, project, VM/physics and complete editor/game workflows are integration
or acceptance checks; opening desktop windows is not needed for CPU unit behavior.
Godot's test setup uses mock display/dummy audio services for scene/UI checks, while
Epic documents lightweight module tests separately from editor/PIE/screenshot
automation. Epic private-repository reads via the connected GitHub account returned
404; the Unreal comparison uses Epic's official testing documentation, not a claimed
private-source review. See Testing.md for primary references and local commands.

Split pure CPU cases into DotUnitTests and SDL dummy event/loop cases into
DotPlatformTests. Moved three real native surface cases from GraphicsTests.cpp
into NativeSurfaceTests.cpp, leaving actual offscreen pixel/material cases without
a Window/Application dependency. File/compiler/project tests are no longer labeled
unit; editor/game/distribution workflows have acceptance/native labels. All 812
existing CTest names and assertion bodies remain present. BUILD_TESTING and product
executables/packaging remain unchanged. Matching Debug/Release unit build and
unit/integration/offscreen-GPU/native/acceptance test presets allow focused iteration;
unfiltered Debug/Release remain complete delivery checks.

An initial label-list change exposed doctest's flattening of PROPERTIES values,
which also misplaced the dummy-display environment property: the partial-init SDL
case failed because it unexpectedly used a real display. Fixed registration to
apply list-valued labels after discovery through CTest's TEST_INCLUDE_FILES. The
actual inventory now verifies every device label, RUN_SERIAL flag and dummy
environment, rather than assuming the CMake spelling reached CTest unchanged.
The failing assertion was preserved.

Inventory comparison preserves all 812 identities (755 doctest, 57 executable).
Selections contain 182 CPU unit, 456 device-free integration, 59 offscreen GPU,
39 native and 85 acceptance cases; native/acceptance/device labels intentionally
overlap. Unit runs with deliberately invalid SDL video/audio drivers pass 182/182
in Debug (1.15 seconds) and Release/NDEBUG (0.45 seconds). Final corrected Debug
full suite passes 812/812 in 101.72 seconds. Reviewed dependencies, unchanged
assertions/lifetime cycles, dummy/native separation, preserved errors/thread
constraints, filter exclusions and generated labels/serialization.

Final corrected Release/NDEBUG full suite passes 812/812 in 37.60 seconds.
All 237 C/C++ files pass clang-format; diff whitespace/private-reference checks
pass. Latest native archive remains 267 entries / 43,165,492 bytes, with CRC and
private/test/Finder exclusions verified; staged application signatures are verified
by archive preparation. Active caches, latest package and the private untracked
document remain preserved. Validation is arm64 macOS only.


### Persisted physical materials and scene lights

Added a separate Dot.Assets.Material interface/implementation for GPU-free physical
material descriptions and one strict, bounded .dotmaterial structure. Factors,
texture paths/slot spaces and full sampler options are validated before loading;
files reject links/special files and unsafe parents, and saves are atomic. Material
codec/file acceptance passed before rendering consumers were exercised. Texture
sampling validation is now shared with direct renderer upload. No format version,
legacy loader, public header tree or compiler/linker override was added.

World scenes now own optional material bindings and directional/point/spot light
descriptions. Setters validate without opening devices/assets and preserve old
state on failure. Scene codec, document history, inspector/agent component edits,
Play clones and exported packages preserve these descriptions. Lights use entity
world position and local -Z direction; selected-camera world pose supplies actual
perspective/orthographic view rays. More than 64 enabled lights fails explicitly.
Built-in cube geometry is replaced by 24 sharp face-corner normal/UV vertices;
quad carries positive-Z normals and full UVs. Authored missing attributes are not
invented. CPU mesh-surface analysis is shared by GPU upload and export validation.

SceneRenderer now owns bounded shared mesh/material/texture caches, including
texture GPU bytes in its budget. Keys include actual paths and sampling choices;
new slot textures publish only after complete loads, and partial failure releases
pending resources. Pruning, explicit clear/reload, counters and creating-thread
constraints remain real. GPU readback tests cover saved physical response, point
light hierarchy/range, camera overrides, shared sampling, cache refresh/pruning,
wrong/missing resources, normal encoding, light counts and byte budgets. Export
checks actual materials/images and physical mesh normal/UV requirements before
publishing, with no leftover destination on failure.

Native PBR acceptance now loads saved scene/mesh/material/image assets before
rendering its fifteen-sphere gallery, checks HDR highlights and shared counts,
presents/resizes and verifies release. The GPU image was visually inspected after
lossless format conversion. Feature Lab retains its independent editable project
and now includes copper PBR, a normal-mapped quad and a directional headlight; its
real preview/play/export acceptance checks the new native components/cache counts.
These are basic inspector/resource/agent authoring capabilities, not a mature
material UI, IBL/shadows/SSAO, realistic finished game or large-scene performance.
Material/light script accessors and the requested script namespace/editor replacement
remain planned, along with the rest of the original roadmap.

Read-only Unreal source access succeeded through local gh as Ariaszzzhc, on the
repository's release branch. DateTimeTest.cpp and FoundationTests.Target.cs were
reviewed; the earlier connected-app 404 did not reflect the user's account access.
Testing.md records primary references. No Unreal code was copied.

Initial checks caught an unsupported expected comparison in a test, material-file
symlink handling inherited from canonical FileSystem access, test aggregate/API
usage and float32 JSON roundtrip expectations. Fixed explicit value comparison,
strict material path checks and test construction. Replaced the old primitive
layout assertion with full normals/UV checks; the missing-normal GPU failure case
still explicitly supplies missing normals. No failure assertion was removed.
Focused final material/scene/native regressions pass 38/38 in 17.08 seconds.
The inventory retains every prior name and adds 15 cases: 827 total, with 184 pure
CPU unit, 63 offscreen GPU, 40 native and 87 acceptance selections. Debug full
suite passes 827/827 in 122.47 seconds.

Final arm64 macOS Release/NDEBUG full suite passes 827/827 in 32.74 seconds
(769 doctest cases, 58 executable checks). Unit-only runs with invalid SDL video/
audio drivers pass 184/184 in Debug/Release (1.15/0.45 seconds). All 242 C/C++
files pass clang-format; diff/private-reference checks pass. Reviewed owned
asset descriptions, strict parsing/types/paths, cache failure/pruning/byte accounting,
texture sharing/sampling/normal encoding, camera/light hierarchy, CPU/GPU surface
requirements, editor transactions and export-before-publication failure cleanup.

Latest native ZIP is refreshed at its existing path: 273 entries, 43,401,235 bytes.
CRC, new Feature Lab material assets, executable modes, runtime manifests and
private/test/Finder exclusions pass. Staged app and all 16 native strict signatures
and relative/system loader references pass. Preserved build caches/latest package
and the private untracked document; no Windows/Linux validation is claimed.


## 2026-10-13 — Engine-owned script modules

The single SDK implementation now lives in assets/scripts/sdk and is compiled by
pinned TypeScript at build time. Generated ESM and matching ambient declarations
are embedded privately in the static engine. Dot.Script.Modules provides the
immutable exact-name registry for dot:engine and native dot:host; unknown reserved
names never use a project-file fallback. Engine imports are realm-cached, with
project-module quotas separate from the fixed registry and VM memory still counted.
Unregistered native services remain contextual DotHostError failures.

All project, template, example, scaffold and acceptance callers now import
engine-owned dot:engine. Removed copied SDK source trees and SDK directory inputs,
including the native create command. Source statistics and published module trees
count only project files. Feature Lab still independently compares its inventory
with the sole authored SDK and exercises every existing runtime member.

Project imports uniformly omit extensions. The shared resolver requires exactly
one regular .js/.mjs output with portable canonical paths and exact filename case;
it rejects extensions, ambiguity, missing files, symlinks, root escape, packages
and directory/index guessing. Compilation injects engine declarations outside the
snapshot. Private declaration aliases give extensionless .mts imports their real
types/default exports while preserving .mjs emission and original source maps.
Before returning a candidate, the compiler uses a separate bounded VM to parse and
link the static graph without initializers/constructors/host calls. Dynamic imports
are checked at execution. Final review added an explicit post-link deadline check.
No legacy import/SDK loader or format compatibility branch was added.

Retained failure, boundary, cleanup, reload, scene/native-service, Play, export and
relocation coverage. Added six tests for registry ownership, strict resolution,
injected typing, static graph rejection without execution, .mts default typing/
ambiguity and cached engine modules with explicit missing-service errors. Four old
case titles now describe the replaced contracts. Protected resource-path guards
and their write-rejection tests remain; only test-owned marker files supply their
fixtures. Automatic review rejected an earlier proposed removal of those guards;
the safer implementation leaves Resources.cpp byte-for-byte unchanged.

Early verification caught source/module-count expectations, TypeScript's lack of
extensionless .mts probing, a leftover reload import, and incorrect test assumptions
about HostError (a type) and its actual name/code/message transport. Fixed those
contracts and retained rejection assertions. The final deadline check initially
missed the nested Clock qualifier; compilation caught it and Impl::Clock fixed it.
An archive invocation named a nonexistent test target; corrected it to the existing
DotTests/Template targets before any verification result was claimed.

Final arm64 macOS Debug and Release/NDEBUG full suites pass 833/833, in 118.20 and
29.90 seconds (775 doctest cases, 58 executable checks). The six added cases retain
184 pure CPU unit, 63 offscreen GPU, 40 native and 87 acceptance selections;
device-free integration is 469. Intermediate final CLI integration checks passed
469/469 in Debug/Release (31.01/5.42 seconds), and reviewed module/project/template/
command checks passed 21/21 (5.01/1.81 seconds). No sanitizer was used. All 243
engine/test C/C++ files pass clang-format; diff/reference checks pass. Reviewed
registry/source ownership, reserved-name rejection, exact/portable resolution,
static linking without execution, quotas/deadlines, partial failure cleanup,
project/template publication and protected resource write checks.

Refreshed the latest native ZIP at its existing path: 257 entries, 43,408,651 bytes.
CRC, modes, manifests, embedded-module imports and private/test/Finder/SDK-copy
exclusions pass. The app passes deep strict signature checks; all 16 Mach-O entries
(including three symlink aliases) pass signatures and relative/system loader checks.
Removed the owned temporary migration script; retained build caches, latest package
and the private untracked document. Windows/Linux remain implementation-only.
The larger rendering/editor/game roadmap remains unfinished; this stage closes
script namespace and import syntax requirements only.


## 2026-10-14 — External source editing

Removed the embedded source-edit widget, code/preview switch and duplicated GUI
source draft. The viewport stays visible. Resources select text files and open
externally by button/double click; new scripts and Script inspector source buttons
use the same open-external workspace operation. Agent TextDocument drafts, file
identities/revisions, exact disk baseline/conflict checks, save/close/discard,
compiler checks and Play hot reload remain. Scene shortcuts address the scene;
agent saves are explicit, with status/save/discard controls but no source editor.

Dot.Platform.ExternalFiles encodes bounded absolute UTF-8 file URIs, including
reserved/non-ASCII bytes and Windows drive/UNC handling, without shell text.
Existing regular files launch through SDL's system-associated application service
on the main thread. Success is launch acceptance, not proof of loading; application
ownership is external. Workspace hosts can supply an owned launch service; tests
use this explicit seam for failure/exception/reentry behavior, not device success.
ResourceStore.externalTextPath reuses actual metadata, exact path, symlink and
readonly guards without loading file bytes, allowing malformed source repair.
Protected resources cannot bypass their write policy through an external editor.

Bounded pure diagnostic parsing maps src/file.ts(line,column) back to authored
source paths with positions and open buttons, leaving other errors as raw context.
It rejects unsafe/overflowing positions/paths and excess input/count without partial
publication. Actual Go compiler output initially showed its first diagnostic sharing
the exit-status line; separating context and diagnostics fixed the real integration
case. Parsing does not replace compilation failure with success. GUI/agent queries
expose check text and source diagnostics; default app opening does not claim universal
line/column navigation. External language-server configuration and automatic external
asset watching are not implemented.

Added eight cases: URI encoding/input bounds, pure diagnostic mapping/capacity,
worker/missing/special-file launch rejection, resource/readonly/move/metadata guards,
workspace launch/reentry with preserved scene and agent state, expected/throwing
launch failures and retained drafts, and actual compiler diagnostics/repair/clearing.
Native workspace acceptance retains its source-save and lifecycle checks using the
agent save operation and adds an actual external filesystem edit before hot reload.
Initial compilation caught a void-result helper use and doctest macro precedence;
fixed explicit expected-result checks and parenthesized diagnostics. Reviewed the
Script inspector against actual scene JSON and corrected its source field lookup.
Focused real compiler/native/URI/resource checks pass 12/12 in 7.89 seconds.

A live packaged Debug workspace accepted open-external for a test-owned .txt path
containing Chinese, spaces, # and %. Native TextEdit showed the exact decoded file
and content. TypeScript mover.ts opened through the real OS association in Zed,
with the exact project path observed; closed only that added tab, retaining the
previous tab. The test workspace closed through its public control channel.
Visual inspection showed the preview and absent code-page switch. Computer-use
pointer attempts did not switch the self-drawn tab; mouse interaction is not claimed
as manually verified. Slow UI observations did not cause duplicate launches: the
specific live editor handle was polled and retained until explicit closure.

Final arm64 macOS Debug and Release/NDEBUG full suites pass 841/841, in 134.51 and
35.76 seconds (783 doctest cases, 58 executable checks). Selections are 187 pure CPU
unit, 471 device-free integration, 63 offscreen GPU, 43 native and 90 acceptance;
labels overlap. No sanitizer was used. All 250 engine/test C/C++ files pass
clang-format and diff/private-reference checks. Reviewed URI/path/thread handling,
callback captures/reentry/exception propagation, nonexecuting diagnostics, readonly
checks, agent draft ownership/conflicts and source/editor/Play lifecycle behavior.

The latest native ZIP is refreshed: 257 entries, 43,413,654 bytes. CRC, executable
modes, manifests, embedded namespace imports and private/test/Finder/SDK-copy
exclusions pass. The app passes deep strict signatures; all 16 Mach-O entries pass
strict signatures and relative/system loader checks. Cleaned only the owned manual
acceptance project after closing its workspace and added source tab; retained build
caches/latest ZIP and the private untracked document. Windows/Linux implementation
uses SDL's platform launch support; neither was validated locally. Remaining PBR
lighting/shadows/IBL/SSAO, mature editor tooling and realistic game/scalability work
are still required by the active goal.


## 2026-10-14 — HDR environment preprocessing

Added the bounded CPU environment contract/face convention before GPU consumers:
opaque linear 2:1 HDR panoramas, power-of-two dimensions, sample/work limits, exact
logical byte accounting and native WebGPU face axes. Renderer now owns immutable
shared environment maps with radiance cubes, cosine-weighted diffuse irradiance/pi,
GGX reflection mips and correlated-Smith split-sum BRDF integration. Source mip
averages use latitude solid angles; manual float32 interpolation avoids optional
filtering features and uses sampling-footprint LOD. Uniform data is independent for
every dispatched face/mip. Error scopes, actual queue completion and source release
precede publication. Shared output and temporary source storage share the texture
budget; failure does not publish partial maps, and timeout faults further device
operations. Shared copies count once and release correctly after renderer shutdown.

Compiled the single GPU shader and tested before planned material/scene consumers.
Explicitly cast the C++ bit_width result and imported the CPU image module after
compilation caught narrowing/module visibility. Float readback exposed NaN at the
+Y pole of the one-texel mip: atan2(0,0) had no defined longitude. Guarding the pole
fixed it without weakening pixel validity/energy assertions. Native tests cover
constant HDR energy on every cube face/mip, independent linear-field moments and
roughness attenuation, analytical BRDF limits, bounds/budget/foreign/thread/shared
and post-device lifetimes, plus actual Poly Haven HDR processing. Focused checks
pass 7/7 in 5.06 seconds, including the new runnable offscreen acceptance driver.

Downloaded Studio Small 09 1K from Poly Haven's official asset endpoint, verified
its official byte count/MD5 and recorded SHA256/author/CC0/source links. Stored the
unmodified 1,615,248-byte asset locally; no runtime API client or online dependency.
The acceptance driver writes a diagnostic Reinhard/sRGB cube cross from actual GPU
float pixels; visually inspected after lossless PPM-to-PNG conversion. It shows the
studio roof, walls/floor and softbox highlights with the expected adjacent faces.
This evidence establishes preprocessing, not a lit game/editor scene or full IBL.
Surface/background sampling, saved environment authoring and editor/Play/export
integration remain the next dependency layer. Shadows/SSAO and large-game/editor
quality work remain required by the original active goal.

Separated all three built-in shaders into independently authored WGSL files after
review exposed formatting omissions inside C++ raw strings. Expanded compact
structs/cases/statements, restored two-space indentation/operator spacing, bounded
lines to 100 characters and separated functions/control blocks without padding
block interiors. Preserved the ACES attribution/notice. Comparing comment-stripped,
whitespace-normalized code with the original shader strings passed for all three;
no shader expressions or GPU bindings changed during extraction. CMake generates
a private embedding header with explicit dependencies and delimiter validation;
a subsequent source edit regenerated it and rebuilt only the renderer implementation
within the engine. Product executables and the archive need no WGSL source paths.

Reviewed the environment boundary, storage accounting, shader layouts, ownership,
error scopes, GPU completion/readback and test-only acceptance wiring. All 253
engine/application/test C/C++ files pass clang-format; all three WGSL files pass
two-space indentation, 100-character line and trailing-whitespace checks. Debug
passes 848/848 in 131.60 seconds; Release/NDEBUG verification follows below.
The seven added cases are two pure CPU, four real offscreen GPU and one runnable
offscreen acceptance case; existing coverage remains. Only arm64 macOS was tested.

Release/NDEBUG passes 848/848 in 35.32 seconds. Refreshed the native editor ZIP:
257 entries, 43,455,260 bytes; CRC, executable modes, complete project manifests,
and private/test/Finder/copied-SDK/WGSL-source exclusions pass. The application
passes deep strict signature checks; all 16 Mach-O entries pass strict signatures
and relative/system library-reference checks. Removed only this stage's owned
temporary extraction/format/package-audit/download-metadata files; retained build
caches, latest native archive and local HDRI. Surface IBL, sky and persistent
authoring consumers remain pending, as do soft shadows, SSAO and mature editor/game
capabilities. No Windows/Linux validation or full IBL delivery is claimed.

## 2026-10-14 — HDRI surface lighting and camera sky

Connected the independently tested environment maps to physical mesh rendering:
shared transactional binding, bounded intensity/yaw, mapped-normal diffuse,
perceptual-roughness specular LOD and correlated-Smith split-sum BRDF. Environment
absence uses an explicit direct-only shader variant; no dummy environment asset
or hidden light is substituted. Unlit draws retain their established behavior.
Added an HDR fullscreen sky with inverse-view-projection perspective rays and
orthographic emitted rays, matching panorama rotation, preserved geometry depth
and the existing post-processing pipeline. Lazy native pipeline creation is checked
before publishing a new binding. Singular/horizon/on-plane sky input fails before
render submission and retains target pixels/frame index. Shared snapshots preserve
map ownership across unbinding. No renderer environment settings were added to
scene serialization or editor UI yet.

New real GPU tests compare constant diffuse/metal energy against the analytic
rough correlated-Smith integral, intensity and emission/unlit/unbound behavior;
verify foreign/missing/nonfinite/range/thread/retained-binding failures and shared
lifetime; check directional camera/yaw/orthographic/depth/HDR/display behavior.
A scene test consumes decoded material resources and actual scene-camera poses
without direct lights, preserving independently owned maps across cache clearing.
The acceptance driver additionally renders six copper/ceramic spheres under the
actual local Studio Small 09 HDRI and writes GPU ACES output plus float readback.
Visually inspected the resulting 960x540 image: distinct studio reflections,
roughness blur, colored diffuse/highlights and coherent sky. This is renderer
capability evidence, not an authored large game or mature environment editor.

Initial test compilation required explicit Math import and required draw matrix
arguments. The first GPU run identified incorrect test expectations: the field's
negative Z channel is zero, and the test's invalid-settings copy still owned the
map. Corrected the reference/explicit lifetime rather than changing the renderer
to satisfy those expectations. Four focused cases then passed in 4.10 seconds.
Review also guarded a zero camera view ray before cube sampling, matching direct
BRDF handling; subsequent verification includes that boundary. All WGSL remains
standalone with two-space blocks, separated statements/functions and bounded lines.

Final verification: Debug full suite 852/852 in 136.25 seconds, followed by all
72 Debug offscreen cases after the final zero-view-ray guard in 36.50 seconds;
Release/NDEBUG full suite 852/852 in 37.66 seconds. Registered layers are 189 unit,
471 device-free integration, 72 offscreen, 43 native and 91 acceptance (overlapping
labels). All 253 C/C++ files pass clang-format; all six WGSL sources pass indentation,
line-length and trailing-whitespace checks. Reviewed shared ownership, transactional
error paths, uniform alignment, variant binding layouts, HDR/depth and camera-ray
boundaries before commit. The refreshed native ZIP contains 257 entries and
43,474,864 bytes; CRC, executable modes and source/test/private/Finder/SDK-copy
exclusions pass. Deep strict application signatures and all 16 Mach-O signatures/
relative-system library references pass. Only arm64 macOS is validated.

Next dependency layer remains persistent HDRI references/settings, environment
asset/cache lifetime and editor/Play/export authoring. Soft shadows, SSAO, advanced
editor interaction/scalability and the realistic game remain required by the full
active goal. No copied engine sources, compatibility wrappers or new dependency
were introduced in this stage; build caches/latest package and the private document
remain intact.

## 2026-10-14 — Authored HDRI environments

Added scene-owned HDRI references/settings with CPU-only portable-path, bounded
processing and finite intensity/yaw validation. Strict scene serialization retains
the environment independently of entities. Document environment operations share
atomic validation, revisions, undo/redo and saving. Play copies the configuration
without sharing mutable authoring state. SceneRenderer loads real local HDR bytes
and owns one shared bake within the existing residency budget. Intensity/yaw/sky
edits reuse it; resource/quality changes replace it transactionally within old-plus-
new peak capacity. Failure retains the bound map/frame with resource context.
No-environment scenes clear stale bindings. Cache clearing/destruction compare
resource identity to preserve another service's newer binding. Asset reload bakes
changed bytes at the same path. Export validates actual HDR pixels/work before
publishing and retains local references/bytes across relocation. Feature Lab now
includes its offline Studio Small 09 HDRI and saved scene settings in templates.

Added an Environment pane with Apply/Reset, selected HDRI, intensity/yaw/background
and explicit quality presets; custom saved work settings remain until selection.
Drafts protect Save/Play/export/close and participate in save-close. Agent staging,
apply/reset and pane navigation accompany document edits. Native panel rendering
caught a missing EndDisabled; fixed it with GUI validation enabled. Focused checks
also caught the document operation's early allowlist, missing fixture parents and
incorrect test helper usage (close revisions and document-only snapshot metadata).
Corrected implementation/API usage while retaining strict boundaries. Tests cover
CPU/codec failures, transactions, cache/reload/quality/budget/symlinks/lifetimes,
Play isolation, workspace drafting/reopening and package relocation.

Opened the signed application and visually inspected the HDRI Feature Lab and
Environment pane. Navigation used the supported agent command because native UI
automation did not activate the custom-drawn tabs; no manual mouse-apply result is
claimed. Closed only verification windows/control stages before full regression.
The first Debug run passed 857/859; two unchanged Prism gameplay tests hit script
wall-clock deadlines while the Release compiler was concurrently active. Scheduling
contention is the working hypothesis; sequential full reruns follow, with no script
budget or assertions relaxed. A documentation write accidentally replaced this
log with the delivery summary; restored the full committed history before adding
this entry. Async bake/import/disk caches, shadows, SSAO, advanced editor tooling
and the realistic game remain required by the original active goal.

Final sequential verification: Debug 859/859 in 139.71 seconds, Release/NDEBUG
859/859 in 39.22 seconds, including unchanged script deadline checks and actual
Feature Lab native workspace/standalone export. Both previously timed-out gameplay
cases pass without changing budgets. Finish concurrent compilation before full
CTest runs to avoid competing with wall-clock integration limits. Registered
coverage is 190 unit, 475 device-free integration, 73 offscreen, 44 native and 92
acceptance (overlapping labels); 800 doctest cases plus 59 executable checks.
All 254 engine/application/test C/C++ files pass clang-format; six WGSL sources
retain line/whitespace checks. Reviewed schema boundaries, atomic edits, renderer
ownership/detachment, cache accounting/error recovery and export path validation.

The refreshed native ZIP has 259 entries, 45,871,767 bytes. CRC/executable modes,
exact HDRI hashes in project/template copies, scene references and private/test/
control-stage/Finder/SDK-copy/WGSL-source exclusions pass. Deep strict application
signatures and all 16 Mach-O signatures/relative-system library references pass.
Only arm64 macOS was validated. Removed only owned manual request files; verification
windows/control stages are closed, while build caches/latest package and the private
untracked document remain intact. Full large-game/editor requirements remain active.

## 2026-10-14 — Cascaded directional soft shadows

Built and tested the CPU shadow contract before GPU consumers: bounded work,
finite camera/light/caster validation, practical logarithmic/uniform splits,
sphere-square footprints, texel-aligned XY, overlap and off-camera caster depth
coverage. Three pure cases passed before rendering implementation. Added actual
Depth32Float caster passes, independent per-draw/cascade uniforms, checked working
map allocation/reuse/release and single-command-buffer ordering before HDR shading.
Shader variants apply sixteen-tap/hardware comparison PCF to one explicit directional
source, blend cascade overlaps and preserve other lights, IBL and emission.
Caster/receiver controls are independent; matrices are checked before depth passes.

Initial GPU compilation rejected WGSL's reserved meta identifier; renamed it.
Native pixel tests then exposed self-shadowing on the tilted receiver because
comparison references ignored the plane slope. Derived gradients from world
position derivatives outside nonuniform flow, corrected each tap's reference and
included the full bilinear neighbor footprint. Retained the self-casting floor
and its unoccluded-energy assertions. Separated wide-softness measurement from
four-cascade coverage; the wide kernel physically produces partial illumination
inside small shadows. Camera-height cases verify each cascade and off-camera
casters. Geometric normals drive bias independently of normal-map perturbation.
Three GPU cases pass for moving occlusion, cast/receive flags, measured softness,
map resizing/release, cascades, light isolation, IBL/emission and input retention.

Saved light quality and mesh flags share scene codec/document/inspector/Play/export.
Added explicit directional-shadow enabling and integer work widgets. Unsupported
point/spot or multiple directional requests fail. Feature Lab's authored camera
headlight requests four 512-pixel cascades, alongside its real local HDRI. A saved
scene GPU test verifies light pose, encoded quality and caster toggles. Strict codec
checks reject wrong types/work; document checks include failure rollback, undo/redo
and save/reopen. Expanded Light storage required an existing range loop to borrow
its test values. Full Debug found only the old canonical light expectation missing
shadows:null; the added roundtrip then exposed double fixture decimals versus the
engine's float32 storage. Corrected expected schema/types and kept full comparisons.

The BUILD_TESTING-only --shadow-test driver writes hard/soft/disabled GPU ACES
images and checks HDR differences/map release. Visually inspected the hard and
soft 960x540 renders after lossless conversion: matching caster geometry and softer
shadow edges on the ground. Ten focused cases passed in 5.73 seconds. Final Debug
passes 869/869 in 141.65 seconds; Release/NDEBUG passes 869/869 in 37.34 seconds.
Registered layers: 194 unit, 476 device-free integration, 78 offscreen, 44 native,
93 acceptance (overlap); 809 doctest and 60 executable checks. All 259 C/C++ files
and nine standalone WGSL files pass formatting/indent/line/whitespace checks.
Reviewed fit precision, ownership, shader layouts, receiver bias, error scopes,
pre-submission matrix failures and saved-scene consumers before commit.

Refreshed ZIP: 259 entries, 45,927,437 bytes. CRC/executable modes, authored
Feature Lab shadows/HDRI and private/test/control-stage/Finder/SDK-copy/WGSL-source
exclusions pass. Deep strict application signatures and all 16 Mach-O signatures/
relative-system references pass. Only arm64 macOS is validated. Retained build
caches/latest package and the private untracked document. Fixed-radius PCF is not
contact-hardening PCSS; point/spot/alpha-mask shadows, temporal filtering, pooling/
culling/instancing, SSAO, mature editor/scalability and realistic game work remain
required by the full active goal. No compatibility loader or new dependency added.


### 2026-10-14 — Native audio failure cleanup

Complete renderer-stage verification exposed the existing Output preparation test:
Debug and Release both retained three allocations/240 bytes after a 256-byte
backend budget failed. An isolated rerun reproduced it. LLDB allocation stacks
identified CoreAudio stream descriptions retained when the following sample-rate
query could not allocate; device post-initialization then allocated a converter
before fixed-callback storage failed. miniaudio's uninitialized-state guard skipped
normal device teardown, leaving the tracked-device table/converter behind.

Added a pinned miniaudio 0.11.25 overlay using the original upstream source checksum.
The patch frees descriptions on sample-rate failure and marks ownership after the
backend creates the device, allowing ordinary teardown on subsequent failures.
No allocator sweep, budget increase or relaxed assertions. Expanded native failed
preparation budgets from 1/16/256 to 1/16/256/512/1024. Reviewed partial ownership,
backend teardown/worker state, callback lifetimes and the source/patch pin.

The actual macOS failure test now passes three consecutive runs in 0.27 seconds,
with counters restored to zero. The combined working tree's full Debug suite passes
877/877 in 153.52 seconds; final dependency-comment rebuild then passes all 114
selected audio/occlusion cases in 13.08 seconds. The same combined working tree
passes Release/NDEBUG 877/877 in 36.29 seconds, including all 32 audio-native cases. Only arm64 macOS is validated; supported
Windows/Linux branches are not claimed tested. The overlay changes no dependency
version beyond its local port revision.


### 2026-10-14 — Geometric SSAO renderer foundation

Added separate Dot.Graphics.Occlusion interface/implementation with device-free
settings, finite camera reconstruction and working-budget validation. The renderer
owns explicit optional settings, lazily checked shader variants and four textures:
Depth32Float, world-normal RGBA16Float, raw AO RGBA16Float and filtered RGBA8.
Current storage is 24 logical bytes/pixel with a 256 MiB/4096 dimension bound;
replacement/in-flight driver storage can overlap. Disable releases working maps;
readOcclusion reports actual grayscale visibility only after a committed mesh frame.
Settings/geometry/camera failures retain prior state and target frame counts.

An opaque geometry prepass supplies smooth geometric normals without normal-map
microdetail. Normal-aware radial horizon searches reconstruct world positions,
use bounded world distance, deterministic pixel rotations and quadratic near-field
steps; a world-distance/normal bilateral filter preserves silhouettes/background.
This is a radial approximation informed by NVIDIA's published SSAO presentation,
not a claim of full HBAO+/GTAO or temporal filtering. Composition attenuates only
IBL diffuse before HDR/ACES and preserves direct/specular/emission/unlit output.
Shadow depth, geometry/AO, HDR and tone mapping share an ordered command encoder.
Publication is restricted to successful mesh submissions, so a subsequent unrelated
readback cannot publish an abandoned pending result.

All maintained shaders are independent WGSL source files embedded through explicit
CMake dependencies into a private generated header; products need no source paths.
Added WGSL-specific two-space/100-column editor configuration, preventing the prior
four-space fallback. Retained operator/parameter spacing, one statement per line,
separated functions/control blocks and unpadded block interiors. Fourteen WGSL
files pass indentation/line/trailing-whitespace checks; all 264 engine/application/
test C/C++ files pass clang-format. WGSL syntax is validated by actual Dawn pipeline
creation; no unsupported claim of an installed automatic WGSL formatter is made.

Three CPU and four actual offscreen GPU cases cover bounds/reconstruction, flat
surfaces/background, contact/lifted objects, orthographic camera motion, normal-map
isolation, diffuse-only composition, resize/release, owner-thread errors and input
retention. The BUILD_TESTING-only --occlusion-test combines IBL, cascaded shadows
and AO, saves enabled/disabled/visibility images and checks HDR attenuation/storage
release. Inspected real 960x540 enabled and grayscale renders after lossless PNG
conversion: contact regions are occluded and background remains clear. Eight
focused checks pass in 9.05 seconds. GPU compilation first identified the reserved
filter identifier; renamed its entry point and retained all pixel assertions.

Complete checks initially exposed the audio initialization failure documented in
the prior entry. After its independent pinned overlay fix, Debug passes 877/877
in 153.52 seconds and Release/NDEBUG 877/877 in 36.29 seconds. Final dependency
comment rebuild additionally passes 114 selected Debug audio/occlusion cases in
13.08 seconds. Inventory: 816 doctest and 61 executable checks; 197 unit, 476
device-free integration, 83 offscreen, 44 native and 94 acceptance cases (overlap).
Reviewed CPU bounds, shader bindings/layouts, geometric coverage, thread/device
ownership, lazy allocation failure, ordered submissions and image publication.

Refreshed macOS ZIP: 259 entries, 45,963,331 bytes; CRC/executable modes, resource/
private/test/control-stage/Finder/SDK-copy/WGSL-source exclusions and actual HDRI
hash pass. Deep strict application signature and all 16 Mach-O signatures/relative
system loader references pass. Only arm64 macOS is validated. Retained caches,
latest archive and the private untracked document; removed owned patch/debugger/
audit scripts after verification. Saved scene/editor AO controls, temporal quality,
multi-layer/offscreen geometry, performance/scalability and realistic-game/mature-
editor work remain required by the full active goal. This foundation does not
claim those consumers or completion of large-game rendering.


### 2026-10-14 — Saved scene and editor ambient occlusion

Added optional scene-owned OcclusionSettings with CPU/thread/move validation;
strict scene JSON retains radius/bias/strength and integral directions/steps/filter
radius, with null/absence meaning disabled. Unknown/missing fields, wrong types and
unsupported ranges fail. Document occlusion operations share atomic decode/history,
undo/redo and save/reopen. No new schema version or alternate loader was added.
Play's scene copy remains isolated from authoring edits; relocated data packages
retain the exact configuration alongside their real HDR source.

SceneRenderer binds current scene AO settings for each prepared mesh frame. A
successful renderer setting request advances a checked binding generation, including
same-valued requests. Cache clearing and scoped destruction compare ownership by
generation, preserving another scene or a manual caller's equal settings. Disabling
releases AO working storage; it is renderer-owned, outside asset cache budgets.
The saved-scene GPU case measures actual HDR attenuation, diagnostic visibility,
disable/release, shared equal-value ownership, manual rebind and scoped destruction.
Failed setting requests preserve the generation; worker getters/setters reject access.

The Rendering pane enables AO, edits world radius/bias/strength and selects Low
4/2/1, Balanced 8/4/2 or High 16/8/4 directions/steps/filter quality. Controls fit
full-width below their labels; scrolling project tabs keep the selected pane readable.
Apply/Reset share the stage-occlusion/apply-occlusion/reset-occlusion transaction.
Inspect exposes draft status; show-pane accepts rendering. AO, environment and
inspector drafts cannot overwrite one another. Pending AO blocks unrelated edits,
Save/Play/export, protects native close, and Save and close commits before saving.
Discard leaves disk unchanged. Play/export errors now describe all scene drafts.

Feature Lab's ordinary project, template and exported native player now retain
balanced AO with the local Poly Haven HDRI and cascaded shadows. Native acceptance
reads actual AO pixels/dimensions/storage, draws both cameras and presents gameplay,
exercises workspace Play isolation, relocates an export, deletes authoring data and
starts the shipped player. The runtime AO driver now writes mesh/material/HDR assets,
saves/reopens the ordinary scene per mode and renders it through SceneRenderer;
enabled/disabled/visibility images still verify actual HDR attenuation and release.
Inspected the saved-scene 960x540 render after lossless PNG conversion. Opened the
current macOS app package with Feature Lab, navigated through its supported agent
show-pane operation and inspected Rendering controls/units/quality plus real preview
with native UI screenshots. Closed that owned verification window/control stage;
no manual mouse Apply result is claimed.

Added six identities: one pure scene case, codec/document device-free cases, one
saved-scene GPU case and two native workspace cases. Expanded existing GPU binding,
Play, relocated package and Feature Lab native assertions. Eighteen selected checks
including native laboratory and saved-scene driver pass in 29.60 seconds. Complete
Debug passes 883/883 in 163.10 seconds; Release/NDEBUG 883/883 in 34.00 seconds.
The final wording-only rebuild also passes both AO workspace cases in 3.08 seconds.
Inventory is 822 doctest plus 61 executable checks; 198 unit, 478 device-free
integration, 84 offscreen, 46 native and 96 acceptance cases (overlap). All 264
C/C++ files and fourteen WGSL sources pass the established formatting checks.
Reviewed module direction, optional schema boundaries, atomic draft/history rules,
thread/generation ownership, teardown, working-resource accounting and real consumer
coverage before committing.

Refreshed ZIP: 259 entries, 45,970,044 bytes. Source project/template AO and HDRI,
CRC/executable modes, private/test/control-stage/Finder/SDK-copy/WGSL-source exclusions
and exact HDRI hash pass. Deep strict application signature and all sixteen Mach-O
signatures/relative-system loader references pass. Only arm64 macOS is validated;
Windows/Linux implementation is not claimed tested. Removed owned UI request/audit
scripts; retained build caches/latest archive and private untracked document.
Temporal AO, offscreen/multi-layer geometry and populated-scene quality/cost remain
planned. The larger goal still requires scalable 3D submission/assets, mature editor
navigation/picking/gizmos/docking and the realistic polished Prism Relay game.


## 2026-10-14 — independent editor viewport and opaque picking

Implemented Dot.Graphics.View and Dot.Editor.Camera as separate module interfaces
and implementation units. Validated pose/lens/aspect/world limits; pixel projection
crops the exact top-left framebuffer pixel center without changing depth/W. Bounds
transform all eight corners through hierarchy/reflection. Camera flight normalizes
diagonal motion, orbit/pan/dolly are transactional and focus adjusts the clip range
for larger scenes. No editor camera entity or history operation is manufactured.

SceneRenderer now takes an explicit SceneView variant (primary game camera, entity
camera or independent view), feeding that camera to actual lighting/sky/shadow/AO
consumers. Loaded mesh bounds use the existing cache, without duplicate CPU geometry.
Renderer opaque picking uses a 1x1 R32Uint/depth pass with independent per-draw integer
uniforms, the same physical backface policy and synchronous readback. CPU mapping
retains full scene EntityIds, including values above 2^53. Visible preview targets,
HDR/AO publication and frame counters survive picking. Background clears selection.

Workspace defaults to independent Editor view; explicit Game mode and Play still
require actual game cameras. Stop retains the editor rig. Supported set-view,
navigate-view, focus-view and pick-view commands share native GUI implementation.
Current revision, displayed view and draft guards prevent selecting stale pixels.
Native SDL input starts drag ownership inside the viewport and cancels on release,
Escape or focus loss; game capture retains its prior path. The fixed control region
preserves viewport size across Play/Stop. Updated the prior missing-camera test to
prove Editor availability plus Game failure/undo recovery, preserving error coverage.

Added 10 CTest identities: 3 pure CPU, 2 offscreen picking, 1 scene rendering,
3 native workspace and 1 saved-project executable acceptance. The driver is under
tests/acceptance and compiled only with BUILD_TESTING; production mains gain no test
dispatch. Native input includes actual mouse/keyboard/release/focus-loss events.
The camera-free ordinary-project driver draws/picks/frames/orbits, compares actual
preview pixels, verifies authoring/history isolation and saves without inserting a
game camera. Initial driver fixture used an incomplete scene-scoped EntityId;
corrected it to resolve the actual primary camera. All existing consumer tests remain.

Sandbox native tests initially lacked a macOS display service; repeated them with
graphical-session access instead of changing SDL drivers or reporting fake success.
The 31 other focused checks passed in 24.45s; corrected driver passed in 1.86s.
Complete Debug passes 893/893 in 165.13s; Release/NDEBUG 893/893 in 37.69s.
No toolchain failure or sanitizer use occurred. Inventory: 831 doctest identities
plus 62 executable checks; 201 unit, 87 offscreen, 50 native and 100 acceptance labels
overlap. All 271 C/C++ sources pass clang-format; 15 standalone WGSL sources pass
indentation/line/whitespace checks and actual Dawn pipeline validation. Reviewed
module direction, ownership/thread/move checks, error paths, publication invariants,
projection/depth conventions, native input routing and appropriate test coverage.

Read the supplied dark/light editor references and prompt text. Recorded compact
hierarchy/Scene-Game/Inspector/assets placement and neutral dark/light palettes in
EditorDesign.md, explicitly keeping live engine behavior authoritative. The concepts
are visual inputs, not implemented features or performance evidence. Existing
workspace layout remains; theme/layout adaptation is the following stage. Opened
the refreshed native app with Feature Lab and inspected new controls/real preview;
verified an agent focus operation changed actual pixels while dirty remained false.
Closed that owned window and verified its control stage was removed.

Refreshed ZIP: 259 entries, 46,013,826 bytes; CRC/executable modes/exclusions, two
Feature Lab AO scenes and two exact HDRI copies pass. All 13 regular Mach-O files
and 3 relative signed library symlinks pass strict signature and relative/system
loader/rpath checks; deep strict app verification passes. Removed owned UI request
files; retained build caches, latest archive and supplied reference artwork. Private
untracked design source remains untracked. Only arm64 macOS is validated. Opaque
picking currently submits visible draws and reads synchronously; acceleration,
transparent/marquee picking, gizmos, docking/themes, populated-scene production and
the realistic polished Prism Relay game remain unfinished. The larger goal is active.


## 2026-10-14 — Docked editor workspace and mixed-script font

Implemented actual ImGui docking windows for Hierarchy, Scene, Inspector, Environment,
Rendering, Assets and Console. The initial layout keeps Inspector full height and
Assets/Console below Hierarchy/Scene. Tabs and splitters can be rearranged within the
same native window. Dark/light themes, reset-layout, queued docking and bounded UTF-8
hierarchy/asset filters expose inspected session state without changing scene/history
or game-camera data. Menus invoke existing document/export/external-source actions;
Console shows real compiler, Play and viewport diagnostics. No fabricated scene,
thumbnail, frame-rate or profiler control was introduced.
实现原生停靠面板、深浅主题、布局重置和有界搜索；界面及代理使用同一实现，
不改变场景历史或游戏相机，诊断来自真实引擎。源代码继续通过外部应用打开。

Pinned Sarasa UI SC Regular 1.0.42 by immutable archive SHA512 and license revision
f31c230af7fb4854f40d96560e2616eafdd85926. Its upstream UI family integrates Inter
Western glyphs with CJK. Installed only the regular face as EditorSans.ttf plus OFL
license; the application needs no system font installation. The 24,049,996-byte
packaged font equals the installed/source regular font, SHA256
81fc5857cbc9b05a16a75c7ad3e848032df52dddd29192f14b48d4b449c973f7.
采用更纱 UI SC Regular，西文字形由上游整合 Inter；固定版本、哈希及许可证，
随包提供一份字体，避免依赖操作系统字体安装。

Added Renderer.guiViewport to reserve an image during GUI layout and render it
synchronously afterward with current physical pixel dimensions. Normal GUI-build
GPU reentry remains rejected. Deferred callbacks retain completed same-renderer
targets through GUI encoding/submission; explicit absence removes the image command,
errors/throws publish no GUI frame and callbacks are never replayed. Sixteen slots,
finite dimensions/device limits, target ownership and recursive GUI/close boundaries
are checked. Private opaque layout tokens are resolved before any GPU submission.
新增布局后同步渲染阶段，使用本帧实际像素尺寸并保留目标所有权；错误、明确缺席
和异常均有可观察结果，不以假帧或旧尺寸掩盖失败。

Initial validation exposed focus-dependent Undo, diagnostics shrinking the viewport
after Stop, image scrollbar width oscillation and inactive docking leaves temporarily
expanding Scene to the host. Fixed shortcut routing while retaining text-input undo,
moved diagnostics into Console, disabled Scene scrolling and preserved initialized
DockBuilder bounds/visible leaves. Explicit window position/size was rejected because
it implicitly undocked Scene. The final bootstrap is covered by actual geometry and
first-frame/native pixel checks; existing history/pixel assertions were preserved.
An initial font test lacked an algorithm include and a new renderer variable shadowed
an outer name; both were corrected. The first package audit incorrectly expected two
font copies; actual packaging intentionally contains one font inside the application.
No toolchain failure or sanitizer use occurred.

Added focused docking/theme/search boundaries and deferred GPU viewport/font lifetime
tests. Font tests load a staged copy, delete its file, move the renderer and verify
Western, Chinese, Japanese and Korean glyphs plus actual output pixels. GPU cases
cover DPI rounding/resize, lifetime release, foreign/unrendered/wrong-size targets,
absence, callback throws, limits and recovery. Native acceptance checks cover real
theme/docking/reset pixels and Cmd/Ctrl Undo. Home/Feature Lab first-frame checks remain.
All acceptance drivers stay under tests/acceptance and BUILD_TESTING.

Final macOS Debug: 898/898 CTest cases, 177.40 seconds. Release/NDEBUG: 898/898,
42.59 seconds. Inventory: 836 doctest identities plus 62 executable checks; labels
overlap (201 unit, 90 offscreen, 52 native, 102 acceptance). All 272 C/C++ files pass
clang-format and git whitespace checks; 15 standalone WGSL sources are unchanged and
actual pipelines are covered by the suites. Reviewed module direction, ownership,
GUI phase/reentry guards, contextual failures, bounded session commands, input focus,
pinned font/resource packaging, platform assumptions and appropriate coverage.

Refreshed macOS ZIP: 259 entries, 47,092,128 bytes. CRC, executable modes/exclusions,
exact font/license/HDRI copies and two Feature Lab AO scenes pass. All 13 regular
Mach-O files and 3 relative library symlinks pass strict signatures and relative/system
loader/rpath checks; deep strict app verification passes. Inspected actual dark/light
windows and mixed Chinese/English hierarchy text using an unsaved temporary rename,
then discarded it. Inspected the final bundled default dark workspace and closed
that owned window with discard=false; source Feature Lab remains unchanged.
Removed owned research/request scratch files, retaining build caches, latest archive,
validation evidence and supplied reference artwork. Private design source remains
untracked. Only arm64 macOS is validated. Persisted layout profiles, gizmos, thumbnails,
large-scene production and realistic polished Prism Relay remain unfinished; the
larger engine goal remains active.


## 2026-10-14 — Transactional viewport translation handles

Implemented Dot.Editor.TransformTool as separate interface/implementation units.
AxisDrag computes closest world-axis/ray displacement with the engine's right-handed
[0,1] depth and top-left UV conventions, absolute start sampling and optional meter
snap. Near-parallel rays, non-finite/invalid UV, singular matrices and unbounded
deltas are explicit failures. TransformGesture owns an independent decoded preview
scene. World updates convert through the original parent inverse and strict TRS
decomposition, rejecting shear/singularity without approximating authoring data.
Commit checks original document address/scope/revision and adds one undo entry;
no-op/return-to-start adds none. Moved/stale/foreign/thread-invalid gestures fail;
cancellation/destruction releases the draft without document edits. No additional
third-party dependency or environment override was introduced.
新增独立模块，射线移动使用绝对首样本与米吸附；克隆预览不更改文档，父局部 TRS
校验拒绝错切与奇异，提交检查文档/范围/版本且只产生一条撤销记录。

Scene view now offers Select/Move, world/local axes and snap controls. Colored XYZ
segments are projected from actual selected entity transforms; hit-testing uses the
same observed segments. Behind-camera/clipped and parallel handles are omitted.
Native pointer gestures capture until release, commit once and cancel on Escape or
focus loss. Complete press/release in one frame cannot leave capture active. Local
axes respect hierarchy rotation/reflection; translation still maps correctly through
nonuniform parent scale. The preview clone renders through the existing post-layout
GUI viewport phase. Begin/update/finish invalidate stale target readiness until GPU
drawing completes; view.source explicitly identifies document/transform/Play.
世界/局部移动手柄来自真实投影并共享命中几何；原生释放提交、Esc/失焦取消，
预览变化即失效旧目标状态，布局后的同帧渲染重新发布实际就绪状态。

UI and agent share set-tool/begin-transform/update-transform/finish-transform. During
a gesture, document/view/layout/tool actions are locked; inspect/gesture/close/theme/
filter operations remain available. Agent gestures persist across frames until finish
and do not consume incidental native pointer motion. Inspector changes and Play are
blocked; close includes the pending draft in confirmation and releases it on discard.
Only translation handles are delivered; no fake rotation/scale, plane or multi-select
controls appear. The common implementation targets arm64 macOS and x86_64 Windows/Linux.

Foundation Debug tests passed before the workspace consumed the new module. New
coverage: one pure axis math identity, three document transaction identities and two
native/GPU workspace identities. The existing runnable viewport acceptance additionally
exercises live move preview, single commit and pixel-restoring Undo. Tests cover
perspective/orthographic/snap, parent reflection/shear rejection, stale and foreign
documents, thread/move ownership, no-op/cancel, actual changed/restored pixels, save/
Undo/Redo, stale readiness, native release/Escape/focus loss and same-frame click.
Initial compilation found incorrect filesystem fixture types, a signed color index
and unparenthesized doctest diagnostic ternaries; corrected them. A cancellation test
initially compared against a pre-Undo/Redo revision; changed its baseline to the actual
post-Redo snapshot while retaining full cancellation invariants. The first focused
workspace/foundation/acceptance run passed 7/7 (4.81s).

Full macOS Debug: 904/904 CTest, 182.62s. Final whitespace/interface-comment rebuild
then focused Debug: 7/7, 4.57s. Full Release/NDEBUG: 904/904, 45.81s. Inventory is
842 doctest identities plus 62 executable checks, with overlapping labels: 202 unit,
90 offscreen, 54 native, 104 acceptance. All 277 C/C++ files pass clang-format and
git whitespace checks; 15 standalone WGSL sources are unchanged. Reviewed module
direction, owned preview lifetime, reentry/thread/conflict boundaries, strict input,
parent transform conventions, capture/focus behavior and relevant test coverage.
No sanitizer or toolchain workaround was used. Automatic approval twice reported
an already-exited Debug test as live and rejected a rebuild/Release launch. Rechecked
completed logs and the process list (no CTest process), then retried successfully.
Actual builds and native/GPU test suites did not overlap.

Refreshed archive passes CRC/executable permissions, exclusions, exact packaged
font/license and two HDRI hashes. ZIP: 259 entries, 47,110,227 bytes. All 13 regular
Mach-O files and 3 relative library symlinks pass strict signatures and relative/
system loader/rpath inspection; deep strict app verification passes. Opened bundled
Feature Lab, selected its real Dynamic entity and inspected actual XYZ overlay and
Inspector; closed with discard=false, terminal exit zero and owned control-stage
removal. Source project remains unchanged. Removed owned UI requests and exploratory
probe source files; kept build caches, latest archive, audit/log evidence and private
reference artwork. Private design source remains untracked. Only arm64 macOS is
validated. Rotation/scale and plane handles, multi-selection, persisted profiles,
thumbnails, large-scene scalability and realistic polished Prism Relay remain
unimplemented; the full engine/editor/game objective remains active.


## 2026-10-14 — Rotation rings and axis scale handles

Extended Dot.Editor.TransformTool with RotationDrag, affine transformFrame and
absolute rotate/scale gesture operations. Rotation projects rays into an affine
operation frame and intersects a canonical plane; samples unwrap by shortest arcs
and snap the accumulated degree value, bounded to 100 turns. Invalid/non-affine/
singular frames, center samples, parallel rays, out-of-clip planes and invalid snap
fail without advancing the sampler. Local frame is parent-world * local T*R, excluding
entity scale and retaining parent stretch/reflection. Local rotation preserves original
position/scale; local scale preserves position/rotation. World operations preserve
the world pivot and reject parent-local shear. Positive factors .001..1000 retain
existing reflections without crossing singular zero. No compatibility path or new
third-party dependency was added.
新增旋转射线与仿射操作框架，按最短弧连续展开角度；局部操作保留原位姿中的其他
分量，世界操作严格拒绝无法表示的父局部错切，不以近似结果覆盖场景数据。

The Scene toolbar now offers Select/Move/Rotate/Scale with world/local controls and
mode-dependent meter/degree/ratio snap units. Genuine projected colored rings and
square scale endpoints use their rendered segments for hit-testing. Parallel/clipped
ring segments are omitted. All modes consume the same owned preview/commit/cancel
transaction and post-layout GPU rendering phase; native release commits once, Escape/
focus loss cancels and Undo restores actual data/pixels. The agent uses the same
revision-checked commands; tool.value reports the last successfully applied distance,
angle or factor while active, null otherwise. Kept interfaces/implementation separate
and external source editing unchanged.
界面、原生输入与代理共用真实事务；旋转环和缩放端点直接编辑场景克隆，提交、取消
和撤销保持一致，吸附单位明确且不伪造状态或新渲染能力。

Three new foundation cases passed 3/3 (0.24s) before UI consumers were added: affine
rotation-ray unwrapping/reflection, oblique perspective sampling and local/world
rotation/scale boundaries. Two native/GPU workspace identities and expanded runnable
viewport acceptance cover exact angles/factors, changed/restored pixels, single Undo,
world-shear rejection and ring/endpoint hit-testing. Existing Move, ownership, stale
revision, no-op, cancellation and focus coverage remains. Added a non-symmetric
30-degree quaternion check to distinguish sampling from matrix errors.

Initial focused verification failed two new assertions. The native history test
incorrectly expected total history growth after Undo despite redo-branch replacement;
corrected the fixture expectation while retaining one-step data/pixel Undo. Rotation
probes exposed a real prior move-stage defect: intended agent/native input isolation
was not actually enforced in viewportInput. Incidental native UV (0,0) overwrote an
agent's approximately 30-degree sample with 150 degrees. Added the missing source
guard, removed temporary probes and strengthened exact agent angle/factor/meter
assertions before and after frames. The deterministic 45-degree test accounts for
viewport aspect. Final focused suite passes 12/12, 6.79s. This fixes actual command
semantics rather than weakening pixel or scene coverage.
定向检查发现代理手势被原生鼠标覆盖的真实缺陷，补充输入来源保护与精确数值断言；
另外修正了 Undo 后 redo 分支替换的测试预期，保留单步撤销与像素还原要求。

Full Debug CTest: 909/909, 172.22s. Full Release/NDEBUG: 909/909, 41.16s, executed
sequentially after both builds completed. Inventory is 847 doctest identities plus
62 executable checks, with overlapping labels: 204 unit, 90 offscreen, 56 native,
106 acceptance. All 277 C/C++ files pass clang-format/whitespace checks. Fifteen
standalone WGSL sources are unchanged and real pipeline validation remains covered.
Reviewed module direction, lifetime/thread/move/reentry/conflict boundaries, affine
frame conventions, snapping/limits, strict commands and native/agent source routing.
No sanitizer, environment override or toolchain workaround was used.

The first archive audit caught a 6,148-byte Finder .DS_Store at the generated
distribution root, yielding 260 entries. Removed that temporary metadata, strengthened
the audit exclusion and rebuilt the archive. Final ZIP: 259 entries, 47,120,689 bytes.
CRC, executable permissions/exclusions and exact font/license/two HDRI copies pass;
all 13 regular Mach-O files and 3 relative symlinks pass strict signatures and relative/
system loader/rpath checks; deep strict app verification passes. Opened the bundled
Feature Lab, selected real Dynamic entity and visually inspected rotation rings, scale
endpoints and explicit units. Closed with discard=false; terminal exit zero, owned
control-stage removal and unchanged source project verified. Removed owned UI request
files, retaining build caches, audit/log evidence, latest ZIP and supplied reference
artwork. Private design source remains untracked. Only arm64 macOS is validated.
Plane/uniform-scale tools, multi-selection, persisted profiles, thumbnails, scalable
production scenes and polished realistic Prism Relay remain unfinished; the full
engine/editor/game goal remains active.


## 2026-10-14 — Conservative mesh frustum submission clipping

Added Dot.Graphics.Frustum with separate interface/implementation and a pure value
lifetime. Six homogeneous planes use right-handed [0,1] depth; transformed local
AABB corners are tested without W division. Double intermediates and conservative
float-roundoff margins retain boundary contact, reflection/shear/projective models
and extreme finite inputs without premature clipping. Invalid cameras/bounds/models
return contextual errors. Two pure foundation identities passed 2/2 (0.04s) before
Renderer consumers were added. Initial compilation exposed missing string operator
header visibility and an unexported Bounds dependency; corrected both explicitly.
新增独立纯数学模块，按齐次六平面保守裁剪变换后的局部包围盒角点；不除 W，
保留边界及浮点裕量，输入失败明确返回错误。

Renderer prepares and validates all mesh/material/texture/transform inputs before
pruning main HDR and SSAO geometry passes. Outside candidates skip per-draw uniform/
bind allocation and actual mesh emission. Shadow fitting and cascade passes still
consume off-camera casters, so their shadows on visible surfaces survive. Picking
tests against the cropped 1x1 pixel frustum while preserving original input indices
and full SceneRenderer entity identities. Persistent target, readback and native
surface paths all share this implementation. No culling toggle, compatibility path,
additional dependency or shader-source change was introduced.
主 HDR 与 SSAO 阶段共享裁剪，但阴影保留画外投射体；拾取裁剪保留输入索引，
避免选中错误实体。资源与材质仍完整校验，不吞掉画外无效输入。

VisibilityStats separately retains latest successful mesh/pick calls. Counters
increment beside actual draw encoding, not predicted flags, and publish only after
successful submission/validation; failures and Deferred presentation keep previous
reports. Before the first successful call, report is null. Main candidates may include
conservative false positives; counts are submissions, not rasterized objects, FPS or
performance guarantees. Rendering panel and workspace.view.draws expose actual
visible/input/culled/shadow/SSAO counts. Queries enforce owner thread, allow read-only
GUI layout observation and survive renderer moves without resource ownership changes.
统计来自实际绘制编码循环并在成功后发布，失败保留旧报告；界面不伪造 FPS 或
可见像素数量，阴影按各级真实提交次数计数。

Added five offscreen GPU identities. A 1024-input frame submits one main draw and
1023 culled draws with pixel equality to visible-only rendering; malformed hidden
tint is rejected while target/report stay unchanged. Pixel picking keeps original
index 2 after excluding earlier inputs and independently retains mesh reports.
An off-camera caster at (-5,5,0) still darkens a visible receiver, with two shadow
draws and only one main draw. SSAO geometry emits one draw for the visible-only
image. A 513-object scene shares one uploaded mesh, emits one color draw and updates
after a camera move. Native viewport acceptance now includes an outside object and
checks actual counts. Initial focus run passed seven cases and failed a dense-scene
assertion that pure green should display no red; existing ACES color transformation
correctly contributes red. Corrected it to green-channel dominance plus pixel change,
retaining actual scene/camera/counter invariants. Final focused checks: 8/8, 5.30s.

Full engine/editor Debug CTest: 916/916, 177.18s. Release/NDEBUG: 916/916, 40.53s,
run sequentially after both builds finished. All 281 C/C++ files pass clang-format
and whitespace checks; 15 standalone WGSL sources are unchanged and actual pipelines
remain exercised. Reviewed module direction, homogeneous depth/boundary conventions,
owned prepared handles, complete input validation, pass separation, stable picking
identity, success-only counter publication and thread/move behavior. No sanitizer
or toolchain workaround/environment override was used.

Package auditing twice found regenerated Finder .DS_Store at distribution root
after target recreation; manual unlink did not solve the race. Added ArchiveEditor
script and routed the archive target through a separate owned filtered snapshot,
with source/empty-directory/permission/symlink preservation. Staging validates its
owned name and disjoint paths; the new ZIP is published only after creation succeeds.
The BUILD_TESTING archive regression additionally rejects source/nested-source staging
and verifies the existing source/ZIP stay unchanged on failure. Passed separately
in Debug 1/1 (0.27s) and Release 1/1 (0.12s) after engine suites, without rerunning
unchanged rendering code. Final inventory: 917 CTest identities = 854 doctest plus
63 executable/script checks; labels overlap (206 unit, 95 offscreen, 56 native,
106 acceptance). This is suite-plus-regression evidence, not a claimed 917-case
combined-run timing.
发行目录重建后 Finder 会再次写入元数据；改为独立过滤快照并验证权限/链接/空目录
与失败保护，原目录保持原样，成功后才发布新 ZIP。

Final ZIP audit: 259 entries, 47,132,050 bytes. CRC/executable permissions/exclusions,
font/license/two HDRI copies and all 228 file/link payload equality checks pass. All
13 regular Mach-O files and 3 relative library symlinks pass strict signatures and
relative/system loader/rpath checks; deep strict app verification passes. Opened
bundled Feature Lab and inspected Rendering panel. Actual counts were 7 visible/7
input, 28 shadow and 7 SSAO draws; camera yaw 180 changed them to 0 visible/7 input,
7 culled, 28 shadow and 0 SSAO draws while ready=true and dirty=false. Closed owned
window with discard=false, terminal exit zero and control-stage removal verified.
Removed owned UI requests; retained build caches, latest archive, verification logs
and supplied reference artwork. Private design source remains untracked. Only arm64
macOS is validated. CPU preparation remains linear, still bounded to 4096 inputs;
spatial indexing, instancing, streaming, occlusion culling, per-cascade caster pruning,
production editor tools and polished realistic Prism Relay remain unfinished. The
full engine/editor/game objective remains active.


## 2026-10-14 — Stable GPU mesh instancing

Added Dot.Graphics.DrawBatches as an owned pure-value planning module with separate
interface/implementation. Compatible consecutive mesh/texture identity tokens merge
while disabled inputs are skipped and enabled source order is preserved. Scalar
material/transform data stays per-instance. The plan owns compact original indices
and run offsets; zero mesh tokens (including disabled) and more than 10000 inputs
fail. Initially missing explicit span/array headers caused module compilation errors;
fixed them. The stable-run selection passed before GPU consumers. The boundary case's
initial name had an underscore rather than the intended category dot; corrected the
name so both CPU cases are included in final focused/full runs.
新增独立拥有的稳定相邻批次规划，不重排启用输入，实例标量独立保留；明确验证
身份与 10000 项边界，不引入旧路径兼容代码。

Replaced private single-object surface/geometry/picking/shadow shader data with bounded
read-only storage arrays. Vertex instance_index addresses compact data; flat fragment
indices retain varying PBR/emission/sidedness/reflection policies. Pixel picking stores
the original integer input index, not an instance-renumbered or float identity.
Surface runs share mesh/four texture bindings; SSAO/shadow/picking runs share geometry.
Main and SSAO reuse one ordered attribute upload; all shadow matrices are stored per
cascade with the correct base instance. Actual device maxStorageBufferBindingSize/
maxBufferSize bounds are checked before allocation. Command buffers retain referenced
GPU buffers/groups; full malformed/foreign input validation precedes planning.
真实实例缓冲保存所有属性与整数身份；主/SSAO 共用上传，阴影按级联索引矩阵，
分配前按设备限制校验，不吞掉画外或非首实例的无效输入。

SceneRenderer/Renderer/CPU caster fitting limits now match the 10000-entity scene
capacity. A primary camera consumes one entity. Updated invalid bound tests to 10001
and replaced the old valid-4097 rejection case with actual 9999-mesh rendering, one
color command, mesh-cache sharing/release and retained invalid-clear-before-upload
checks. Per-pass statistics now distinguish instance quantities and actual draw
commands, publishing after success only. Renderer queries/moves/failures, culling
and original entity mapping retain coverage. Rendering panel labels both quantities.
No additional dependency, installed headers, toolchain override or sanitizer was used.

Initial real GPU smoke: 10/10 (10.22s), including primitive rendering, shadows and
picking. New PBR instance/reference case varies surface model, base color, emission,
metallic/roughness, reflection and nonuniform transforms with real IBL/SSAO. Sixteen
instances submit one color and one SSAO command, byte-identical to 16 independent
same-geometry resources preventing merging. Texture binding/equal-depth cases keep
source order and pick ID 0. Four-cascade three-instance shadow case matches HDR
reference while reducing 12 commands to 8. Native viewport acceptance uses two visible
shared cubes plus one culled cube, requiring one color command. Test compilation
initially referenced nonexistent uploadImage; corrected to the actual uploadTexture
API. Final focused checks: 16/16, 12.91s.

Full Debug CTest: 922/922, 184.64s. Full Release/NDEBUG: 922/922, 40.17s, run
sequentially after both builds completed. During final review found an old interface
comment still claiming 4096/uniform data; corrected only contract text to the verified
10000/storage-backed implementation, rebuilt both presets, then focused Debug
16/16, 13.00s. Inventory is 859 doctest identities plus 63 executable/script checks;
overlapping labels: 208 unit, 98 offscreen, 56 native, 106 acceptance. All 285 C/C++
files pass clang-format/whitespace; 15 standalone WGSL files pass indentation/line/
whitespace rules and real Dawn pipeline validation. Reviewed source ordering, scalar
attribute layout/offsets, original IDs, texture ownership, per-pass selection, device
limits, temporary GPU resource retention, complete failures and publication invariants.

Refreshed archive via metadata-filtered owned snapshot. ZIP: 259 entries, 47,143,881
bytes. CRC/executable permissions/exclusions, exact font/license/two HDRI copies and
all 228 file/link payload equality checks pass. All 13 regular Mach-O files and
3 relative symlinks pass strict signatures/relative-system loader/rpath inspection;
deep strict app verification passes. Opened bundled Feature Lab and read actual
control-interface statistics: 7 visible/input instances, 4 color draws; 7 SSAO
instances, 4 SSAO draws; 28 cascade instances, 16 shadow draws; ready=true, dirty=false.
Requested Rendering pane. Additional native screenshot inspection was blocked because
macOS was locked; no screenshot/visual result is claimed for this stage. Native input/
acceptance tests already passed, and actual application commands remained usable.
Closed owned window with discard=false, terminal exit zero and control-stage removal
verified. Removed owned UI requests and pass-edit helper, retaining caches, latest
archive, verification evidence and supplied reference artwork. Private design source
remains untracked. Only arm64 macOS was built/run. Grouping is consecutive and CPU
preparation remains linear; spatial acceleration, streaming, production import/
animation/editor tools and realistic polished Prism Relay remain unfinished. The full
engine/editor/game objective remains active.


## 2026-10-14 — Refit BVH mesh visibility

Added Dot.Graphics.SpatialIndex with separate module interface/implementation and
owned finite world AABBs, bounded to 10000 entries. Count changes build a balanced
replacement tree; same-count movement refits affected leaf/ancestor branches.
Validation and allocation precede publication, preserving the old index on failure.
Queries restore source order and classify outside/intersecting/strictly inside
subtrees; accepted inside subtrees avoid per-object plane tests. Rebuild statistics
report zero refitted nodes, distinguishing construction from actual branch refit.
新增拥有式平衡 BVH，数量变化重建，同数量变化重拟合；校验/分配失败保留旧树。
查询恢复原始顺序，严格内部子树免逐对象测试，重建与重拟合计数明确区分。

First built the CPU foundation and passed 4/4 spatial-index/frustum checks (0.06s)
before adding its renderer consumer. Tests compare randomized ordered perspective/
orthographic/moving/reordered queries with linear classification, owned data/copies,
unchanged reuse, partial refit, clear, 10000 bounds and invalid update preservation.
Frustum classification preserves homogeneous reflection/shear/projective/contact/
float-extreme behavior while making strict-inside classification available.

Renderer still validates every mesh/material/texture/transform before classifying.
Double-precision affine corner transforms produce outward-rounded float world boxes;
projective or float-unrepresentable bounds use direct homogeneous tests with explicit
unindexed counters. Intersecting BVH candidates retain exact local-box/model tests.
Preparation retains original slots, consecutive batch order, integer picking IDs
and all off-camera shadow casters. Fresh bounds reflect parent/world changes;
camera-only changes reuse the index. Published mesh/pick statistics retain the last
successful report on failures. Rendering and inspection expose real spatial counters.
渲染输入完整校验，世界边界向外舍入，特殊边界明确直接齐次测试；保留对象顺序、
拾取身份及画外阴影。统计只在成功提交后发布，编辑器与真实引擎行为一致。

Initial focused Debug checks passed 18/18 (14.50s), including GPU/native acceptance.
The dense 1024-input/one-visible fixture retains byte-identical pixels with fewer
than 64 total node/bounds/exact tests. New GPU cases cover camera reuse, one-object
motion/refit, deletion/rebuild, original pick IDs, projective/extreme unindexed tests,
and rotated/reflected/sheared/contact cases against homogeneous classification.
Native viewport acceptance requires actual unchanged-index statistics. Review then
clarified rebuild/refit counters and reserved bounds/index storage, rebuilt both
presets, and ran final full tests sequentially after compilation finished.

Final Debug: 926/926, 180.80s. Release/NDEBUG: 926/926, 44.83s. Inventory is 863 doctest
identities plus 63 executable/script checks; overlapping labels: unit 210, integration
610, gpu 168, offscreen 100, native 56, acceptance 106, compiler 134, platform 27,
process 19, audio-native 32. All 288 C/C++ files pass clang-format and whitespace
checks. WGSL is unchanged; real pipeline and pixel validation passed. Reviewed input
bounds, publication/allocation guarantees, finite precision, ownership, tree depth,
source ordering, malformed off-camera inputs, GPU resource lifetime and platform
assumptions. No dependency/toolchain/sanitizer change was introduced.

Packaged Feature Lab QA initially asserted index reuse on the first cached successful
submission, which correctly still reported rebuild. Corrected the QA helper to pan
the real editor camera before checking reuse. Actual final report: 7 indexed/visible
objects, 1 node test, 0 bounds/exact/unindexed tests, changed/refitted=0, rebuilt=false;
4 color/SSAO commands, 28 shadow instances and 16 shadow commands. Ready=true and
dirty=false. Closed the owned application normally (exit zero), verified private
control cleanup. No additional screenshot review was performed for this stage.

Refreshed the archive through the metadata-filtered owned snapshot. ZIP: 259 entries,
47,172,615 bytes; all 228 file/link payloads match the signed source tree. CRC,
executable modes/exclusions, exact Sarasa/Inter font/license and both HDRI copies,
13 regular Mach-O files/3 relative links, strict signatures, app deep verification
and portable loader/rpath checks pass. Removed owned app-request/helper scratch,
retaining caches, latest archive, audit evidence and supplied reference artwork.
Private design source remains untracked. Only arm64 macOS was built/run; Windows/Linux
implementation is unverified locally. Input validation/world bounds/update comparison
remain linear, and refit retains topology. Adaptive rebuilding, streaming, production
import/animation/editor tools and realistic polished Prism Relay remain unfinished.
The full engine/editor/game objective remains active.


## 2026-10-14 — Authored normal-map tangent frames

The model-import audit found the vertex contract could not retain author tangents,
so implemented this rendering prerequisite before glTF consumers. Vertex now owns
an explicit tangent XYZ/W: unit, orthogonal to a unit normal, W=+1/-1; all-zero means
absent. Meshes require complete or absent frames and triangle corners share W,
preventing interpolation across handedness seams. Shared validateVertex keeps immediate
and mesh-upload boundaries consistent. Geometry analysis permits constant UVs when
a complete authored frame exists, while retaining geometric/UV magnitude limits.
Flat-normal generation explicitly clears invalidated tangents and preserves source.
模型导入审查发现顶点不能保留作者切线，先实现渲染基础；框架明确校验单位、正交、
手性和完整性，接缝须拆分。作者框架不依赖非退化 UV 导数；生成平面法线清除失效切线。

Replaced the current DotMesh structure with five required vertex fields, including
losslessly encoded tangent. Missing/unknown/malformed attributes fail; there is no
old-format/version compatibility branch. OBJ and primitives explicitly retain absent
tangents where their source supplies none. GPU vertex stride is now 64 bytes, with
checked tangent offset 48. Surface instance flags choose authored/derived frames;
instance storage and stable original identities remain. Authored tangent uses model
transform, normal uses inverse transpose, and the interpolated frame is orthogonalized.
Model determinant preserves reflection handedness; observer-facing double-sided normals
preserve UV bitangent orientation. Derivative division executes only for absent frames.
保存格式干净替换，无旧版兼容。GPU 保留作者框架，并处理非均匀缩放、反射及双面
背面；仅缺失框架时重建，作者坐标不会被偷偷替换。

Added normal-quad.dotmesh to the ordinary Feature Lab project and bound its existing
normal-map panel to that actual saved asset. It has four vertices/six indices with
T=(1,0,0), W=-1, preserving the original UV orientation. Asset loading verifies the
saved basis; existing native editor/Play/standalone/export acceptance exercises it.
The showcase and bundled project/template copies retain normal-map material/image,
scene lights, HDRI, shadows and SSAO. No generated image or fake rendering backend.

First CPU geometry checks: 14/14 (0.11s). Initial focused Debug: 48/48 (17.43s), with
asset, PBR/visibility/instancing and native viewport coverage. Further review added
triangle seam validation and double-sided basis handling; focused authored checks:
5/5 (0.92s). Full Debug: 929/929, 190.96s. Then strengthened only the new GPU fixture
with significant Y perturbation and diagonal light (the earlier near-zero Y sample
was weaker evidence for bitangent direction), rebuilt both configurations and passed
final authored GPU 1/1 (0.44s). Runtime implementation did not change in this fixture
strengthening. Full Release/NDEBUG after that rebuild: 929/929, 43.31s. Tests compare
reference HDR pixels under reflection/nonuniform scale/back faces and prove that a
rotated author basis changes lighting; CPU tests cover nonfinite/unit/orthogonal/
missing/partial/seam failures, source ownership and flat-normal clearing.

Inventory: 866 doctest identities plus 63 executable/script checks; overlapping labels
unit 211, integration 612, gpu 169, offscreen 101, native 56, acceptance 106, compiler
135, platform 27, process 19, audio-native 32. All 288 C/C++ files pass clang-format;
15 standalone WGSL files pass spacing/line/whitespace checks and real pipeline creation.
Reviewed attribute layouts/limits, frame completeness/handedness, transform precision,
double-sided behavior, malformed input failures, RAII/cached asset lifetime, source
preservation, current schema and platform assumptions. No dependency, compiler/linker
environment override or sanitizer was introduced. No additional screenshot review.

Refreshed the distribution through the owned metadata-filtered archive snapshot.
ZIP: 261 entries, 47,174,666 bytes; all 230 file/link payloads match signed source.
Both saved tangent assets have the exact authored frames. CRC/modes/exclusions,
Sarasa/Inter font/license, two HDRI copies, strict app/Mach-O signatures and portable
loader/rpath checks pass (13 regular Mach-O files, 3 relative links). No unpublished
control/archive stages remain. Preserve caches, latest package, audit logs, private
untracked design source and reference images. Only arm64 macOS was built/run;
Windows/Linux implementation remains unverified locally. glTF/GLB parsing, material/
hierarchy conversion, transactional import, animation, production editor tools and
realistic polished Prism Relay remain unfinished. The full objective remains active.


## 2026-10-14 — Static glTF/GLB model bundles

Added pinned cgltf 1.15 through the existing vcpkg baseline, with one private static
C backend and separate Dot.Assets.Model interface/implementation. The loader uses
owned output and engine-rooted file access, not the library's implicit file loader.
GLB/internal, glTF/external and strict base64 resources share bounded conversion.
Triangle lists/strips/fans, sparse/strided accessors, normalized attributes and required
position quantization are supported. Normals/tangents/UV0/colors and selected node
TRS/reflection/hierarchy survive native conversion; missing normals become flat.
Metadata loss is explicitly reported. Default/first or explicit indexed scenes work.
新增固定解析依赖与独立模块；由引擎控制读取和边界，结果独立拥有数据，支持静态
网格、访问器与层级转换。保留节点与可渲染属性，元数据损失明确报告。

Added baked occlusion as the fifth linear PBR texture slot with strength [0,1],
affecting indirect HDR/IBL alongside SSAO while retaining direct light and emission.
Material codec/scene caches/game packaging/batching bind and validate it. Instance
storage is 272 bytes, occlusion offset 256, with matching surface/geometry shader
layouts. Current material files/schema are replaced cleanly, without compatibility
branches. PNG encoding adds bounded owned lossless output. Imported opaque PNG/JPEG
images become canonical RGBA8 PNG, retaining decoded RGB without gamma conversion;
alpha is ignored by opaque semantics. Base/emission use sRGB, MR/normal/occlusion linear.
五个 PBR 槽贯通渲染/保存/缓存/出包；遮蔽不暗化直接光或自发光，布局有静态断言与
真实像素验证。不透明导入按格式语义处理 alpha，色彩空间按槽指定。

Loader validates UTF-8/NUL/duplicate JSON, numeric/boolean/index types, extension and
unsupported-content semantics before relying on parser conversion. Reviewed cgltf's
numeric conversion behavior and added these boundary guards to prevent malformed
primitives being treated as zero/defaults. Range/stride/alignment/overflow and sparse
index ordering are checked before reads. Remote/absolute/traversal/percent-escaped
traversal/symlink references fail. Limits: 64 MiB/file, 128 MiB loaded bytes/parser
allocation separately, 256 MiB decoded pixels, aggregate one million vertices/three
million indices, 10000 generated entities. Singular/cyclic/deep hierarchies fail.

ResourceStore prepares complete native meshes/materials/images/model.dotscene in an
owned sibling directory, checks remaining project catalog capacity, then publishes
without replacing any destination. Failures before publication preserve source/project
and remove staging. Public writer validates prepared capacities, primitive/material/
image references and native data. Source updates are caller-serialized; no hostile
race isolation/crash durability is claimed. Existing-directory errors, owner-thread
checks and external file failures retain context. Catalog refresh after publication
explicitly reports committed state if it fails.

Native Assets Actions and the shared import-model command expose conversion/report/
scene selection. Add model to scene uses the same instantiate-model document operation,
loading the generated native scene and remapping all entity/parent IDs within one
history transaction. Undo retains reusable assets, preserves the main camera/environment
and restores pixels; stale/capacity failures preserve authoring. Generated text assets
can open externally, without an embedded code editor. Native acceptance authored a
real light; importer does not insert substitute illumination into the project.
导入资源与实例化分离；完整资产目录原子发布，插入层级统一事务/撤销，保留主场景
相机和环境。错误不覆盖已有内容，生成资源可外部编辑，不内置代码编辑器。

Foundation material/GPU checks passed 17/17 (9.44s) before the model consumer.
First parser cases passed 3/3 (0.23s); initial end-to-end focused cases 8/8 (3.53s).
Initial compilation needed an explicit renderer/files module import, test vector
header, parenthesized doctest condition and actual edit/remove/workspace inspection
APIs; fixed those errors. A later guard insertion duplicated a formatted declaration
and shadowed a local name; corrected before final builds. No environment override or
sanitizer was introduced. Final focused checks: 10/10 (3.54s), including invalid
numeric/index inputs, sparse/base64/quantized data, collision/capacity/thread/symlink/
partial-publication failures and PNG ownership/invalid-storage boundaries.

Imported textured reflected hierarchy produces HDR pixels exactly equal to an
independently authored native geometry/material/texture reference. GPU picking retains
actual entity identity and cache clearing releases binding entries. Baked-occlusion
pixels verify indirect-only attenuation and strength zero. Runnable native editor
acceptance imports the actual GLB through the workspace command, inserts hierarchy
and lighting, changes/restores actual viewport pixels, undoes/redoes once and saves/
reopens. Tests are only built with BUILD_TESTING. Fixtures are owned diagnostic data,
not production art; no realistic-game readiness claim is inferred from this workflow.

Both final builds completed before serial full runs. Debug: 939/939, 191.67s.
Release/NDEBUG: 939/939, 48.13s. Inventory: 875 doctest identities plus 64 executable/
script checks; overlapping labels: unit 211, integration 621, gpu 172, offscreen 103,
native 57, acceptance 107, compiler 135, platform 27, process 19, audio-native 32.
All 296 C/C++ (including C backends) and 15 WGSL files pass established formatting;
real pipeline/pixel validation passes. Reviewed owned parser/buffer lifetimes,
allocation/range math, input semantics, current codec references/layouts, publication
and history guarantees, resource cleanup, portable paths and platform assumptions.
No additional screenshot review was performed.

Archive refreshed through metadata-filtered owned staging. ZIP: 263 entries,
47,317,150 bytes; all 232 file/link payloads match signed source. Both cgltf notices,
exact font/license/HDRI/tangent assets, CRC/modes/exclusions, 13 regular Mach-O files/
3 relative links, strict app signatures and portable loader/rpath checks pass.
No unpublished distribution/archive stages remain. Removed owned fixture generator
scratch, preserved caches/latest ZIP/audit evidence/private untracked source/artwork.
Only arm64 macOS was built/run; Windows/Linux implementation is not locally verified.
Animated/skinned/morph/compressed content, camera/light import, nonopaque materials,
additional UV/texture transforms/mixed filtering, richer metadata, asynchronous imports,
streaming/production editor tools and realistic polished Prism Relay remain unfinished.
The complete engine/editor/game objective remains active.


## Material alpha coverage / 材质透明度覆盖 (2026-10-14)

Implemented a separate CPU RenderQueue module interface/implementation before its
renderer consumers. It validates bounded inputs, retains owned original IDs and
solid source order, and stably orders blended instances back to front. Current
material persistence explicitly requires alphaMode/alphaCutoff; static glTF/GLB
imports preserve factor, vertex and texture alpha for Opaque/Mask/Blend. Authored
alpha is retained even for opaque materials; rendering applies their coverage policy.
There is one current schema and no old-format compatibility branch.

Actual mesh rendering uses 288-byte surface instances and 128-byte picking/shadow
coverage records. Opaque ignores source alpha; Mask writes depth for survivors;
Blend composites straight alpha in linear HDR before tone mapping, tests solid
depth and never writes it. Shared standalone Coverage.wgsl governs color, shadow,
SSAO guides and picking. Mask texture holes affect all those passes; Blend contributes
no shadow/SSAO geometry. Integer picking retains original identity after sorting,
ignores zero-alpha fragments and chooses the nearest surviving fragment. Completed
frame counters publish actual solid/blended instances and GPU draw quantities.

Feature Lab is still an ordinary editable project. Added actual Glass and textured
Cutout entities/assets to its scene and shipped templates. Native acceptance verifies
editor/Play/standalone behavior with 8 solid and 1 blended instances. Editor font
remains pinned Sarasa UI SC Regular 1.0.42 with Inter Western glyphs and bundled OFL
license. Reference artwork and the private source document remain untracked.

Verification proceeded from 2/2 CPU queue tests (0.04s) to renderer/material/import
checks. Initial focused GPU tests exposed a missing group-2 AO binding without an
HDR environment; fixed the blend pipeline to use the same indirect-light AO policy
as solid rendering. Coverage texture bindings legitimately split the previous
geometry-only picking batches; updated the asserted quantity. Focused checks then
passed 41/41 (16.13s). Native Feature Lab initially exposed outdated material/texture
quantities; corrected them to actual 4/2 and strengthened solid/blend assertions;
its final focused native run passed 1/1 (12.18s).

The first full Debug run failed 5/947: three texture/HDR tests and runtime.textures
asserted the former opaque alpha output, while home creation asserted 13 instead of
15 Feature Lab entities. Updated these to the explicit opaque policy/new authored
scene and added a real HDR blend readback asserting that sRGB texture alpha remains
linear. RGB sampling/ownership/color conversion assertions remain in place. Both
final builds completed before serial full validation. Final Debug: 947/947, 197.14s.
Final Release/NDEBUG: 947/947, 48.41s. Inventory: 883 doctest identities plus 64
executable/script checks. Overlapping labels: unit 213, integration 627, gpu 176,
offscreen 107, native 57, acceptance 107, compiler 135, platform 27, process 19,
audio-native 32. All 300 C/C++ files pass clang-format; all 16 standalone WGSL files
pass established spacing/line checks and real pipeline/pixel validation. Reviewed
queue ownership/order, input validation, compact storage offsets, pass identities,
shader coverage/derivatives, error propagation, resource release and platform paths.
No additional screenshot review was performed.

Refreshed the latest archive through the existing owned metadata-filtered staging.
ZIP: 269 entries, 47,353,213 bytes; all 238 file/link payloads match signed source.
Two Glass/Cutout materials, two cutout textures, cgltf notices, exact Sarasa/Inter
font/license/HDRIs, CRC/executable modes/exclusions, 13 regular Mach-O files/3 relative
links, strict app signatures and portable loader/rpath checks pass. Owned archive
staging and implementation-generator scratch are absent; removed the temporary
format file list. Preserved build caches/latest ZIP/audit evidence and private
untracked document/artwork. Only arm64 macOS was built/run; Windows/Linux remain
implemented without local validation.

Object-center depth sorting is approximate for intersecting/large transparent
geometry. Refraction/OIT, transparent shadows, animation, streaming, mature production
editor tooling and polished realistic Prism Relay remain unfinished. The full
engine/editor/game objective remains active.


## Structured material authoring / 结构化材质编辑 (2026-10-15)

Added separate MaterialDocument module interface/implementation as the filesystem
foundation before consumers. It owns a typed material draft through the existing
ResourceStore/TextDocument path/metadata/atomic-write contract. Current revisions,
no-op edits, bounded 128-state branching history, undo/redo after Save, exact initial
source restoration, disk conflicts, owner-thread/move behavior and destruction
without saving are covered by 3 integration cases without a device/window (3/3,
0.27s). Explicit enum/path module imports were required during compilation; fixed
those errors without changing compiler/linker environment.

SceneRenderer now owns one explicit material preview by portable asset path and
reuses actual texture loading/normal validation/cache budgets for every matching
scene binding. Overrides never write files or mutate entities. Clearing caches
retains this explicit preview; clearing the override restores disk rendering.
Independent HDR pixel tests cover owned values, invalid descriptions, missing-image
errors, cache reload/restoration, unchanged disk/scene and texture release. The
first fixture sampled an uncovered corner; corrected its assertion to the existing
quad's center, retaining actual red/green pixel evidence.

Assets now creates and opens typed material assets in Inspector; entities can open
their bound material directly. Fields cover PBR/unlit, RGBA/emission, metallic,
roughness, normal scale, occlusion, sidedness, Opaque/Mask/Blend/cutoff and five image
slots with project selection and sampler options. Staged fields preview the actual
scene without writes; Apply records one history step rather than one per slider
frame. Reset, independent undo/redo, save, close/discard and project save-close use
the same strict workspace commands. Buffer identity/revision and pending-draft
checks preserve content across stale requests/external conflicts. Toolbar/File/Edit
actions address the open material. Play locks material mutations and uses disk
assets; Stop restores authoring previews. Scripts remain in the external editor.

Native material workflows initially passed 2/2 (1.94s); focused foundation/GPU/native
checks passed 6/6 (3.30s). Opened the real release-directory editor with Feature Lab,
read its actual material Inspector and viewport. Screenshot inspection exposed
clipped right-side labels; changed properties/sampling to labeled two-column rows,
full-width image paths and subdued section headers. Reopened and visually confirmed
complete labels in the actual dark UI. QA commands did not write the project.
Normal close/control cleanup verified; no QA editor process remained.

Both builds completed before serial full runs. Full Debug passed 953/953 (206.63s).
Then strengthened the existing native material case to save a different blue disk
material, render Play's actual blue pixels and recover the red authoring preview on
Stop without implicit file changes; focused Debug passed 1/1 (1.33s). No production
implementation changed after that full Debug run.

The first full Release passed 952/953 (43.11s). Its existing move-tool native pointer
case missed an axis click and cleared selection; isolated reproduction passed.
Inspection found unsynchronized async window state and synthetic input queued ahead
of pending OS events. Added explicit SDL_SyncWindow and pumped already-pending OS
events before test pointer samples, preserving the complete queue and all original
release/Escape/focus-loss/history/pixel assertions. This strengthens the test
preconditions; one intermittent failure does not prove a material-rendering defect
or establish a complete causal diagnosis. The focused Release case passed ten
consecutive runs (3.28s). Final Debug focused: 7/7 (4.14s), including stronger Play
and pointer cases. Final Release/NDEBUG full: 953/953 (37.56s).

Inventory: 889 doctest identities plus 64 executable/script checks. Overlapping
labels: unit 213, integration 631, gpu 179, offscreen 108, native 59, acceptance 109,
compiler 135, platform 27, process 19, audio-native 32. All 303 C/C++ files pass
clang-format; all 16 WGSL files pass established spacing/line checks. Reviewed
ownership, transactional history allocation, exact-byte conflict preservation,
revision/buffer boundaries, error paths, preview cache/Play transitions, resource
release, GUI form/command correspondence, source formatting and portable paths.

Refreshed archive through owned metadata-filtered staging: 269 entries, 47,386,765
bytes, all 238 file/link payloads match signed source. Font/license/HDRIs/material
assets/cgltf notices, CRC/executable modes/exclusions, 13 regular Mach-O files/3
relative links, strict app signatures and portable loader/rpath checks pass.
Owned QA request scratch and staging are absent; caches/latest ZIP/audit evidence,
private source document and reference artwork are preserved. Only arm64 macOS was
built/run; Windows/Linux implementation remains locally unverified. Material graphs,
standalone material spheres/asset thumbnails, broader production authoring/animation/
streaming and realistic polished Prism Relay remain unfinished. The full engine/
editor/game objective remains active.


## Asset browser and isolated previews / 资产浏览与独立预览 (2026-10-15)

Added GUI texture images with same-device validation and submission/error-scope
ownership, then exposed independent material resolution through SceneRenderer's
existing bounded texture loader. Shared color-space/normal checks, complete-load
publication and owned handles are retained. The foundation's two GPU cases passed
2/2 (1.13s), covering exact GUI pixels, foreign/empty inputs and lifetime through
submission plus independent texture-cache validation/release.

Added AssetPreview as separate interface/implementation with a stable independent
offscreen renderer/resolver. It renders real PNG/JPEG images with preserved aspect/
alpha, DotMesh/OBJ geometry framed from actual bounds, and material UV spheres with
actual PBR, five slots, normal maps and Opaque/Mask/Blend. Studio lights are explicit;
optional HDRI is baked at creation. Workspace requests its own packaged Studio
Small 09 HDRI/credit rather than depending on an example project. Unsupported
scene/model/audio/HDR/script thumbnail types retain type labels; errors never become
sample images. The image bridge is bounded synchronous GPU readback/GUI upload,
not asynchronous/zero-copy processing or a claim of stall-free performance.

Initial preview pixel checks found opposite-edge color bleeding from default
Repeat image sampling; corrected previews to Clamp. The HDR diagnostic fixture
violated the existing minimum panorama height; replaced it with valid 4x2 data and
kept explicit error reporting. Preview cases then passed 2/2 (1.43s): real pixels,
source preservation, draft sphere changes, HDR/normal failures, mesh framing,
thread/move behavior and resource release. Fixed missing explicit STL inclusion
and incomplete designated initialization under the existing warnings-as-errors
configuration without changing the caller's environment.

Assets now has grid/list, folder breadcrumbs and global search for nonempty filters.
Visible rows are clipped and actual previews are cached with LRU eviction: at most
64 128px thumbnails and one 256px current material sphere, each CPU/GPU image set
at most 4.25 MiB. The isolated resolver budget is 64 MiB; decoded sources and transient
rendering use their existing bounds. One synchronous job per frame prioritizes the
material form; Play pauses generation. Save/import and explicit refresh invalidate
images. Strict workspace commands expose selection, directory/view mode, queue/
cache/error/warning/current-material states; pending work is never reported as
successful generation. Capture returns owned completed pixels, not stale images.

Native workflows passed 5/5 (7.42s), including existing material editing and new
thumbnail navigation/search, independent form spheres, explicit missing-image
errors, bounded queue/cache and unchanged main viewport pixels/camera/draw counters.
Opened the real release-directory Feature Lab workspace. Actual UI inspection
showed eight real image/mesh/material thumbnails and a live copper sphere; the
control response recorded nine completed jobs, zero pending and no preview errors.
Grid labels were shortened to asset names, retaining full paths in tooltips/list;
shading labels now read PBR/Unlit. Normal close and no remaining QA editor process
were verified; no project source or scene was written. Owned QA request scratch
and control staging were removed, preserving captured response/log evidence.

Both builds completed before serial full checks. The first Debug full run passed
960/960 (208.56s). Final review identified that the current-material status could
briefly report an old image ready after staging a new form. Readiness/error now
match the exact current buffer/form key, with explicit materialPending, and closing
releases the old sphere. Strengthened native checks assert immediate pending/not-
ready semantics and closed state. After rebuilding both configurations, final
Debug passed 960/960 (211.45s); final Release/NDEBUG passed 960/960 (38.93s).

Inventory: 896 doctest identities plus 64 executable/script checks. Overlapping
labels: unit 213, integration 635, gpu 186, offscreen 112, native 62, acceptance 112,
compiler 135, platform 27, process 19, audio-native 32. All 307 C/C++ and 16 WGSL
files pass established formatting checks. Reviewed renderer/GUI ownership, stable
borrows, independent device state, bounded cache/queue/readbacks, error publication,
form generations, source preservation, color encoding and portable file paths.

Archive refreshed through existing owned metadata-filtered staging: 272 entries,
48,604,643 bytes, all 240 file/link payloads match signed source. The third exact
HDRI copy and source credit, font/license, material assets/cgltf notices, CRC/modes/
exclusions, 13 regular Mach-O files/3 relative links, strict app signatures and
portable loader/rpath checks pass. Build caches/latest ZIP/audit evidence/private
source document/reference artwork remain preserved and private inputs untracked.
Only arm64 macOS was built/run; Windows/Linux remain implemented but not locally
validated. Async imports/previews, scene/model/audio thumbnails, material graphs,
production authoring/animation/streaming and realistic polished Prism Relay remain
unfinished. The full engine/editor/game objective remains active.


### 2026-10-15 — Hierarchy mouse selection lifetime fix

A real user click closed the release editor with JSON type_error.305. Hierarchy
traversal borrowed entities/parent IDs from the workspace snapshot, while an inline
selection action refreshed and replaced that snapshot before recursion resumed.
Selection is now recorded by owned entity ID and dispatched after the entire tree
traversal returns. No borrowed scene reference survives the refresh.

Added actual rendered row rectangles/expansion state to workspace inspection for
native automation, and an SDL mouse-down/up regression for parent, child, changed
and repeated selection. It verifies scene data, dirty state and undo entry count
are preserved, changed selection advances revision, and viewport capture survives.
Temporarily restoring the old inline action makes this regression fail with the
same JSON exception family (number rather than null from invalid storage); the
fixed path passes 1/1 in Debug (1.03s). No assertion or error handling was weakened.

The combined working tree, including the separately delivered preview worker,
passes full Debug 967/967 (219.82s) and Release/NDEBUG 967/967 (48.18s); both include
the native mouse regression. Subsequent formatting/comment-only rebuilds completed
before the Release suite. Reviewed traversal lifetime, deferred ID ownership,
selection revision/history semantics and real native input coverage. All 310 C/C++
files pass clang-format; 16 WGSL files pass established spacing/line checks.
Only arm64 macOS was validated. CUA coordinate-click attempts on the release window
did not change selection, so they are not counted as mouse-selection proof; the
SDL mouse-down/up test supplies that evidence.


### 2026-10-15 — Owned asynchronous editor previews

Added separate Dot.Editor.PreviewWorker interface/implementation and an owned
jthread service. Source decoding, packaged studio HDRI loading/baking, isolated
GPU creation/rendering/readback and provider destruction run on the worker. The
owner submits owned requests and polls bounded display pixels; only GUI texture
upload remains on the GUI thread. No source/GUI/device borrow crosses threads.

Queues hold at most 64 waiting requests, one active request and eight completions.
Completion backpressure prevents unbounded images. Monotonic slot tickets coalesce
queued/completed work and reject late active results after form changes, refresh,
eviction or close/reopen. Material forms have priority over FIFO thumbnails.
Initialization failures stay explicit until refresh requests provider recreation
on its worker. Play pauses starting work; Stop resumes. Accepted close cancels
waiting work and joins active work, with no detached thread or fake GPU success.
Active driver/file calls may finish before close returns; main-thread upload and
model import remain synchronous, with no zero-copy or general FPS guarantee.

Four controlled-provider integration cases exercise nonblocking submission/polling
under a blocked job, priority/coalescing, late discard, capacity/backpressure,
validation/exceptions, initialization reset, pause, owner/move and joined provider
lifetime. They create no GPU/window. A real offscreen worker case verifies actual
Metal pixels and missing-studio errors. Native workflows retain previous pixel/
source/main-scene isolation coverage, awaiting actual completion with deadlines;
new rapid-form/refresh/reopen/close checks reject stale samples and prove joined
shutdown/post-close rejection. Play tests also assert worker pause/resume.

Foundation/provider/offscreen selection passed 5/5 (1.48s). An initial expanded
native run exposed an incorrect test assumption that ACES leaves a red material's
blue channel exactly zero (actual value 9); the test now compares the entire
reopened image against an independently completed initial saved-red render. It
retains the staged-blue pixel assertions. Final focused checks passed 11/11
(11.33s). Both final builds succeeded; full Debug passed 967/967 (219.82s), and
Release/NDEBUG passed 967/967 (48.18s), including the separate Hierarchy fix.
Final whitespace/comment-only rebuilds completed before Release verification.
Inventory: 903 doctest identities plus 64 executable/script checks; overlapping
labels unit 213, integration 640, gpu 189, offscreen 113, native 64, acceptance 114,
compiler 135, platform 27, process 19 and audio-native 32. All 310 C/C++ and 16 WGSL
files pass established formatting checks. Reviewed stable worker ownership,
bounded transactional enqueue, callbacks outside queue locks, exception/error
publication, exact form/ticket checks, pause/refresh and joined shutdown.

The actual release-directory Feature Lab displays eight generated thumbnails and
the real copper studio sphere. Inspection records nine finished jobs, no pending
work/errors and a ready current material image. Normal close returns exit 0 and
removes the control staging directory; source scene/material files were not saved.
Refreshed ZIP through the owned archive target: 272 entries, 48,635,402 bytes, all
240 file/link payloads match signed source. CRC/modes/exclusions, exact font/license
and three HDRI copies/credit, cgltf notices, 13 regular Mach-O files/three relative
links, strict signatures and portable loader/rpaths pass. Build caches, latest ZIP,
audit/log evidence and private untracked document/artwork remain preserved.
Only arm64 macOS was built/run; Windows/Linux remain implementation targets without
local validation. Broader asynchronous imports/streaming, scene/model/audio
thumbnails, material graphs, mature production authoring and polished realistic
Prism Relay remain unfinished. The full engine/editor/game goal remains active.

Final diff review added only blank separators around two control-flow blocks;
clang-format/whitespace checks passed and the archive was rebuilt afterward.
No executable behavior or test assertion changed after full-suite verification.


### 2026-10-15 — Scene multiselection and group edits

Document history now owns ordered unique entity selections; the last member is
active. Changed selection advances revision without scene dirtiness/history-entry
creation, validates owner/scope/liveness/count and includes selection payloads in
the bounded history budget. Snapshot/stats own their sets; scene scope changes
reconstruct fresh handles, removal prunes missing members and undo/redo restores
each entry's selection. No selection data is written into game scenes.

Transactional duplicate/remove-many operations expand descendant unions once.
Duplication remaps internal parents, retains external parents/native components/
asset bindings and opaque script properties, assigns fresh monotonic IDs, applies
bounded UTF-8 copy names and demotes copied camera/listener primary flags. Input
selection maps to copies in order. Entity/identity/light/history limits and later
operation failures leave document/selection/ID allocation unchanged.

Hierarchy plain/Command-or-Ctrl/Shift/combined clicks share replace/toggle/range/
additive-range operations with the agent protocol. Open/filtered row order drives
ranges, with the endpoint active and stable anchor semantics. Membership is cached
for drawing; click requests remain owned and deferred until traversal ends. Native
row inspection reports actual selected/active flags. Duplicate and Delete buttons/
shortcuts use one group undo transaction. Inspector states selected count and
active-only properties. Group transforms/framing/box selection/mixed-value fields
are still unfinished; multi-selection hides/rejects the single-object gizmo.

Four new device-free integration cases cover set/revision/history/owner/move/byte
contracts, overlap and parent remapping, primary components, Unicode names, script
properties, ID exhaustion, rollback and undo/redo branches. Runnable document
acceptance now also selects/duplicates/undoes two entities. Native mouse/key cases
cover arrow expansion, platform modifiers, inclusive range/highlighting, shortcut
duplication/deletion and restored selection. Agent checks add reversed/additive
ranges, stale rejection and multi-object gesture rejection.

The first native test used Control-click on macOS, which Dear ImGui intentionally
turns into right-click; platform input now uses Command on macOS and Ctrl elsewhere.
Both native cases then passed 2/2 (2.39s). A combined focused run passed 27/28 with
one old mouse regression missing initial selection; its unchanged input sequence
passed three isolated repetitions (2.95s). Both pointer tests now explicitly supply
window focus before interaction, retaining every scene/revision/history assertion.
The acceptance extension initially had a local variable-name collision, corrected
without compiler/environment overrides; both complete builds subsequently passed.

Both builds finished before serial full validation: Debug 972/972 (223.16s), then
Release/NDEBUG 972/972 (56.52s), including both native Hierarchy cases and the
extended document acceptance. No test assertions were relaxed. Inventory is 908
doctest identities plus 64 executable/script checks; overlapping labels unit 213,
integration 644, gpu 190, offscreen 113, native 65, acceptance 115, compiler 135,
platform 27, process 19 and audio-native 32. All 310 C/C++ files and 16 unchanged
WGSL sources pass established formatting checks. Reviewed snapshot/handle lifetime,
bounded selection/history accounting, subtree identity remapping, input focus/
platform modifiers, transaction rollback and preserved single-object behavior.

Actual release-directory Feature Lab inspection shows three highlighted originals
and the active-only Inspector count; shared live commands duplicate all three,
show three selected Copy rows and one added undo entry. One undo restores the
exact original scene and selection, with dirty false. Normal close returns exit 0
and removes control staging. QA request scratch is removed; response/log evidence
is retained. Source scene/material files were not saved.

The archive target refreshed the latest ZIP: 272 entries, 48,646,602 bytes and
240 payloads matching signed source. CRC/modes/exclusions, exact font/license/HDRI
copies/credit and cgltf notices, 13 regular Mach-O files/three relative links,
strict signatures and portable loader/rpaths pass. Caches/latest ZIP and private
untracked document/artwork remain preserved. Only arm64 macOS was built/run;
Windows/Linux input branches are implemented but not locally validated. Group
transforms, production animation/streaming and polished realistic Prism Relay are
still unfinished. This stage advances editor scene production; the full
large-game/mature-editor objective remains active.


### 2026-10-15 — Transactional group transforms

TransformGesture now freezes an ordered selection with its active entity as the
pivot. It retains initial root world poses, external parent inverses and the
active local affine frame. Selected descendants of another selected entity are
not edited twice; their local values remain unchanged while the hierarchy moves
them once. Group world rotation/scale orbit/stretch around the active origin;
Local uses parent-world × local T×R, retaining ancestor stretch/reflection and
excluding active entity scale. Single-object local behavior remains intact.

Samples are absolute from the starting pose. Every root is converted to strict
parent-local TRS before one transactional Scene transform batch updates the stable
draft. Later-root shear, singular/invalid/overflow samples preserve every previous
valid draft transform. Returning to the start restores original values exactly.
Cancel/escape/focus loss discards the owned preview; commit validates owner,
revision and scene scope before one history entry. No partial group publication.

Document now has a typed <=10000-handle transform transaction, independent of the
generic JSON edit transport/256-operation limit. It shares prepared scene/history
commit, byte accounting, no-op behavior and selection pruning with ordinary edits.
Current-scene handles/counts and the final batch are validated before publication.
Gizmos and existing agent begin/update/finish commands now consume the same group
gesture; inspection exposes frozen entities, root count and active pivot. The
viewport shows the selection count/active pivot alongside tool controls.

Four new CPU/filesystem integration cases exercise ancestor/active-child overlap,
world/local pivots, reflected nonuniform parents, absolute/reset samples, later
root rollback, owner checks and 301 roots committed as one transaction. Typed
empty/stale/duplicate/foreign/10001-handle failures retain prior state. Two native
cases exercise group Move/Rotate/Scale through agent and actual mouse input, real
preview/commit/undo/redo pixels, descendant local preservation, cancel/focus loss
and native saved scene data. Runnable viewport acceptance now moves two roots
and restores data/selection/pixels on one undo. Existing single-object coverage
is retained. Foundation selection passed 29/29 (2.03s); focused native/GPU checks
passed 7/7 (9.09s). Initial test compilation required explicitly importing the
World.Scene module for its handle/update declarations; no environment overrides.

Both builds completed before serial full checks. Full Debug passed 978/978
(227.37s), followed by Release/NDEBUG 978/978 (47.24s). Explicit standard-library
includes were rebuilt in both configurations after Debug and before Release; no
behavior changed in that cleanup. Inventory: 914 doctest identities plus 64
executable/script checks. Overlapping labels: unit 213, integration 648, gpu 192,
offscreen 113, native 67, acceptance 117, compiler 135, platform 27, process 19 and
audio-native 32. All 310 C/C++ and 16 WGSL files pass established formatting checks.
Reviewed root/active-frame ownership, one-time descendant inheritance, frozen
parent inverses, all-or-nothing preview/commit, stale/foreign/moved lifetime and
unchanged scene/selection/history semantics on failure.

Actual release-directory Feature Lab shared live commands previewed three roots
moving 3.5 meters without changing the document, committed all three once and
restored the exact original scene/selection with one undo. Normal close returned
exit 0 and removed control staging. Visual inspection found the new multi-select
label introduced an extra blank control row; corrected its line break without
changing viewport dimensions. After rebuilding both configurations, the eight
affected native/GPU/viewport acceptance cases passed Debug 8/8 (11.45s) and
Release/NDEBUG 8/8 (4.02s). Full suites above preceded this final row-spacing fix;
the focused checks validate that final UI change. A second release UI inspection
confirms inline active-pivot text, complete navigation hints and no controls
scrollbar; cancel retains the exact scene/selection, then normal close returns 0.
QA helpers/requests and control staging are removed; responses/logs remain. No
authoring source files were saved during manual checks.

Latest archive refreshed through owned staging: 272 entries, 48,655,590 bytes,
240 payloads matching signed source. CRC/modes/exclusions, exact font/license/HDRI
copies/credit and cgltf notices, 13 regular Mach-O files/three relative links,
strict signatures and portable loader/rpaths pass. Caches/latest ZIP/private
untracked document/reference artwork remain preserved. Only arm64 macOS was
built/run; Windows/Linux implementation is not locally validated. Shared active
pivot groups are implemented; group framing, median/individual pivots, box/mixed
property tools, production animation/streaming and polished realistic Prism Relay
remain unfinished. The full engine/editor/game goal remains active.


### 2026-10-15 — Native TRS animation foundation

Added separate Clip, Files and SceneAnimator module interfaces/implementations.
Clips own immutable shared channel data with explicit node/path identities, unit
XYZW rotations, ordered finite key times and bounded tracks/keys. Sampling clamps
each channel independently, uses Step/vector Linear/shortest-arc quaternion SLERP
and interval-scaled cubic Hermite with normalized cubic rotations. Cubic signs/
tangents are retained, including explicit failure when interpolation yields a zero
quaternion/scale or overflow. Behavior was checked against Khronos glTF Appendix C;
the model importer still explicitly rejects animated input until integration.

Single strict .dotanimation JSON supports owned native clips with explicit cubic
tangents, bounded size/depth and duplicate/unknown/type validation. Rooted portable
regular-file paths reject links/special files; saves encode fully before atomic
replacement, preserving prior files on invalid input. The codec is independent of
rendering and does not resolve arbitrary external resources.

SceneAnimator owns clip/binding/rest-pose data, with one-to-one live node/entity
coverage and scene-scope/owner checks. It uses explicit elapsed seconds, checked
seeks, forward/reverse speed, loop/nonloop, pause/resume, zero-time/rate and static
clip handling. Samples and every node's local transform update commit together
with playback state. Unanimated fields retain current scene values; stop restores
only authored fields to bind pose. Removed/foreign bindings, singular samples and
final world overflow preserve every prior transform and clock/state. Scene moves
preserve scope; animator moves retain ownership and moved-from calls fail.

Added lightweight Scene::transform owned reads so playback does not copy names/
scripts/unrelated entity components. Animation still allocates sample/pose batches;
allocation-free caches, parallel evaluation and populated-scene performance remain
unproven. Native CPU tests cover independent times, exact keys/cubic derivatives,
short-arc signs, immutable concurrent ownership, clocks/bindings/moves and atomic
rollback including a valid-local/failing-world batch. File tests cover strict schema,
rooting, actual symlinks and failed-save preservation. GPU sampling matches an
independent native pose and restores bind-pose pixels; drawing resources return to
baseline. Runnable --animation-test saves a clip and nine real hierarchical TRS PNG
frames; start/mid/end images were inspected. These checks use no desktop window.

First Clip selection passed 5/5. Full initial animation selection passed 14/14
(2.46s), separating ten CPU unit cases, two file integration cases, one offscreen
GPU case and one executable acceptance. The lightweight pose getter adds one more
unit case. Initial compilation identified explicit module/header imports, a signed
index and renderer test API/designated initializer issues; fixed those sources
without weakening warning policy or changing compiler/linker environment.

The first full Debug run passed 992/993 (224.00s), with the pre-existing native
viewport input test failing initial selection and outside-viewport navigation.
Its deliberate pointer samples could be followed by newly pumped OS motion/focus
events, unlike the ordering already used by the gizmo fixtures. The test now
provides initial focus and pumps pending OS events before each deliberate pointer
sample, preserving every event and every original assertion. Three consecutive
focused runs pass (2.96s). No engine input/navigation behavior, retry-on-failure
assertion or production timing budget was changed.

The final Debug suite passes 993/993 (224.56s). Eleven new animation/pose-read unit
cases additionally pass with deliberately invalid SDL video/audio drivers (0.08s),
confirming no window/device dependency. Scene animation integration remains a
separate, explicitly unfinished consumer stage: glTF animated-node import, saved
GameSession components, editor timeline/Play controls and TypeScript SDK are not
implemented here; skinning/morph/blending/events/streaming remain open.

Final Release/NDEBUG passes 993/993 (45.96s), alongside final Debug 993/993
(224.56s): 928 doctest identities plus 65 executable/script cases. All 321 C/C++
files pass clang-format and 16 WGSL files pass established formatting checks.
Reviewed module ownership, interpolation failures, scene transaction/state rollback,
file boundary checks, platform assumptions and behavioral coverage. The atomic
archive target succeeds; ZIP audit passes CRC, all 240 exact payloads in 272 entries
(48,656,310 bytes), executable modes, font/HDRI/licenses and excluded private inputs.
Strict signatures and portable loader references pass for 13 regular Mach-O files
and three relative links. Only arm64 macOS was validated; Windows/Linux remain
implementation targets. The overall engine/editor/game goal remains active.

Following the user's hierarchy crash report, rechecked the existing deferred-click
fix (1f31835) and current packaged executable. No new DotEditor diagnostic report
was found. Both native hierarchy cases pass three consecutive runs per case in
Debug (6.73s) and Release/NDEBUG (2.52s), covering parent/child/repeated selection,
modifier ranges, duplicate/delete and undo. This confirms the current code path;
the user's launched application path has been requested to distinguish an older
package from a new failure. No additional production fix is claimed for this check.


### 2026-10-15 — glTF TRS clips and native model bundles

Animated glTF/GLB node translation, rotation and scale now convert to immutable
native clips. Import checks scalar float time accessors/bounds, vector float outputs,
normalization/stride restrictions, output cardinality and all existing clip/key
contracts before remapping source node indices into the selected hierarchy. Source
clips/channels/keys have aggregate budgets. Channels outside the selected scene are
validated then excluded with explicit warnings; clips with no remaining targets are
omitted. Unnamed clips use stable source-index names. Matrix-target animations,
skinning and morph targets remain explicit unsupported failures.

Non-matrix nodes preserve authored TRS axes/signs instead of decomposing their
composed matrix into an equivalent but animation-incompatible representation.
Native model preparation validates the scene, clip targets and aggregate encoded
bytes before writing. Each bundle now owns model.dotmodel with canonical node/entity
IDs, scene and ordered native clip paths, alongside editable geometry/material/images.
The separate ModelBundle interface/implementation reloads owned scene/clips and live
bindings without source glTF lifetime. Strict descriptor schema/UTF-8/size, same-bundle
paths, canonical unique live IDs, aggregate clip budgets and link/special-file checks
reject malformed resources. Scene moves retain bindings; playback is explicit.

ResourceStore accounts for the descriptor, clip files and optional animation folder
before atomic publication. Editor GUI/agent import reports native model path and
animation count, while .dotmodel/.dotanimation remain externally editable resources.
Existing Add model to scene still inserts geometry only: saved animation authoring,
GameSession/editor timeline/SDK playback are the next consumer stage, not implemented
by this import milestone. Skinning/morphs/blending/events/streaming and polished game
production remain open requirements.

Owned glTF/GLB diagnostic fixtures intentionally reorder source nodes versus tree
traversal, combine cubic parent translation/linear reflected-child rotation/STEP
scale and include another scene's unnamed clip. File integration covers remapping,
source-independent reload/moves/playback, malformed binary/channel/accessor data,
excluded invalid channels, strict descriptor paths/IDs, actual symlink rejection and
failed-publication/staging preservation. An independent native pose matches real
imported animation pixels and stop restores bind pose; GPU resources return to
baseline. Runnable --model-animation-test saves nine real offscreen PNGs; first,
midpoint and last images were inspected. Native editor model acceptance checks the
actual import command/report without claiming timeline support.

Initial tests required one explicit Core.Result module import; no warning policy or
compiler/linker environment was changed. Initial seven new focused cases pass 7/7
(0.91s). After reviewed time-bound validation and runnable acceptance, all affected
model/import/editor cases pass 17/17 (5.77s). All 325 C/C++ sources pass clang-format;
16 WGSL sources pass established indentation/trailing-space/line-length checks.

The first full Debug suite passes 1001/1001 (218.20s). During final review, split
scene filtering and node remapping so erase_if's predicate never mutates elements;
this avoids depending on standard-library predicate invocation details. Strengthened
file integration to exercise primitive-interleaved node IDs and the exact resource
catalog capacity (one entry short rejects, exact fit publishes). Both configurations
are rebuilt before final validation. Also corrected stale Assets documentation that
still called the already-implemented PBR/IBL/shadow/SSAO/HDR renderer planned.

Final Debug passes 1001/1001 (215.74s) after the final predicate review and expanded
binding/catalog checks; final focused animation cases also pass 7/7 (0.91s).
The inventory is 935 doctest identities plus 66 executable/script checks; overlapping
labels include 224 unit, 658 integration, 196 GPU, 117 offscreen, 67 native and 119
acceptance cases. Final formatting recheck passes all 325 C/C++ files; WGSL remains
16 passing files. Only arm64 macOS is locally validated.

Final Release/NDEBUG passes 1001/1001 (38.71s). Reviewed module ownership/lifetime,
source accessor arithmetic, scene-selection filtering/remapping, file boundaries,
pre-publication resource budgets, exception/result propagation and platform-neutral
paths. No engine product main gained acceptance code. The owned archive target
succeeds; final ZIP is 48,680,122 bytes/272 entries/240 exact payloads with passing
CRC, executable modes, original font/HDRI/license payloads and private-input/source
exclusions. Strict signatures and portable loader references pass for 13 regular
Mach-O files/three relative symlinks; archive staging is removed. Windows/Linux
remain implemented targets without local validation. The full large-game/editor/
realistic Prism Relay objective stays active.


## 2026-10-15 — Saved scene animation consumers

Added separate Source and ScenePlayers module interfaces/implementations. Strict
saved player descriptions and live scene-local bindings persist through scene
serialization, imported model insertion, independent duplication, undo/save and
Inspector editing. Bound-target deletion fails before mutation. GameSession validates
all player libraries during preparation, applies initial poses at start and advances
native animation after variable script callbacks before audio. Fixed physics order
is retained. Changed descriptions restart; unchanged descriptions retain clocks.
Missing clips, shared writers and rigid-body authority conflicts fail explicitly.

Reviewed ownership, moved/foreign/thread checks, canonical IDs, aggregate budgets,
remapping, error propagation and per-player transaction limits. No hidden clock or
window is introduced. Native editor acceptance verifies actual import/Inspector/Play
pixels, Stop, save/reopen and duplicate/undo/deletion behavior. The relocated native
export case removes authoring sources and its script independently observes actual
animation motion. Focused animation checks pass 7/7 (6.30s); combined animation/home
checks pass 13/13 (10.59s). Both Debug and Release builds succeed. Full verification
of this tree, including the pending launcher stage, is recorded below after completion.
Skinning, blending, visual binding/timeline tools, SDK controls and production
streaming/performance remain unfinished.

Final combined-tree Debug passes 1011/1011 (234.55s): 944 doctest identities and
67 executable/script checks, including the pending launcher tests. No full suite
runs concurrently with compilation or another configuration. All 334 C/C++ files
and 16 WGSL files pass their established formatting checks. Native/GPU acceptance
uses real macOS resources; Windows/Linux have no local validation claim.

Final combined-tree Release/NDEBUG passes 1011/1011 (47.78s). Both configurations
include the actual native animated-model editor and relocated animated-game cases.
The pending launcher changes are kept separate in the following cohesive commit.


## 2026-10-15 — Project launcher and SDL folder dialogs

Replaced the single blue-field form with graphite Open/Create pages, a persistent
sidebar, aligned identity fields, actual template descriptions and a destination
preview. Content scrolls at smaller sizes. Open Browse and creation Location Browse
call SDL_ShowOpenFolderDialog on the GUI owner thread with its native parent window.
Project selection uses the same strict open operation; parent selection only fills
Location. Cancellation preserves values and failures remain visible. One outstanding
dialog disables form mutations and rejects command close/releaseWindow until it
finishes. Strict stage-form and browse-folder commands share the real GUI operations.

Reviewed callback lifetime/threading, borrowed-string copying, one-shot consumption,
portable paths, expected errors and ImGui stack/layout balance. Callback userdata
owns a mailbox independent of Home/window lifetime; no GUI pointer crosses threads.
Pure CPU cases cover cancel/error/copied UTF-8 paths and late worker completion.
Native acceptance covers actual pointer navigation, retained forms and invalid
staging preservation. Combined focused tests pass 13/13 (10.59s). Debug and Release
builds pass; full Debug passes 1011/1011 (234.55s), Release/NDEBUG 1011/1011 (47.78s),
run sequentially after both builds completed. Inventory is 944 doctest identities
plus 67 executable/script checks; all 334 C/C++ and 16 WGSL files pass formatting.

The actual Release app was visually inspected. Clicking Open Browse opened the
macOS system picker; Cancel returned to Home. The parent browse shared command
opened that same picker and selected Location. Shared Create entered a real new
workspace. A fresh app's actual Browse click plus native folder selection reopened
that project and showed DotEngine — Native Dialog Demo. Both owned app sessions
closed normally. Final Create identity fields align. Evidence logs and the manual
verification record remain under build/release. Full destruction during a pending
OS picker is not manually verified. Windows/Linux rely on SDL implementation and
have no local validation claim. The large-engine/game objective remains active.

The owned DotEditorArchive target succeeds. ZIP audit passes 272 entries/240 exact
payloads, 48,839,304 bytes, CRC and executable modes, original font/HDRI/licenses and
private/source exclusions. Strict signatures and portable loader references pass
for 13 regular Mach-O files/three relative links; archive staging is removed.


## 2026-10-15 — Native animation library control

SceneAnimator now retains all original node bindings, restores outgoing animated
fields and applies a replacement clip's directional start in one scene transform
transaction. ScenePlayers owns bounded immutable clip libraries and exposes native
state/play/pause/stop/seek/configure controls. Manual selections/configuration retain
clocks across ordinary synchronization and never rewrite saved descriptions. New
actual writers are checked against other players and rigid-body authority before
switching; invalid samples/world transforms leave old pose/clip/clock unchanged.

Reviewed scope/thread/move ownership, shared immutable clip lifetime, original rest
poses across disjoint channels, synchronization and post-commit operations. CPU
transaction cases and file integration cover pause/reverse/restart/seek/stop, failed
clip/writer/physics selection, untouched authoring and playback after deleting an
unselected clip's original file. Both builds succeed. Focused Debug passes 12/12
(3.93s), Release/NDEBUG 12/12 (1.34s), including the runnable GPU switch acceptance.
After adding original-file deletion coverage, that strengthened case passes in both
configurations. The actual tenth PNG was inspected. Formatting and diff checks pass
for all touched C++ files. No SDK control or production animation completion is
claimed by this foundation stage; that consumer follows next.


## 2026-10-15 — Animation SDK and interactive Feature Lab

Added AnimationSession interface/implementation with an owned native ScenePlayers
backend and callbacks retaining service lifetime. GameSession prepares animation
resources/services before script construction; script preparation and migration may
query state but cannot issue controls. dot:engine exports typed animation state/
play/pause/stop/seek/configure and Entity conveniences. No script-side clock or
backend creation is exposed. Strict finite transport, canonical scoped IDs and
catchable native failures retain established host access controls.

Feature Lab gains a saved PBR panel and two native clips, Digit 1/2 selection and
Space pause/resume. Its independent inventory now verifies all 93 SDK members plus
six extra behavior checks. Native SDL input verifies switched/paused/resumed poses
while the saved selected clip remains unchanged. Editor Play isolation and actual
relocated standalone execution after authoring removal consume the same project.

The first build needed an explicit Core.Result import in the new test. Initial
fixtures missed the required mesh visible field and a TypeScript assertion triggered
control-flow narrowing; corrected both without weakening behavior. Expanded audit
initialization then hit the actual 256 host-call limit. Resolved repeated fixed-name
queries once per realm and placed animation assertions in the first update, after
initialization checks. Completed checks migrate across reload; all assertions remain
and no production budget was increased. Improved failure diagnostics before realm
fault assertions. The focused Debug suite passes 10/10 (11.70s), including actual
native input, GPU/audio, editor and relocated export. Both final builds succeed.

Reviewed callback/scene/clip ownership, moved/retired wrapper lifetime, wrong-thread/
replaced-scope failure, strict JSON/IDs, ReadOnly preparation, transient controls and
script/native start/update/close ordering. New SDK cases exercise actual TypeScript
state/pose/clock, disjoint clip selection, reverse/restart/seek, failures, all write
prohibitions during constructor/migration, retained clocks and typecheck rejection.
All 334 current C/C++ files and 16 WGSL files pass established formatting checks.
Inventory is 1019 checks: 952 doctest identities and 67 executable/script checks;
labels include 229 unit, 668 integration, 199 GPU, 117 offscreen, 70 native and
122 acceptance cases. Full final results are recorded below after completion.
Timeline/binding authoring, skinning, blending/state machines/events, streaming and
realistic polished Prism Relay remain unfinished; the complete goal stays active.

The first full Debug run completes 1018/1019 (232.12s). Its only failure was the
Home template acceptance's old 15-entity expectation after adding the saved panel.
Updated the expected 16 entities and strengthened the case to inspect the actual
panel player and decode both copied two-second clips. Other template expectations
and no-overwrite checks are retained. Both builds are completed again before final
full suites; no runtime assertion or production limit was relaxed.

Final Debug passes 1019/1019 (228.83s), including the strengthened template case.
The runnable animation acceptance retains actual switch/restoration pixels and
resource checks; Feature Lab retains native SDL, GPU/audio, Play and export tests.
Only arm64 macOS is locally validated; Windows/Linux remain implementation targets.

Final Release/NDEBUG passes 1019/1019 (45.93s). Debug/Release full runs remain
sequential and begin only after both builds finish. The final touched Home test
also passes clang-format and diff checks. No Windows/Linux execution is claimed.

The owned archive target succeeds. The latest ZIP is 48,883,115 bytes, 276 entries
and 244 exact payloads. Both shipped Feature Lab copies contain the exact two
source clip files and the saved panel binding. CRC/executable modes, original
font/HDRI/licenses, private/source exclusions, strict signatures and portable loader
references pass for 13 regular Mach-O files/three relative links. Private archive
staging is removed. Only arm64 macOS execution is validated.


## 2026-10-15 — Native skin data and deformation reference

Added separate Animation.Skin and Graphics.Skinning interfaces/implementations.
Immutable skin data owns up to 1024 inverse binds and one million eight-slot vertex
influences; strict joint/weight validation precedes one-time normalization. Pure
mesh-local palette evaluation retains joint order and checks finite invertible
affine transforms and bounded entries. The CPU reference preserves source geometry
and unanimated attributes, uses inverse-transpose blended normals and orthogonal
tangents with reflected handedness, and rejects singular/overflow/seam results.

Reviewed immutable ownership, precise counts, affine/matrix order, normalized
weights, source preservation and error propagation. A first implementation build
needed an explicit span include; the LLVM environment was unchanged. Debug builds
and focused cases pass 7/7 (1.95s), including a development-only runnable artifact
acceptance and six CPU cases. Exact joint/vertex boundaries, failed samples, copies/
moves and concurrent immutable reads retain coverage. GPU and imported-scene
consumers follow this tested foundation; they are not claimed implemented here.

Release/NDEBUG focused checks pass 7/7 (0.70s). All touched C++ files pass
clang-format and diff checks. Tests require no window or device at this stage;
only arm64 macOS is validated. Separate unit and runnable file cases retain their
actual layer labels. The large-engine objective remains active.


## 2026-10-15 — GPU skin controllers and shared render passes

Added owned GPU skin controllers sharing static mesh/index uploads, immutable
weights and palettes. Two posed buffers retain the current view until an entire
batch succeeds. Separate WGSL computes eight-weight positions, inverse-transpose
normal directions, orthogonal/reflected tangents, triangle-handedness checks and
exact signed bounds. Compact status is read once per batch; errors preserve every
previous buffer, bound and counter. Rescaled position arithmetic guards overflow
before multiplication. Normal-map eligibility combines stable UVs with current
geometry. Ordinary mesh views feed unchanged PBR, shadows, SSAO and picking, with
updated bounds used by spatial classification.

Reviewed packed data offsets, finite/affine and sample boundaries, alias rejection,
shared source/output lifetime, device/thread checks, queue ordering, budgets and
all-or-none publication. Added logical skin residency counters; retained Mesh views
keep current geometry after their controller retires. readSkin provides explicit
diagnostic vertex readback. Synchronous compact-status waiting is a known cost;
large populated-scene throughput, asynchronous scheduling and imported-scene/editor
skin resources remain unfinished.

The first actual GPU focused suite passes 5/5 (4.50s): vertex/normal/tangent/bounds
reference, physical normal-map pixels, batch rollback, culling/picking and frame
recovery, shared lifetime/foreign/thread/moves/budgets, and matching shadow/AO
reference passes. The expanded runnable passes 1/1 (3.20s); native joint animation
drives nine real frames with CPU vertex/pixel comparisons, bind/reference/GPU mesh
artifacts and resource release. First/mid/last PNGs were inspected and show actual
bending. Added explicit GPU overflow, reflected seam and exact budget checks before
final validation. Final results follow below; no Windows/Linux run is claimed.

Final focused Debug passes 13/13 (9.03s), Release/NDEBUG 13/13 (1.25s), including
explicit float-overflow rejection before invalid arithmetic, GPU seam rejection
and an exact 688-byte extra-residency fit. Corrected the prior foundation release
timing from command-wall time to its actual 0.70s CTest result. All 341 C/C++ and
17 standalone WGSL files pass formatting. The inventory is 1032 identities: 964
doctest cases plus 68 executable/script checks. Labels overlap: 235 unit, 674
integration, 206 GPU, 124 offscreen, 70 native and 123 acceptance. Both final builds
finish before full suites; full final results are recorded below after completion.

Final Debug passes 1032/1032 (238.56s), including all existing editor, native game,
relocation, physics/audio and rendering contracts. No full suite overlaps builds
or another configuration. The new GPU path retains actual device failures and
resource checks; unit cases remain free of native device initialization.

Final Release/NDEBUG passes 1032/1032 (49.65s). Full configurations run sequentially
after both builds complete. Only arm64 macOS is built/run; Windows/Linux use the
same portable CPU/WebGPU implementation without a local validation claim.

The owned archive target succeeds. Final ZIP is 48,935,380 bytes/276 entries/244
exact payloads with passing CRC, executable modes, original font/HDRI/licenses and
private/source exclusions. Strict signatures and portable loader/rpath references
pass for 13 regular Mach-O files/three relative links. Private archive staging is
removed. Shader sources stay private; the new skin shader is embedded normally.
Windows/Linux remain implementation targets without local execution evidence.


## 2026-10-15 — Saved skins and ordered scene joints

Added separate SkinFiles, SkinSource and SceneSkin module interfaces/implementations.
The single little-endian .dotskin binary owns inverse binds and eight-slot vertex
influences, validates exact counts/length before allocation and uses rooted regular
paths plus atomic writes. Saved scene components retain ordered local joint IDs;
mesh/reference/capacity guards prevent dangling deletion and partial changes. The
CPU scene sampler owns immutable data and component values, follows current world
hierarchies and reports foreign/deleted/changed bindings without borrowing a scene.

Reviewed endianness/packed offsets, bounded reads/allocation, Unicode paths, link
rejection, atomic replacement, serialization order, thread/scope/move lifetime and
mesh-local palette order. First builds found a copied JSON range warning and a
wrong MeshRenderer aggregate initializer in new tests; both were corrected without
environment changes. Both complete builds succeed before testing. Focused Debug
passes 22/22 (9.12s), Release/NDEBUG 22/22 (0.99s), including the extended runnable
GPU acceptance which reloads actual saved assets and compares nine posed frames.
All touched C++ files pass formatting and diff checks. No Windows/Linux run is
claimed. Automatic scene rendering, glTF skin import and editor authoring follow
this foundation; no full-engine or polished-game completion is claimed.


## 2026-10-15 — Automatic scene skins, editor and export

SceneRenderer now loads enabled saved skins, shares immutable inputs and retains
one GPU controller per mesh owner. Current joint-world palettes update in one
visible batch before drawing. Color/PBR/shadow/SSAO/pick and bounds use the same
posed geometry. Both posed buffers plus weight/palette/control storage are charged
to the scene budget; hidden/disabled/rebound/deleted or foreign-scope controllers
retire before their static inputs. Explicit clearCache reloads actual files.

Added CPU-only actual-asset/count/initial-deformation checks before GameSession
constructs services/scripts; GamePackage publication inherits the same validation.
The Inspector exposes asset/enabled/ordered live-joint controls. Component edits,
duplicate/model instantiation remapping, history and saves share native contracts.
Physics colliders remain authored shapes, not implicitly deformed geometry.

Reviewed scoped cache identity, GPU/source lifetime and byte accounting, span/draw
pointer stability, palette evaluation order, failed updates/targets, native file
errors, initial validation before scripts and ImGui draft/stack balance. First
builds caught a shadowed UI label and missing direct Scene import in the new native
test; both were corrected. Both final builds finish before tests. Initial focused
Debug passes 30/31 because the new test assumed an inactive docked Inspector had
already drawn. Explicitly showing that pane fixes the test; its rerun passes 1/1
(1.33s). Focused final Release/NDEBUG passes 31/31 (1.70s).

Actual tests cover exact cache fit/one-byte shortage, resource reuse/pruning,
current bounds/pick, independent CPU mesh pixels, persistent targets, malformed
assets/counts, immutable reload and failed blend recovery. The native workspace
compares authoring/undo/isolated animated Play pixels and restores authoring on
stop. Relocated package checks delete the source project before loading copied
skin data and sampling live runtime joints. The runnable additionally compares
automatic scene PBR/shadow/AO pixels and writes skin-scene.png; that actual image
was inspected and shows a bent diagnostic strip. This is not realistic game art.

Full final suites run sequentially after both builds; results follow below. glTF
skin import, skeleton visualization, mixing/retargeting, asynchronous skin status
and populated-game throughput remain unfinished. Only arm64 macOS is validated.

The first full Debug passes 1050/1050 (253.45s). Initial full Release passes
1049/1050 (59.73s): the existing native ring/scale input case intermittently fails
to begin/commit its synthetic drag. Five unchanged Release reruns pass; a first
focus/pump-only adjustment still fails in repeated Debug execution. The test was
mixing injected and desktop pointer traffic. Added a scoped device-tagged SDL
pointer filter for that window, preserving focus/keyboard/window events and
restoring any previous filter. An unrelated pointer event is explicitly rejected;
the real SDL translation, editor, GPU pixels and undo checks remain active. Final
builds succeed; the isolated case passes ten successive runs in each configuration
(Debug 11.00s, Release 3.10s). Final full suites follow after that harness change.

Final Debug passes 1050/1050 (242.57s) after rebuilding both configurations with
the scoped input fixture. It includes every new skin/editor/export test and the
previously intermittent ring/scale case. Final Release runs afterward, with no
competing build/full suite. All 355 C/C++ sources pass clang-format; all 17 unchanged
WGSL sources pass established indentation/line/whitespace checks and actual GPU
pipeline execution. Inventory is 982 doctest identities plus 68 executable/script
checks: 242 unit, 684 integration, 212 GPU, 129 offscreen, 71 native, 124 acceptance,
147 compiler, 20 process, 27 platform and 32 audio-native labels (overlapping).

Final Release/NDEBUG passes 1050/1050 (45.47s), including the ring/scale case and
every new skin check. Complete configurations remain sequential, after their final
builds. No Windows/Linux execution or full large-game/editor readiness is claimed.

The owned archive target succeeds. Final ZIP is 49,049,965 bytes/276 entries/244
exact payloads, with passing CRC/modes, original font/HDRI/licenses and private/
source exclusions. Strict signatures and portable loader/rpath references pass
for 13 regular Mach-O files/three relative library links. Private archive staging
is removed. No new external dependency, compiler override or linkage policy change
was introduced.


## 2026-10-15 — glTF skins through native editor and games

glTF/GLB import now retains four/eight influences, ordered joints and optional
inverse binds through native .dotskin assets and scene components. Shared
primitive/skin pairs reuse immutable data; distinct rigs retain distinct bindings.
Publication constructs all nodes before joint remapping, validates initial CPU
deformation and encodes skins before writing owned staging. Flat-normal expansion
copies original per-corner influences. Bundle reload verifies local asset paths and
real skin/mesh counts. Resource catalog growth includes skin files/directories;
import reports expose skins and skinnedMeshes through actual workspace commands.

Strict tests cover glTF/GLB ownership, optional/extra/malformed inverse binds,
four/eight and float/normalized integer weights, exact quantized sums, sparse and
strided attributes, zero-weight out-of-range indices, selected-scene/common-root
rules, duplicate joints, different rigs, multiple primitives and exact owner/catalog
capacities. Non-unit float weights warn and normalize; unsupported extra influence
sets fail explicitly. No compatibility layout or version dispatcher was added.

The runtime acceptance imports the actual owned eight-joint GLB, reloads native
files, advances GameSession animation and compares nine PBR/shadow/AO frames with
independent CPU deformation. Picking/bounds and resource release are checked. A
relocated package reproduces a known pose after both authoring and relocated input
trees are removed. Actual editor import/Inspector/complete-joint duplicate remapping,
undo/isolated Play/save/native export checks use production handlers; the relocated
native game starts after removing its source project. First/middle/final runtime
frames and editor bind/Play viewport images were inspected: a diagnostic metal ribbon
bends as expected. This is not realistic character or game artwork.

The physical mirrored-owner test initially failed: the skin palette cancels its
mesh transform, but physical front-face selection used only that transform sign.
GPU status now carries blend orientation into the published posed mesh; every
physical raster pass receives the combined face sign. Tangent transport separately
uses world reflection because skin deformation already writes tangent handedness.
The first combined-sign implementation double-counted reflection and failed the
existing eight-weight normal-map pixel comparison; separating those signs fixes
it. Uniform reflection preserves authored faces/picking. Mixed reflected islands in
one primitive fail atomically, with an explicit split-primitive contract in both
CPU and GPU paths; previous buffers, bounds and counters remain intact.

Earlier iteration also corrected a shadowed parser variable, a temporary-vector
borrow in a new test, missing direct module imports and ignored nonvoid script
lifecycle reports. Final review covered accessor bounds/normalization, source/node
ownership, deferred references, output byte/catalog budgets, error propagation,
transactional GPU publication, reflected normals/tangents and test-only dispatch.
The corrected focused Debug selection passes 45/45 (24.28s). Both final configured
builds succeed with the caller's existing environment; strengthened editor coverage
requires every duplicated joint to leave the original joint set. All 359 C/C++
sources pass clang-format and all 17 WGSL files pass two-space indentation,
100-column and whitespace checks. Final focused Release and full suites follow
sequentially after both builds. Only arm64 macOS execution is claimed. Skeleton
visualization, blending/retargeting, asynchronous skin status, populated-game
throughput and polished realistic Prism Relay remain open.

The expanded final Release selection passes 63/63 (6.64s), including previous model
import/animation and editor animation cases. Full Debug passes 1064/1064 (254.43s);
full Release/NDEBUG runs afterward and passes 1064/1064 (56.25s), with no competing
compilation. Inventory is 994 doctest cases plus 70 executable/script checks: 243
unit, 695 integration, 216 GPU, 132 offscreen, 72 native, 126 acceptance, 149 compiler,
21 process, 27 platform and 32 audio-native labels (overlapping). Final style review
adds one blank line before a test loop, with no behavior change; both affected
test targets rebuild and the nine model-skin integration cases pass again in Debug
(0.75s) and Release/NDEBUG (0.10s). All 359 C/C++ files and diff whitespace pass again.

The owned archive target succeeds. Final ZIP is 49,065,340 bytes, 276 entries and
244 exact payloads. CRC/modes, original font/HDRI/license bytes and private/source
exclusions pass. Strict signatures and portable loader/rpaths pass for 13 regular
Mach-O files and three relative library links. Private archive staging is removed.
No external dependency, compiler override or linkage-policy change was introduced.


## 2026-10-15 — Native atomic animation cross-fades

SceneAnimator now supports two moving clips with linear translation/scale and
normalized shortest-arc quaternion SLERP. Math exposes the reusable checked rotation
operation. Fade progress uses unscaled elapsed time; pause freezes both clocks and
fade, speed zero freezes clip clocks, and nonlooping endpoints hold until fading
finishes. Missing source fields start at the visible local pose; missing destination
fields fade to original binding values. Other transform fields remain live.
Interruptions freeze the current composite in one bounded channel map rather than
retaining an accumulating layer chain. Paused/unapplied sources stay frozen.
Seek/stop/immediate switching restore outgoing-only channels and retire transitions
only after a successful scene transaction. Complete poses, both clocks and progress
are retained on sampling or final world validation failures. Clips and snapshots
are owned; scene moves preserve scope and wrong threads/retired bindings fail.

The native foundation follows the repository's bottom-up rule before library/SDK/
editor consumption. Cross-fade is not yet routed through ScenePlayers authority
reservation, dot:engine, editor controls or exported gameplay transition commands.
Those are required next integration stages; current immediate playback is retained.
No animation blend graph/state machine, populated-game performance, mature editor
or realistic Prism Relay completion is inferred.

Math boundary/shortest-arc checks pass 1/1 (0.04s). Initial new test compilation
caught an assumed equality operator on World::Transform; comparisons now check all
three actual fields. Expanded native transition cases pass 9/9 (0.09s), including
invalid source and target samples followed by recovery that proves both clocks
rolled back. Other cases cover partial channels, old rest values, dual-clock phase,
pause/reverse/loop/zero speed, repeated interruption, zero/day-long durations,
singular blended scale, world overflow, moves/scopes/threads and failed starts.
Existing native animation checks also pass during iteration.

The runnable imports the actual eight-joint GLB, saves/reloads an opposing native
clip, then cross-fades and interrupts it. Ten PBR/shadow/AO frames match independently
calculated joint angles and CPU-deformed geometry. Starting/interrupted pixels are
continuous; a single persistent GPU skin is reused, stop restores bind pixels and
all drawing resources return to baseline. Debug runnable passes 1/1 (3.18s). Three
actual first/intermediate/final PNGs were inspected: the diagnostic ribbon changes
bend as expected. This is not realistic character or game artwork.

Both final configured Debug/Release builds succeed using the caller's toolchain
environment. Review covers sample/channel ownership, bounded interruption state,
unscaled versus clip clocks, partial-channel fallback, normalized rotation endpoints,
failure-before-commit ordering, reflected/singular scales, scene/thread identity,
test-only dispatch and unchanged shader/resource lifetimes. All 361 C/C++ files pass
clang-format and 17 unchanged WGSL files pass established indentation/line/whitespace
checks. Focused final selections and full suites follow after both builds finish,
with configurations run sequentially. Only arm64 macOS is locally validated.

Combined Debug animation/math/script/skin/editor checks pass 65/65 (17.72s).
Final review then covers a clock-only restart followed immediately by a fade: the
old sampled pose is marked unapplied until sampling, so the fade freezes what is
still visible instead of treating the rewound clock as an already applied pose.
That boundary is added to the ownership/move case. Both final builds succeed again;
Release/NDEBUG focused checks pass 65/65 (3.70s). Full suites follow without any
concurrent compilation.

Full Debug passes 1075/1075 (267.42s). Inventory is 1004 doctest cases plus 71
executable/script checks: 253 unit, 695 integration, 217 GPU, 133 offscreen, 72
native, 127 acceptance, 149 compiler, 21 process, 27 platform and 32 audio-native
labels (overlapping). Full Release/NDEBUG runs afterward without competing builds.
Per the user's updated delivery preference, interim ZIP generation and distribution
handoff are deferred until the full engine/editor/game objective is complete.
Necessary native export/relocation tests remain part of subsystem verification.

Final Release/NDEBUG passes 1075/1075 (58.81s), following complete Debug after both
final builds. Every new transition/math/runnable case and prior editor/script/game
path is included. Source formatting and staged whitespace checks pass. No interim
distribution ZIP was generated; no Windows/Linux execution or full-goal completion is
claimed.

## Scene-library and script animation transitions

ScenePlayers now exposes library-index cross-fades without editing saved player
properties. Outgoing/incoming entity writers and interrupted channels remain
reserved during transitions, including pause. Successful completion, seek, stop,
restart or immediate switching retires outgoing ownership with preallocated storage;
competing players and rigid-body ancestors fail before mutation. Sampling failures
retain pose, both clocks, progress and reservations. Cached clips survive source
file removal. Each player retains the existing per-player atomicity contract.

The embedded dot:engine SDK adds animation.crossFade and Entity.crossFadeAnimation,
plus owned nullable transition snapshots. Host controls validate strict arguments
and remain forbidden during constructors/state migration. Feature Lab independently
inventories 95 runtime members and demonstrates gradual Digit 1/2 transitions.
Actual imported GLB rig acceptance drives the SDK, checks independent joint angles
and CPU/GPU PBR/shadow/AO frames, and replays a known fade after source removal.
Editor acceptance now runs the scripted fade in isolated Play, checks remapped joint
angles and diagnostics, restores authoring pixels, saves/exports, and rejects hidden
script errors in the relocated native child. Visual skeleton/timeline/transition
editing, blend graphs, streaming/performance and realistic game content remain open.

Initial focused ScenePlayers/SDK checks pass 10/10 (0.72s); actual imported skin,
editor and Feature Lab checks pass 7/7 (21.57s). The latter are then strengthened
with independently calculated editor joint angles and explicit script error checks.
Compilation catches a shadowed local in that additional acceptance code; the local
is renamed before final builds. No toolchain/environment workaround is introduced.
Final Debug/Release builds and complete validation follow below. No interim ZIP is
prepared; native export checks are subsystem evidence, not a distribution handoff.

Final review corrects the added editor angle check to look up live Play entities,
not authoring entities, and avoids rejecting a legitimately completed fade in a
slower native child. Both final builds succeed. Final focused Debug passes 18/18
(21.45s), including all four cancellation ownership subcases and hidden script
error checks. All 361 tracked C/C++ files pass clang-format. The 17 unchanged WGSL
sources pass whitespace, 100-column and two-space checks, allowing JavaDoc comment
continuation alignment; actual pipelines are also covered by native/GPU tests.
The current inventory is 1080 identities: 1009 doctest cases and 71 executable/
script checks, with 253 unit, 700 integration, 217 GPU, 133 offscreen, 72 native,
127 acceptance, 154 compiler, 21 process, 27 platform and 32 audio-native labels
(overlapping). Complete Debug then Release/NDEBUG follow after compilation ends.

Complete Debug passes 1080/1080 (257.76s). Every new library/SDK case, diagnostic
model frame, editor workflow and prior engine/game check is included. Release/NDEBUG
runs afterward without concurrent compilation. Runtime and editor diagnostic skin
PNGs are visually inspected; they show the intended bent ribbon, not character art.


Complete Release/NDEBUG passes 1080/1080 (53.36s), after Debug and both completed
builds. Final diff review covers allocation-before-pose ordering, writer retirement
and interruption ownership, shared callback/scene lifetime, strict host arguments,
ReadOnly migration gates, unchanged authoring, platform-neutral implementation,
formatting and focused failure/normal/boundary coverage. Only arm64 macOS is
executed; Windows/Linux support is implementation-only. No full-goal completion or
production animation performance is claimed.

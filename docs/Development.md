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

Scene audio asset/loading/playback synchronization, scripting, project/editor/
export tooling, and the example game remain to be implemented.

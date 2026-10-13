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

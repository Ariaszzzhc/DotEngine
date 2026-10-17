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

## Physical example environment and preview diagnostics

Prism Relay now owns local photo-derived concrete/metal PBR resources, original
hard-surface meshes with explicit normals, HDRI illumination, directional soft
shadows, SSAO
and a perspective industrial bay. Source authors, CC0 links and SHA-256 inputs are
stored with the project. Geometry is reproducible with a standard-library Python
authoring tool; runtime/editor depend only on the generated native assets. Mechanical
slats/vents are grouped as meshes rather than individual hierarchy entries. The
scene has 173 entities, with its existing collision/sensor route still intact.
This stage establishes usable game content inputs, not realistic art completion.

The user clarified the next target: a non-shooting over-the-shoulder humanoid
character template with movement/jumping and camera control. Roadmap/example docs
now explicitly record that requirement. The intermediate collection scene is not
presented as the requested final gameplay. Character physics, model/animation,
follow/orbit camera and camera collision remain required next integration work.

Actual PBR rendering initially rejects the old crystal OBJ's absent normals;
the authored crystal now supplies explicit facet normals. A device-free asset
integration checks normal-map frames and actual local 1K texture inputs. Real
rendering verifies six shared textures, HDR values above one, ACES-fitted output,
and independently visible contributions from IBL, texture maps, soft shadows and
SSAO. Preview override removal restores exact original pixels. The ordinary editor
runs the project in isolated Play, restores authoring pixels, and an owned package
reproduces the gameplay-ready frame after both source trees are deleted. Existing
native input/audio/standalone and collection/damage/restart/reload cases are retained
until the clean character-template replacement stage.

The new editor case initially reads a viewport before native Docking initialization
settles; it now uses the existing three-frame layout initialization. Actual preview
failures now appear through inspect's nullable view.error and contextual capture
errors; a missing OBJ/undo test checks recovery. The initial diagnostic test used
the document-only snapshot API instead of workspace inspect; the endpoint is
corrected. Final targeted physical workflow passes 1/1 (15.61s), diagnostic recovery
passes 1/1 (1.14s); earlier complete game/source/template focus passes 9/9 (26.54s).
An explicit ColorSpace module import fixes new test compilation without environment
changes. All 361 C/C++ files pass clang-format. Regenerating all 15 original mesh/OBJ
outputs produces identical SHA-256 values. Shaders are unchanged. Both configured
Debug/Release builds succeed; full suites run serially below. No interim ZIP is made.

The first full Debug run passes 1080/1083 (331.06s); three project-home/CLI
assertions still hard-code the earlier 40-entity template. They now compare the
created scene count with its actual source template, retaining the complete-copy
contract. Both builds succeed again. Final full Debug passes 1083/1083 (321.83s);
Release/NDEBUG runs afterward without concurrent compilation. The inventory is
1012 doctest plus 71 executable/script identities: 253 unit, 701 integration,
133 offscreen, 219 GPU, 74 native, 129 acceptance, 155 compiler, 21 process,
27 platform and 32 audio-native labels (overlapping). All tracked C/C++ formatting
and diff whitespace checks pass. Actual editor Play and relocated-export PNGs
are visually inspected; neither image is presented as final character-template art.

Complete Release/NDEBUG passes 1083/1083 (71.87s). Final diff review checks
project-local asset references, explicit normals/UV frames, license/source hashes,
owned export input lifetime, preview-error recovery, source-template copy checks,
platform-neutral changes and retained existing coverage. Only arm64 macOS is
executed. No interim package or character-template completion is claimed.

## Native character physics foundation

The requested non-shooting over-the-shoulder template now has a native physical
foundation. A separate Dot.Physics.Character interface/implementation validates
Y-up feet-origin capsule properties, initial pose and horizontal/jump intent without
initializing Jolt. World owns real Jolt virtual characters in its existing body
simulation; creation prepares shape, contacts and ownership before publication.
Public handles remain scoped and monotonic across moves. World destruction removes
all capsule references before physics storage, and explicit destruction is available
after faults. Character capacity is independent from body capacity.

Actual capsule sweeps support falling, one-use jumping, wall sliding, ceilings,
walkable/steep slopes, configurable stair traversal, dynamic-body pushing and
horizontal/vertical platform support. Ground is refreshed before using it, including
after deleting a platform or another capsule. Characters and bodies share the
existing 60 Hz substep bound; body-only simulation retains its existing update path.
Reported native query overflow and unexpected post-mutation failures fault the world
with context, rather than returning a successful incomplete update. Input/pose
validation failures preserve state and pending jump intent. Teleport explicitly clears
motion/intent; yaw preserves both. Solver velocity includes native support probing.

The initial focused run passes 9/11; the resting-velocity assertion incorrectly
assumes a rigid body's zero velocity rather than the virtual controller's downward
support probe. It now checks that actual bounded probe. The stair failure is a real
integration error: pre-clipping steep-slope velocity removes the desired direction
needed by ExtendedUpdate's own stair test. The redundant clipping is removed;
actual stair traversal and rejection of a 65-degree ramp both pass. A later test
compile rejects a shadowed local variable; renaming it fixes the warning without
changing toolchain settings. No environment workaround or sanitizer is introduced.

Final focused Debug character/physics selection passes 40/40 (2.37s). The final
character-only selection, including additional yaw/teleport intent checks, passes
17/17 (0.85s): three CPU unit, thirteen actual Jolt integration and one headless
CLI acceptance. It covers capacity recovery, stale/foreign/moved handles, worker
rejection, sensor pass-through, removed support, capsule/capsule contacts and repeated
cleanup, native contact budget failure during creation/teleport/stepping, coupled
substeps and dynamic/platform behavior. The acceptance implementation/dispatch lives
only in tests/acceptance and BUILD_TESTING targets. No new window is required.

Both final Debug/Release builds succeed before the complete suites. Current CTest
inventory is 1100 identities: 1028 doctest plus 72 executable/script checks. Labels
(overlapping) are 256 unit, 714 integration, 133 offscreen, 219 GPU, 74 native,
130 acceptance, 155 compiler, 21 process, 27 platform and 32 audio-native. Full Debug
then Release/NDEBUG run serially below.

This is an engine library foundation. It has no inner body proxy, so rigid-body
ray/sensor/event streams and fast incoming bodies do not see virtual capsules.
Scene persistence/Inspector/SDK, actual humanoid animation, mouse capture and the
over-the-shoulder collision camera are not implemented by this stage. Prism Relay
still has its intermediate environment/startup, not the final requested template.

Full Debug passes 1100/1100 (317.34s). Release/NDEBUG follows without concurrent
compilation. All 366 current C/C++ files pass clang-format, including the new
module units, focused tests and development-only acceptance driver. Diff whitespace
checks pass; the 17 WGSL sources and library linkage are unchanged.

Complete Release/NDEBUG passes 1100/1100 (61.49s). Final source review checks
native lifetime/order, collider-list reservation and prepublication ownership,
strict finite limits, world/thread/handle scope, stale support refresh, jump intent,
post-mutation fault behavior, coupled substeps, unchanged body-only updates and
platform-neutral implementation. Actual CLI acceptance reports a 1.785 m jump
peak, a 2.430 m wall stop, grounded landing and released capsule ownership. Only
arm64 macOS is executed. No character rendering or final template completion is
claimed by this stage.


## 2026-10-15 — Cleanup while the engine goal is paused

The user paused feature development to reassess project structure, test scope and
original goals. No character scene/SDK/camera or new rendering work is resumed.
The unfinished, unverified body/character batch draft was saved as an ignored patch
at build/cleanup/paused-character-batch.patch; the two Physics World module files
were restored to the reviewed 0c6fee2 baseline before cleanup. This draft is not
implemented or validated by the cleanup.

Removed tests/acceptance and both dedicated acceptance executables, per-stage
triangle/cube/visual probes, and automatic desktop-window workspace/home/native
example/distribution tests. Enduring CPU, file/compiler/VM, document/resource,
physics, audio and offscreen GPU contracts remain. Unit tests never initialize
windows/devices; platform integration explicitly uses SDL's dummy video driver.
The headless project-action case extracted from HomeTests retains real creation
and loading of all three templates without opening a Home or Workspace.

Native export regressions now load actual relocated GamePackage data into compiled
headless GameSession after deleting authoring inputs, check script/animation
advancement and shutdown, and never start a native game window. They no longer
claim native presentation or loader execution coverage. The first full Debug run
passed 973/974 (133.47s): a newly added RuntimeKit::open assertion mistakenly treated
an exported game directory, which also owns game/, as a pure kit. Removing that
incorrect assertion retains the actual publication/package/session checks. Both
configurations were rebuilt before the final serial full runs.

Moved Feature Lab from acceptance/ to examples/, including generated copies,
project-home templates and distribution inputs. Removed the one-off Prism Relay
mesh authoring script; its authored meshes/materials and CC0 provenance remain.
Deleted retired binaries/obsolete generated Feature Lab copies, old scratch patches,
helper scripts and investigation directories. Compiler/dependency build caches,
the existing latest archive, user design inputs and untracked planning document
remain. No new editor distribution/archive was built.

The editor's complete AI control requirement is preserved in AGENTS, skills and
current workflow documentation. Prefer real CLI/strict JSON/live mailbox operations;
computer use supplements human-style clicks/keys and unexpected visual/UI failures.
No test-only product operation or substitute editor state was introduced. Current
documentation removes retired commands; historical log entries remain historical.

Final macOS arm64 results: Debug 974/974 (133.63s), then Release/NDEBUG 974/974
(15.77s), without concurrent compilation. Identical inventories contain 256 unit
and 718 integration cases, including 123 offscreen GPU, 27 dummy-platform and 27
actual audio-device checks (labels overlap). No acceptance/native-window target
or retired executable remains in the inventory. All five changed/new C++ test
files pass clang-format --dry-run --Werror; diff whitespace checks pass.

A separate single actual Debug editor session opens examples/FeatureLab. Product
agent operations inspect a ready viewport and completed previews, select entity 2,
focus the scene, start Play and inspect running state at 1302 frames/1458 fixed
steps with no diagnostics, no error and no dropped time. Stop reports 1314 frames/
1473 fixed steps; authoring scene equals the original snapshot, remains clean and
retains its selection revision. The close request is queued; the editor exits 0
and removes its owned control directory. The helper cannot read a close response
after that retirement, so close is verified by actual process exit and cleanup,
not a retained receipt. Temporary request/response observations live only under
ignored build/cleanup. No new window-test driver or manual visual-quality claim
is made. Windows/Linux remain unexecuted locally.

Precommit review checks test/device separation, actual package ownership/error
paths, module files unchanged, all Feature Lab source/build/template/install paths,
asset provenance, current documentation and excluded user inputs. Fewer tests do
not establish production readiness. Prism Relay remains an intermediate project;
its requested non-shooting shoulder-view character template and mature editor/
large-game requirements remain open pending reassessment.


## 2026-10-15 — Saved character controllers and shared scene simulation

The user resumed the original engine/editor objective after cleanup. Large-game
capability, mature editor workflows, complete product AI control and the requested
non-shooting shoulder-view Prism Relay remain the goal. This stage connects native
character physics to ordinary saved scenes and editor operations; it does not
claim that the requested humanoid/template or general production readiness is done.

The previously archived batch draft was reviewed and extended into mixed body/
character synchronization. Both sets prepare before mutation, retain state/IDs on
invalid input and capacity failure, and support replacements at full capacity.
Final native contact queries run after publication; a failure then explicitly
faults the world, preserving diagnostic reads/destruction instead of pretending
rollback. Focused tests cover result ordering, handles, capacity, motion and jump
intent retention, owner-thread/move behavior and post-publication contact failure.

Scene owns an optional character description without initializing physics. It is
mutually exclusive with a rigid body on the same entity. The strict existing scene
codec rejects malformed/unknown fields and validates integer contact budgets
before conversion, including oversized integers that would otherwise wrap.
Document/agent edits, history, save/reload, duplicate and Inspector drafts consume
that same description. Inspector defaults come from the canonical scene codec;
there is no parallel test-only editor operation or compatibility format.

SceneSimulation binds Y-up feet-origin capsules, checks positive uniform world
scale and scales capsule dimensions while retaining world-metre stair/stick
settings. Characters and bodies synchronize together; their world poses write back
in one hierarchy-ordered scene transform transaction. Pose edits reset intent;
property-only replacements preserve live velocity, queued jump and explicit yaw.
Ground support uses scene-scoped entity IDs. Support lookup currently scans body
bindings; no crowd/performance claim is made. Faulted binding can recover through
an independently prepared reset. Characters retain the documented virtual-capsule
limitation: no inner broad-phase body, ray/sensor or rigid-body event participation.

Character roots and their ancestors reserve physical transform authority, while
child bones/meshes remain animatable. Both ordinary animation and cross-fade clip
switches reject conflicting writers before changing pose/playback. Focused tests
exercise actual child animation and root/ancestor/cross-fade rejection.

The first focused mixed/native-controller selection passes 26/26 (1.29s), followed
by scene/document/animation/physics selection 94/94 (4.66s). Both configurations
build successfully. The first full Debug run passes 989/990 (135.63s): the new
relocated-package test incorrectly calls only variable update, which intentionally
does not step physics. The test is corrected to call the existing fixedUpdate then
update contract, and passes its focused rerun. No engine fallback or altered time
semantics is introduced. Final rebuilt full-suite and actual product observations
are recorded below once complete.


Final rebuilt macOS arm64 full suites pass sequentially: Debug 990/990 (134.22s),
then Release/NDEBUG 990/990 (19.33s), without concurrent compilation. The inventory
has 257 unit and 733 integration cases, including 123 offscreen GPU and 27 dummy-
platform cases (labels overlap); no desktop-window acceptance target is added.
All 16 changed/new C++ files pass clang-format --dry-run --Werror, and diff
whitespace checks pass. Dependencies, caller toolchain flags and linkage contracts
are unchanged. Windows/Linux remain implemented but unexecuted locally.

After both suites, one actual Debug editor session opens a writable blank project
created through the product project-action command in ignored build storage.
Strict live agent operations author a saved character, floor and child visual,
select and duplicate its hierarchy (the copied controller is preserved), undo/
redo/undo, save, focus the scene, start Play and inspect actual runtime state.
At 3400 frames/1707 fixed steps the capsule's feet are y=-4.10e-8, with no script
or realm diagnostics, no native error and no dropped time. Stop reports 3429 frames/
1722 fixed steps and exactly preserves the saved authoring scene/revision/clean
state. The agent then exports a real data package and queues clean close; the
editor exits 0 and retires its control directory. No native game window, screenshot
comparison or end-user visual-quality claim is made. This uses ordinary product
operations, not a retained test driver or a committed generated example.

Precommit review covers mixed-batch prepublication ownership, final contact fault
semantics, retained motion/IDs, hierarchy writeback, strict integer decoding,
component exclusivity, animation authority, editor/agent sharing, separate module
interfaces/implementations, formatting and test scope. The new headless package
regression additionally removes authoring and relocated input before running the
owned compiled scene, proving actual capsule landing without source/compiler use
at runtime. Character SDK controls, realistic humanoid content, camera/input
integration and the final Prism Relay template remain subsequent work; none is
claimed complete by this stage.


## 2026-10-15 — Character script controls and interactive Feature Lab

The next stage exposes the actual scene controller through the engine-owned
TypeScript module. components.character and Entity.character share the native
validated authoring service; EntityInfo includes its owned description. State
queries return feet/rotation/velocity, ground data and a scene-scoped support ID.
Entity.moveCharacter/setCharacterYaw and their physics counterparts supply native
input/facing, without scripted gravity, collisions or hidden simulation stepping.
PhysicsStats counts characters independently. Queries remain ReadOnly in lifecycle
preparation/migration; controls/component writes remain ReadWrite and are denied.
Strict argument and integer validation rejects malformed/oversized values while
retaining queued motion/jump intent. No module-name fallback, compatibility format,
new dependency or toolchain setting is added.

Feature Lab adds an ordinary saved controller and child box visualization through
actual DotEditor document edit/save operations. Its platform is enlarged, and its
arrow/J script supplies normalized 3 m/s input and grounded jump requests. The SDK
inventory/audit includes nine new public operations, spread over a separate update
from animation checks to preserve the per-hook host-call budget. Native canonical
comparison confirms all sixteen original entities retain semantics apart from the
intended platform scale; the larger scene diff is ordinary codec normalization.
IDs above JavaScript's safe integer range remain canonical strings throughout.

Focused native-host and real-TypeScript tests cover queries, malformed arguments,
owned snapshots, integer overflow rejection, jump/input retention, authoring,
constructor/checkpoint write denial and retained motion through realm replacement.
A Feature Lab regression publishes actual native input, jumps, edits movement speed,
reloads source without resetting physics, applies the changed speed, releases input
and lands. No window is opened by these tests. The first test compile incorrectly
refers to ReloadReport.failures; it is corrected to retired.failures. An initial
21/22 focused result rejects an incorrect expectation that two components produce
checkpoint migrations: the stateless character uses undefined base checkpoints,
while only Audit saves state. The original one-checkpoint expectation is restored;
no lifecycle implementation is changed. The complete focused selection then passes
23/23 (2.92s), and initial full Debug 993/993 (135.30s) and Release/NDEBUG 993/993
(19.68s) pass before actual product inspection.

Actual product inspection finds a real showcase layout issue missed by the short
runs: the preexisting continuously moving kinematic box pushes an idle capsule off
the platform. A new four-second headless idle regression reproduces x=6.47954 and
y=-4.00256 instead of x=4/y=-1.5 (3.36s). The controller is moved into the separate
z=2 showcase lane through the product document commands. The same real editor
session stops/restarts Play with that authored correction. The fixed idle/input/
SDK selection passes 19/19 (3.33s), including the new idle case (3.31s). Final full
runs after rebuilding both configurations are recorded below.

In that actual editor session, live agent operations open/replace/save the character
script while Play runs, temporarily requesting one native jump and horizontal
motion, then restore the original source through the same clean buffer. Observed
feet move from [4,-1.5,2] to [4.133335,-0.798100,2]; after restoring/reloading source,
physics continues at [4.766677,-0.423249,2] and subsequently lands at
[4.766677,-1.5,2]. No script/native error is reported, all nine SDK audit operations
are observed, and stop preserves the saved authoring scene. Data export succeeds;
clean close exits 0 and retires the owned control directory. Temporary script
changes are byte-for-byte restored in the disposable project. There is no screenshot
or human-keyboard acceptance claim. One hot-reload frame reports 199999992 ns of
dropped time; compilation remains synchronous and background compilation is still
needed for editor responsiveness. This limitation is not hidden behind green tests.

The previously archived body/character draft patch is removed from ignored storage
after its reviewed implementation is committed/pushed in 99ef05e. Investigation
requests/responses/logs stay only under ignored build storage. No interim editor
archive, new mesh-generation helper or per-stage acceptance executable is added.


Final rebuilt, post-fix suites pass serially on arm64 macOS: Debug 994/994
(135.18s), then Release/NDEBUG 994/994 (19.08s), without concurrent compilation or
an open manual editor. Matching inventories include 257 unit and 737 integration
cases, including 123 offscreen GPU, 27 dummy-platform and 27 audio-device cases
(labels overlap). No desktop-window driver/target is added. All five changed C++
files pass clang-format --dry-run --Werror; new/changed TypeScript sections are
formatted, and the real pinned compiler/runtime execute the SDK/example. Diff
whitespace checks pass. Dependencies/linkage/toolchain environment are unchanged;
Windows/Linux remain unexecuted locally.

Precommit review covers the eleven registered physics methods and access flags,
strict DTO/number handling, callback/shared scene ownership, template propagation,
canonical scene semantics, component exclusivity, motion retained through reload,
fixed-step input edges, stopped input, sample platform separation and test scope.
Current code preserves the complete original goal. The new lab visual is a simple
component showcase; realistic humanoid content, shoulder-camera/input integration,
Prism Relay quality, background compilation and mature large-project production
workflows remain unfinished.


## 2026-10-15 — Native sphere sweeps for camera collision

The next camera foundation adds a real Jolt volume sweep, not a center ray used as
a substitute for camera clearance. World and SceneSimulation expose sphereCast with
bounded radius/center endpoints/displacement, explicit sensor inclusion and an
optional ignored current rigid body. Default queries exclude sensors. Scene/script
results preserve owned center/surface/normal/depth and scene-scoped owner identity.
Invalid/stale/foreign filters fail explicitly. Native temporary query shapes do not
occupy body slots, move/wake objects, advance time or publish contact events.

Closest-hit results report initial overlap at fraction zero without pretending to
depenetrate. Degenerate overlap can lack a unique normal. Contact points use an
origin-relative Jolt base offset before converting to world coordinates; this is
not an origin-rebasing/streaming system. Virtual character proxies remain absent.
Implementation follows the pinned local Jolt ShapeCast/NarrowPhaseQuery contracts;
no new dependency, versioned format or compiler/linker override is introduced.

The engine-owned TypeScript SDK exposes physics.sphereCast and typed filters/hits.
The native callback is ReadOnly, including during component preparation. Strict
argument/DTO validation and stale returned/ignored owner checks match existing
query contracts. Feature Lab's actual physics audit sweeps onto its authored
platform and checks owner, center/surface separation and outward normal. Its SDK
inventory includes the new public method without a standalone visual driver.

Initial native/scene selection passes 6/6 (0.49s). Native/scene/host/real-TypeScript/
Feature Lab selection passes 26/26 (3.45s). Additional curved-collider and translated
world coverage brings the final sphere selection to 8/8 (0.57s), including box/
sphere/rotated-capsule contacts, a center-ray miss caught by sphere radius, initial
penetration without waking a sleeping body, sensors/ignored owners, malformed
ranges, moved/foreign/stale handles, worker rejection and scene ownership.
Both configurations build before full serial suites; final results follow below.

No separate desktop session is opened solely to inspect a volume query. Integrated
camera/pointer behavior will use the actual editor/runtime product. Shoulder camera,
continuous mouse capture and realistic humanoid content remain unfinished; the
broader engine/editor objective is not narrowed to a query API or green tests.


Full serial arm64 macOS results pass: Debug 1003/1003 (137.42s), then Release/NDEBUG
1003/1003 (18.56s), without concurrent compilation. All eight changed/new C++ files
pass clang-format --dry-run --Werror; changed TypeScript sections are formatted,
and diff whitespace checks pass. The 257 unit tests initialize no windows/devices;
new volume/query tests are integration checks. Windows/Linux remain unexecuted.
Precommit review checks query-shape lifetime, borrowed native filters, body locks,
contact offset/sign conventions, overlap/normal semantics, explicit error paths,
scene mapping, host access flags, SDK/template propagation and actual test scope.


## 2026-10-16 — Native relative pointer input

Added continuous native relative mouse capture for runtime and editor Play, without
creating windows from scripts or headless game sessions. SDL window operations
require a live owner on the main thread and propagate backend failures. InputSource
now separates a ReadWrite capture request from the native publisher's actual
acknowledgement; nine query bindings remain ReadOnly. Preparation/checkpoint guards
deny capture writes, requests survive realm reload, and failed input publication
retains the previous snapshot/acknowledgement. Pointer-mode transitions clear only
accumulated motion, preserving other current-frame input.

GameSession and Play publish the acknowledgement with the real input frame. Runtime
honors requests while focused and releases on Escape before exiting. Editor Play
requires explicit Game input, a running game and native focus; Escape, focus loss,
Stop and closing release it. Focus loss disables Game input until re-enabled.
Relative motion/buttons/wheel do not operate editor widgets, and absolute
outside-viewport release rules do not misinterpret captured pointer coordinates.
Strict existing AI commands share this policy; Play inspection reports requested
and actual native state separately rather than claiming immediate command effects.

Feature Lab's ordinary Camera script provides M-toggle mouse look, consumes motion
once per variable update and rotates only on acknowledged frames. It derives its
orientation from the real transform, clamps pitch and does not reset the camera
on source reload. The SDK audit checks the new calls without keeping capture on.
Native/VM/compiler/Play/scene regressions cover malformed requests, thread/moved
owners, publication failures, callback ownership, preparation/checkpoint denial,
reload persistence, fixed-step motion isolation and acknowledgement gating.
The focused Debug selection passes 53/53 (4.83s), without desktop windows.

A deliberate actual Debug editor session uses the Feature Lab project and normal
CLI/JSON mailbox for inspect, Play, Game input, Stop and close. Computer use presses
M, moves the pointer, presses Escape and minimizes the actual window. Native Play
reports request/capture true after M, and pointer motion changes camera rotation
from identity to approximately [0.639615, 0.091263, -0.076870, 0.759378]. Escape
reports Game input false and actual capture false while the script request stays
true. Re-enabling Game input recaptures; minimizing then reports Game input false
and actual capture false. Merely selecting another computer-use target did not
prove focus loss; the minimized-window observation supplies that evidence.

The attempted window raise did not restore native focus, so subsequent Game input
true still truthfully reports pointerCaptured false. Stop therefore verifies
stopped execution and unchanged authoring state, not an additional captured-to-
released OS transition. The stopped scene remains clean at revision 1 with its
original identity camera rotation; final counters are 7945 frames/9733 fixed steps,
zero dropped nanoseconds, no diagnostics/error. Close is verified through actual
process exit 0. Temporary native app linkage and observations live only in ignored
build storage; no distribution archive or permanent visual driver is produced.

Shoulder-camera collision/following and the realistic animated Prism Relay humanoid
remain unfinished. This input stage does not satisfy those requirements or the
broader mature-editor/large-game goal. Windows/Linux implementation is retained,
but execution evidence is arm64 macOS only. Full serial suite results follow below.

Full serial arm64 macOS suites pass: Debug 1010/1010 (141.64s), then Release/NDEBUG
1010/1010 (17.91s). Inventory contains 258 unit and 752 integration cases, including
123 offscreen GPU, 28 SDL-dummy platform and 27 audio-device cases (labels overlap).
Both configurations built before full suites; a final camera-source whitespace
formatting pass is propagated through both build resource copies before Release.
All changed C++ module/test files and the new camera script pass clang-format checks;
diff whitespace checks pass. Precommit review covers main-thread/window guards,
requested versus actual state, input edge preservation, realm lifetime/guards,
editor GUI isolation, error propagation, platform-neutral SDL use, module boundaries
and permanent behavioral test scope. No temporary helper/app or user planning/design
input is included in the commit.


## 2026-10-16 — Rigged shoulder character template

Replaced Prism Relay's drone/collection/health/timer prototype with an ordinary
editable third-person movement/jump project. A CC0 Quaternius 65-joint mannequin
and seven authored, in-place locomotion/jump clips are retained as source GLB and
native imported mesh/skin/material/animation resources. The author license and
upstream/adapted SHA-256 provenance accompany the assets. The real editor's normal
model import, instantiate, strict scene edit and save operations produced the
224-entity scene; no runtime asset generator or game-specific renderer is added.
Obsolete drone geometry/materials, pickup/damage audio and round/spin scripts are
removed. Disposable build example copies are replaced so deleted files cannot
linger; persistent projects must be copied outside those generated directories.

Player intent drives the native saved character capsule: normalized camera-relative
WASD/arrows, acceleration, sprint, buffered one-shot jumping, yaw and native
animation cross-fades. Jump input publication is deduplicated across fixed/variable
hooks. Native sphere sweeps handle shoulder-camera corners, immediate inward
collision, damped outward recovery and initial-overlap separation; impossible
geometry reports a failure without publishing an intersecting pose. Reload retains
intent, pending jump and orbit without restarting native bodies or animation.

Focused regression work initially exposed two genuine integration issues: orbiting
the camera-attached listener exceeded bounded audio velocity, and the long authored
jump-start clip outlasted the physical ascent. A separate listener now follows the
avatar head with camera orientation, and ascent/descent controls the airborne
transition. The step regression then exposed Start listener placement being treated
as instantaneous motion: GameSession establishes its initial audio pose baseline
with a zero-time synchronization after the first Start, without resetting velocity
on repeated Start. The final traversal rise is 1.2m; the earlier .9m geometry could
still be walked onto by native capsule support. Actual native checks now distinguish
walkable steps from the rise requiring a jump.

The first Debug selection was 10/12; the next was 12/13, then 29/30 before the final
step geometry correction. The corrected step/audio pair passes 2/2 (5.85s), and the
pre-input-fix Release focused selection passes 30/30 (1.34s). Durable headless
regressions use the actual game scripts, Jolt, animation, audio/session and template
ownership. Camera tests join the existing example target. No desktop-window test
or bespoke visual acceptance executable is introduced.

Actual Debug editor validation imports/authors/saves the model, runs Play and stops
without authoring mutation. Actual Release Play renders the rigged mannequin and
industrial PBR scene. Computer-use arrow input moves the real native character;
the initial W/Space attempt under the user's Chinese IME was intercepted and did
not establish movement/jumping. SDL text input was active during relative gameplay.
The editor and runtime now suspend native text/IME input during relative capture
and restore it on release, without switching the selected OS input method.
Window text-mode mutation is idempotent, native query is main-thread/live-owner
guarded, and strict Play inspection exposes actual text mode and query errors.

The corrected actual Release editor run confirms pointerCaptured true and
textInputActive false. Computer use holds W for .7s: position changes from
[-3,0,2] to approximately [-3,0,-.239974], a 2.24m displacement. A Space press is
observed through 100 bounded live AI inspections over 12.93s, with height reaching
1.7745m and returning to the ground. Mouse orbit changes camera orientation without
an audio error. Escape releases capture and restores text input; AI recapture
restores relative mode, and Stop then releases it and restores text input again.
Final Stop reports 5403 frames/5414 fixed steps, zero dropped nanoseconds and no
script diagnostics/error. The authoring scene stays clean at revision 1 with its
original Player pose [-3,0,2]; normal close exits 0 and removes its owned mailbox.
The input/window/project/audio focused selections pass Debug 19/19 (10.09s) and
Release/NDEBUG 19/19 (.86s), using SDL dummy video for platform cases.

The mannequin is functional template content. Finished realistic character/suit art,
cohesive environment polish, populated-game performance, mature editing workflows
and the broader engine goal remain unfinished. Windows/Linux code is retained;
execution evidence is arm64 macOS only. Temporary asset preparation/download/control
scripts, native app linkage and investigation output are removed after verification;
only the used upstream archive remains as ignored build cache. No distribution
archive is produced. Final full-suite results and review follow below.


The first full Debug run inside the restricted execution sandbox passes 870/1015
(81.23s); all 123 offscreen GPU and 22 native audio failures are device-access
failures, including `Request GPU adapter (3): No supported adapters` and
`Initialize native audio context: No backend`. The unchanged suites are rerun
with host-device access rather than weakening or skipping those checks.


With host-device access, the full serial arm64 macOS suites pass Debug 1015/1015
(145.98s), then Release/NDEBUG 1015/1015 (20.17s). Inventory is 258 unit and 757
integration tests, including 123 offscreen GPU, 28 SDL-dummy platform and 27 native
audio cases (labels overlap). Both builds complete before the full suites. Changed
C++ module/test files and both character/camera scripts pass clang-format checks;
staged diff whitespace checks pass after normalizing only license line endings and
trailing whitespace, preserving its authorization and attribution text.

Precommit review covers native ownership and first-Start audio error/state handling,
main-thread/moved-window input guards, actual versus requested capture/text state,
reload/edge scheduling, native collision and animation use, portable SDL calls,
module boundaries, durable test scope and asset/scene references/provenance. The
source GLB and upstream cache hashes match their recorded values. The commit excludes
user planning/design inputs, build/cache/helper files and any distribution package.


## 2026-10-16 — Native character collider presence

Characters now own real native kinematic inner capsules in the same Jolt broad
phase as bodies. This fills the gap where ordinary rays, volume sweeps, sensor
triggers and incoming-body solver contacts could not see a character. Native
ColliderId is a BodyId/CharacterId variant, used by query hits, contact parties and
ground support. Character proxies never masquerade as publicly mutable rigid bodies.
Backend user data separates handle kinds; opaque caller tags remain separately
owned metadata and survive lifecycle events. Scene bindings supply entity tags for
both kinds and map query/support results back to current entity identities.

The backend reserves an extra character set for batch preparation, retaining
independent public body/character limits and full-capacity replacement. RAII destroys
prepared proxies on rejected batches. Virtual movement excludes all proxy bodies
and retains native character-to-character solving, avoiding self/duplicate collision.
Body/active statistics count only public rigid bodies. Peer support resolves only
while its owner is live and cannot enable a jump after removal.

Character deletion, teleport and replacement preflight the owned contact queue.
Mixed changes reserve the union of affected body/character pairs before either set
publishes, retaining motion, contacts and IDs on rejected preparation. Teleport
preparation failures do not fault a still-unchanged world; failures after publication
remain explicit faults. Native worker callbacks retain the same capture/owner-side
reconciliation and error boundaries. Native proxy geometry/events are real solver
manifolds; virtual predictive-shell encounters that never touch the proxy are not
claimed as complete character contact events. Arbitrary-speed CCD guarantees,
per-body CCD authoring and virtual-only contact callbacks remain unfinished.

Scene/SDK sphere filtering now uses ignoreEntity for either collider kind, with no
old filter-name alias. Native filtering accepts the typed ignoreCollider. Prism
Relay's shoulder camera excludes its own character while retaining all other
colliders. Feature Lab's ordinary character audit demonstrates native ray/sphere
identity and self-filtering. No new host invocation method, public header tree,
special renderer, temporary source generator or visual test executable is added.

The first focused Debug run passes 89/91. One old assertion still required rays to
miss characters and is updated to the new identity contract. The incoming-body
case initially used motion resolved by the virtual predictive shell before a native
proxy manifold; a 20m/s incoming sphere now establishes the actual proxy contact.
A follow-up fixed-location assertion was incorrect: physical bodies can push a
virtual character. The durable regression instead checks real Begin contact,
separation on every step, bounded physical push and no horizontal drift after the
incoming body is removed. Corrected Debug selection passes 91/91 (13.61s); peer
support/removal passes 1/1 (.24s). Expanded Release/NDEBUG selection including
Feature Lab passes 99/99 (1.30s), all without desktop windows.

An actual Release editor run opens the generated Prism Relay project, starts Play
and enables Game input through the strict live mailbox. Computer use holds W for
.6s; the native Player changes from [-3,0,2] to approximately [-3,0,.026693]. Its
sphere-swept shoulder camera follows and subsequently orbits without colliding with
the player, audio failure, script diagnostic, dropped fixed time or viewport error.
Stop reports 3306 frames/3318 fixed steps. Its immediate response precedes the
native capture-mode update; the following inspection confirms capture released and
text input restored, authoring revision 1 clean with the saved Player [-3,0,2] pose.
Normal close exits 0 and removes the owned mailbox. This workflow uses the actual
editor and CUA supplement; no automated test opens a desktop window. Final full
suite/review and cleanup results follow below. Only arm64 macOS is locally executed.


Full serial arm64 macOS suites pass Debug 1022/1022 (147.56s), then Release/NDEBUG
1022/1022 (20.59s), after both builds complete and the native editor has closed.
Inventory is 258 unit and 764 integration cases, including 123 offscreen GPU,
28 SDL-dummy platform and 27 native audio cases (labels overlap). Host-device access
is used for offscreen GPU/audio integration. No automated test opens a desktop
window. All 20 changed C++/TypeScript source files pass clang-format checks, and diff
whitespace checks pass.

Precommit review covers typed owner identity, callback thread safety, proxy shape/
body lifetime, slot bounds and full-capacity batches, shared queue preflight and
post-publication fault handling, stale/foreign owner checks, native-only collision,
scene tags/support mapping, script filtering/host guards, module separation and
platform-neutral Jolt/SDL usage. Temporary native app linkage, control scripts and
observation files are removed; build caches and test logs remain ignored. User
planning/design inputs stay untracked, and no distribution archive is produced.
The broader mature-editor/large-game goal, finished realistic art, per-body CCD,
virtual-only contacts and crowd/scalability evidence remain incomplete.


## 2026-10-16 — Native continuous collision detection

Added CollisionDetection::Discrete/Continuous to the shared CPU-only body contract.
Continuous maps to Jolt LinearCast for solid dynamic bodies. Unsupported motion or
sensor combinations and invalid enum/input types fail before replacement, rather
than storing an ignored flag. Body state reads query actual native motion quality.
The saved required collisionDetection field is shared by scene files, script
components/default factory, native state DTOs, Inspector selectors and strict agent
component edits. Current example scene inputs are updated directly; there is no
old-field fallback, compatibility layer, alternate format version or JS solver.

Mode-only scene replacement retains real linear/angular motion and public IDs,
including full-capacity batches. Durable regressions check thin-wall collisions for
box/sphere/capsule at 240m/s versus the actual tunneling Discrete control, native
character proxy contacts with zero/two workers, malformed/unsupported inputs,
roundtrip state, document undo/redo/save, shared host authoring and realm reload.
Native limits are documented: linear shape casting excludes angular sweep and
crossed sensors, uses backend tolerances/time stealing, and can report provisional
pre-solver contacts. This does not claim unrestricted CCD correctness.

Feature Lab now contains a thin blue wall and yellow CCD slider authored through
normal headless document operations, alongside its existing Dynamic body using
Continuous mode. The ordinary collision.ts project component implements T reset,
input-publication deduplication and fixed-update display-boundary stopping only.
Jolt determines wall collision. Checkpoint reload retains reset-edge state and
native motion. No bespoke executable, runtime debug hook or committed generation
script is introduced. The SDK inventory remains derived independently; stale README
member/extra-check counts are replaced with the current inventory contract.

The first focused Debug selection passes 61/61 (4.40s). Adding the saved collision
component increases state-migrated instances from one to two; the previous exact
count assertion fails 61/62 and is updated to the actual contract. Corrected focused
Debug 62/62 (4.50s) and Release/NDEBUG 62/62 (.68s) pass before native product QA.

Actual Release editor QA opens Feature Lab, selects the CCD slider through the
strict mailbox and observes the Inspector Continuous selector. Play stops the
240m/s slider at approximately x=-2.090005 against the wall centered at x=-2.
Only changing the saved mode to Discrete through the same component edit makes it
pass the wall. That native multi-step frame also exposes a demonstration flaw:
variable-update boundary stopping runs too late and permits x=32/out-of-view motion.
The project component moves this boundary policy to fixed update and uses the
current scene pose so an explicit reset is not confused with the previous native
pose. Editing the ordinary script in the open project and restarting Play now
shows Discrete stopping at the display endpoint x=0, while Continuous remains at
x=-2.090005. This is a genuine native-mode comparison, not scripted wall collision.

The regression now runs eight fixed steps before variable updates/reset. An overly
strict repeated-contact equality fails on a .00051m change caused by the current
box orientation/native tolerances; it is replaced with the actual wall-side interval,
while reload-without-advancement still requires exact pose preservation. Final
focused Debug 62/62 (4.61s), Release/NDEBUG 62/62 (.60s) pass. T reset is exercised
through actual InputState publication in headless GameSession; native QA uses Play
restart and mode edits, not an unverified keyboard/IME claim.

Native Stop/undo restores the original Continuous source at clean authoring revision
4 with the slider at [-4,0,2.2]. An attempted Continuous edit on the static wall is
rejected as InvalidArgument without revision/data mutation. The final Continuous
run reports 951 frames/958 fixed steps, zero dropped time and no script/viewport
errors; normal close exits 0, removes its owned mailbox and leaves no stderr output.
The Inspector selector renders correctly; generic long property labels can still
clip in narrow panels and remain editor-polish work. Source authoring in the actual
examples and native/agent UI operations share implementation. No automated test
opens a desktop window, and no distribution archive is produced. Final full-suite
and cleanup/review results follow below; local evidence is arm64 macOS only.


Full arm64 macOS suites pass Debug 1029/1029 (150.24s), then Release/NDEBUG
1029/1029 (20.88s), after both builds and native-editor closure. Inventory is 259
unit and 770 integration cases, including 123 offscreen GPU, 28 SDL-dummy platform
and 27 native audio cases (labels overlap). Host-device access supports offscreen
GPU/audio integration; no automated case opens a desktop window. A final test-only
blank-line formatting adjustment is rebuilt in both configurations; the slider
regression passes again in Debug 1/1 (.62s) and Release 1/1 (.25s).

All 20 changed C++/TypeScript source files pass clang-format checks and staged diff
whitespace checks pass. Precommit review covers input/mode constraints, actual native
quality reads, full snapshot/strict schema parity, body replacement ownership and
motion preservation, native contact identity, root/edge/checkpoint lifetime,
shared Inspector/agent validation, fixed-step demonstration behavior and portable
backend use. Compatible reload skips old Stop/new Start, preserving native motion.
No compiler/linker environment, dependency pin or target-platform scope is changed.
Temporary authoring requests, native app linkage/control and observation files are
removed; ignored caches and validation logs remain. User planning/design files stay
untracked and no distribution archive is created. Finished realistic art, complete
virtual-character contacts, mature editor workflows and scalability remain open.


## 2026-10-16 — Inspector fields and agent drafts

Replaced right-hand generic property labels with wrapped label/value rows. Ordinary
fields use two columns at useful widths; narrow panels stack labels above controls.
Numeric vectors keep their full row and reduce their axis column count as needed.
Name/parent, skin and animation asset selectors use the same layout. Scalar edits
preserve signed/unsigned 64-bit kinds, and vector edits change only the edited
element instead of narrowing and rewriting all components through float. Float
display stays concise, with full double precision available on hover. Script JSON
uses the full panel width. Existing directional lights now expose their shadow
toggle while editing, rather than only inside the Add-light button branch.

Added complete-entity preparation to Document and strict live stage-inspector
control using that same implementation as GUI Apply. Preparation validates the final
scene without changing document state, scope, selection, revision or history.
Primary camera and listener transfers demote the prior owner atomically. Staging
requires the current selected ID/revision, rejects malformed/unknown fields and
conflicting drafts, and can replace the current Inspector draft. Apply creates one
history entry. Reset and successful Apply immediately publish canonical fields and
script properties text; inspection no longer depends on first drawing that panel.
Three enduring headless document regressions cover primary transfers, undo/redo/save,
no-op ownership, stale plans, malformed/bounded input, thread/move ownership and
unchanged state after failure. No additional executable or visual probe is retained.

The initial build caught a doctest message-expression precedence error and an
incompatible ranges projection; both are corrected. Focused document suites pass
Debug 26/26 (1.52s) and Release/NDEBUG 26/26 before native workflow validation.
Actual Feature Lab agent operations verify strict staging, rejection without draft
mutation, blocked selection, Apply, undo/redo, Reset, large unsigned/nested JSON
preservation, Play locking and environment-draft exclusivity. Existing-light shadow
removal applies and undoes through this same flow. Document revision/dirty state stay unchanged during
drafts and return clean after undo/reset. Native close exits 0 and removes its
owned mailbox; stderr is empty.

Computer-use inspection covers actual 278px default, 491px wide and 131px narrow
logical Inspector panels, dark/light themes and a Chinese name staged through the
product agent interface. The first layout was too tall at default width, and
vector axes had unequal text baselines; layout thresholds and axis baseline
alignment are corrected. One native scalar edit is confirmed to change only X,
leaving exact Y/Z unchanged, then reset. Later coordinate-based text attempts
reach other controls instead of the intended Name field; the cause is unconfirmed
and these attempts are not counted as successful name-input validation. Chinese
name staging/rendering is verified separately through the product interface.
The user clarified that an earlier unconfirmed W press triggered the Chinese IME;
that observation is not evidence of a movement failure.

Final parity review removes an unnecessary pending-text guard: GUI entity drafts
can coexist with separate agent text-file drafts. The final Release editor verifies
this coexistence, independent buffer/document revisions, Inspector Apply and undo,
explicit text discard and unchanged script bytes on disk. Its final stopped scene
is clean, with no Inspector/file draft or viewport error. Normal close exits 0,
removes the mailbox and leaves stderr empty. Final native inspection confirms
aligned vector axes. Rebinding the computer-use app does not confirm Name-field
click focus; that human-style input check remains open without an asserted cause.

After the final source change, full suites pass Debug 1032/1032 (145.56s), then
Release/NDEBUG 1032/1032 (20.69s). Inventory is 259 unit / 773 integration cases,
including 123 offscreen GPU, 28 SDL-dummy platform and 27 native audio cases
(labels overlap). The earlier full Debug run before the pending-text parity
correction also passes 1032/1032 (142.63s). No automated case opens a desktop
window. Changed C++ sources pass clang-format checks and diff whitespace checks.

Precommit review covers strict schemas/revisions, owner-thread/move checks, owned
plans and failed-preparation lifetime, atomic primary transfers/history, scalar
precision/type preservation, balanced ImGui row/ID ownership, immediate canonical
draft publication and native/agent parity. No compiler/linker environment, dependency
pin or target-platform assumptions change. Temporary native app linkage, request/
observation files and control helpers are removed; ignored build caches/test logs
and existing distributions remain. User planning/design files stay untracked. No
distribution archive is produced. Evidence is arm64 macOS only. Mature editor
workflows, the outstanding simulated text-click check, finished realistic game art
and populated-scene performance remain incomplete.


## 2026-10-16 — Agent gameplay input and native focus

Added a CPU-only PlayInput controller with a strict bounded JSON request. It
publishes the ordinary InputState consumed by PlaySession, scripts and native
simulation for 1..600 native Play frames, followed by a release frame. It does not
advance a separate simulation or edit runtime transforms. Keyboard/button names
move into one immutable platform vocabulary shared with the existing script input
source. Press/hold/release edges compare against the last published frame; cancelling
a queued request does not invent unseen edges. Text publishes once, pointer/wheel
deltas repeat per held frame and stop on release. Invalid input or active replacement
fails before state mutation. Frame preparation returns owned snapshots and preserves
controller state on allocation failure.

The actual editor exposes input-play and cancel-play-input using a Play session
identity, independent of scene revision. New Play sessions get new identities;
stopped/faulted sessions, stale requests and pending close reject injection. Escape
and the GUI Release control button use the same cancellation path. Stop/fault clear
the controller. Native input capture resumes after an agent request when enabled.
Inspection reports actual input source and remaining/published progress. Pointer
capture acknowledgement remains native; submitting deltas alone never claims it.
Window.raise and the strict focus-window operation request foreground activation;
inspection reports actual focus, including deferred/denied requests.

Six new CPU regressions cover held/edge/text/delta lifetime, owned snapshots,
cancellation before/after publication, source handoff, exact frame budgets, canonical
name coverage, malformed/duplicate/reserved inputs and bounds. One headless Play
integration drives real script/native-body motion and verifies stopping on release
without changing authoring. Existing SDL-dummy window checks add raise/focus and
moved/worker rejection coverage. No automated case creates a desktop window.
An initial integration build used a nonexistent velocity getter; the subsequent
assertion confused authored body velocity with live state. The regression now checks
real pose progression and stopping, rather than reading a static configuration field.
Focused Debug passes 30/30 (1.10s), then 42/42 (1.14s) after adding native focus checks.

Temporary mouse sampling isolates the earlier simulated Name-click discrepancy
before Inspector layout: SDL emits boundary positions such as (0,0)/(1279,0) while
its global pointer query stays at the physical screen edge. The pinned Cocoa backend's
macOS 26+ branch uses global pointer coordinates instead of event-local coordinates.
All sampling code is restored byte-for-byte and no platform-coordinate compensation
is added. A symlinked temporary app also fails to obtain native focus; repeating QA
with an owned executable and SDL's standard bundle resources obtains actual focus
and pointer capture. The product packaging already uses that layout. A simulated
Name click remains unconfirmed even in the proper app; it is not counted as a
successful human-style text-edit check. Agent property editing remains independently
verified by the preceding stage.

Actual Release Prism Relay operations verify W movement of 1.62635m over a 30-frame
request (31 publications including release), and a 120-frame Space hold reaches
approximately 1.78433m then lands while still held without jumping again. Cancellation
releases after 7 publications instead of completing the 600-frame budget. Stale
injection/cancel requests are rejected across session identities 2/3/4 at unchanged
authoring revision, and restart restores the original [-3,0,2] spawn without held
input. No OS key injection or IME switch is used for these operations.

Without acknowledged capture, nonzero agent deltas preserve camera orientation.
Product focus-window then obtains actual keyboard focus; capture-input obtains
native relative mode and disables text input. Five input frames rotate the shoulder
camera with quaternion distance .0474281. Capture release and Stop restore the
editor with unchanged authoring, no drafts or viewport error. Recorded runtime
snapshots have zero dropped time, no diagnostics or Play errors. Clean close exits 0
and stderr is empty. These are local arm64 macOS observations, not a general
performance or finished-game-quality claim.

Final focused Release/NDEBUG checks pass 42/42 (.40s). Full arm64 macOS suites pass
Debug 1039/1039 (148.39s), then Release/NDEBUG 1039/1039 (20.12s), with 265 unit and
774 integration cases. Hardware-labelled subsets include 123 offscreen GPU and
27 native audio cases; 28 platform cases use SDL dummy video (labels overlap).
No automated case opens a desktop window. All 11 changed C++ source/interface/test
files pass clang-format checks, and diff whitespace checks pass.

Precommit review covers module direction and interface/implementation separation,
canonical-name lifetime, strict numeric/name/text/schema bounds, transactional input
preparation and owned frame copies, source handoff and queued cancellation edges,
session identity exhaustion/restart, fault/Stop/close cleanup, actual focus/capture
reporting and platform-neutral SDL use. Compiler/linker environment and dependency
pins are unchanged. Temporary diagnostic instrumentation, native app shells, control
scripts and observation/request files are removed. Ignored caches, verification logs
and the existing distribution remain; user design/planning inputs stay untracked.
No release archive is produced. Complete editor operation parity, finished realistic
sci-fi art, complex game authoring and populated-scene performance remain open.

## 2026-10-16 — Actual viewport PNG export

The editor now exposes Save viewport PNG and Save captured PNG through the File
menu and strict capture-viewport/save-capture/browse-capture/discard-capture product
operations. Capture is queued for a new native viewport frame; inspection separates
queued, ready, saved, cancelled and failed states. The actual completed render
target is read after GUI presentation, without rerendering a different scene/view.
Retained pixels exclude editor panels and transform handles. Provenance includes
frame serial, scene scope/revision, editor/game/Play mode, Play identity/frame,
actual pixel dimensions/view-projection matrix and material-preview state.

A bounded CPU capture service owns at most one queued/ready image. Newer matching
frames are required; stale IDs, revisions/scopes/modes/sessions and inconsistent
metadata fail. Frozen pixels survive later rendering and Stop. Successful save,
cancellation and discard release them; failed saves retain them for retry. The
existing direct viewport readback also rejects hidden/failed/stale previews.

PNG export prepares an explicit existing parent and portable UTF-8 filename,
requires deliberate overwrite, rejects links/special/read-only files and ASCII
case aliases, bounds existing files to 64 MiB and scans to 65536 directory entries.
It checks exact byte baselines before encoding and atomic replacement. This is
serialized local editing, without hostile filesystem race isolation or promised
crash durability. File commit precedes capture acknowledgement; a later catalog
refresh failure explicitly reports the committed PNG instead of claiming no write.

The shared SDL dialog module replaces the Home-specific callback header. Callback
results own copied paths/errors, are consumed once and can outlive their initiating
owner without a GUI borrow. Invalid/empty/oversized/non-UTF-8/multiple paths fail.
Save dialogs open after pixels are frozen and pointer capture is released. Conflicting
operations/window close are rejected until native completion; cancellation discards
the capture and native errors retain it for retry. Existing Home folder selection
uses the same callback ownership and preserves its close/release guards.

Eight new lasting regressions add three pure CPU capture cases, one pure CPU dialog
validation case, three filesystem PNG cases and one filesystem capture/save retry
case. The two existing callback lifetime/result tests remain. No new executable,
window/device initialization in unit tests, desktop-window integration test or
visual probe is added. Initial builds found a missing default aggregate member,
a missing standard vector include for the test comparator and an unparenthesized
doctest diagnostic expression; these source/test errors were corrected. Focused
Debug passes 10/10 (.26s); focused Release/NDEBUG passes 10/10 (.06s). Precommit
platform review also makes Unicode test paths explicitly UTF-8 for Windows.

Actual Release Prism Relay product operations save an editor view at 1480x704 and
an over-shoulder Play view. A capture at Play frame 15 remains byte/provenance-stable
while 30 real W input frames move the character from [-3,0,2] to approximately
[-3,0,1.27846]. A later capture at frame 84 has different pixels and camera matrix.
Saving a frozen Play capture after Stop also succeeds with the original provenance.
Existing-file overwrite refusal preserves both file and ready image; explicit retry
succeeds. Unknown fields, stale revisions and saved/stale capture IDs are rejected.
Native SDL Save is observed and operated through the system accessibility controls:
cancel returns cancelled state, a subsequent save creates native-save.png with the
reported dimensions and bytes. Native-selected and direct editor-view PNGs are
byte-identical. Close/save/discard conflicts are rejected while the dialog is open.

A deliberate unsaved missing-mesh edit makes the viewport unavailable; capture then
reports failed and creates no PNG. Undo restores rendering and a clean authoring
state. Docking resizes the target to 872x704; PNG headers and reported dimensions
match. Layout reset and clean close succeed, followed by a separate actual Home
folder-dialog open/cancel and clean close. Both native runs exit 0, stderr is empty,
and their control directories are removed. Temporary native app shells, clients and
request files are removed; observations remain only in ignored build storage.

The user identified Chinese IME interception as the reason a prior single native W
keypress did not produce confirmable movement. That observation is not evidence of
an engine movement defect; the actual agent-input checks above bypass OS text entry.
This correction does not establish a successful human-style text-field edit or resolve
the independently observed virtual-pointer coordinate issue.

本轮交付实际视口 PNG 保存、固定画面与来源信息、系统对话框及同源智能体操作。
这些工作改善真实项目的视觉检查流程；观察画面仍是原型场景，不代表写实科幻美术已达标。
完整编辑器操作覆盖、成熟内容制作流程、完成度更高的示例和复杂场景性能仍需继续推进。

Full arm64 macOS Debug passes 1047/1047 (144.37s). After the Unicode-path review and
interface contract clarification, both configurations rebuild successfully and the
focused final Debug checks pass 10/10 (.28s). Final Release/NDEBUG passes the full
1047/1047 (16.96s). There are 269 unit and 778 integration cases; hardware-labelled
subsets include 123 offscreen GPU and 27 native audio cases, with 28 SDL-dummy platform
cases (labels overlap). Automated checks open no desktop windows. All 12 changed C++
source/interface/test files pass clang-format and diff whitespace checks pass.

Precommit review checks module direction and interface/body separation, borrowed
readback lifetime versus owned snapshots, monotonic IDs/context freshness, bounded
paths/files/images and strict command schemas, atomic-save failure/retry behavior,
callback ownership/main-thread submission and close guards, exact PNG row/alpha
preservation and UTF-8 Windows path construction. Compiler/linker settings, dependency
pins and platform targets remain unchanged. Windows/Linux have not been executed.
No distribution archive is generated. The broader engine goal remains incomplete.

## 2026-10-16 — Isolated native animation audition

The editor now auditions saved animation players without starting a game. The
Inspector launcher clones the current unsaved scene and the Scene panel offers
native clip playback/pause, time scrubbing, speed/looping, bind pose, model framing
and End preview. Strict preview-animation, control-animation-preview and
inspect-animation-preview operations share the GUI implementation. Session identities
change independently of document revisions; old/ended sessions cannot control newer
previews. Inspection returns actual playback metadata and the posed clone scene.

The AnimationPreview service owns its scene, native ScenePlayers and immutable
loaded clips. All cloned auto-play flags are disabled, and only the selected player
is controlled. Other players remain at authored poses while native writer/library
and rigid/character authority checks still apply. No compiler, scripts, simulation,
audio or hidden timer is initialized. Control failures preserve pose/clock/generation.
Native sample failure retains the last valid pose and faults audition until recreation;
End preview remains available. Moves retain borrowed scene addresses; worker-thread
and moved-handle operations are rejected. Source scenes and animation files can be
retired after preparation without losing owned poses/clip samples.

Starting preview requires an enabled player, current revision, no conflicting mode,
pending draft, close confirmation or native save dialog. Authoring/resource writes,
selection, scene save/export and Play are locked during audition. Camera navigation,
framing, layout/theme, inspection, PNG capture and close remain available. End/close
retire the clone without changing authoring/history/selection/revision/dirty state.
Bind pose is separately observable from a paused clip at time zero; playing from it
restarts the directional clip. Seeking preserves playing/paused state.

The displayed-scene source is shared by rendering, capture and focus. Model framing
now merges actual visible mesh bounds in a selected subtree, including posed skins,
instead of framing only a skeleton/group root's default cube. Empty groups retain
their own bounds. Frame model uses the existing revision-checked focus-view operation.
The capture service records audition source/session and entity/generation/clip/time/
bindPose metadata with frozen pixels. Stale generations cannot be read as current;
frozen frames remain saveable after End preview and later game execution.

Five new enduring filesystem regressions cover native seeking/playback/reverse/bind
pose, invalid inputs preserving state, thread/move lifetime, retired source/files,
other players retaining authored poses, missing/disabled/conflicting inputs, physics
ownership and native singular-sample fault/recreation. One new pure CPU capture case
covers animation metadata/context validation and ownership. No new test executable,
window/device unit initialization or desktop-window integration test is added.
An initial fixture build found a signed vector index; it is corrected to size_t.
Focused Debug passes 13/13 (.65s); focused Release/NDEBUG passes 13/13 (.08s).

The first native UI observation found an oversized control child and a distant
character. The child now fits real content under a bounded height and buttons wrap;
current native clip names are shown. A separate final Release editor session selects
Surveyor in Prism Relay and starts paused at Idle_Loop time zero. Real subtree bounds
frame its meshes. All seven authored idle/walk/jog/sprint/jump-start/airborne/landing
clips switch, pause and seek successfully. Walk time .5000000149011612 remains fixed
across observation frames; saved PNGs show real deformation and carry exact clip/time
metadata. Idle/walk pixels differ. Bind-pose exports identify that state explicitly.
Reverse nonlooping playback reaches zero and pauses. No script/game session starts;
the character root remains at its authored position.

Actual product requests reject invalid times/indices/extra fields, stale sessions,
concurrent Play and authoring/selection changes without altering the paused generation.
Repeated audition uses distinct session identities 1/2/3 at authoring revision 2.
Native screenshots show the actual controls at normal and narrowed widths; narrow
export is 570x592, while ordinary audition export is 1480x650. Frame metadata and PNG
sizes match. A frozen capture saves after End preview with unchanged provenance.
Subsequent actual Play starts normally, consumes ten W frames and moves the character
to approximately [-3,0,1.55204]; Stop restores exactly the pre-preview authoring scene,
revision and clean state. Close while a third audition is active exits 0 with empty
stderr. Temporary app shells, clients and requests are removed, control directories
are retired, and observations remain only in ignored build storage.

Precommit error-path review adds an explicit expected-result check before reading
preview stats in the render loop. This avoids assuming a potentially allocating
snapshot succeeded. No dependency pins, compiler/linker settings or platform targets
change. Keyframe/skeleton authoring, visual transition/state-machine tools, finished
realistic game art and populated-game performance remain unfinished.

本轮交付独立动画预览及真实模型子树取景，支持同源 GUI/智能体操作和带来源的 PNG。
已经验证真实人物蒙皮与运行隔离；没有将动作预览描述为完整动画制作器，也没有据测试数量
宣称大型游戏生产就绪。自动化检查不打开桌面窗口，实际窗口检查使用产品操作与视觉观察。

Full arm64 macOS Debug passes 1053/1053 (147.36s). After final expected-result and
member-naming review, both configurations rebuild, focused final Debug passes
13/13 (.62s), and final Release/NDEBUG passes 1053/1053 (20.08s). The suite has 270
unit and 783 integration cases; labelled subsets include 123 offscreen GPU, 27 native
audio and 28 SDL-dummy platform cases (labels overlap). Automated checks open no
desktop windows. All seven changed C++ source/interface/test files pass clang-format;
diff whitespace checks pass. No new distribution archive is produced.

Precommit review covers module interface/implementation separation and dependency
direction, source/clone and clip ownership, thread/move/resource lifetime, runtime
writer authority, strict bounded command fields and session/generation freshness,
failed-operation preservation and native fault recovery, read-only authoring guards,
GUI/agent parity, subtree skin bounds and frame metadata, portable standard-library
code, formatting and focused behavioral coverage. Windows/Linux remain supported in
code without local execution claims. The complete engine goal remains open.

## 2026-10-16 — Current-frame overlay and skeleton data

The graphics module now provides bounded display-color viewport lines/points and a
pure CPU homogeneous projector. Projection validates an invertible finite matrix,
clips all six frustum planes plus positive W before division and retains visible
segments with offscreen/near-plane-crossing endpoints. Invalid inputs return errors;
valid outside geometry returns explicit absence. Overlay UVs, colors, logical sizes
and per-vector 4096-element bounds are validated.

GuiViewportDraw now returns GuiViewportFrame with its owned target and optional
geometry. The clean current API replaces the old target-only result at all consumers.
After actual viewport rendering, the GUI backend builds an owned clipped draw list
at the source window's stacking layer. Borders, DPI and window clipping are respected;
foreground windows still cover overlays. Total GUI budgets are checked before draw
list publication. Failed callbacks/geometry/budgets do not replay side effects or
publish a successful GUI frame. Lists are retired while their owning ImGui context
still exists. Scene targets and PNG/HDR readbacks remain untouched.

The new pure CPU skeleton sampler uses the actual enabled mesh skin, reuses native
SceneSkin count/palette validation and owns joint names/world positions. Palette
ordering is preserved while parents resolve to the nearest bound real ancestor;
unbound helper distances and multiple roots remain explicit. It never invents edges
from index order or mesh origin, initializes devices or writes scene state.

Eight new unit cases cover clipping, perspective near-plane intersections, invalid
geometry/bounds, owned snapshots, reordered joints, real hierarchy updates/helpers,
multiple roots, source lifetime, disabled/missing/foreign/count-mismatched inputs and
thread/move rules. Two enduring offscreen GUI cases cover borders/DPI, current-frame
pixels, window order, clean scene readback, maximum overlay geometry above 65535
vertices, budget/invalid-input recovery and lifetime. Existing viewport tests retain
their owner/size/absence/failure coverage under the new result type.

Initial builds identify signed geometry-count arithmetic, a missing aspect argument
in a test and a partially designated GuiConfig fixture; these are corrected. A local
text-edit search also misses a formatter-wrapped test name and is corrected before
rebuilding. Full Debug and Release builds succeed. Focused Debug unit checks pass
8/8 (.08s); final Debug GUI checks pass 14/14 (7.46s). Release results are recorded below.
No automated case opens a desktop window. The actual skeleton panel/control workflow
remains the next implementation, rather than being claimed from these foundations.

Release/NDEBUG unit checks pass 8/8 (.05s) and all GUI offscreen checks pass 14/14
(.65s). All twelve changed C++ source/interface/private-header/test files pass
clang-format; whitespace checks pass. Review covers module direction, homogeneous
clipping and UV bounds, skin/scoped-entity validation, owned snapshots, ImGui context/
font/list lifetime, source-window stacking, geometry budgets and vertex-offset use,
callback non-replay, retained targets and unchanged scene pixels. Compiler/linker
settings and dependency pins are unchanged. Only arm64 macOS is executed. No new
archive or desktop-window test is produced. Product bone inspection follows next.

## 2026-10-16 — Live skeleton inspection in the editor

The product now includes a dockable Skeleton panel, enabled mesh-skin choice/Off,
filtered palette-ordered joint list, world/parent/link information, x-ray viewport
points/bones, selected point/incoming-bone highlighting and whole/joint focus. Edit
selected joint reuses real document selection and existing Inspector/undo tools;
it does not create animation keyframes or alter inverse binds/weights. Animation
preview and Play remain isolated from authoring. The viewer follows the requested
entity value into the currently displayed scope, validates native bindings and
samples only after that exact scene frame renders. PNG/HDR remain clean scene data.

GUI and strict product requests share implementation: set/inspect skeleton, select
joint, focus and normalized viewport picking, plus existing filter/layout operations.
Scene-scoped requests reject stale source/preview/Play controls. A pick means clicking
the latest displayed position now; its response identifies the actual processed frame,
rather than requiring a serial that ordinary repainting would immediately expire.
Current scene/view/revision readiness protects stale geometry; no hit preserves the
selection. Snapshot inspection publishes geometry only for the current completed
frame. The GUI explicitly labels its last completed pose while animation advances.

The CPU/file-only SkeletonView owns loaded immutable skin data and frame snapshots;
root/path-keyed caches invalidate on normal resource refresh. Invalid controls preserve
settings; render failure retires stale geometry with observable retryable errors.
Disabled inspection has no per-frame asset loading/sampling. Five focused filesystem
integration cases cover live pose/projection/highlighting/focus, no source writes,
invalid controls, native failure/recovery, cache/root/invalidation, source/clone binding,
obsolete selection, owned snapshots and thread restrictions. They open no devices or
windows. Existing pure projection/skeleton and durable offscreen GUI cases remain.

Initial implementation builds identify missing standard filesystem/thread includes;
these are added to the implementation global fragment. Initial fixtures use a skin
path outside the required assets prefix and fail before assertions; corrected fixtures
create/use assets/skeleton.dotskin. Final Debug focused service checks pass 5/5 (.41s),
Release/NDEBUG 5/5 (.05s). Full arm64 macOS Debug passes 1068/1068 (154.23s), followed
by a final full rebuild and 14/14 affected checks (1.80s) after pick protocol review.
Final full Release/NDEBUG passes 1068/1068 (18.13s). The suite contains 278 unit and
790 integration cases; overlapping subsets include 125 offscreen GPU, 27 native audio
and 28 SDL-dummy platform cases. Automatic tests open no desktop windows.

Actual product validation opens Prism Relay through its native editor app and uses
its normal mailbox. The real Surveyor mesh 283 exposes 65 joints, 64 projected bones
and one actual root. Head selection/highlighting, normalized position picking, search,
whole/joint camera focus, bad/missing target, foreign joint, invalid UV, unknown fields
and stale-scene rejection are observed. Switching overlay on/off produces byte-identical
1480x704 scene PNGs without authoring revision/dirty/scene changes. Editing the real
head transform changes live world coordinates; ordinary Undo restores exact prior
coordinates and clean scene data. Native screenshot observation confirms actual model/
bone alignment, yellow selected bone, normal and 151-logical-pixel narrow docked panel,
wrapped controls/labels, scrollable joint list and dark/light themes. No unconfirmed
simulated mouse click is counted as product acceptance.

The first native run uses a generated build example while an independent full build
regenerates that directory, removing its live mailbox. The product reports the real
missing-root I/O error and exits; this validation-process interference is not hidden
or described as a clean close. After confirming termination, a separate ignored example
copy removes the shared-directory conflict. In that run source scope 2, animation scope
4 and Play scope 5 remain distinct. Idle versus Walk at .4s changes real joint positions;
old scoped controls fail. Twenty agent W frames move the skeleton root from [-3,0,2]
to approximately [-3,0,.99208], consistent with actual character movement. Stop restores
the unchanged source scene/revision/clean state. The earlier human W observation was
Chinese IME interception and is not evidence of a movement defect.

Precommit review covers module separation/direction, ownership and thread lifetime,
strict fields/scoped identities, cache invalidation and failure recovery, current-frame
projection and GUI-only geometry, native binding/nearest parent topology, read-only
source/clone authority, edit/undo reuse, camera guards, logical DPI hit radius, portable
code and focused coverage. No dependencies/compiler/linker environment/target platforms
change; Windows/Linux are not executed. Production animation/skeleton authoring, state
and layering tools, finished realistic sci-fi art, streaming and populated-game performance
remain open. This is one integrated capability, not completion of the engine goal.

The independent native run closes normally with launcher exit 0, no final-run error,
empty stderr and retired control directory. Temporary application shell, example copy,
requests and investigation clients are removed. Observations and PNGs remain only in
ignored build storage; build caches and existing archives are preserved. All four
changed C++ interface/implementation/test files pass clang-format; diff whitespace
checks pass. No distribution package is generated. The unrelated local specification
and visual-reference output remain untracked and are not included in commits.

## 2026-10-16 — Animation edit transactions and drafts

A native pure CPU edit layer now applies <=256 ordered key/channel/name operations
by logical node/path and exact float time. Insert/replacement preserves key ordering;
removal and explicit interpolation conversion have defined tangent behavior. The final
immutable Clip validates once before publication, allowing complete channel replacement
or creation of two-key cubic tracks without exposing invalid intermediate data. Errors
preserve the source. No scene, filesystem or device is used by these unit contracts.

AnimationDocument owns a typed clip resource draft, cheap immutable snapshots, exact
source-byte history, current resource revision and saved baseline. It reuses existing
resource path/metadata/conflict and atomic-write checks. Semantic no-ops preserve
revision/redo; invalid revisions including zero fail. Save retains history, failed saves
retain the draft and destruction never writes. History is bounded by both state count
and serialized bytes (defaults 128 states/128 MiB), evicting oldest entries. Oversized
individual states fail unchanged. Counters explicitly exclude shared clip/current text/
disk-baseline storage. Candidate history values can be inspected before a same-revision
preview/undo transaction. Ordinary resource text remains 1 MiB by default; explicit
native-data consumers may request up to the animation codec's 64 MiB maximum. This
addresses real imported clips already larger than the ordinary default.

ScenePlayers now owns optional immutable decoded asset handles keyed by exact portable
library paths. Duplicate, invalid or initially unused entries and >1024 assets fail.
All native binding, samples, aggregate budgets and writer/physics authority still apply;
other libraries still load actual disk data. AnimationPreview can select and own a draft
library entry only in its clone, reporting its draft asset, without temporary files or
source writes. The ordinary native playback/seek/switch path evaluates that data after
the original asset is removed. Failed candidates preserve an existing preview.

Four new pure CPU edit cases cover sorted upsert, exact times, sampling changes,
explicit step/linear/cubic conversion/tangents, ordered atomic removal/replacement,
invalid native values/modes/time/handles and batch bounds. Six file-only document cases
cover edit/save/undo/redo/branch, semantic no-ops and redo retention, exact source bytes,
revision/conflict preservation, state/byte eviction and oversized rejection, >1 MiB
resources with unchanged default capacity, path/move/thread/lifetime rules and no save
on destruction. Three added preview/decoded-library cases cover real draft samples,
source/disk isolation, missing overridden files, ownership, invalid/unused/duplicate
entries, bad targets and unchanged physics authority. No automated case opens a desktop
window and no visual acceptance executable is added.

Initial builds identify missing direct module imports in tests/the decoded-asset
interface and a nested-comment token in Doxygen path notation; corrected before tests.
Focused Debug checks pass 18/18 (.94s). Final full arm64 macOS Debug passes 1081/1081
(150.77s), Release/NDEBUG passes 1081/1081 (18.61s). There are 282 unit and 799 integration
cases; overlapping subsets include 125 offscreen GPU, 27 native audio and 28 SDL-dummy
platform cases. All thirteen changed C++ source/interface/test files pass clang-format;
diff whitespace checks pass. No compiler/linker settings, dependency pins or supported
platforms change; Windows/Linux are not executed. No distribution package is generated.

Review covers module direction/interface separation, immutable ownership and thread/move
lifetime, final-state edit validation, history bounds and revision/baseline synchronization,
source-byte undo and no-op preservation, expected failures, resource path/conflict guards,
native decoded-library authority, unchanged default callers and meaningful device-free
behavioral coverage. This stage supplies native editing/save/preview foundations; the
actual timeline GUI and strict product editor operations remain the next integration.
Production keyframe authoring is not yet claimed. The complete engine goal remains open.

## 2026-10-16 — GUI command inspection snapshots

Actual editor use exposes a post-command inspection error: GUI-originated actions
return the same inspection response as agent actions, but guiStats incorrectly
rejects a read inside its own GUI callback as rendering reentry. A selection may
already have committed before the frontend then reports bad expected access.
Renderer::guiStats now uses the same owner-thread/device checks with read-only GUI
access as visibilityStats. It copies already assembled state, never issues GPU work,
completes/submits a frame or replays widget effects. Frame count remains successful
submissions; geometry/input can describe an assembled yet unsubmitted build.

The existing durable offscreen callback-recovery case now requires a valid enabled
snapshot with zero submitted frames inside its callback, while still rejecting
closeGui, nested renderGui and createTarget. The new assertion reproduces the failure
before the fix (1/1 fails, .06s). All 14 offscreen GUI cases then pass Release/NDEBUG
(0.90s). Combined affected Debug animation/GUI checks pass 33/33 (8.84s), including
all 14 GUI cases. Existing wrong-thread, budget, ownership, stack-recovery and
non-replay checks remain. Automatic checks create no desktop window.

Precommit review verifies that only this read gains GUI-callback access, preserving
thread/device checks and every rendering/mutation/shutdown guard. Snapshot copying
has no borrowed GUI pointers or ImGui-context mutation. Public contracts and GUI
documentation state precisely what assembled versus submitted data means. Formatting
and whitespace checks pass for the two renderer files and GUI test. Only arm64 macOS
is executed, with no dependency/compiler/linker or platform-target changes. Animation
editor workflow verification continues separately; test counts do not prove its UI.


## 2026-10-16 — Integrated animation clip editing

The editor now opens an existing enabled player's library clip as a typed animation
resource document, with an isolated native draft preview. The docked Animation panel
provides named tracks, clipped key lists, time scrubbing, save/history, playback options
and explicit close/discard. Inspector edits local key time/value and cubic tangents,
prepares a key from a real bound node's current pose and supports channel interpolation/
removal. Component rows retain readable float precision in narrow panels; selected
tracks/keys scroll into view. Existing File/Edit menus and save/history shortcuts route
to the animation document. Visual binding, curve/dope-sheet and state/layer tooling are
still unfinished; this workflow edits existing bound library assets.

The GUI calls the same strict product operations exposed to agents. Queries use bounded
pages (1..256); mutations require observed buffer/resource revision and exact shapes.
Source document revision/history are independent. Staged forms canonicalize float32
values and require explicit apply/reset before selection/history/save. Invalid candidates
retain the form, old draft/history/preview/clock/session and disk. Native clip edits and
undo/redo preflight a new isolated scene against actual bindings, writer/physics authority
and enabled skins at both initial and retained-current samples before committing history.
Successful edits retain speed/looping/playing and clamp time to the new duration; new
scene/session identities invalidate old controls. Semantic no-ops retain session/revision
without asset reload. Saving reuses resource baseline/conflict checks and reports any
post-save catalog failure as already saved; destruction never saves.

Actual product verification uses an independent ignored copy of PrismRelay and the real
editor CLI/JSON mailbox. Its 1,492,781-byte Idle asset, 65-joint character, 195 channels
and 14,820 keys open without changing source or disk. Editing Head translation changes
the native cloned pose; undo/redo restores the actual poses, save/reopen reads changed
bytes, and later Play uses the saved asset. Fifteen product agent W-input frames produce
measured character displacement; Stop preserves the authoring scene. The earlier physical
W-input observation was Chinese IME interception, not evidence of a movement failure.
Bad page limits/offsets, stale buffers/revisions/sessions, unbound nodes, singular scales,
dirty-close/end, unapplied forms and external-file conflicts preserve the appropriate
state. Exact .0125 input round-trips to its native float32 key identity. Key insertion/
removal, cubic conversion/tangents, no-op edits, CJK names, reverse/non-looping clock and
playing-state retention are exercised. Frozen viewport capture survives later close;
1480x906 dark/light captures retain identical scene pixels and independent GUI themes.
Computer-use observations check named track scrolling, readable component/tangent rows
and the actual unsaved modal. An unchanged computer-use Save-and-close click is not
counted as successful interaction; product operations provide the verified close path.

Verification reveals and corrects several real issues. Initial compilation catches a
missing direct ClipAsset import and a shadowed label. An investigation-client assertion
incorrectly compares a Python double to float32 exactly; corrected with native precision.
A finite legal clip can exceed supported skin matrix range: initially accepted, now
rejected before publishing initial/current preview poses. A durable file-only regression
uses an actual CPU skin binding and verifies prior preview/source preservation, without
creating a window or rendering device. GUI callback inspection is fixed and tested in
the separate preceding commit. Animation-mode guards initially block cancel/save-close;
now Cancel retains the draft/history/form and conflicts remain retryable in the modal.

The first save-close attempt saves data but then mistakenly calls the Play-report helper
instead of document refresh, so exit fails in a never-Play session. This is corrected to
refresh the authoring snapshot; the helper is renamed rememberPlayFrame and explicitly
rejects absent Play state. Final actual verification starts without Play, dirties a real
floor transform and the animation resource, then uses the shared save-close operation.
The animation name persists and the saved scene exactly equals the intended authoring
snapshot, proving that posed preview transforms were not baked. The control directory
retires and computer-use inventory confirms the application is no longer running.
The final launcher's exit code is not observed; it is not claimed. Stderr contains one
macOS IMKCFRunLoopWakeUpReliable mach-port diagnostic, with no product error observed.
The failed earlier save-close receipt and successful final disk assertions are retained
only in ignored build observations.

Final current-source arm64 macOS Debug passes 1082/1082 (151.53s), Release/NDEBUG passes
1082/1082 (22.20s). There are 282 unit and 800 integration cases; overlapping subsets
include 125 offscreen GPU, 27 native audio and 28 SDL-dummy platform cases. No automated
check opens a desktop window and no dedicated visual probe executable is added. The
four changed C++ files pass clang-format and diff whitespace checks pass. Review covers
ownership/snapshot lifetime, module direction, preflight-before-history publication,
revision/no-op/conflict behavior, staged forms, source/preview isolation, pending-close
recovery and shared agent/GUI paths. No dependencies, compiler/linker environment or
supported platform targets change; Windows/Linux are not executed.

The temporary application shell, copied project and investigation clients are removed;
ignored observations/PNGs, build caches and existing archives remain. No distribution
package is generated. The local specification and reference output remain untracked.
Production animation/skeleton binding, curves, state/layers and scalability, finished
realistic sci-fi art, streaming and populated-game performance remain open. Passing
checks establish these contracts, not completion or production readiness of the engine.


## 2026-10-16 — Preserve native button coordinates

Native GUI investigation reveals that normalized button events discard SDL's event-local
client coordinates. A click/release without an independent preceding motion event can
therefore consume the previous pointer position. ButtonChanged now owns optional logical
XY coordinates: actual SDL button events provide them, semantic button-only input may
omit them. InputState updates absolute pointer position before the edge without inventing
relative motion. Nonfinite coordinates reject the whole event before changing button or
pointer state. Finite coordinates outside the window remain valid during capture.
The GUI forwards the same validated position before its button edge in both native and
offscreen contexts, retaining native SDL event coordinates for the vendor backend.

The existing SDL-dummy event translation case now injects motion at (12,23) followed by
a button event at (85,37), then consumes the normalized events through InputState. It
reproduces the bug before the fix: both position assertions fail (1/1, .03s), while the
button edge itself succeeds. The added pure CPU input case covers press/release position,
no invented delta, foreign-window rejection, invalid-coordinate atomicity and omitted
coordinates. The existing offscreen GUI input case moves outside a button, then clicks
inside using only button-local coordinates and observes its actual callback; invalid
coordinates remain rejected. These focused Debug checks pass 3/3 (.69s). No automated
check creates a desktop window.

Release/NDEBUG full arm64 macOS checks pass 1089/1089 (18.52s), including the concurrently
prepared animation curve work. The suite has 288 unit and 801 integration cases; overlapping
subsets include 125 offscreen GPU, 27 native audio and 28 SDL-dummy platform cases. Native
computer-use click/drag behavior is still inconsistent in the actual editor; this change
fixes the independently reproduced event conversion contract, not a claim that it is the
sole cause or that native curve dragging has passed acceptance. Product animation work
continues through strict real editor operations. Windows/Linux are not executed.

Precommit review checks owned optional coordinates, unchanged semantic button-only input,
window scoping, finite validation before mutation, native/offscreen forwarding order,
pointer-versus-delta semantics and focused device-free/dummy/offscreen regression scopes.
No dependency/compiler/linker setting or platform target changes. Formatting and whitespace
checks pass; the unrelated local specification and visual-reference output stay untracked.


## 2026-10-16 — Animation curves and key retiming

Native UpdateKey replaces an existing exact-time key in sorted order using a complete
new time/value/tangent payload. It never inserts a duplicate or overwrites an occupied
destination; invalid/missing sources or final native values preserve the original clip.
Channel interpolation is retained and authored per-second tangents are not automatically
rescaled. The same operation travels through AnimationDocument revision/history/save
and the actual editor's strict batched edit and selected-key form operations.
Clip::sampleTrack evaluates only one immutable-description track using the ordinary
native evaluator, without allocating samples for unrelated channels. It returns owned
values with the same clamps, step/linear/cubic behavior, shortest-arc rotation,
normalization and sampling failures as the complete clip sampler.

The Animation panel adds native sampled XYZ/XYZW curves, authored key markers, value
range, playhead, hover sample, key selection and click-to-seek. Time range control has
strict machine-readable parity and does not alter resource history, preview clock or
key forms. Bounded queries accept 2..1024 samples and 0..86400s with a >=1us span;
failures return no partial/fake-success curve. The UI caches 256 samples by buffer,
resource revision, channel and time range, and bounds visible markers to 2048 keys.
Step discontinuities use actual key times; other lines are sampled approximations,
not proof of extrema or all valid poses. Numerical tangent editing remains in Inspector.

Inspector explicitly distinguishes insertion/replacement from updating/moving the
selected source key. The latter keeps key count and rejects destination collisions.
Horizontal dragging has a ghost time and one revision-checked update on release;
Escape, focus loss, inactive/stale input, changed channel/view/resource cancel it.
Final release displacement is included even when no earlier held frame reports a drag.
This is a code-level correction from the ImGui held-button contract, not proof that
native computer-use drag input now works. Current native drag acceptance remains open.

Three added pure CPU edit cases cover sorting/retiming without copies, same-time updates,
changed sample/duration, collision/missing/invalid times and values, atomic batch failure
and retained cubic tangents. Two single-track cases compare exact full-sampler results
across step/linear/cubic/rotation and endpoint times; they check independent channel
failure, invalid indices/times, move/owned-result lifetime and concurrent reads. One
file-only document case verifies count/history, failed-collision revision/byte retention,
undo/redo and real save/reload. No new visual acceptance executable or automated desktop
window is added. The separately committed native input fix adds its own pure CPU case
and extends existing SDL-dummy/offscreen checks.

Actual product verification uses a copied PrismRelay through the real editor's strict
mailbox, with its 65-joint character, 195 channels and 14,820 keys. Curve samples match
actual cloned Head poses at six times for linear, rotation, moved-key, cubic and step
workflows. Updating the first key to .0125 changes its value/time without adding a key;
undo/redo restores exact times. Occupied destination, stale revision, malformed/bad
query bounds, tiny spans and mismatched staged interpolation preserve state. Range
changes retain resource revision/form and native preview identity/time. A cubic scale
with valid endpoints but a singular midpoint produces a contextual curve-query error
without changing the document/preview. An earlier negative-scale candidate is rejected
by the existing reflected-skin primitive constraint; it is not counted as a successful
query-error setup. Saving/reopening observes exact moved times in actual bytes. The final
same-build workflow saves/reopens .02250000089406967 and retains all 14,820 keys, with
unchanged authoring scene/revision/dirty state.

Computer use verifies curve/key selection, time-fit interaction, the CJK clip name,
readable values/tangents and dark/light rendering. An earlier Inspector button update
is observed in the document/history, but later native click attempts are inconsistent,
including an unexpected close/discard result; those later attempts are not counted as
successful GUI updates. Drag attempts also produce no confirmed movement in the ordinary
time slider, so neither successful curve dragging nor its root cause is claimed.
Investigation independently reproduces dropped native button coordinates, fixed in the
preceding commit with durable tests. The remaining native pointer acceptance gap is
explicitly retained in the editor documentation/roadmap. No synthetic test-only GUI
control path or vendor input workaround is introduced to declare success.

Actual screenshots reveal that the initial extra control rows hide the curve at default
dock height. Toolbars are compacted, resource identity/status moves to Inspector and the
default lower dock grows from one quarter to about one third. The final default layout
shows the graph and named key lists. Light-theme curve colors are darkened after visual
inspection; native geometry remains unchanged. Bounds/flat-value fitting avoid division
by tiny time spans or loss of range at large float values. Graph handles, multi-key/
dope-sheet operations, visual bindings, state/layers and populated-animation scalability
remain unfinished, as do realistic sci-fi art and the wider engine goal.

Initial restricted full runs fail to acquire native GPU/audio resources; one parallel
Debug script run also exceeds its deadline. Normal host runs retain the checks and pass.
Final arm64 macOS Debug passes 1089/1089 (154.30s); Release/NDEBUG passes 1089/1089 (18.52s).
There are 288 unit and 801 integration cases, with overlapping 125 offscreen GPU,
27 native audio and 28 SDL-dummy platform subsets. Earlier focused curve/edit checks
pass 10/10 (.28s); final independent native-coordinate checks pass 3/3 (.69s). Tests
establish these contracts and do not establish a production-ready engine or accepted
native dragging. Windows/Linux are not executed; dependencies/compiler/linker settings
and target platforms are unchanged.

Review covers module interface/implementation separation, immutable snapshot ownership,
exact-time collision handling, final candidate validation before history publication,
native sampler reuse, bounded queries/cache/geometry, staged form semantics, stale drag
cancellation, resource/source isolation and honest native GUI acceptance limits. All eight
changed animation/editor/test C++ files pass clang-format; whitespace checks pass. The
final real editor session exits with launcher status 0, empty stderr and retired control
directory; native app inventory confirms it is stopped. Temporary app shell, project
copy, requests, clients and insertion snippet are removed. Ignored observations and build
caches remain; no distribution archive is generated. Local specification/reference output
remain untracked and outside commits. The full engine goal remains open.


## 2026-10-16 — Product pointer input diagnostics

GuiStats now owns a processed-input snapshot: a per-context NewFrame counter, logical
size, framebuffer scale, optional logical pointer and five held-button bits. It is
copied before invoking drawing callbacks, including those that subsequently fail.
Successful submission count and assembled geometry/capture flags retain their separate
meanings. Snapshots have no GUI pointers, survive context retirement and reset in a
new/disabled context. Counter exhaustion rejects a new frame instead of wrapping.

The real editor's ordinary inspect exposes workspace.input alongside existing focus,
view and layout state. It reads current SDL client sizes, last consumed application
pointer/delta/named button state and the latest GUI input snapshot. Exactly one owned
native press, release and motion event is retained for the application's own window,
with timestamp/window/position metadata; no keyboard/text log or unbounded event trace
is added. Absent events/unknown GUI positions are null, and invalid event coordinates
are explicitly marked rather than serialized as plausible positions. Native and GUI
samples may differ in age; reading does not poll/inject pointer input, advance a GUI
frame or mutate authored resources. Existing frame edges/deltas still reset normally.

A durable offscreen GUI case reads the snapshot inside a callback that deliberately
fails: native button-local position (32,24), scale 2, logical size 80x60 and held left
button are visible before drawing, while submission count remains zero. A release at
(40,30) and a succeeding frame advance the processed/submitted counters independently.
The old snapshot remains unchanged after new input and closeGui, which resets current
stats. An initial test call incorrectly assumes an anisotropic public renderGui scale;
it is corrected to the existing uniform-scale API, without expanding that API.

Actual native diagnosis runs the real editor on macOS 27.0.1 with the pinned SDL 3.4.18.
The window reports 1280x800 logical / 2560x1600 physical client pixels and GUI scale 2.
With actual window focus confirmed, a computer-use drag requesting screenshot positions
(1040,282)->(1140,282) delivers press and release at the same SDL position
(435.58984375,577.01171875); the last motion timestamp predates the press. GUI position
is (435,577), consistent with ImGui flooring, so the requested displacement is already
absent before editor hit-testing. Per-gesture timestamps establish this; positions can
change between separate observations when the physical system pointer moves.

The pinned dependency's compiled Cocoa source explains the observed virtual-input
limitation: on macOS 26 and later, mouseMoved substitutes SDL_GetGlobalMouseState for
the event-local location as an operating-system workaround. Foreground button delivery
uses that tracked mouse state. Virtual pointer events can therefore disagree with the
physical global position even when focus is correct. This is evidence for the current
computer-use/SDL boundary, not acceptance of a physical mouse drag or proof that every
native interaction works. No vendor patch, global-poll bypass, fake success or alternate
test-only GUI injection is introduced. Product agent operations remain the primary
control/verification path; physical pointer acceptance stays explicitly outstanding.

The final same-build editor run verifies input snapshots during actual PrismRelay
animation editing: open the native library, move Head's first key to .0125 and change
its value through the shared product operation, observe the actual isolated native pose,
then undo and close the resource. Input counters progress while source scene, authoring
revision/dirty state and all nine on-disk scene/animation files remain unchanged. Null
GUI pointer and absent last events are observable without fabricated values. The
application exits with launcher status 0, empty stderr and a retired control directory.
Temporary app shell, copied project and investigation clients are removed; observations
remain only in ignored build storage, with build caches/existing archives preserved.

Release/NDEBUG full checks pass 1090/1090 (17.74s). Focused Debug input/platform/GUI checks
pass 33/33 (9.40s): 17 unit cases, one SDL-dummy platform case and all 15 offscreen GUI
cases. The complete suite has 288 unit and 802 integration cases, with overlapping
126 offscreen GPU, 27 native audio and 28 SDL-dummy platform subsets. After strengthening
the snapshot guard, its final Release case passes 1/1 (.12s) and Debug passes 1/1 (.68s).
No added automatic check opens a desktop window. Windows/Linux are not executed; compiler/linker environment,
dependency pins and supported platform targets are unchanged. No archive is generated.

Precommit review covers copied-value lifetime, bounded event retention and window scope,
invalid/unknown positions, context/frame counter semantics, read-only callback access,
expected query errors, logical/physical coordinate contracts, documentation and useful
failure/recovery coverage. All four changed C++ interface/implementation/test files pass
clang-format and diff whitespace checks pass. Workspace documentation also corrects stale
bottom-dock proportions and obsolete planned-gizmo/preview wording to match current
behavior. The specification/reference output remain untracked. Native physical-pointer
acceptance and the wider production engine/game requirements remain open.


## 2026-10-16 — Scoped animation key moves

The pure native AnimationKeyMove tool owns an immutable clip and one exact source
node/path/key, with absolute-time ghost updates and owned snapshots. It preserves
values, tangents and interpolation; occupied targets remain visible and recoverable.
Invalid samples retain the previous target. Returning to the source restores its
original float representation, including signed zero. Move construction/assignment
retire the source handle; independent instances may share the clip. It owns no
scene, file, device, clock or history. Module interface and implementation are separate.

The actual editor now exposes begin-animation-key-move, update-animation-key-move and
finish-animation-key-move through its ordinary strict JSON/mailbox product interface.
Begin selects the exact source with current document identity/revision and a clean
form; the monotonically scoped move session survives updates and resource reopen
cannot reuse a retired session. Status reports source/target, changed and occupied.
Updates change only the curve ghost, never document revision/history, preview pose/
clock/session or disk. Commit uses the ordinary native UpdateKey, candidate initial/
current pose and enabled-skin preflight, then one shared resource/history publication
boundary also used by ordinary edits/undo/redo. Failed preflight/history retains the
move and previous resource/preview; no-op commits retain revision, redo and preview
identity. Successful changed commits preserve key count and create one history step.
Animation session exhaustion now stops before generating the reserved maximum ID.

The GUI uses those same operations for curve dragging and displays the same ghost,
Move time, Apply move and Cancel move. Occupied targets are red and cannot be applied;
a failed commit retains the gesture for correction. Competing form/track/key selection,
range changes, save and history are locked while moving, with the lists still scrollable.
Native pointer binding includes the final release position, cancels on Escape/lost
focus/changed graph span, and commits once on release. Agent moves have no native
pointer binding and do not depend on operating-system focus. Active moves participate
in unsaved close handling; save-close refuses them, cancellation is available during
the modal, and explicit discard retires them without writing a posed scene.

Five durable window/device-free unit cases cover owned absolute samples, source
isolation, collision recovery, return-to-origin/negative-zero no-op, invalid source/
path/key/time inputs, finite boundary, cubic payload/mode retention, moved-handle
retirement, independent ownership and synchronized transfer to a joined worker.
Release/NDEBUG final full checks pass 1095/1095 (21.50s): 293 unit and 802 integration,
with overlapping 126 offscreen GPU, 27 native audio and 28 SDL-dummy platform cases.
Focused final Debug animation/clip/document/preview checks pass 57/57 (2.16s), including
29 unit and 28 integration cases (one offscreen GPU case). Earlier equivalent full
Release and focused Debug runs also pass; no automatic case opens a desktop window.

Real native editor operations on a copied PrismRelay project verify its 65-joint,
195-channel, 14,820-key clip: repeated absolute updates leave the actual cloned scene,
preview identity, history and all nine disk scene/animation files unchanged. Occupied
commit, malformed samples/fields/IDs, stale sessions and competing mutations fail
without losing the ghost. Removing another real library asset makes candidate native
preflight fail; restoring it permits the same move to commit. A changed commit at
current time .02 changes the actual cloned Head pose, and a single undo restores it.
No-op return-to-origin retains redo and preview; cancel discards only the ghost.
Pending key forms reject begin without losing their form. Read-track/curve requests
remain available during a gesture. Pending close/save guards, either cancellation
order, explicit resource discard, retired IDs and changed reopen buffers are checked
through the same product operations without injected keyboard/mouse events.

Saving and reopening retains .02250000089406967 and all 14,820 keys; only the intended
copied animation file changes. Actual screenshots show the occupied warning/disabled
Apply action, recovery to an available target and locked competing Inspector/track/key
controls. A final same-build native run again commits/undoes the real pose, preserves
authored scene and clean resource, closes normally with exit 0/empty stderr, retires
its control directory and is confirmed stopped by native app inventory. All nine
original project scene/animation files remain unchanged. This does not claim native
physical-pointer dragging acceptance; the previous SDL/macOS virtual-coordinate
limitation remains separate. The earlier single W press was intercepted by the user's
Chinese IME and is not evidence of a movement defect.

Precommit review covers immutable ownership/lifetime, identity/revision and numeric
validation, no-op/redo semantics, shared publication/preflight failure recovery,
source/preview/disk isolation, modal cancellation, bounded curve work and balanced GUI
disabled scopes. All four changed/new C++ files pass clang-format and diff whitespace
checks pass. Temporary native app/project, investigation clients/requests and insertion
snippets are removed; ignored observations/build logs, caches and existing archives
remain. Compiler/linker environment and dependency pins are unchanged. No distribution
archive is generated; Windows/Linux are not executed. Specification/reference output
remain untracked. Multi-key/tangent-handle/binding/state/layer animation authoring,
finished realistic game art, rendering scale/streaming/performance and the broader
production engine goal remain unfinished.


## 2026-10-16 — Cross-track animation group retiming

Native RetimeKeys resolves 1..16384 unique exact node/path/time source keys together
inside an ordinary ordered clip-edit batch. It preserves payloads, tangents, channel
modes and key count, rebuilds/sorts affected tracks and checks final time uniqueness.
Selected keys may occupy positions vacated by the group, including swaps. Duplicate/
missing sources, invalid targets and collisions with unselected or other destination
keys reject the complete candidate. Numeric no-op targets preserve source float bits.
Ordinary UpdateKey remains the full value/time replacement operation; group retiming
has one simultaneous implementation, without another format or compatibility layer.

AnimationKeyMove now owns an ordered group, with the last key as absolute-time anchor.
Each update adds the same double offset to original times before native float rounding;
all translated keys must remain in 0..86400. Ghost snapshots retain per-key payload and
occupancy, with aggregate changed/occupied flags. Float-collapsed destinations remain
visible and reject commit; returning to origin restores exact source representations.
The immutable source is shared, updates never copy/publish the clip, and prior owned
snapshots remain unchanged. Moved handles retire correctly; self-move assignment is a
no-op so clip/vector ownership cannot diverge. The unit interface remains separate
from implementation and has no scene/file/device/history/hidden-clock dependency.

The actual editor adds strict select-animation-keys and select-all-animation-keys
operations. Selection validates every source before publishing ordered references and
its active Inspector form; errors retain both. Empty selection clears the form, while
pose preparation clears authored-key selection. Select-all builds a bounded group in
the product rather than sending thousands of references through the 1 MiB request
boundary; oversized clips fail without partial selection. Begin-animation-key-move
consumes current selection, and subsequent scoped update/finish operations share the
native group retime, pose/skin preflight and ordinary history publication boundary.
Moved references are prepared before publication and selected through the same native
selection implementation after commit. A post-commit selection failure explicitly
reports publication and clears stale form/references, without claiming rollback.

GUI Ctrl/Cmd toggle and Shift range selection, cross-track membership, Select all,
Clear selection and Move selected keys route through these product operations. The
curve highlights selected keys, renders matching ghost markers and moves an existing
group when dragging one of its selected markers. Modifier-click selects without
starting a move; failed source selection cannot start a different old group. Single-key
value/tangent controls are disabled for multiple keys. Existing range/save/history/
modal/focus safeguards remain active; one group creates one undo step regardless of
key/update count. Changed ordinary edits/history clear stale selection; no-ops retain
selection and redo. Dope-sheet, copy/value group editing, tangent handles, visual binding,
state/layers and production animation performance remain unfinished.

Durable checks extend the existing pure tool/edit cases and file-only document cases,
without a new target or desktop-window test. They retain former single-key coverage
and add simultaneous swaps/vacated-source movement, cross-channel payload/mode retention,
absolute updates, duplicate/empty/oversized selection, every-key boundaries, denormal
float collapse, exact return to origin, the full 16384-key bound and group history/
exact-byte save/undo/redo. Final Release/NDEBUG full checks pass 1102/1102 (18.65s):
299 unit and 803 integration, with overlapping 126 offscreen GPU, 27 native audio and
28 SDL-dummy platform cases. Final focused Debug animation/edit/tool/document/preview
checks pass 71/71 (2.25s): 42 unit and 29 integration, including one offscreen GPU case.
Earlier foundation and complete runs also pass. No automatic case opens a desktop window.

Real editor product operations on a PrismRelay copy verify a three-channel Head group:
repeated ghosts leave the actual native pose, preview identity, history and disk intact;
malformed/duplicate/missing selections preserve selection/form. Occupied time and an
actual missing library asset reject commit while retaining the same recoverable move.
Restoring the asset allows one commit; actual cloned pose changes at .02 and one undo
restores it. Retired sessions fail and a return-to-origin no-op retains redo. Close
confirmation preserves the group session, save/update/commit are guarded in the modal,
and cancellation remains available without losing selection, pose or disk state.

All 195 channels and 14820 keys then select through the actual product operation and
move together by one second, including destinations originally held by selected keys.
The clip remains 14820 keys, duration changes to 3.5, one undo restores the native pose,
and save/close/reopen loads those exact saved times. Only the copied animation changes;
the source authoring scene stays identical. Screenshots show the multi-selection
Inspector guidance, locked single-key controls, ghost, and visible move/all/clear actions.
Native window observation stalls in the computer-use interface; after it returns, the
same launcher/control is confirmed live and normal product inspection/screenshot resume.
That observation delay is not treated as application termination or engine failure.
Physical mouse modifier/range/drag acceptance remains unproven at the previously
observed SDL/macOS virtual-input boundary; no synthetic event hook is introduced.

A final same-build product run adds 2048 keys through eight ordinary edit batches,
checks that selecting all 16868 rejects atomically while retaining a selected key,
and undoes those batches to a clean 14820-key resource. It again commits/undoes the
full group with real native pose comparison, checks pose-form/selection separation,
and closes normally. Both native sessions exit 0 with empty stderr and retired control
directories. All nine original scene/animation files match their initial hashes.
Temporary app/project copies, clients/requests and edit helpers are removed; ignored
observations/build logs, caches and existing archives remain. No distribution archive
is generated. Compiler/linker environment, dependencies and platform targets are
unchanged; Windows/Linux are not executed.

Precommit review covers simultaneous source resolution, immutable ownership and
self-move invariants, float boundaries/collisions, selection validation before state
changes, prepublication allocations, expected error context, shared product/GUI paths,
source/resource/preview isolation and balanced GUI scopes. All eight changed C++ files
pass clang-format and diff whitespace checks pass. Specification/reference output
remain untracked. Broader animation authoring, finished realistic game content,
rendering/streaming/performance and the full production engine goal remain open.


## 2026-10-17 — Owned animation copy and paste

Native KeyGroup owns an immutable 1..16384-key selection with original values,
tangents, modes, record order and minimum/maximum times. It outlives source clip
retirement or later edits and supports partial cubic selections without pretending
they are standalone valid clips. Placement aligns the earliest time, adds one double
offset before float rounding, checks every target and rejects same-channel collapse.
Origin placement retains source float representations. Shared KeyReference/selection
bounds now belong to clip data; no compatibility alias or new asset format is added.

PasteKeys merges selected records into affected candidate tracks in sorted order,
creating missing channels with the source mode and requiring matching existing modes.
Default Reject refuses occupied times; explicit Replace changes only equal-time keys.
The final ordinary batch validates cubic key counts and all native clip constraints.
Values/tangents are copied unchanged, without implicit mode conversion or retargeting.
The source clip and key group remain unchanged on any failed batch.

The window/file/device-free AnimationClipboard service validates portable provenance,
source IDs and the full bounded unique node/entity map, including selected nodes,
before replacing copied content. Failed copies retain content and ID. Successful copy
and clear retire IDs without reuse; immutable shared snapshots outlive clear, replace
or service destruction. Same scene/player/full mapping and current clipboard ID are
required to prepare a native candidate plus destination selection. The service is
noncopyable/nonmovable and has explicit mutation synchronization contracts. It touches
neither OS clipboard nor resources/history/preview; the product performs actual pose/
skin preflight and one ordinary resource publication after preparation.

The actual editor exposes strict copy-animation-keys, inspect-animation-clipboard,
read-animation-clipboard, clear-animation-clipboard and paste-animation-keys operations.
Inspect metadata includes source identities/path/revision, range, counts and current
destination compatibility; owned key pages retain copy order and use 1..256 entries.
Read/clear remain available without an open clip and during other modes/dialogs.
Copy/paste require a clean form and no active move/close/capture dialog. Cross-clip
reuse within the same player's library is supported; matching node numbers in another
player or changed authoring scene never silently imply compatible bindings. Save/
close/source discard do not destroy owned copied data; workspace retirement does.

The Animation panel adds Copy keys and a Paste keys popup with time, source/range/count,
explicit Replace matching key times and clear controls. Both use the same native
clipboard/preparation/publication path as agents. A cheap shared snapshot owns GUI data
through the frame, including a clear action. Selection hints move to a tooltip so
controls remain visible at the normal window size. Paste success selects destinations
in copy order; identical explicit replacements retain revision, redo and preview
identity. Failures retain existing draft/history/pose/clock/selection and clipboard;
a rare post-publication selection failure explicitly reports the committed edit and
clears stale selection instead of claiming rollback.

Six new pure key-group cases cover ownership/moves, ordered placement, invalid/empty/
duplicate/oversized sources, partial cubic payloads, float collapse and the 16384-key
bound with concurrent owned placements. Three native paste cases cover sorted merge,
explicit replacement, channel creation/mode mismatch, partial cubic final validation
and atomic later-batch failures. Five pure clipboard cases cover provenance, all
context dimensions, retired IDs, failed-copy/prepare retention, snapshot lifetime and
read-only concurrent preparation. One file-only document case checks single history,
no-op redo preservation and exact-byte saved undo. Existing single/group move cases
retain their coverage under the shared native selection type. No new test executable
or desktop-window automatic test is introduced.

Release/NDEBUG full checks pass 1117/1117 (18.34s): 313 unit and 804 integration, with
overlapping 126 offscreen GPU, 27 native audio and 28 SDL-dummy platform cases. Focused
Debug animation/edit/group/clipboard/move/document/preview checks pass 86/86 (2.59s):
56 unit and 30 integration, including one offscreen GPU case. Earlier focused native
foundations pass 32/32 (.68s). Dependency pins, compiler/linker environment and platform
targets are unchanged; Windows/Linux are not executed.

One real editor session on a PrismRelay copy verifies 14820 keys/195 channels copied
and pasted into 29640 keys with one history step, one undo, and an identical explicit
replacement that retains redo/preview identity. Owned pages survive source draft
closure/discard and temporary source asset retirement. An actual unsaved five-centimeter
Head adjustment is copied from Idle into Walk, with source discard, contextual missing-
library preflight failure/retry, actual native pose change, save/close/reopen and exact
saved value comparison. The authoring scene remains unchanged through these key edits.
Only the intended copied Walk asset changes; original source Idle bytes do not.

A partial cubic key retains its .2 tangent; pasting into a linear destination is
rejected unchanged, then succeeds after explicit conversion with actual pose evidence.
Both edits undo to the clean saved destination. Close-modal read/cancel and independent
clear retain their stated ownership contracts. A deliberate normal scene duplication
in the QA draft then proves stale-scene rejection, rejection for the separate actual
player 355, and successful fresh local copy/no-op paste for that player. Clear and
replacement reject old IDs. The temporary authoring duplication is explicitly discarded.
All nine original scene/animation files retain initial hashes, and the editor exits 0
with empty stderr and retired control directory.

Screenshots confirm visible Copy/Paste controls and normal layout. A computer-use
virtual click requests the Paste button, but native press/release report (0,0), so
popup pointer acceptance is not claimed. The previously recorded SDL/macOS input
boundary remains separate from product-operation results. Window selection in the
observation tool stalls even with a bounded JavaScript wait; the same live launcher/
control is revalidated after return, without restarting on timeout. Later screenshots
through the already bound window are available. No input bypass/vendor patch or
synthetic test hook is added.

Precommit review covers module/interface separation, immutable payload/snapshot
ownership, exact source/target scope, monotonic IDs, float/mode/collision boundaries,
linear merging, source/preview/disk isolation, failed-publication recovery and GUI
lifetime/disabled scopes. All fourteen changed/new C++ files pass clang-format and
whitespace checks pass. Temporary app/project, requests, clients and editing helpers
are removed; ignored observations/build logs, caches and existing archives remain.
No distribution archive is generated. Specification/reference output remain untracked.
Dope-sheet, bulk value/tangent-handle/binding/state/layer authoring, finished realistic
game art, rendering/streaming/performance and the overall production engine remain open.

### Local light shadow projections and shared authoring

Extended the existing single shadow source to directional, point or spot lights.
Point lights render six independent 90-degree perspective depth layers; spots use
one projection with twice their outer half-cone. A shadowed spot requires an outer
half-cone strictly below 90 degrees. Local radial coverage is min(light range,
shadow distance), with a bounded positive near plane. Directional cascade count
and split blending remain directional controls. The CPU result is now one owned
ShadowProjection type with six matrix slots, without aliases or compatibility code.

Perspective spot receiver-plane derivatives use the quotient rule. Point PCF
remaps every bilinear neighbor across cube faces, intersects its actual texel ray
with the receiving geometric plane and compares that face's projected depth. The
soft path performs sixteen bilinear taps/64 depth loads, while radius zero uses
four loads. Normal offsets reaching the light and nonpositive homogeneous weights
return lit before division. This is a fixed-radius filtering foundation, not
contact-hardening or a claim of populated-scene performance. One source per frame
remains explicit; multiple requested sources fail instead of silently dropping one.

Depth storage follows actual projection layer count (six for point, one for spot,
one through four for directional), reuses matching allocations and releases them
when disabled. Maximum logical current depth storage is 96 MiB at 2048²/six faces;
driver replacement/in-flight overlap is separate. Existing opaque/masked coverage,
skinning and independent cast/receive behavior share the same rendering path.
Inspector Cast shadows is available for all light kinds through existing strict
scene/agent validation, history, persistence, preview and Play implementations.

Added two pure CPU cases for local projection/coverage/cone/range boundaries and
three enduring offscreen GPU cases for local occlusion, cast/receive flags, moving
casters, masked holes, filtering, cutoff, resource transitions, all six faces,
two-/three-face seams and the normal-bias/source numerical boundary. Expanded saved
scene and document regressions to all three kinds, preserving invalid-input atomicity,
undo/redo/save/reopen and directional pixel thresholds. No test window or new test
executable is introduced. Latest Release/NDEBUG full checks pass 1122/1122 (18.63s):
315 unit and 807 integration, including overlapping 129 offscreen GPU, 27 native
audio and 28 SDL-dummy platform cases. Focused Debug checks pass 18/18 (12.91s):
eight unit and ten integration, including eight offscreen GPU cases.

An early full run overlapped an incremental link and reported an absent DotEditor
executable, a script time-budget interruption under concurrent compilation, and
one obsolete scene-file assertion rejecting point shadows. The stale assertion now
checks roundtrips and the actual invalid spot boundary. Builds and tests were then
ordered per configuration; subsequent full Release runs passed without changing
script deadlines or removing coverage. Both presets configure/build successfully
with the caller's toolchain/environment. Windows/Linux are not executed.

Two actual native editor sessions on a FeatureLab copy verify buffered Inspector
point-light application, six-face statistics, spotlight component/transform editing,
one-face statistics, invalid 90-degree spot rejection without document mutation,
undo/redo, save, exact saved-scene reopen and real Play/Stop. Initial captures used
mostly unlit demonstration geometry, so these were not treated as visual shadow
acceptance. PBR bindings and the actual material editor then produce visible frozen
point/spot shadow-versus-disabled comparisons, including a plain receiving surface
without the demonstration normal texture. Both sessions exit 0 with empty stderr
and retired control directories. Original tracked examples remain unchanged; only
the QA scene and material were edited. The final homogeneous-division guards are
also verified by the focused offscreen boundary regression. Computer use is not
used or claimed for this stage.

Precommit review covers projection/bias conventions, CPU/WGSL layout, cross-face
filtering, caster/receiver coverage, resource replacement/release, strict shared
validation and authoring history. All thirteen changed C++ files pass clang-format;
whitespace checks pass. Temporary app/project, request files and Python clients are
removed; ignored capture/state observations and build logs remain. Caches and prior
archives are preserved, and no distribution archive is generated. Multiple shadow
sources, scalable local-face culling, temporal quality, complete animation authoring
and a finished realistic game remain open. The broader engine goal is unfinished.

### Mixed-light shadow atlas and PrismRelay integration

Replaced the single-source restriction with independent shadow requests on all 64
frame lights. Added a pure CPU ShadowAtlas module, with separate interface and
implementation, for deterministic largest-first quadtree packing of up to six
128..2048 square maps per source. Input source/face identities and resolutions are
preserved; unused face slots remain zero. Page size grows only to satisfy the
device layer limit. Whole pages/padding count toward RendererConfig.maxShadowBytes
(1 byte..512 MiB, default 256 MiB). Budget rejection preserves previous renderer
lighting/maps and completed pixels; no source is silently dropped or downscaled.

The shared Depth32Float array clears each physical layer once. Per-face viewport/
scissor and independent instance buffers preserve adjacent tiles and avoid queued
uniform aliasing. Read-only storage contains 64 light-indexed records rather than
one selected-light uniform. Clamped hardware footprints and remapped point texel
loads stay inside their own tiles. Caster world bounds validate once per frame;
local shadow faces conservatively test their own frusta, independent of main-camera
visibility. Directional fits continue to retain overlapping off-camera casters.
Zero intensity or zero range/cone attenuation skips unnecessary shadow sampling.

Actual native observation identified a serious performance regression in the first
multi-source implementation: four-source PrismRelay advanced only 3 Play frames in
2.5808s with 716666638ns dropped simulation time; the single-directional comparison
advanced 177 frames in 2.1554s without drops. This was not accepted as a usable
example. Point interiors now use hardware bilinear PCF; only cube-edge footprints
perform explicit per-neighbor ray/plane remapping. Both paths share perspective
receiver-plane bias. Sampling helpers read individual source fields by index instead
of passing whole six-matrix records by value. Seam checks now include rays just to
either side of two-/three-face boundaries, as well as exact seams and all six axes.

After rebuilding, a second native session on the same saved project/view measures
257 frames in 2.1466s for four shadow sources and 256 in 2.1440s for the directional
comparison, both with zero dropped simulation time and no runtime error. These are
short local wall-clock/Play-counter observations, not a sustained GPU benchmark or
large-scene performance claim. Both optimizations were applied together; their
individual contributions were not profiled separately.

PrismRelay now saves its directional cascades and all three point-light requests,
using 512px local faces and radial coverage matching their 6/8/8m ranges. These were
authored through actual Inspector staging/apply and strict component operations,
with one batch for the two service fills. Undo/redo, save, exact saved-scene reopen,
static Game view, frozen PNG capture and real Play/Stop were verified. Authoring is
unchanged by Play and by the temporary comparison, which was undone before close.
The source scene diff contains only the three local shadow settings. Render reports
142 input instances, 284 directional-only shadow instances, and 628 with all four
sources (170 shadow draws), rather than sending all 142 instances to all 20 faces.
The expected atlas has seven 1024px pages/28 MiB logical depth storage. Both actual
editor sessions exit 0 with empty stderr and retired control directories. No native
mouse/keyboard acceptance or computer-use interaction is claimed.

Added two device-free atlas unit cases covering deterministic mixed-size placement,
non-overlap, identity/ownership, 384-map layer growth, padding/budget boundaries and
invalid work. Three new offscreen GPU cases compare mixed RGB-separated sources
against independent reference frames, reorder/remove sources, retain state on
budget failure, and exercise all 64 point sources/384 maps with real occlusion from
light 64 and resource resizing. Existing directional/point/spot, mask, skin, IBL,
SSAO and caster/receiver regressions retain coverage. The authored-project regression
now requires the four saved shadow sources. No new executable or window test is added.

macOS Release/NDEBUG full checks pass 1127/1127 (24.70s): 317 unit and 810 integration,
including overlapping 132 offscreen GPU, 27 native audio and 28 SDL-dummy platform
cases. Focused Debug checks pass 31/31 (29.45s): ten unit and 21 integration, including
eleven offscreen GPU and ten compiled PrismRelay cases. Both presets build with the
existing caller toolchain/environment. Windows/Linux are not executed. Early build
header/optional-value/copy diagnostics and two incorrect new-test assumptions were
fixed rather than bypassed: soft visibility compares against its actual single-light
reference, and the expanded seam fixture contains actual boundary rays rather than
default zero vectors. Final checks pass without changing deadlines or dropping tests.

Precommit review covers packing/limits/ownership, CPU/WGSL storage layout, per-tile
clearing/filter boundaries, main-camera-independent local culling, dynamic source
mapping, budget failure atomicity, resource release and example persistence. All ten
changed/new C++ files pass clang-format; whitespace checks pass. Temporary app,
requests, clients and editing helper are removed, while ignored captures/timing/state
observations and logs remain. Build caches and existing archives are preserved; no
distribution archive is generated. Cached/pooled shadow updates, populated-scene
profiling/streaming, temporal quality, remaining authoring systems and finished
realistic game art remain open. The broader engine goal remains unfinished.

### Active scene files, shared file forms and Lighting Studio

Document now owns an active scene path rather than binding every save to startup.
Added strict open-scene/create-scene/save-scene-as operations to the shared typed/
CLI/JSON service. Paths use existing portable scenes/assets parents (or the configured
startup path when opening), exact spelling and no symlinks/engine staging components.
New files never replace existing destinations under the caller's serialized filesystem
contract. Open/Create prepare allocation/history before publication/replacement,
clear selection/history and advance revision/scope while retaining the borrowed
Scene object's address. Previously allocated entity values remain reserved. Save as
retains scope/selection/history, changes destination/baseline and leaves the old file
unchanged. Normal Save validates and replaces only the active file.

Metadata validation compares the owned project descriptor directly, without requiring
a still-readable startup scene. External edits still conflict on Save; a valid new
Save as can rescue current content even when an external editor has damaged startup.
Metadata conflicts remain failures. Invalid load/name/path, collision and history
capacity failures preserve document state; failed creation leaves no destination.
File APIs retain owner-thread/moved-state checks and strict canonical revisions.

Resource authoring/catalog includes scenes alongside assets/scripts. File menu and
asset-browser open operations use the same implementation. The viewport identifies
the active file. Scene file forms expose mode/path/name/discard through inspection,
with bounded owned staging, current-revision confirmation and shared cancellation.
The GUI confirmation button invokes the exact machine operation; Cancel/Escape
share scene-dialog close. Dirty document discard is explicit and independent of
resource drafts. Competing forms/Play/gestures/animation audition reject navigation;
project-close Save/Discard/Cancel and independent owned clipboard reads remain usable.

A successful switch releases stopped Play state, closes clean resource buffers,
resets skeleton/selection bindings and invalidates viewport/asset caches. Frozen
owned captures remain independent. UI snapshots refresh immediately after document
publication, before fallible catalog/reset follow-up; failures after publication
explicitly report committed effects. Startup metadata is unchanged. Existing Play
and export use the active scene. These are single-active-file authoring workflows;
runtime/additive scene composition and multi-document sessions remain unfinished.

Authored examples/FeatureLab/scenes/LightingStudio.dotscene and studio-floor.dotmaterial
through the actual native editor operations: a camera, PBR floor, copper/normal-mapped/
dielectric cubes, glass/cutout panels, HDRI IBL, SSAO and three shadowed light kinds.
It is a persistent editable capability scene in the existing FeatureLab project,
with no gameplay script/probe executable or retained content-generation script.
Its material/resource paths load through the real engine. Main remains the startup
SDK demonstration and its bytes are unchanged. Existing example documentation now
explains opening/saving/exporting the studio and separates offscreen checks from
actual native workflows.

The first native session verifies creation/material authoring, actual saved-camera
pixels, Save as with retained history/scope, undo/redo, repeated file switching,
Inspector/material draft guards, Play/Stop and data export of the active Studio
under the package's startup path. Exported scene data equals the current authoring
scene; original project/source file hashes remain unchanged. No distribution archive
or user delivery bundle is produced; the data package is temporary verification.

A second native session exercises complete form staging/confirmation: empty names
fail without creating files, wrong boolean types/stale revisions preserve state,
Create/Open/Save as succeed, and switching releases stopped Play state. An owned
invalid catalog filename on macOS deliberately causes post-publication catalog
failure; inspection still reports the newly committed scene and matching UI snapshot.
Removing the fixture permits catalog recovery. Close/Cancel while a file form is
active retains the form; Save and close writes only an owned temporary active copy.
All stable project/studio/material hashes remain unchanged. Both sessions exit 0,
with empty stderr and retired control directories. Client assumptions about mutation
responses containing workspace fields and close commands not needing revisions were
corrected by inspecting/resuming the same live session, without restarting the app.
Native pointer/keyboard interaction is not claimed.

Build example/template copies and install preparation now exclude live .dot-stage-*
mailboxes/import staging and Finder metadata. A shared CMake copy helper preserves
normal authored files and source data; callers retain owned destination replacement.
This prevents building an open authored project from carrying session data into
example/template copies. Resource/scene creation rejects engine staging names.

Added four document behavior cases, one real CLI session case, one scene-root resource
case, one native Studio resource/document case and one filesystem-only CMake copy
regression. Cases cover scope/revision/history/identity, ownership/thread/moves,
failures/collisions/aliases, metadata/conflicts/rescue, active-file-only persistence,
private staging exclusion and retained authored data. No unit test starts devices or
windows, no automated integration opens a desktop window, and no new executable is
introduced. macOS Release/NDEBUG full checks pass 1135/1135 (18.02s): 317 unit and
818 integration, including overlapping 132 offscreen GPU, 27 native audio and 28
SDL-dummy platform cases. Focused Debug checks pass 51/51 (5.51s), all file/process/
project integration cases, with twelve carrying the existing compiler label. Both
presets build using the caller's toolchain/environment. Windows/Linux are not run.

Precommit review covers prepare/publication ordering, stable borrows and monotonic
identity, active-file baselines, metadata recovery, strict forms, close-modal
composition, cache/session release, postcommit observation and portable build copies.
All nine changed C++ files pass clang-format; whitespace checks pass. Temporary app,
source copies/failure fixture, request clients, editing helpers and exported test data
are removed; ignored screenshots/state/log evidence, caches and prior archives remain.
Runtime scene composition/streaming, broader authoring/performance systems and the
finished realistic game remain open; the original engine goal remains unfinished.

### Deferred native audio activation

Runtime scene replacement needs a prepared candidate that cannot play before it is
accepted. Native GameSession previously started its audio worker/output during
preparation, allowing authored autoplay voices to advance before Start. Added an
explicit deferred-start configuration and Prepared playback state. A prepared worker
accepts owned voice controls but neither mixes PCM nor starts native callbacks;
start prefills the current graph and activates once. Healthy repeated activation is
idempotent; moved, foreign-thread, closed and faulted services reject it. Prepared
workers wait at the configured interval and still detect endpoint/mixer faults,
retain diagnostics and release resources. There is no null-device fallback.

SceneAudio and the native-only AudioSession scheduler forward activation; resetting
retains whether playback has been activated. GameSession always prepares native
audio deferred, dispatches Start, samples animation and establishes initial audio
poses, then activates. Pre-activation constructor/realm/native failures do not start
the endpoint. Isolated component callback failures retain their existing reporting
semantics; activation failures fault the session. The TypeScript observation contract
includes prepared, with no script operation to activate or replace a device. Offline
explicit rendering and normal direct Playback startup retain their contracts.

Four enduring native-audio regressions cover prepared autoplay/cursor/zero-frame/
zero-callback behavior, deferred reset and activation, ownership/move/thread/close
boundaries, repeated unstarted teardown, partial initialization and actual compiled
GameSession startup/failure ordering. The GameSession case loads a real published ESM
generation without invoking a compiler and checks native audio observations from
component construction/Start/Update. No new executable, unit-test device or automated
desktop window is introduced.

Actual FeatureLab editor operations complete two Play/Stop cycles in one native
session: all 116 checks finish, all six deliberately caught invalid operations match
the example contract, native output consumes PCM, no Play diagnostics/faults appear,
and revision/dirty state plus project/scene/script bytes remain unchanged. The native
session exits 0 with empty stderr and its control directory removed. The temporary
client initially mistook the example's expected-error counter for unexpected errors;
reading the implementation and resuming the same live session corrected validation.
Owned temporary app/client/requests are removed; ignored observations/logs, build
caches, prior archives and untracked user references remain. No delivery bundle is
created. Runtime scene switching/composition itself remains unfinished.

macOS arm64 builds succeed in Debug and Release with the caller's toolchain and
linker environment. Full integration-stage CTest runs pass 1139/1139 in Debug
(176.79s) and Release/NDEBUG (19.72s): 317 unit and 822 integration, including
overlapping 132 offscreen GPU, 31 native-audio and 28 SDL-dummy platform cases.
After final worker fault/activation review and a bounded native callback wait,
rebuilt final sources pass all 110 audio-related cases in both Debug (9.23s) and
Release/NDEBUG (4.71s). Windows/Linux are not executed. The four new cases remain
native-audio integration checks, not unit cases. The broad engine/game goal remains
unfinished; these counts establish the recorded contracts, not production readiness.

Precommit review covers worker-only activation state, initialization/publication
ordering, callback/voice cursor timing, healthy idempotence and terminal rejection,
retained fault diagnostics, scene reset state, script access restrictions, RAII and
foreign-thread teardown, platform-independent API placement and existing offline
behavior. All ten changed C++ files pass clang-format; whitespace checks pass.

### Runtime scene replacement and scene-preserving export

Added a scene-scoped request service shared by GameSession's realms. dot:engine
supplies scenes.current/request/status; the current read identifies the calling
realm, including prepared candidates. Requests accept portable relative .dotscene
paths without engine staging components, queue one pending/loading operation and
retain sixteen owned results. Canonical decimal IDs survive cuts within the session;
unknown/expired IDs, stale scopes, wrong threads, malformed arguments and busy writes
fail explicitly. Constructors/migration have read access, not request access. Closing
or faulting cancels uncommitted work; retained observations remain readable.

Variable Update completes the current scripts/animation/audio, then processes at
most one request. Fixed updates and invalid deltas do not cut. File spelling/type,
scene decode, real graphics inputs (including PBR normals/UV frames, normal encoding
and HDR validation), skins, physics, clips, animation and script construction prepare
before acceptance. Graphics preflight bounds unique inputs to 32768 and decoded
mesh/image bytes to 512 MiB; native subsystem limits still apply. Preparation failures
record an owned diagnostic and keep the old world running. Native audio candidates
stay prepared/silent. Acceptance precedes irreversible Stop hooks, then old services
are released and the new scene starts. A committed Stop/Start realm/native failure
retains the new scene, records the actual cause and faults the session. Start may
queue the following frame's cut without recursion. Component failures retain their
existing isolation/reporting behavior; diagnostics now include scene scope so reused
entity values do not collapse unrelated failures across cuts.

Every replacement owns fresh scene/input/physics/animation/audio/realm state. Shared
old scenes remain independently owned snapshots, not active services; raw Play scene
borrows must be reacquired after advance. The standalone player now reacquires the
active scene for rendering and reports failed scene requests once. Disabled hot
reload pins the existing compiled generation across cuts; compiled games never call
a compiler. Close releases retained code/config/service ownership. An authoring clone
with an origin path owns its initial encoded scene seed (up to the codec's 64 MiB),
shared across replacements and released on close. Returning to that origin rebuilds
unsaved starting content, independent of runtime mutations or later disk edits;
other targets load saved files. This matches exported unsaved startup content.

Play exposes a Change game scene chooser and strict request-play-scene and
play-scene-status operations through the shared dispatcher. Inspection includes the
current scope/path and latest request; request acceptance is not completion. Current
Play token/revision, running state and close guards prevent stale mutation. Stop
returns to the unchanged authoring document. Native verification caught a missing
origin-path argument in the workspace factory; the corrected rebuilt editor reports
the active path immediately and completes the same scene loop.

Data/native export can select the current scene's own startup path. Workspace supplies
its active Document path, preserving other scene files instead of overwriting the
original startup. Library callers without an origin use the configured startup.
FeatureLab's existing camera script now supports L between the main showcase and
Lighting Studio, with queued-result retry handling; the studio camera attachment was
authored/saved through the real editor. Its SDK inventory/auditor includes scene
operations and eight deliberately caught invalid operations. No temporary generation
script or new acceptance executable is retained.

Two actual native editor sessions verify keyboard-equivalent agent L round trips,
strict/stale request rejection, missing-target recovery with the studio still running,
current scene provenance and real saved-camera pixels. The rebuilt session reports
119 live checks and 120 after Stop, with eight expected caught errors, no Play fault
or unexpected diagnostics. The viewport capture shows the real studio renderer.
Exporting Studio starts at scenes/LightingStudio.dotscene, preserves original Main
bytes, contains compiled camera code and excludes TypeScript/private control files.
Project metadata/Main hashes remain unchanged; the studio's intended camera script
is the authored scene change. Both sessions exit 0 with empty stderr and retired
control directories. Native mouse/keyboard hardware interaction is not claimed.

Added sixteen durable cases: three pure queue/transport/ownership tests, eleven
session/Play/package regressions, one real native-audio transition case and one
compiled FeatureLab package L-loop case. They cover normal/error/commit boundaries,
constructor and Start/Stop faults, malformed/missing/case/symlink paths, graphics/
audio/animation failures, retained snapshots, service recreation, program pinning,
source-free package execution, origin-preserving export and unsaved Play returns.
The actual FeatureLab exported-data case exercises its real assets/scripts and all
SDK checks without a window. Unit cases initialize no devices; no automated case
opens a desktop window. Existing compiler/native-audio targets are reused.

macOS arm64 Debug/Release builds succeed with the caller's environment. Integration-
stage full runs pass Debug 1153/1153 (177.97s) and Release/NDEBUG 1153/1153 (21.13s).
After final diagnostics/origin/example additions, final Release/NDEBUG full CTest
passes 1155/1155 (19.43s): 320 unit and 835 integration, with overlapping 132 offscreen
GPU, 32 native-audio, 28 SDL-dummy and 186 compiler cases. Final Debug targeted checks
pass 154/154 (39.35s), including all new behavior and the real package loop. Two
intermediate Debug FeatureLab cases exceeded the unchanged 100 ms script wall budget
while another build ran; isolated reruns pass, followed by serialized final checks.
No runtime budgets were raised. Windows/Linux are not executed.

Precommit review covers immutable origin ownership, request/realm access, no-allocation
acceptance, Stop/Start irreversibility and error provenance, program/service lifetime,
stale-handle rejection, portable path/type checks, payload/codec limits, frame/input
ordering, cache/render borrow boundaries, active-file export and authored example
isolation. Sixteen changed/new C++ files pass clang-format; whitespace checks pass.
Owned temporary apps, request/client/edit helpers and exported validation data are
removed. Ignored logs/captures/state evidence, build caches, prior archives and the
untracked user references remain; no distribution archive is generated. Synchronous
preflight can stall a frame; GPU allocation/render failures remain post-commit renderer
errors. GPU staging/background streaming, additive scenes, persistent cross-scene
state, broader authoring/performance systems and finished realistic game art remain
open. The complete engine/game objective remains unfinished.

### Gameplay state and save storage foundations

Added Dot.Game.State and Dot.Game.Saves as separate interfaces/implementation units
in the static engine library. State owns bounded scalar-Unicode JSON independently
of scene/realm/device lifetime. Copies share the store; observations own bytes.
Literal root keys, strict duplicates/numeric/depth/size validation and expected
revision replacement prevent ambiguous input and stale publication. Semantic no-ops
retain revision. Candidate allocation/encoding completes before the store swaps.
Methods require the creator thread; moved facades fail explicitly.

Saves explicitly receives an existing game-owned filesystem directory. Portable
lowercase slot names and strict unversioned envelopes retain data and source
revision, without assigning that revision into a live store. Reads never mutate
State. Existing slots require explicit overwrite; the existing flushed temporary
file/atomic replacement implementation publishes prepared data. File failures retain
the prior slot under serialized filesystem access. No directory crash durability
or hostile concurrent mutation protection is claimed. Symlink/special/case aliases,
malformed envelopes, excessive enumeration and invalid names fail with context.
Listing does not parse contents, permitting corrupt/oversized saves to be removed;
capacity reduction still permits existing-slot operations. Lost roots are not
silently recreated. See GameState.md for the exact native contract and limits.

Thirteen focused regressions reuse existing targets: four pure CPU cases, seven
filesystem cases and two actual GameSession/compiler cases with application-provided
custom services. They cover normal/no-op/conflict/invalid-input behavior, copy/move/
thread/owned observations, persistence/reopen/corruption/capacity, alias/symlink/root
failure and atomic-write failure without retained scratch files. The POSIX permission
fixture actually exercises its failure branch on this macOS host; Windows excludes
that fixture rather than claiming equivalent directory-permission behavior.

The GameSession cases use the real compiler and lifecycle: one shared store records
visits across realm/scene replacement and Stop, persists, reopens and supplies a new
session. Another rejects constructor writes during read-only preparation without
changing the store or the already running session. These are custom application
bindings, not new built-in dot: APIs. No test opens a desktop window; unit cases
initialize no devices/filesystem. Focused Debug CTest passes 13/13 (0.78s).

macOS arm64 Debug/Release builds succeed using the caller's environment. Full CTest
passes Debug 1168/1168 (178.35s) and Release/NDEBUG 1168/1168 (19.46s): 324 unit and
844 integration, with overlapping 188 compiler, 132 offscreen GPU, 32 native-audio
and 28 SDL-dummy cases. Windows/Linux are supported in source but not executed.

Precommit review covers shared/owned lifetimes, validate-before-publish and no-
allocation successful file publication, contextual failures, bounds, canonical
portable paths and platform guards. Seven changed/new C++ files pass clang-format;
whitespace checks pass. Existing compiler/linker environment and dependency pins
are preserved. Build caches/logs and untracked references are retained; no release
archive is produced.

This is a native foundation, not a complete player/editor save workflow. Stable
application identity and per-user directories, built-in realm-restricted script
services, isolated Play state, matching editor/agent controls and actual example
workflows remain next. Synchronous save I/O, background scheduling and crash
recovery also remain open. The full engine/game objective is not complete.

### Integrated game state, save slots and isolated Play

Project metadata now requires a stable portable applicationId, independent of its
display name/location. Blank creation generates a random identity when unspecified;
strict encoding/opening requires one. Template instances receive independent IDs;
rename, relocation and native/data export preserve the stored identity. Updated both
editable example manifests and metadata contracts directly, without compatibility
fallbacks or format-version increments. SDL user-data directory resolution lives in
its own platform interface/implementation, validates IDs before filesystem effects
and needs no video/audio initialization. The native player uses its per-app user
directory's saves child and propagates failures without a project/install fallback.

GameSession now owns one shared GameData service across scene/realm replacements.
The native strict JSON protocol implements owned state observations, semantic edits,
optimistic replace/load and explicit slot operations. Built-in dot:engine gameState
and saves wrappers use that same service. Only in-memory observations are read-only
preparation calls; constructors/migration cannot write state or perform any disk
operation. Failed candidate preparation preserves the active store. Close lets Stop
hooks persist before retirement; fault/destruction retire writes and disk access.
Retained handles still allow owned in-memory observations. No implicit save/load,
entity restoration or scene replacement is added by storage itself.

Play creates fresh state and an owned private temporary directory, rejecting direct
player-save roots. Stop explicitly discards it even when hooks fail. Added fallible
StagedDirectory::discard: cleanup errors retain ownership/path for retry, while
destruction stays best effort. A stopped Play can retry cleanup; restarting through
the workspace cleans the previous store first. Realm faults retire retained data
writers too. A configurable temporary parent still receives only owned children.

Window -> Game state is a docked product panel with live JSON observation and
explicit Save/Load/Delete/Refresh controls. GUI and the strict game-data workspace
operation share GameData::perform, reject stale Play identities and permit only
observations during pending close/non-running Play. show-pane/docking include the
real panel. Slot listings are cached instead of scanned each frame. Revision/size
tokens preserve uint64 bits as decimal strings. Document history and player progress
are separate, and the GUI explains temporary Play storage.

FeatureLab covers all ten new SDK methods using actual storage, restores its
initial state and deletes its reserved demonstration slot. Work is spread over
existing lifecycle phases to stay within the unchanged host-call/time budgets.
F6 saves the game camera's scene/pose; F7 explicitly restores it, requesting the
saved scene and applying its pose from the new camera's Start. Missing saves and
scene failures publish a status without disabling camera input. The auditor now
compares prepared audio with the pre-Start authored pose: Start callbacks may move
the camera before the session's first audio synchronization.

Ten enduring regressions reuse existing unit/filesystem/compiler targets: four pure
CPU protocol/path cases and six integration cases for identity, cleanup retry,
corrupt/stale loads, built-in SDK scene/restart/preparation and Play storage isolation.
Existing cases additionally verify fault retirement, template identity separation
and the actual compiled FeatureLab F6/L/F7 route restoring a displaced camera. Unit
cases initialize no files/devices; automated cases open no desktop windows. Final
focused Debug CTest passes 43/43 (8.32s).

One actual native editor session completes 43 product requests with eight expected
strict/stale rejections. It verifies save/overwrite/conflict/read/load behavior,
F6 -> L -> F7 through both scenes, restoring a modified checkpoint pose, current
panel visibility/geometry, retained observations after Stop, fresh Play isolation,
missing-save recovery and source-free export preserving the application ID. The
actual Report contains 129 live checks and eight deliberately caught errors, without
unexpected Play diagnostics. Project metadata and both scene-file hashes remain
unchanged. Native editor exits 0 with empty stderr and retires its control directory;
terminal close is verified through the supervising process, since its mailbox
response can disappear during retirement. Documented that terminal observation.

The actual native player starts the exported data package for 180 frames, exits 0
with empty stderr and creates the expected SDL user/app saves directory on macOS.
Owned empty user directories created for this review are removed. CoreGraphics did
not provide window titles, so no native screenshot/check-count or pixel-comparison
claim is made. The editor's product/geometry observations and compiled behavior
checks are separate evidence. Neither workflow is installed as a window-opening
test or bespoke acceptance executable.

macOS arm64 Debug/Release builds succeed. Full CTest passes Debug 1178/1178
(179.16s) and Release/NDEBUG 1178/1178 (19.15s): 328 unit and 850 integration,
with overlapping 191 compiler, 132 offscreen GPU, 32 native-audio and 28 SDL-dummy
cases. Windows/Linux code is not executed locally. Release commands retain NDEBUG.

Review covers explicit roots/IDs, shared state and retirement, candidate/Stop
boundaries, owned cleanup and retry, UTF-8/portable names, strict schemas, error
propagation, platform assumptions, cached listing and GUI/agent parity. Thirty-one
changed/new C++/TypeScript files pass clang-format and whitespace checks pass.
Dependencies and caller compiler/linker settings remain unchanged. Background I/O,
directory crash recovery/cloud persistence and finished game-specific checkpoint
workflows remain open alongside the wider engine/game requirements.

Owned temporary client/request helpers and exported validation data are removed.
Ignored actual-result JSON/log evidence and build caches remain, as do the untracked
user references and existing archives. No distribution archive is generated. The
complete engine/game goal remains active and unfinished.


## World-preserving hierarchy organization

Added native world/local reparenting, grouping minimal selected roots and atomic
ungrouping of component-free containers. Shared immediate parents retain the
original local basis; mixed parents use scene root. The active entity supplies
the group origin. Nested selections, full graph/camera validation, singular/shear
rejection, skin/animation references, scene scope and monotonic identity allocation
are explicit contracts. A failed unpublished group returns its reservation; IDs
of committed removed containers remain retired. Pure scene operations initialize
no device. Existing physics binding retains motion and ray-hit identity across
world-preserving group/ungroup edits.

The SDK's Entity.reparent/scene.group/scene.ungroup use these same native operations
and remain forbidden in read-only constructor/migration phases. Editor document
transactions publish selection, scene, identity allocation and undo/redo together.
Edit menus/shortcuts, hierarchy multi-drag and root detach route through this
implementation. Drag payloads freeze scene/revision/IDs, and GUI actions run after
traversal releases borrowed snapshot data. Inspector parent changes explicitly
retain local pose. Tree expansion now has a strict shared scope/revision checked
agent operation and owned view-state inspection without document mutations.

Feature Lab's persistent Lighting Studio now contains the Material studies group,
authored and saved through the real editor. Original model IDs/world poses remain;
project metadata and startup scene are unchanged. Its runtime auditor demonstrates
the three new SDK methods without a scene or physics substitute. This stage does
not implement prefabs, linked scene instances, additive scenes or streaming.

Verification uses the existing CPU unit/document/script/physics targets; no new
acceptance executable or desktop-window test was added. Boundary checks include
10000-entity capacity, depth 256, uint64 identity exhaustion, lifetime/thread and
foreign handles, duplicate/overlapping selections, rejected transforms/references,
history budgets, save/reopen and branch undo/redo.

Actual macOS release editor validation completed 73 product requests: open Studio,
group five models, undo/redo, collapse/expand, reject stale scope/search collapse,
save, reject functional ungroup/cycle, world-detach and undo, ungroup and undo,
Play and change to the main scene, inspect runtime SDK report, Stop and close.
All original world matrices agree within 1e-5. Before/after 1480x574 captures are
byte-identical, from distinct rendered serials 1537/1909, revisions 2/14 and scene
scopes 4/17. The actual viewport image was inspected. The live main-scene report
contains 132 completed checks and eight deliberately caught errors, without
unexpected diagnostics. Editor stderr is empty and its owned process exits 0.
Native physical mouse drag/shortcut interaction was not exercised; those GUI paths
were reviewed but the product-operation results do not establish mouse acceptance.


The first full Debug run passed 1189/1190 cases (180.66s). Its sole failure was an
existing Lighting Studio fixture asserting ten entities before the new authored
container. Updated that persistent example regression to expect eleven and verify
the five intended members, retaining material/light checks. Both configurations
then rebuilt successfully; the focused 22-case Debug run passed in 3.97s.
Review covered publication/rollback ownership, scope and stale payloads, references,
world/local transform semantics, resource lifetime, error propagation, formatting,
platform assumptions and GUI/agent parity. Fourteen changed/new C++/TypeScript
files pass clang-format and whitespace checks pass. Dependencies and caller
compiler/linker settings are unchanged.


Final macOS arm64 checks pass: Debug 1190/1190 (179.17s), Release/NDEBUG
1190/1190 (19.74s). Each suite has 334 unit and 856 integration cases, including
132 durable offscreen rendering cases, 32 native-audio and 28 platform cases.
Neither suite opens desktop windows. Windows/Linux source targets remain supported
but were not executed locally. The editor workflow above is separate product
validation and does not imply complete GUI interaction or engine/game readiness.

Removed 77 owned ignored client/request/counter/PID helpers; retained actual
response/result JSON, captures/logs and build caches. User references remain
untracked and existing archives remain intact. No distribution archive was made.
The broader engine/game goal remains active, including reusable content authoring,
streaming/performance, mature animation tools and finished Prism Relay art/game
workflows. Test totals do not establish that these requirements are finished.


## Unified native character contact events

Virtual capsule constraints now join the existing owned contact stream. Previously
an inner proxy had to touch before a contact event existed, so a controller could
resolve a wall/support encounter without publishing that observation. Live Jolt
character callbacks now copy actual solved contacts; final active-contact snapshots
keep resting constraints alive. Nearby predictive candidates that were never solved
remain excluded. Native body/character pairs have one identity and one observation
per outer step, with native manifolds preferred when both providers observe them.
Final virtual membership is replaced per substep, so a transient pair may begin and
end within one subdivided step. Awake character constraints do not falsely sleep.

ContactGeometry is a native variant of ContactManifold and CharacterContactGeometry.
The script union uses kind=manifold/character. Virtual geometry owns the native
counterpart surface point, pair-ordered normal, signed separation including shell
padding and observer side; it does not manufacture paired manifold points or
penetration depth. Observed virtual separation can be positive. No Jolt pointers
or callback locks escape to game code. The pinned Jolt 5.6 headers/implementation
were checked against these semantics.

Listener attachment is scoped to live ExtendedUpdate and reset by RAII, including
exceptions/failures. Preparation, teleports and rejected edits do not publish
callbacks with unpublished/stale identities. Contact records share native pair and
queue bounds, lifecycle preflight, historical tags and monotonic IDs. Capture or
queue failure after advancement faults the world and rejects incomplete delivery.
The native world, scene mapping and PhysicsSource JSON publication use the same
stream; independent script reads preserve owned geometry.

Added focused existing-target regressions for ground/wall/peer constraints,
Begin/Stay/End, observer and pair-normal orientation, owned snapshots after removal,
world moves, preparation isolation, queue preflight, overflow/faulted cleanup,
unsolved predictive proximity and substep transients. Existing rigid manifold tests
now narrow the geometry variant explicitly. Scene removal preserves historical
entity identity; a real TypeScript component reads and narrows native geometry,
mutates its copy and verifies the next read is independent. Feature Lab's persistent
auditor records character.virtualContact and exposes actual geometry in Report.
No temporary visual probe, acceptance executable or committed helper was introduced.

Debug focused checks pass 63/63 (5.38s), with the added predictive/transient boundaries
passing 5/5 character-contact cases (0.41s). Initial scene/SDK fixtures used the
default spherical body with a nonuniform floor scale; explicit box floors fixed the
invalid fixture without changing native validation. Automated tests remain headless.

Actual release editor validation completed 34 product requests across two ordinary
projects. Feature Lab reports 133 live completed checks and eight deliberately
caught errors, with a real character/Static-floor Stay at step 9: world point
(4,-1.5,2), upward normal, signed separation approximately -1.86e-8 and the expected
entity values (including the high-precision character ID). Prism Relay's agent W
input moved the native avatar 1.532 m; Space reached sampled Y=1.511 m and the avatar
landed near Y=0. Both sessions have no unexpected diagnostics, stop without changing
authoring data, retire control directories and exit 0 with empty stderr. Project
metadata and startup scene hashes remain unchanged. These inputs use the normal
agent channel; no claim is made about native keyboard/IME or physical GUI interaction.


Review additionally separated native manifold provenance from virtual membership:
sleep retention is queried only for a pair actually observed by the native body
provider. A nearby inactive proxy cannot invent a dormant virtual-only contact.
An incoming moving-body trace verifies character→manifold→character provider
handoffs remain Stay events with one observation per step. The solver also leaves
and re-enters its solved set during this slowing encounter, so the regression
checks valid event alternation rather than assuming one unbroken contact. Temporary
trace instrumentation was removed; only ignored diagnostic output remains.
The first full Debug run passed 1196/1197 (180.17s); its sole failure was an older
scene sensor test expecting one event. It now checks both the sensor manifold and
new ground constraint, plus both removal events, preserving the prior sensor
identity and filtering checks.


After provenance refinement, both reviewed configurations build and all 70 focused
cases pass: Debug 5.81s, Release/NDEBUG 0.71s. Repeated the two actual release editor
workflows on the reviewed binary (another 34 requests): Feature Lab observes real
character ground geometry at step 15 with 133 live checks; Prism Relay moves
1.520 m, reaches sampled Y=1.471 m and lands near Y=0. Both have empty stderr,
no unexpected diagnostics, unchanged authoring/metadata/startup files, retired
control directories and process exit 0. Real product validation remains separate
from automated headless tests. The native input-method path was not exercised.

Final review covers provider coalescing/provenance, signed geometry and ordered
identity, callback/worker lifetime, scoped listener cleanup, bounds and faulted
stream rejection, native/scene/SDK publication, historical handles and example
behavior. Twelve changed C++/TypeScript files pass clang-format and whitespace
checks pass. No dependency, platform guard, compiler or linker environment changed.
Removed 70 owned temporary helper/request files from both validation passes and
removed the temporary trace instrumentation. Actual result/response JSON and logs,
including the diagnostic trace, remain in ignored build storage. Build caches,
existing archives and untracked user references remain intact. No release archive
was generated. Character scalability/performance and the wider editor/content/game
requirements remain unfinished; this contact stage does not complete the goal.


Final macOS arm64 suites pass: Debug 1197/1197 (179.86s) and Release/NDEBUG
1197/1197 (19.10s), each containing 334 unit and 863 integration cases. The
integration suite includes 194 compiler, 132 offscreen, 32 native-audio and 28
platform cases. Neither automated suite opens desktop windows. Windows/Linux
code remains supported but was not executed locally. The unchanged broad goal
remains active and is not inferred complete from these counts.


## Reusable authored scene content

Added Dot.World.SceneContent with separate interface/implementation. CPU-only
extraction prepares the descendant union of a unique ordered selection, rooted
around the active entity's world origin with compact asset IDs. External parent
bases are flattened only when representable as TRS; reflected bases remain valid,
while shear fails. Native skin/animation references must remain inside the union;
script JSON stays opaque and paths stay project-root relative. Environment/AO are
excluded from content extraction. Sources and caller entity allocation remain intact.

Scene insertion prepares an independent, validated target candidate with a placement
container and owned scoped ID map. Placement is parent-local; original target
entity values, globals and primary camera/listener remain, imported primary flags
are demoted and native references remap. Final depth, entity, codec and history
limits reject before publication. Candidate handles use a fresh scope: this is
explicit authoring preparation, not in-place runtime instantiation or a linked asset.
Linked updates/overrides and reusable prefab editing remain unfinished.

Editor Save selection as scene publishes a new assets file without changing active
scene, selection, scope, dirty state, revision, history or high-water mark. Paths,
project metadata, missing parents, aliases and existing files are checked. Failures
after an applied file operation carry committed-effect context. GUI and agents use
save-selection-scene; Play, drafts, gestures and dialogs are guarded. General
instantiate-scene replaces the previous model-specific insertion implementation,
without an alias or compatibility parser. Imported models and authored scenes now
share native preparation, selecting the placement root with one undo/redo entry.
Resource catalog refresh failures report successful file publication explicitly.

Added five existing-target pure CPU cases for overlap/layout/reflection, scoped
ownership, native/opaque references, globals/primary flags, world placement,
shear/depth/capacity/high IDs, foreign/moved/thread handles and source preservation.
Three document regressions cover new-file publication, unsaved source preservation,
normal insertion/history/save/reopen/branch high-water, metadata/stale/path/alias
failures and history/batch rejection. Existing model and native-binding regressions
now check the shared operation and placement container. No new acceptance executable
or desktop-window test was added. Initial focused 32 cases pass (4.28s).

Actual macOS release editor validation completed 53 product requests in one native
workspace: open Lighting Studio, save the Material studies selection, verify all
active document state unchanged, open/edit/save the content root as Surfaces, return
to Studio, remove the original group and insert the new file at the original
placement, compare every original world matrix, capture, Play, reject content saving
during Play, Stop, undo both scene edits and close. It created the persistent
examples/FeatureLab/assets/MaterialStudies.dotscene through the product rather than
a committed generation script. This scene contains seven editable entities and
shares its material resources with the studio.

All original world matrices agree within 1e-5. Two actual 1480x574 viewport PNGs are
byte-identical, from frame serials 131/213, revisions 7/11, and scopes 10/16 with
Game view/document source and no material preview. The captured viewport image was
inspected. Native Play runs without unexpected diagnostics; source project metadata,
main scene and Lighting Studio hashes remain unchanged. Undo restores clean original
scene data. Editor stderr is empty, the control directory retires and the owned
process exits 0. GUI button bindings were reviewed; physical pointer clicks were
not exercised by these agent requests. This is specific content-workflow evidence,
not proof of completed prefab tooling or large-game production maturity.


Final rebuilt configurations include the authored asset in generated example copies.
Focused checks pass 41/41 in Debug (5.99s) and Release (0.83s). Full macOS arm64
checks pass Debug 1205/1205 (181.32s), Release/NDEBUG 1205/1205 (19.69s). Each full
suite has 339 unit and 866 integration cases, including 194 compiler, 132 durable
offscreen, 32 native-audio and 28 platform cases. Automated suites open no desktop
windows. Windows/Linux source targets were not executed locally.

Final review covers module direction/interface separation, scoped/candidate ownership,
reference closure/remapping, primary/global preservation, matrix representation,
integer/depth/capacity boundaries, exclusive file publication, metadata and alias
conflicts, error context after possible effects, GUI/agent guards, history/ID lifetime
and appropriate test coverage. Eight changed/new C++ files pass clang-format and
whitespace checks pass. Dependencies and caller compiler/linker settings are unchanged.
Removed 54 owned helper/request files, keeping actual JSON/log/image evidence and
build caches. The authored example asset remains; user references remain untracked
and existing archives are preserved. No distribution archive was generated. The
full engine/editor/game objective remains active, including linked content, mature
animation workflows, streaming/performance and finished Prism Relay experience.


## Persistent scene entity allocation

Confirmed a prerequisite gap for linked content: SceneFiles reconstructed allocation
from surviving entities. Deleting a highest-valued source node and reopening could
reuse its value for another node, invalidating durable source-node associations.
Scene now exposes an owned-thread next-value observation and monotonic reservation
operation; null seals allocation permanently. Reservations/deletions survive native
codec and file round trips, including sparse and empty exhausted scenes.

Scene JSON requires nextEntity, a canonical positive decimal next-value string above
all live values, or null when exhausted. Missing/noncanonical/numeric/regressing
metadata is rejected. Repository scene assets were replaced together with this
strict format change; no fallback or format-number increment was added. Existing
unrecorded reservation history cannot be reconstructed retroactively; initial fields
use current live maxima, before durable link associations exist. Subsequent saves
retain the real allocation state rather than inferring it from entity count.
Scene-content extraction creates a fresh compact identity namespace, while insertion
respects target reservations and publishes the advanced candidate floor.

Document allocation remains non-undoable. Private history stores authored content
without allocator metadata; reconstruction injects the current floor through the
strict codec. Public snapshots expose matching top-level and scene.nextEntity;
undo restores content/selection without rolling allocation backward. Dirty state
compares authored content, so a content-clean undo need not be marked dirty solely
for reservations. Save/Save as still publish the live floor. The saved baseline
tracks its allocation separately and rejects external metadata-only edits. This
avoids duplicate full snapshot storage or brittle serialized-text/hash comparisons.
History byte budgets measure owned authoring text plus selection storage.

Added native ownership/reservation/sealing tests, content insertion/extraction floor
checks, canonical/missing/high/exhausted codec cases, sparse/empty file round trips,
and independent Document reopen after saved node deletion. Further regressions cover
undo/Save as, external allocation conflicts, no-op unpublished creation/removal and
failed batches. Existing content-only undo comparisons now retain all authored-field
checks and separately verify allocation, while history boundaries use actual owned
history bytes. Initial focused 98-case run exposed five older budget/full-snapshot
assumptions; after those contract updates all 98 pass (6.18s). Coupled content/model/
Feature Lab and document checks pass 92/92 (8.67s), including two offscreen cases.

Actual release editor validation uses two independently started native processes.
Across 29 product requests, the first opens the editable MaterialStudies asset,
allocates value 9007199254740998, saves it, deletes it and saves again. After clean
close/process exit, the second process reopens the asset and allocates
9007199254740999. Undo returns to clean authored content while retaining the advanced
floor; a clean Save persists 9007199254741000. A complete create/remove no-op batch
preserves scope, revision, allocation and published content. The final asset retains
its seven original entities and no validation nodes; only allocation metadata differs.
Project metadata, main scene and Lighting Studio hashes are unchanged. Both native
processes exit 0 with empty stderr and retire their control directories. This is
product lifetime/persistence validation, not a claim of mouse/IME or visual acceptance.
Linked updates and override authoring remain unfinished; stable persisted node values
are the necessary groundwork rather than completion of that workflow.

The first full Debug run passed 1211/1212 (182.76s). Its sole failure was an older
audio-codec fixture that appended entity 2 without advancing nextEntity beyond 2;
strict allocation validation correctly rejected it before listener validation. The
fixture now supplies floor 3 and checks the specific duplicate-primary-listener
diagnostic. Both focused audio-codec cases pass (0.20s), preserving the semantic
coverage instead of relaxing production validation.

Final macOS arm64 Debug/Release builds succeed. Full suites pass 1212/1212 in Debug
(181.14s) and Release (19.70s, NDEBUG), each containing 341 unit and 871 integration
cases. Integration includes 194 compiler, 132 durable offscreen, 32 native-audio,
28 platform and 20 process cases. Automated suites open no desktop windows.
Windows/Linux source targets were not executed. These results establish the
specified identity contracts, not completion of the full engine/editor/game goal.

Final review covers monotonic/exhausted allocation, thread/moved-owner boundaries,
strict codec input, exception-safe candidate/history/file publication, undo/no-op
semantics, external conflict baselines and module interface separation. All 14
changed C++ interface/implementation/test files pass clang-format; whitespace checks
pass. All five changed example scenes retain identical authored data with valid
allocation metadata. Removed 30 owned temporary client/request files, preserving
actual product response/log evidence, build caches and existing archives. No
distribution archive was generated. EngineDoc.md and output/ remain untracked;
dependencies and caller compiler/linker settings are unchanged.


## Linked scene source refresh and overrides

Added persisted SceneLink authoring provenance with a project source path, owned
native baseline and disjoint stable source/target value mapping. Native Scene
validates replacement metadata atomically, including owner lifetime, reserved IDs,
source coverage, bounded aggregate storage and nonrecursive baseline framing. The
scene codec owns strict optional links records; source assets are not read on load.
Runtime consumes the materialized entities. Interface and implementation units stay
separate; native baseline validation reuses the existing codec from implementation
units instead of introducing another component schema.

Dot.World.SceneLinks prepares linked insertion and three-way refresh candidates.
Unmodified properties follow saved source changes; local property/deletion overrides
persist. Objects merge by property, arrays atomically, missing separately from null.
Native parent/skin/animation references remap before comparison. Existing source
nodes retain target values; additions respect reservations/exhaustion. Source floors
cannot regress and retired source values cannot reappear. Removing overridden nodes
is a conflict unless revert explicitly discards those overrides. Final native
validation rejects dangling local references, cycles and incompatible components
before publication. Empty sources can remain linked. Revert restores source-owned
nodes while keeping placement roots/local additions; unlink retains materialized data.

Document edit operations link-scene, refresh-scene-instance, revert-scene-instance
and unlink-scene-instance share revision checks, candidate publication and one-entry
undo/redo with ordinary authoring. No-op refresh and failed batches preserve all
published state/IDs. Source reads use the existing guarded resource path; active-file
self-links reject. Root deletion retires its link; duplication/extraction produce
independent copies, and ungroup requires explicit unlink. Assets provides Add linked
scene; root Inspector provides Open source, Refresh, Revert and Unlink through these
same product operations. Recursive nested refresh, variants, apply-to-source,
retargeting and per-property override visualization remain unfinished.

Added eleven pure CPU regressions and three file/document regressions for merge,
local deletion/revert, empty/deleted source conflicts, dangling local children,
stable native references/primary flags, shared-source isolation, incompatible merged
components, allocation regression/reuse/exhaustion, strict metadata and moved/thread
ownership. Document coverage includes independent reopen, undo/redo, no-op refresh,
missing source, stale/unknown requests, later-batch rollback, duplication and root
deletion. Initial new-code compile errors (optional JSON conversion and a partial
designated initializer) were corrected. The first twelve focused cases pass (0.70s);
after two added boundary cases the coupled suite passes 101/101 (7.24s), including
two existing offscreen model regressions. Debug and Release builds succeed.

Actual release editor operations replace Lighting Studio's original material group
with a linked MaterialStudies instance at the same world placement. The original
five material/mesh records keep their properties and world transforms. The saved
source now names its shared set and copper panel explicitly; the studio retains
Copper (studio override) as a real local override. Source refresh updates the shared
set while preserving that override. Native revert/unlink both undo back to the
complete linked scene. Lighting Studio now has thirteen nodes, six meshes and the
same three shadow sources; its durable example check verifies world matrices,
canonical baseline and no-op source refresh. Main scene/project metadata remain
unchanged. An independently launched editor reopens clean and refreshes without
changing scope/revision/allocation/history/content.

The first temporary client raced control-directory retirement while trying to read
a close response; saved edits and capture evidence were already complete. Subsequent
clients observe close through the owned process result and retired control path.
The following two native sessions complete 18 and 4 product requests and exit 0 with
empty stderr. A further seven-request view check also exits 0/empty stderr. Opening
another scene resets view mode, so the initial before/after captures used different
cameras; they were not presented as equivalent frames. The corrected game-camera
capture has exactly the same PNG bytes as the pre-insertion game-camera capture.
These are real product agent operations and native viewport evidence, not physical
mouse/IME acceptance or proof of mature prefab tooling.

The first full suites pass 1226/1226 in Debug (183.22s) and Release (19.56s).
Final review then added complete encoded-file budget checks before either native
link preparation API returns: individually valid source/target/baseline data can
exceed the 64 MiB file cap when combined. Document publication already checked this;
native candidate callers now receive the same failure directly. Both configurations
rebuild successfully for that final guard before final full-suite verification.

Final post-review suites pass 1226/1226 in Debug (182.04s) and Release (19.74s,
assertions disabled), each with 352 unit and 874 integration cases. Integration
includes 194 compiler, 132 offscreen, 32 native-audio, 28 platform and 20 process
cases. Automated suites open no desktop windows. Verification is macOS arm64 only;
Windows/Linux source support was not executed. The full engine/editor/game goal
remains active and is not inferred complete from these results.

Final review covers metadata ownership and recursion/storage bounds, stable and
reserved mappings, transactional merge/publication, native reference validation,
source-path guards, history/no-op/lifetime behavior, GUI snapshot ownership during
mutations and module interface separation. All eleven changed/new C++ files pass
clang-format and whitespace checks pass. Removed 55 owned temporary client/request
files, retaining native response/log/PNG evidence in ignored build storage. User
reference files, build caches and existing archives remain preserved. No distribution
archive, dependency or caller compiler/linker environment change was introduced.


## Applied scene override inspection and revert

Added owned applied-override records and paged inspection to Dot.World.SceneLinks.
The existing materialized scene and last refreshed native baseline determine
property changes, deleted inherited nodes and local added nodes; no new persistent
patch format is introduced. Direct link ownership precedes ancestor ownership,
including mapped nodes moved outside their placement root. Placement-root properties
and components stay local. Object keys use escaped JSON Pointers, arrays remain
atomic and absent differs from JSON null. Pages use stable source/object-key
traversal followed by added identity order, limit 1..256 and validated offsets.
Only returned records own serialized values, without building an unbounded diff list.

Selected stored-baseline revert validates 1..256 unique active keys before publishing
one complete candidate. Properties and deleted nodes restore before added subtrees
are removed, allowing parent-property restoration and local-group removal in one
valid transaction. Inherited descendants and dangling references still reject
removal when unresolved. Full baseline revert restores all inherited nodes and
removes local additions, while preserving placement roots/unrelated data. Nested
local instance roots removed with their subtrees retire their metadata. Reverts
preserve allocation reservations and validate final native/encoded-file bounds.
They perform no source-file I/O, so stored-baseline recovery works with missing source files.

Document exposes strict inspect-scene-overrides, revert-scene-overrides and
revert-scene-baseline through the existing query/edit protocol. Queries return
bounded data and revision/scope/path/selection context without a full scene snapshot;
read-only requests need no revision. Mutations retain ordinary revision/history/save
conflict checks and preserve surviving selections. No-op full revert preserves all
document state. Inspector lists baseline/local values, instance/entity scope,
pagination and per-row/displayed-page/all revert controls through the same product
operations. Selected-entity queries update the exposed workspace.sceneOverrides view.
Applied values remain readable while drafts/Play/previews block mutation. Common
workspace status construction avoids recursively rebuilding a full inspect response
for these paged queries. Source refresh and latest-source restoration retain their
separate existing semantics.

Added nine CPU-only regressions and four file/document regressions. Coverage includes
stable filtering/paging, escaped/empty object keys, null/absence and arrays, dependent
deleted-node restoration, nested-link cleanup, compound parent/subtree changes,
native reference failures, full/no-op revert, invalid/duplicate/stale selectors,
thread/moved-owner boundaries and snapshot lifetime. Document checks include strict
read-only shapes, bounded replies, missing source, independent reopen, selection,
undo/redo, later-batch rollback and history-budget rejection. Feature Lab verifies its
actual high-precision copper-node name override. Nine initial CPU cases pass (0.57s),
then fourteen focused cases including the example pass (0.93s). After reply/status
review, coupled authoring checks pass 73/73 (4.87s). Debug/Release builds succeed.
New-test initializer syntax and explicit expected-JSON return construction errors
were corrected before these successful checks.

One actual release editor process completes 30 product requests on Feature Lab.
It identifies the saved copper override, creates two temporary property edits,
verifies three one-record pages and selected-node/instance view-cache synchronization,
reverts selected properties in one transaction and checks undo/redo. Single-name and
full baseline reverts both undo to the original saved authoring data. A repeated
clean full revert preserves scope/revision/history/allocation. With an unapplied
Inspector draft, paged inspection still reports applied data while mutation rejects.
An invalid selector also leaves the document intact. Final scene and allocation
match the initial saved data; project metadata, main/LightingStudio scenes and
MaterialStudies asset hashes are unchanged. The owned process exits 0 with empty
stderr and retires its control directory. Removed 33 temporary client/request/barrier
files, retaining native response/result/log evidence in ignored build storage.

Computer Use cannot resolve the directly launched unbundled DotEditor executable by
display name or full binary path. No mouse/screenshot acceptance is claimed for
this stage; native agent operations and view-state evidence above remain distinct.
Recursive linked refresh, variants, apply-to-source, source retargeting and inline
field markers remain unfinished; the broader engine/editor/game goal stays active.

Final macOS arm64 full suites pass 1239/1239 in Debug (184.79s) and Release
(19.76s, assertions disabled), each with 361 unit and 878 integration cases. The
integration suite includes 194 compiler, 132 durable offscreen, 32 native-audio,
28 platform and 20 process cases. Automated suites open no desktop windows.
Windows/Linux source targets were not executed. These results verify the specified
authoring contracts and do not establish completion of the overall engine/game goal.

Final review covers borrowed/owned data lifetimes, root/ancestor ownership,
deterministic bounded paging, escaped key/null/array semantics, selector validation,
dependency-aware candidate mutation, reference/file/history budgets, no-op/allocation
invariants, read-only versus draft behavior and UI snapshot/cache/ImGui stack lifetime.
All eight changed/new C++ files pass clang-format and whitespace checks pass. No
example data, dependency, caller compiler/linker setting or distribution archive was
changed. User references, caches and existing archives remain preserved/untracked
as applicable; temporary validation helpers are not retained in source control.

## Layered scene source identities

Separated persistent source provenance from intermediate materialization integers.
Using an intermediate native ID as source identity could transfer an outer override
to a different nested node, or collide with a later source-local allocation.
Dot.World.SceneIdentity now validates bounded logical keys and ancestor-closed
source namespaces. Dot.World.SceneSources owns independent native captures or
consumes working scenes with explicit original local identities and physical
allocation observations. Interfaces and implementation units remain separate;
these factories perform no file or device I/O.

Linked insertion/refresh matches logical keys, preserving continuing target IDs,
native references and overrides when working IDs change. A genuinely different key
receives a fresh target; removing an overridden node still rejects transactionally.
Namespaces retain source paths, monotonic allocation floors and live physical
allocations, including saved inherited allocations and nodes hidden by an outer
override. Hidden source nodes may reappear with fresh target bindings. Explicit
unlink inside a saved source promotes its inherited physical allocations to local
identities, retiring former nested bindings. Arbitrary script literals stay opaque.

Flattened baselines use compact values in logical-key order, separating their
allocator from original source reservations. Equivalent working-ID reorderings
therefore preserve target/baseline serialization. Strict scene records now require
mapping keys and namespace metadata; the example asset was replaced in place,
without a compatibility loader or version terminology. Override queries expose
sourceKey string arrays alongside canonical-baseline source references. Feature
Lab's authored entities, transforms and local copper override remain unchanged;
only LightingStudio link provenance/canonical-baseline metadata changed.

Added twelve CPU-only regression cases for identity/path/capacity/depth/exhaustion
boundaries, hidden versus retired nodes, native versus working allocation, ownership
and independent capture, reused intermediate integers, references and local overrides,
canonical no-op refresh, strict serialization, saved-source unlink and foreign-thread/
moved-owner rejection. Existing link/example tests retain coverage and verify sourceKey.
Coupled authoring checks pass 79/79 (4.32s). Initial compilation exposed a missing
direct standard-header dependency for vector comparison, a shadowed namespace decode
variable and a missing explicit test-module import; all were corrected before these
successful checks. Debug and Release builds succeed.

One actual Release editor process completes fourteen product requests on Feature
Lab: open/save LightingStudio through the product, open main, insert the saved
nested source, verify a no-op refresh, edit/query a nested copper override, then
undo/redo and restore clean main content. The query retains the logical key
[9007199254741009,2] independently of the outer materialized target. Project metadata,
main and MaterialStudies hashes stay unchanged; Studio's saved authored content also
matches its original. The process exits 0 with empty stderr and retires its control
directory. Removed fifteen owned temporary helper/request files while retaining
native response/result/log evidence in ignored build storage. No mouse/screenshot
acceptance is claimed for this stage.

This stage supplies identity foundations; recursive dependency loading/refresh,
cycle handling and graph-wide budgets remain unimplemented. Ordinary editor refresh
still reads the selected saved source. The overall engine/editor/game goal remains
active; these checks do not imply production readiness or completed game quality.

The first complete Debug run passes 1250/1251 (184.49s); the existing Feature Lab
continuous/discrete slider case faults Audit.onUpdate at the runtime's 100ms
operation deadline. Its isolated rerun passes (0.71s). This is a timeout observation,
not an established source-identity failure or a confirmed scheduling diagnosis;
no production/test deadline was relaxed. Complete Release passes 1251/1251
(19.81s, assertions disabled). The second complete Debug run passes 1251/1251
(183.59s); the earlier timeout does not recur and its cause remains unconfirmed.
Both suites contain 373 unit and 878 integration cases, including 194 compiler,
132 durable offscreen, 32 native-audio, 28 platform and 20 process cases. Automated
suites open no desktop windows. Validation is macOS arm64 only; Windows/Linux source
targets were not executed.

Final review covers provenance ownership, namespace closure/live-allocation/depth/
storage bounds, original versus working reservation semantics, logical-key matching,
canonical no-op behavior, native reference remapping, transactional rejection,
strict serialization and module separation. All thirteen changed/new C++ files pass
clang-format and staged/working whitespace checks pass. No dependency or caller
compiler/linker environment changed; no new test executable or distribution archive
was introduced. User references remain untracked; caches and existing archives are
preserved.

## Recursive linked source resolution

Added Dot.World.SceneResolver with separate interface/implementation units. It reads
saved source graphs bottom-up through an owned-text callback, resolving instances in
root-value order and caching each distinct portable asset once per operation. Shared
dependencies retain independent placements. Cached subtree heights are checked at
every placement, preventing an earlier shallow cache hit from bypassing a later
depth limit. Original source-local identities and physical allocation reservations
are captured before working refreshes, keeping temporary allocations out of provenance.

Editor link/copy insertion, refresh and latest-source restoration now share this
resolver through the existing GUI/strict document operations and resource path guards.
Intermediate source overrides remain applied; outer restoration does not discard
another source's conflicting edits. Cycles, active-document references, missing/
malformed/inaccessible dependencies, merge conflicts and input/work/storage budgets
reject without publication or source writes. Loading/opening, Play/export, unlink and
stored-baseline recovery remain independent of source availability. The resolver is
synchronous and does not provide an atomic cross-file snapshot against external edits.

Graph defaults are 32 source levels, 256 sources, 4096 links, 64 MiB input, 128 MiB
resolved serialization and 100000 entity visits (decoded plus refreshed candidates).
Native callers may lower limits. Source loaders receive the remaining byte quota;
oversized returned text rejects before decode. Per-scene storage/provenance/reference
validation still applies. No new dependency, platform backend or test executable.

Added twelve CPU-only graph cases and three file/document regressions. Coverage
includes bottom-up changes without intermediate saves, layered overrides/references,
working/local ID collisions, shared dependencies, cached-depth limits, empty sources,
cycles/active references, exact budgets, malformed/missing/throwing/oversized loaders,
intermediate conflicts, exhausted allocation and deterministic link processing.
Document coverage includes history/no-op/save/reopen, later local-source allocations,
failed-batch reservations, missing/cyclic/active sources, intermediate conflicts even
on outer restore, and latest-source independent copies. The prior 79 coupled cases
pass (4.25s); the expanded coupled set passes 94/94 (5.31s). Test compilation initially
compared expected objects with non-comparable error payloads; comparisons now inspect
successful values. Two cycle fixtures incorrectly tried to instantiate empty content;
they now contain a real baseline node and preserve the product's nonempty-insertion
contract. Empty saved-source resolution is tested separately. Debug/Release builds pass.

Actual Release DotEditor document commands complete twenty requests across seven
processes on an isolated copy of Feature Lab, opening no desktop windows. The product
links LightingStudio into main, edits/saves the deepest MaterialStudies asset, then
refreshes main without saving Studio. Its copper transform and new descendant arrive
through the intermediate source while the Studio copper-name override survives.
Outer override inspection retains layered sourceKey; undo/redo restores the expected
authored content, save/reopen retains the result, and repeat refresh preserves revision/
scope. Missing nested input and a new indirect active-document cycle both reject.
Studio and project metadata hashes remain unchanged; failed operations preserve main
and the edited source. An initial helper assertion looked for product error JSON on
stderr; the CLI correctly emits it on stdout, and the helper was corrected. No engine
failure is inferred from that helper assertion. The isolated copy is removed after
the workflow; original example assets remain unchanged. Native response/log/result
evidence stays in ignored build storage.

This implements recursive authoring source processing; variants, apply-to-source,
retargeting, background resolution and broader production/game-quality work remain.
The full engine/editor/game objective stays active.

Final complete macOS arm64 suites pass 1266/1266 in Debug (185.82s) and Release
(19.75s, assertions disabled): 385 unit and 881 integration cases each. Integration
includes 194 compiler, 132 durable offscreen, 32 native-audio, 28 platform and 20
process cases. Automated suites open no desktop windows. Windows/Linux source
targets were not executed; these results do not establish overall production/game
readiness. The prior stage's isolated script timeout does not recur in these runs.

Final review covers callback/result ownership, cache/stack lifetimes and subtree
depth, deterministic placement ordering, original versus virtual allocation,
monotonic namespace observations, graph budget arithmetic, contextual failures,
transaction/history rollback, source-path guards and shared GUI/agent implementation.
All five changed/new C++ files pass clang-format and staged/working whitespace checks
pass. Removed eight owned helper/request files and the isolated example copy;
retained only response/log/result evidence in ignored build storage. Original examples,
user references, caches and existing archives are preserved; reference files remain
untracked. No caller compiler/linker setting, dependency or distribution archive changed.

## Signed RGB normal textures

Actual Poly Haven utility/duct model imports exposed the old whole-image B>=128
restriction: a small number of JPEG edge samples contain negative tangent Z.
The importer rejected both real assets before publishing their native bundles.
Normal textures now preserve full signed RGB directions through model import,
texture upload, scene/material preview, Play preparation and data/native export.
Removed redundant positive-Z flags and pixel scans from those paths. Linear data
sampling, valid geometry/tangent frames, decoded-image limits, resource ownership
and transactional load/publication requirements remain enforced.

Surface shading normalizes signed directions. Opposing samples can cancel through
filtering, so a direction with squared length <=1e-12 retains the geometric normal;
source pixels are never clamped or replaced. The new offscreen regression verifies
negative-Z radiance and cancellation against the geometric reference, including
normal scale zero. A new model regression verifies signed pixel preservation.
Existing preview, scene transition, export and scene-material cache cases now cover
supported signed normals and retain malformed-image, color-space, frame-lifetime and
partial-resource rollback checks. Source/color and normal bindings of the same image
continue to use distinct sampling cache keys. No new format/version or dependency.

The initial related 39 checks pass (9.80s), and the signed-normal export case passes
(0.40s). The first combined full Debug run passes 1266/1268 (193.03s): one older
scene-material assertion still expected negative Z rejection and was updated to
exercise a corrupt image plus valid signed/cache reuse; a larger-template CLI
fixture exceeded its 10s helper wait (addressed in the example authoring stage).
Those two repaired cases pass (12.85s). A missing explicit Graphics.Image test
import was corrected before successful Debug/Release builds. No production process
or script execution deadline changed.

Final combined worktree suites pass 1268/1268 in Debug (193.39s) and Release
(21.09s, assertions disabled), with 385 unit and 883 integration cases. Integration
includes 194 compiler, 133 durable offscreen, 32 native-audio, 28 platform and 20
process cases. Automated suites open no desktop windows. Validation is macOS arm64
only; Windows/Linux source targets were not executed. These results do not establish
finished game quality or overall engine production readiness.

Final review covers signed/mip-filtered directions, finite shader fallback, linear
sampling, geometric frame validation, pixel preservation, texture-cache ownership,
partial-load resource release and consistent import/preview/Play/export behavior.
All twelve changed C++ files pass clang-format; WGSL control-flow spacing and staged
whitespace checks pass. No caller compiler/linker configuration or dependencies changed.

## Prism Relay industrial environment authoring

Reviewed the actual game viewport before further authoring. The old purple crystal
props conflicted with the requested realistic industrial direction; the character
remains a functional mannequin, without a finished sci-fi suit. Acquired Poly Haven
CC0 Utility Box 02 and Modular Airduct Rectangular 01 through the published asset
API/downloads. Thirteen original 1K glTF/bin/JPEG files were checked against upstream
sizes/MD5 and recorded with URLs/SHA-256 in PolyHavenSources.json. Original inputs,
licenses and full editable native model bundles remain with the example. Native
import retains one utility mesh and a 25-mesh/18-node duct kit; source pixels are
preserved in PNGs. Native materials explicitly bind ARM R-channel AO, which the
upstream glTF omitted, while retaining its G/B roughness/metallic inputs.

The actual editor imports both models, extracts reusable straight/support scenes,
authors their native components, replaces eight crystal visuals with textured
cabinets and fitted box colliders, and inserts twelve duct runs with simple box
proxies plus fourteen supports. One ordinary 82-operation transaction creates the
116 new instance entities. Hierarchy grouping adds the Mechanical services root.
Final saved main contains 341 entities, with only sixteen intended existing prop/
root entities changed; every original gameplay/rig/camera/global record compares
equal to the previous scene. Removed the now-unused crystal OBJ/material. The
template symlink regression now points at a retained real source asset. The example
surface audit checks every used textured material and permits additional material
sets instead of requiring exactly two.

The final game view is captured from the saved, reopened Release editor with no
render error and a clean document. A submitted frame reports 180 mesh instances,
34 color draws and 246 shadow draws; these counts are not a frame-rate benchmark.
Actual Play accepts a twenty-frame W/Space request and captures the native rig in
mid-jump at approximately [-3,.7019,1.8351], with no Play diagnostics or dropped fixed
time. Stop preserves authoring. Data export succeeds through the product CLI; all
108 content files (64972876 bytes) were compared byte-for-byte against every current
source file in the final export. Internal export directories were removed after verification. No native
distribution archive was produced or delivered.

Four owned editor processes exit 0: baseline review, an extra prior-binary launch
after a test compilation failure, updated authoring/Play, and final saved reopen.
The extra prior-binary process was closed and supplied no validation evidence.
Final stderr is empty and the private control directory is retired. Temporary
helpers were corrected for the import response's scene-path string and terminal
close retiring its response directory; a close request with an unsupported revision
field was correctly rejected before a valid close. Queued acknowledgements were
not used as applied-success evidence. Removed 53 owned helper/request/metadata files
and two internal exports; retained native responses, result/log and four real viewport
PNGs in ignored build storage. The normal-import issue and helper mistakes are
distinct from renderer/Play failures.

The focused scene/template/normal checks pass 15/15 (19.63s). Larger template creation
needs 12.41s in its isolated Debug CLI regression, exceeding that helper's former
10s wait; only this case now requests 20s, within its existing 30s CTest limit.
Default production waits and script execution limits stay unchanged. Combined final
Debug/Release suites pass 1268/1268 (193.39s/21.09s), with 385 unit and 883 integration
cases. Automated suites open no desktop windows; native windows above belong to
actual product review. Validation is macOS arm64 only, without Windows/Linux execution.

Final review covers source licenses/hashes, relative resource references, imported
pixel preservation, collider placement, original character/scene invariants, product
authoring/save/reopen/Play/export evidence and template lifetime/budgets. All three
example-related C++ changes pass clang-format and whitespace checks pass. Build caches,
existing archives and user references are preserved; reference files remain untracked.
Finished realistic character art, cohesive environment direction, populated-scene
performance and the broader engine/editor/game objective remain incomplete.

Final Debug/Release resource rebuilds also succeed. Both generated example copies
match the current provenance document and exclude the deleted crystal OBJ/material.

## Binary native mesh storage

The larger imported industrial meshes made native JSON parsing a visible project
creation cost. Replaced DotMesh with one fixed little-endian binary layout:
DOTMESH plus a NUL byte, uint32 vertex/index counts, sixteen IEEE float32 words
per vertex, then uint32 indices. Exact source length and bounded counts are checked
before allocating decoded geometry; existing finite/color/frame/topology validation
still applies. The byte-span codec returns owned geometry and contextual file-load
errors. Atomic binary saves preserve the prior file on validation failure. OBJ
remains a supported text source through the byte API. There is no JSON fallback,
layout version selection, native struct dump or extra dependency. Explicit standard
headers replace the removed JSON header's formerly transitive includes; an initial
missing filesystem include was corrected before the successful builds.

Replaced all forty tracked native meshes in FeatureLab and PrismRelay. Independently
packed vertex/index payload SHA-256 checks match the original data for every file,
including all normal/UV/tangent/handedness values and vertex/index order. Total size
changes from 31,193,910 to 5,116,736 bytes (83.6% smaller). Source glTF/OBJ, materials,
skins, animations, textures, scene entities and source-license manifests are unchanged.
Native meshes remain previewable and usable in scenes, but are excluded from text
buffers and external text-editor actions; edit/reimport their source model instead.

Moved thirteen pure OBJ/native codec cases from file integration into the existing
unit target, replacing the JSON-specific assertions with binary contracts. Coverage
includes independent little-endian golden bytes, every truncated prefix, surplus
bytes, bad header/counts, nonfinite values in every component, color/frame/index/
topology errors, unaligned input, independent result ownership, nonindexed triangles
and exact finite/subnormal/signed-zero values. File integration retains Unicode/
case-insensitive paths, traversal/extension rejection, atomic failed-save preservation
and corrupt-file context. One resource integration case verifies native meshes
cannot use text editing routes. No new test executable or native window test added.

Measured the actual headless editor project-action create command for the Prism
template three times per configuration before and after the change, without other
builds/suites running. Debug before: 10.8916/10.4030/10.4716s; after:
3.4378/3.4423/3.4530s, medians 10.4716/3.4423s. Release before:
1.3969/.8501/.8445s; after: 1.2876/.4209/.4190s, medians .8501/.4209s.
These are successive local runs including script compilation and file publication,
not a cold-cache disk benchmark or frame-rate claim. Production/test deadlines stay
unchanged. The focused fifteen codec/file/resource checks pass (0.76s), followed
by sixty-two coupled model/skin/template/example/CLI checks (16.43s).

The user clarified that a previously ambiguous physical W key operation activated
the Chinese input method. That observation is not evidence of a movement defect;
agent-delivered Play input and human/system input-method behavior are distinct.

Actual Release editor opens the saved PrismRelay example and switches to Game view
through ordinary mailbox operations. Its completed 1480x574 viewport PNG is byte-
identical to the prior JSON-mesh result (SHA-256 c345f04406d3defdfa323c9e357adfcbc
7ef85a74b2c81879f4a458f6ae23fbe), with the same 180 mesh instances, 34 color draws
and 246 shadow draws. The document stays clean, render error stays empty, the owned
editor exits 0, stderr is empty and its private control directory is retired.
Product CLI data export succeeds; all 108 content files (38,896,802 bytes), including
39 native meshes, compare byte-for-byte with current project sources and manifest
sizes. The internal data export is removed after verification; no distribution
archive is produced or delivered. Both built-in template copies match all forty
current native mesh sources.

Final Debug/Release builds and full CTest suites pass: 1269/1269 in Debug (181.16s)
and Release (19.70s, assertions disabled). There are 398 unit and 871 integration
cases; integration includes 194 compiler, 133 durable offscreen, 32 native-audio,
28 platform and 20 process cases. Automated suites open no desktop windows; the
one native editor session above is actual product validation. Validation covers
macOS arm64 only; Windows/Linux targets were not executed.

Final review covers explicit byte order, bounded allocation, exact length, finite
attributes, geometry ownership, failure propagation, atomic file preservation,
text-route exclusion, binary export and original scalar/index payload equivalence.
All six changed C++ files pass clang-format and whitespace checks pass. No compiler/
linker environment, dependency, scene/gameplay behavior or production deadline
changes. Removed owned benchmark project copies and temporary workflow helpers/
requests; retained useful JSON/log/PNG evidence in ignored build storage. Existing
build caches, archives and untracked user references are preserved. Finished
realistic character art, cohesive environment direction, populated-scene performance
and the overall engine/editor/game objective remain incomplete.

## Retained shadow depth and unchanged skin poses

The populated PrismRelay viewport repeatedly submitted 246 shadow draws even when
nothing changed. Added exact per-physical-layer shadow depth reuse to the shared
renderer, including runtime and editor consumers. Cache dependencies include face
projection/tile, candidate mesh ownership identity, successful geometry revision,
world transform and mask coverage inputs. Point/spot faces retain local frustum
culling; directional faces retain off-camera casters. Dirty layers redraw every
resident tile after one clear, preserving unchanged neighbours. Reused layers skip
depth clear, instance upload and mesh draws. Shading-only intensity/color/receiver/
roughness/filter/bias changes reuse unchanged depth.

Weak ownership comparisons distinguish an expired texture from no texture and
prevent pointer reuse or accidental mesh/texture residency. No hash-equality
assumption, extra depth texture or GPU budget change is introduced. One draw table
and face index lists bound CPU snapshots to 10,000 inputs and 384 faces. Geometry
revisions advance only after validated skin batches, including ping-pong vertex
buffer reuse. Encoding invalidates the old certificate; only successful queue/error
validation publishes replacement dependencies. Abandoned/failed work therefore
forces a fresh pass rather than trusting possibly stale depth. Atlas removal/
replacement discards the cache. Input/fit/classification/batch planning still runs.

SceneRenderer also retains exact last-validated joint palettes per owner. Unchanged
poses no longer execute GPU skin deformation or invalidate shadow depth during
render/bounds/picking. Changed visible palettes remain one atomic batch and publish
only after successful validation; rebound/hidden/deleted/scoped owners follow the
existing lifecycle. An explicit vector include corrected a first-build failure
from relying on imported standard-library comparison declarations.

Mesh frame statistics expose updated/reused physical page counters through the
ordinary Rendering panel and strict live inspection. Shadow instances/draws now
mean newly submitted work, excluding retained pages. Five durable offscreen cases
cover unchanged/shading-only frames and camera changes, isolated local movement/
removal, expired mask resources and mesh lifetime, abandoned encoding recovery,
and unchanged scene skins across rendering/bounds/picking. Existing all-64-point
packing and native skin/SSAO reference cases additionally cover shared-page
neighbours and repeated buffer use. No unit test creates a device/window.

The first focused run passes sixteen of seventeen cases; its recovery fixture
sheared a world axis that did not skew this top-down camera's image axes, so it did
not reach the intended AO error. Correcting the fixture's image-plane shear makes
the real validation fail as intended and verifies preserved pixels/report plus
fresh-depth recovery. All twenty-four related shadow/scene-skin cases then pass
(32.53s). No production behavior was changed to force a fixture failure.

Actual Release editor Game view retains all seven PrismRelay shadow layers, with
zero new shadow instances/draws instead of the previous 734/246. Its 1480x574 PNG
is byte-identical to the previous result. Agent Play input holds W/Space for twenty
frames (plus its normal release frame); Player moves from [-3,0,2] to approximately
[-3,1.7538,1.3366], with no diagnostics, render error or dropped fixed time. The
sampled animated frame updates six layers/reuses one, submits 211 shadow draws,
and still draws 180 input meshes/34 color batches. Other observed animated frames
reuse three layers. These are actual work counts, not CPU/GPU timing or FPS claims.
Stop restores clean authoring and the exact prior PNG, then all seven layers reuse.
The first native helper incorrectly expected its own actor-summary field in the
raw inspect-play response; it was corrected to scene.entities. That owned process
closed normally, and the complete second workflow succeeds. Both native sessions
have empty stderr and retired control directories; the complete workflow verifies
the native editor exits 0. The first helper exits with its schema mistake after
closing its owned editor during cleanup. No gameplay/scene assets
are changed, and no distribution archive is requested or produced.

Final Debug/Release builds succeed. Full Debug CTest passes 1274/1274 (189.00s).
Final review then strengthens the shared-page regression by lighting a neighbouring
source in green, verifying visible changed pixels before moving the adjacent
zero-intensity source; equality now detects accidental erasure of lit neighbouring
tiles. This test is rebuilt and rechecked in Debug (1/1, 1.27s); production code
is unchanged. Full Release with that strengthened case passes 1274/1274 (20.30s,
assertions disabled). The suites contain 398 unit and 876 integration cases,
including 194 compiler, 138 durable offscreen, 32 native-audio, 28 platform and
20 process cases. Automated suites open no desktop windows. macOS arm64 is the
only executed platform; Windows/Linux source support is not a validation claim.

Reviewed cache publication after submission, rejected-frame recovery, weak resource
lifetime/expiry, count/counter bounds, exact coverage dependencies, skin pose
transactions, shared-page clears and Runtime/viewport/agent consistency. All eight
changed C++ sources/interfaces pass clang-format; whitespace checks pass. No new
dependencies, compiler/linker configuration, serialized layout, test executable or
project asset changes. Removed 33 owned temporary helper/request files, retaining
useful logs/responses and native PNGs in ignored build storage. Existing caches,
archives and untracked user references remain preserved. Finished realistic
character/environment art, measured populated-scene frame costs, streaming and
the overall engine/editor/game objective remain unfinished.

## Renderer CPU/GPU profiling

Added an opt-in renderer profiler as a separate private implementation unit. It
retains at most 120 owned CPU and GPU records, with profiling session/submission
identities, actual adapter/backend, pixel resolution and that submission's work
report. Same-value enablement is a no-op; toggling starts a clean session. CPU wall
intervals cover renderer preparation, encoding and post-encoding submission/
validation, excluding scene loading/skinning/scripts/physics, surface acquisition
and pixel-map waiting. They are not complete application frame costs or CPU usage.

GPU timestamp support is negotiated from adapter capabilities. Four bounded async
query/readback slots sample complete physical-pass start/end pairs and sum them
into shadows, SSAO, HDR surface/sky and tone-mapping categories. Their GPU pipeline
intervals can overlap and are explicitly labelled nonadditive in the UI. The total
is the earliest-start to latest-end envelope, excluding query resolution/copies, GUI
and separate skin work. Each slot permits 384 shadow pages, three AO passes and
two color/post passes: 778 counters and 49,792 logical data-buffer bytes across
four slots, excluding opaque driver query allocation. Unlabelled query sets retain
Dawn's native counter pooling. No per-frame wait is added to obtain timings; busy
slots drop GPU samples explicitly while CPU sampling continues. Late callbacks own
readback state, never a departed profiler/renderer. Disabling cancels mappings and
releases query storage. Empty raw counters, nonmonotonic/overflowing ranges and
readback/capacity failures are observable rather than published as valid data.
Present zero differences remain valid when nonempty counters are quantized.

The Rendering pane's checkbox/history and strict set-profiling/inspect-profile
operations use the same product API. Regular viewport inspection includes latest
records/capabilities; explicit inspection returns bounded sample windows. Session/
serial/nanosecond integers use decimal strings. Profiling remains usable during
transform/animation inspection and never changes document history or scene files.
GUI callbacks may explicitly toggle/read profiling; ordinary renderer reentry
remains rejected. Added four durable offscreen regressions for pixels/pass metadata,
history bounds/rejected frames, pending shutdown/thread/move lifetime and separate
devices. Existing GUI recovery regression now covers the shared toggle operation.

Initial test compilation corrected a member-array deduction error and an explicit
vector include. An initial grouped timer design split begin-only/end-only writes
across physical passes. Tests allowed valid quantized zero durations, but the real
PrismRelay measurement exposed all-zero GPU records, so those measurements were
rejected. One earlier pixel readback was also all zero. Debugger inspection then
captured Metal command-buffer Insufficient Memory/aborted execution and empty raw
counters on this pattern, while complete color/post pairs returned real nonzero
absolute counters without those errors. Counter pooling alone did not fix it.
Explicit transfer-clearing was investigated but removed from the final implementation.
The final design pairs every physical pass and aggregates its ranges; empty raw
records now fail the GPU measurement even if the public map callback succeeds.
No pixel assertion was loosened. A separate-device test's incorrect pure-channel
assumption was replaced by exact independent unprofiled references because the
actual tone mapper mixes channels. Diagnostic logs remain in ignored build storage.

The corrected Release product workflow runs on Apple M4 Pro / Metal, macOS 27.0.1
(build 26A434), at 1480x574, with 180 scene mesh inputs and 34 color batches. Two
120-sample windows report no dropped GPU samples. Static Game view reuses all seven
shadow pages and has no shadow interval; GPU envelope median/p95 is 5.799936/
6.291456ms, with CPU preparation/encoding/submission medians .041230/.288292/
.086209ms. During actual W/Space Play input, GPU envelope median/p95 is 5.373952/
6.160384ms; CPU medians are .041875/.417209/.181604ms, and median new shadow draws
is 211. These are different visible workloads and overlapping GPU intervals, not
a controlled before/after benchmark, FPS claim or whole-game throughput claim.

All original, profiled and stopped authoring PNGs compare byte-for-byte equal to
the previous saved viewport. Actual Play moves Player from [-3,0,2] to approximately
[-3,0,-1.1277], with no Play/render diagnostics or dropped fixed time. Stop restores
a clean revision-one document, and disabling reports zero profiling buffer bytes.
The complete corrected native process exits 0 with empty stderr and retired private
control directory. Earlier prototype zero-timing data is retained only as failure
evidence. One additional helper launch ran a prior binary before rebuilding had
finished; its nonzero-timing gate rejected it, its process was closed and it supplies
no final validation evidence. No native distribution archive was created.

Repeated paired sampling exposed an invalid test assumption that a sum of AO
physical-pass intervals must fit inside the overall GPU range. GPU stages can
overlap. Total duration now uses the minimum observed start and maximum end;
the regression bounds individual single-pass categories and preserves strict
pixel, nonempty-counter and metadata checks. No pixel comparison or failure check
is bypassed. Final local records above use that envelope implementation. Four
profiling cases plus GUI recovery each pass ten consecutive Debug runs (50 actual
executions, 40.49s), followed by the complete suite below. All four owned native
control directories are retired with empty stderr; only corrected native results
are used as current performance evidence.

Final Debug/Release builds succeed. Full Debug CTest passes 1278/1278 (193.77s);
full Release passes 1278/1278 (20.43s, assertions disabled). The suites contain
398 unit and 880 integration cases, including 194 compiler, 142 durable offscreen,
32 native-audio, 28 platform and 20 process cases. Automated suites open no desktop
windows; unit tests initialize no windows, graphics devices or audio devices.
Only macOS arm64 is executed; Windows/Linux source support is not a validation claim.

Reviewed timestamp range/counter bounds, validated-submission publication, pending
callback lifetime, partial allocation/shutdown, thread/move guards, unsupported-device
behavior and shared GUI/agent commands. Seven changed C++ sources/interfaces/private
headers pass clang-format; whitespace checks pass. No dependency, compiler/linker,
serialized-layout, project-asset or test-executable changes. Removed 129 owned temporary
helper/request files, preserving measurement JSON, responses, logs and viewport PNGs
in ignored build storage, plus existing caches, archives and untracked user references.
The earlier physical W observation was Chinese IME interception, not evidence of a
movement defect; this stage's movement evidence uses structured product input.
Finished realistic character/environment art, whole-application performance,
resource streaming and the overall engine/editor/game objective remain unfinished.

## Linked field restoration through Prism production

Added complete top-level property counts to native override inspection, independent
of pagination and excluding added/deleted-node entries. The entity Inspector marks
applied Transform/component overrides and provides Restore field beside the owning
fields, including Name, Parent and removed components. Indicators query the selected
entity independently of the lower whole-instance list and exclude unapplied drafts.
Stable widget identities retain collapse state when markers change.

The strict revert-scene-field document edit and GUI share native
prepareSceneFieldRevert. It restores one whole field from the stored, target-remapped
baseline without source I/O or new entity identities. Complete script properties
can be restored beyond the selected-key operation's 256-key limit. Other fields,
placement, selection and allocation reservations remain. Invalid ownership/fields,
stale revisions and invalid final native dependencies reject atomically. Unchanged
fields preserve document scope/revision/history. No shared material/clip resource or
source file is implicitly edited.

Four new CPU-only cases cover pagination-independent counts, over-256 property
restoration, independently owned results, removed/added components, unchanged fields,
foreign/missing/deleted/local entities, foreign threads, invalid restored parents,
remapped skin/animation references and demoted camera/listener flags. Two document
cases cover strict schemas, missing/wrong/extra fields, stale revisions, later-batch
failure, selection/history/no-op/save/reopen and restoration with the source absent.
An initial test compared expected wrappers instead of their successful owned values;
that test compilation error was corrected without changing production behavior.

Used the actual Release editor to replace all twelve PrismRelay straight ducts and
fourteen supports with links to their existing scene assets. The 341 authored
entities retain exactly the same names, transforms, components and hierarchy after
remapping 116 placement/source values. The reserved main floor advances from 472 to
588. Native saved links contain the real source baselines/mappings/namespaces; no
handwritten provenance or direct scene-file replacement was used. Baseline, linked
and reopened 1480x574 Game PNGs are byte-identical, SHA-256
c345f04406d3defdfa323c9e357adfcbc7ef85a74b2c81879f4a458f6ae23fbe.

Actual Inspector staging keeps field counts empty before Apply. Applying a local
name and transform produces both counts; restoring Transform retains the local name,
Undo/Redo restores each state, and a pending name draft blocks restoration without
mutation. Restoring the remaining field returns the document clean. Open/save of
the straight source, refresh of all twelve placements, preservation of a local
name and restoration to the newly refreshed baseline succeed. Returning the source
name restores all authored entities. Source save legitimately publishes the
Document's reserved allocation floor (355 to 588), and linked namespace observations
advance accordingly; that metadata change is retained rather than regressed. The
first helper confused highest identity 471 with entity count 341 and was corrected
before saving; another equality gate was corrected to account explicitly for these
monotonic namespace floors. Actual data/pixel checks remain strict.

Computer use inspects the real field markers, controls and final authored scene.
Simulated button clicks retain an unrelated SDL pointer coordinate, including after
using a development bundle with its own copied executable and standard resources.
They do not establish native mouse activation of Restore field. Product JSON
restoration is verified; no fake GUI input or test-only hook substitutes for it.
One close-response helper races retirement of its temporary mailbox and times out;
the native editor nevertheless exits zero with the control directory removed.
Subsequent close checks retain actual queued receipts and verify owned process exit
instead of claiming an unread applied response.

Created an independent Prism project through project-action, then used its actual
native workspace to import the existing licensed utility-cabinet glTF, add a linked
placement, edit/save its own PBR material, author a point light and child static
collider, check scripts, save, close and reopen. The reopened scene equals the
saved scene, with 346 entities and 27 links. Structured W/Space Play input moves
Player from [-3,0,2] to [-3,1.4705257,1.3894497]; observed height reaches 1.4705257.
An external player.ts edit visibly renames that same runtime entity after hot reload
(frame 75), proving execution of the new source. Original bytes are restored,
movement continues and the restored on-disk project type-checks. Stop retains the
clean authoring scene and exact pre-Play PNG. No Play diagnostics/errors or dropped
fixed time are recorded in this session.

Workspace native export succeeds. All 116 exported content files (45,988,267 bytes)
match the saved authored project byte-for-byte; compiled modules contain no temporary
reload marker. Moved the native export and moved its authoring directory away before
running the actual DotGame for 180 frames; it exits zero. This run reports one frame
exceeding one second and 5,099,999,796ns of dropped fixed updates. A subsequent two-
frame run exits zero in 1.0711s and still reports 466,666,648ns dropped. These expose
startup/frame stalls, not stable throughput; their exact loading/pipeline/scheduling
costs remain unlocated. No warning was suppressed or timing bound loosened. The
native export is temporary validation output, not a new editor distribution/archive.

Debug/Release configure and final builds succeed. Full Debug CTest passes 1284/1284
(201.06s); full Release/NDEBUG passes 1284/1284 (21.11s). The suites contain 402 unit
and 882 integration cases, including 194 compiler, 142 offscreen, 32 native-audio,
28 platform and 20 process cases (labels overlap). Focused native/document/example
checks pass 20/20 in Debug (1.41s). Automatic suites open no desktop windows; native
product workflows above are separate deliberate checks. Only macOS arm64 is tested.

Reviewed the complete code/data diff for ownership, failure propagation, field and
page boundaries, allocation monotonicity, remapped native references, atomic history,
GUI draft guards/cache identity and platform assumptions. All six changed C++ files
pass clang-format and whitespace checks pass. No dependency, compiler/linker setting,
serialized format, compatibility loader or test executable is added. Updated stale
README, FeatureLab, EditorDesign, Delivery and roadmap summaries against current
capabilities. Useful logs, raw responses, PNGs and result.json remain under ignored
build/prism-workflow; obsolete owned helpers/requests and temporary native export
are removed. Existing build caches, prior archives and untracked user references
are preserved. Finished realistic sci-fi art, native pointer acceptance, measured
startup/whole-application costs, background loading/compilation and overall engine/
editor/game production goals remain unfinished.

## Native application frame diagnosis

Added independent opt-in Application CPU wall sampling to the native loop. It owns
120 recent completed steps plus the greatest measured step and greatest supplied
input interval, so a cold first draw and the following catch-up/drop observation
survive history eviction. Events/close handlers, prepare/input, fixed callbacks,
frame/variable updates and rendering are distinct nonoverlapping phases; absent
callbacks remain null. Actual frame metadata and callback errors are retained.
These intervals exclude process initialization, shutdown, idle delay and history
publication, and include resource/GUI/driver waits inside callbacks. They are not
GPU execution, process CPU utilization or FPS.

Same-value switches preserve history, disable releases it and reenable starts a
fresh monotonic session. A callback may toggle without leaving a borrowed profiler
pointer: the interrupted sample is discarded. Invalid/moved/foreign-thread calls
fail; successful snapshots own all records and errors. One shared native encoder
preserves decimal-string identities/counts/nanoseconds, null phases and compact
latest/summary output. Renderer CPU/GPU sampling remains independently controlled.

Window/Rendering and strict set-application-profiling share Application's API.
inspect-profile adds the full application history; ordinary workspace inspection
adds its compact state. Real product validation exposed that the editor advanced
Play inside its render callback, unlike the standalone player's separate callbacks.
Added an exact FrameState update callback after fixed updates and before variable
update/render, and moved editor Play/audition advancement there. It receives the
same nanosecond duration and preserves advancement order without a double-seconds
round trip. The UI groups game/animation CPU intervals and shows a full-width graph;
Core fixed-clock metadata remains distinct from Play's independent fixed counters.

DotRuntime and exported DotGame accept optional --profile files using the same
encoder and atomic filesystem service. Argument/duplicate/value checks precede
native window/device work. Missing parents/directories/special destinations reject;
an existing regular diagnostic file is explicitly replaced. New option paths use
UTF-8 filesystem construction. Saving diagnostics never changes a failed game into
a successful one; a diagnostic write failure is reported. No catch-up cap/warning
or simulation input was suppressed to make the measurements look better.

Added one CPU-only exact-JSON case, five dummy-display application cases and one
CLI rejection case in existing test targets. Coverage includes phase order and
bounds, real catch-up metadata, absent/paused callbacks, failures/standard/unknown
exceptions and close-handler failure, ownership/thread/move lifetime, repeated and
in-callback toggles, bounded history and retained old maxima, exact nanosecond frame
updates and stop/failure skipping, high 64-bit values, strict flags and unavailable
outputs without opening desktop windows. One initial test forgot a retained 5ms
fixed-clock remainder and expected 70ms dropped instead of the actual 80ms; the
assertion was corrected while production timing stayed unchanged.

Actual Release editor toggles application sampling, rejects three malformed requests,
retains same-value sessions, runs W/Space Play input and independently toggles mesh
sampling. Player moves from [-3,0,2] to [-3,1.4705257,1.3672982], without Play errors,
diagnostics or dropped fixed time. Stop retains revision one and clean authoring.
Before/profiled/stopped 1480x574 PNGs remain byte-identical to the established Prism
reference (SHA-256 c345f04406d3defdfa323c9e357adfcbc7ef85a74b2c81879f4a458f6ae23fbe).
Computer use inspects actual live phase values and the corrected full-width graph.
An initial helper ran before readiness and made no request; it was corrected to
observe the flushed control JSON. Its first Play gate incorrectly assumed a Core
fixed callback in the workspace; that failure exposed the real integration boundary
above. Final evidence uses the corrected exact-frame integration, not that attempt.
All owned successful editor sessions exit zero with empty stderr and retired controls.

Three serial Release project runs, each 180 actual frames without competing tests
or builds, retain cold frame zero. Its rendering costs are 511.411375/489.767750/
493.216083ms (median 493.216083); whole-step costs are 540.684750/516.165709/
519.313250ms (median 519.313250). Following input intervals are 541.820875/
517.303834/520.447250ms, with 399.999984/383.333318/383.333318ms dropped. These
successive local runs include warmed filesystem/driver context, not cold-cache
latency guarantees or game FPS. A delayed native system sample identifies PNG/JPEG
decode, texture upload's repeated sRGB pow calls and skin GPU readback waits inside
first rendering; it is call-stack evidence, not a sampled-duration benchmark. An
earlier immediate sample mostly observes OS/window startup outside step.

Temporary native export, relocated playback and a UTF-8 diagnostic filename succeed.
The exported player runs 180 frames, exits zero and owns 120 recent records plus the
old frame-zero/frame-one summaries. That first relocated-player observation records
4995.572083ms rendering, 5014.797083ms step cost and the next 5015.936542ms interval
with 4866.666472ms dropped. It is a distinct startup workload/cache context from the
project runs, not an optimization comparison. The diagnostics make the previously
unlocated render stall observable; background loading/pipeline work is still needed.
Temporary native export is removed after preserving its manifests/results; no editor
distribution archive is created. Independent character-source research remains in
ignored build storage only; no unrigged candidate is substituted for the animated
Prism character or claimed as finished realistic art.

Debug/Release builds and full suites pass 1291/1291: Debug 197.01s, Release/NDEBUG
21.34s. There are 403 unit and 888 integration cases, including 195 compiler, 142
offscreen, 33 platform, 32 native-audio and 20 process cases (labels overlap).
After the final graph/text-only adjustment, relevant application/GUI checks pass
in both configurations and the actual native panel is inspected again. Automated
checks open no desktop windows; product sessions above are deliberate checks.
Only macOS arm64 is executed. No dependency, compiler/linker setting, installed
header tree, scene format, example content or acceptance executable is introduced.
Reviewed event/update ordering, exact clocks, bounded records, failure publication,
thread/move lifetime, callback toggles, CLI I/O and common GUI/agent paths. Eleven
changed C++ files pass clang-format; whitespace checks pass. Useful logs, profiles,
raw responses and PNGs remain in build/startup-investigation. Full engine/editor/
game goals remain open; the next measured work is texture preparation, followed by
remaining loading/compilation, production tools and finished Prism content.

## Exact CPU texture mip preparation

The first-render sample above identified repeated RGBA8 sRGB decode pow calls during
texture preparation. Extracted the existing integer-bin mip algorithm into the
validated CPU-only downsampleImage operation and replaced input transfer calls with
an immutable 256-double table, initialized once safely across threads. Summation
order, linear alpha, inverse transfer, rounding, odd-edge coverage and floor-half
sizes are unchanged. A 1x1 input returns an independent copy; malformed images and
unknown color spaces fail before allocation. Exceptions retain expected error context.
Renderer upload borrows level zero for the queue copy and owns subsequent levels,
eliminating its initial full source-pixel copy without retaining caller storage.
GPU residency budgets, samplers, formats, shaders and resource accounting are unchanged.

Four durable CPU cases in the existing unit executable compare all 65,536 byte pairs
against direct transfer-function arithmetic, exercise linear/sRGB complete chains,
odd and single-axis sizes, maximum width, 1x1 ownership, invalid dimensions/storage/
color spaces, recovery and concurrent independent results. Existing offscreen texture
sampling/ownership/lifetime cases continue to pass. Debug and Release configure/builds
succeed with the existing toolchain environment. Full Debug passes 1295/1295 (197.55s);
Release/NDEBUG passes 1295/1295 (21.11s). Counts are 407 unit and 888 integration,
including 195 compiler, 142 offscreen, 33 platform, 32 audio-native and 20 process
cases (overlapping labels). Only macOS arm64 is executed.

Three serial 180-frame Release project runs use the same project, compiler, command
and profiling mode as the preceding baseline, with no competing owned builds/tests
or editors. First-render costs are 343.369125/324.242625/326.233916ms; median falls
from 493.216083 to 326.233916ms (33.8558%). Whole-step costs are 381.063125/
346.044708/348.723125ms; median falls from 519.313250 to 348.723125ms (32.8492%).
Following dropped fixed time is 233.333324/199.999992/199.999992ms. These successive
local runs retain warmed filesystem/driver context; they are not cold-cache guarantees,
steady FPS, or proof that startup stalls are solved. No clock cap or warning changes.

Actual Release editor runs W/Space for 30 fixed steps: Player moves from [-3,0,2]
to [-3,1.4705257,1.3672982], with no Play diagnostics/error/dropped time. Independent
application/renderer toggles and strict malformed requests still behave correctly.
Stop retains revision one and the clean authoring scene. Before/profiled/stopped
1480x574 PNGs are byte-identical to the established reference, SHA-256
c345f04406d3defdfa323c9e357adfcbc7ef85a74b2c81879f4a458f6ae23fbe. Computer use
inspects the actual native scene and diagnostic pane; the owned editor exits zero
with empty stderr and retires its control directory.

Native export and relocated 180-frame DotGame playback succeed. All 108 exported
content files (39,020,486 bytes) match the current source bytes. The first relocated
player still records 4908.594458ms rendering, 4927.414958ms whole-step time and
4783.333142ms dropped fixed time, with the explicit one-second/drop warnings.
This distinct startup problem remains open; the project improvement cannot be
substituted for a native-export improvement. The temporary unpacked export is kept
in ignored build storage for immediate diagnosis, not distributed as an editor release.
Profiles, logs, content hashes, raw editor responses and PNGs remain under
build/startup-investigation. Source-character/DCC research is also ignored; no candidate
art replaces the functional animated character in this stage.

Reviewed allocation/error paths, immutable table initialization, integer/index bounds,
source/generated-level lifetimes and exact color arithmetic. Four changed C++ files
pass clang-format and whitespace checks pass. No dependencies, compiler/linker settings,
serialized formats, example content, compatibility paths or new executables are added.
Production-scale loading/compilation, editor tools, finished Prism art and the overall
engine/editor/game goal remain unfinished.

## Native named entity lookup and Play diagnostics

Real editor import/animation review of a rigged maintenance-avatar candidate exposed
an authoring-order-dependent startup failure. The original 341-entity Prism scene
places Surveyor at sorted index 150; the isolated 342-entity candidate places it at
index 272. RelayPlayer enumerated wrappers and read each name until it found the
role, crossing the unchanged 256-host-call limit in the latter scene. The editor
returned only a generic realm fault and discarded the failed startup's useful cause.

Added Scene::findEntities and read-only entity.findNamed / scene.findEntities(name).
They scan native Identity data without copying unrelated components, return every
exact name match as owned creation-ID-sorted handles, and return [] when absent.
Names use the existing 1..256-byte, valid UTF-8/no-NUL contract. Owner/move/liveness
rules remain the scene's ordinary rules; rename and deletion are observed immediately.
No index cache or persistent references are introduced. Prism requires exactly one
Surveyor and resolves it through this operation. FeatureLab executes the same SDK
method and declares it in its coverage inventory; the existing example lifecycle
regression uses the new lookup for its named authored roles. Budgets are unchanged.

Play startup now copies the first script failure's entity and diagnostic before
closing/releasing a faulted candidate. Runtime realm errors include that same cause
while retaining the structured frame reports. Ordinary component-failure semantics,
cleanup and authoring isolation are unchanged. No new command or diagnostic fallback.
Two CPU scene cases cover exact Unicode/case, duplicates/order, owned snapshots,
rename/deletion, maximum/invalid names, foreign thread and moved lifetime. A strict
read-only bridge case and actual TypeScript case find two late duplicates after 1000
unrelated entities with a 16-host-call budget. A startup-fault case checks the actual
execution-limit cause, temporary save cleanup, compiler generations and successful
retry; the runtime-fault case also checks its original cause survives.

Initial full Debug passes 1299/1300: the SDK inventory correctly detects that the new
FeatureLab audit call was missing from coverage.json. Added the inventory entry,
rebuilt the bundled templates and passed all nine FeatureLab cases, then reran the
complete suites. Final Debug passes 1300/1300 (196.00s); Release/NDEBUG passes
1300/1300 (21.28s): 409 unit, 891 integration, including 197 compiler, 142 offscreen,
33 platform, 32 audio-native and 20 process cases (overlapping labels). Only macOS
arm64 is executed. Nine changed C++ files pass clang-format and whitespace checks.
Reviewed query allocation/error paths, exact name/ID bounds, read-only classification,
report ownership before teardown, move/thread behavior, SDK/template synchronization
and unchanged runtime budgets. No dependency/compiler/linker/format changes or new
acceptance executable are introduced.

Actual editor first reproduces the old lookup and returns: Start play script realm
faulted; entity 7: Method player.js::RelayPlayer[1].onStart: Script host-call limit
exceeded. Restoring the new lookup in that same project starts Play successfully.
A Debug-only helper initially assumed 30 rendered input frames equalled 30 fixed
updates; actual inspection records 264 fixed updates across 33 frames, so its late
snapshot misses the already completed jump. This is a validation timing assumption,
not evidence of an input failure. No clock or update bounds are changed. The owned
Debug sample records 12 frames with median step/game+animation/render costs of
163.403/85.134/75.561ms, including native work and waits, not just script execution.

Release validation instead observes actual states after one W/Space input frame.
Player rises from [-3,0,2], is captured airborne at [-3,0.8501253,1.9058127], reaches
an observed 1.7807757m and lands near [-3,0,1.8079216]. No Play diagnostics/errors;
Stop restores the clean saved authoring scene and byte-identical before/after PNGs.
The separate 16-sample Release observation has median step/game+animation/render
costs 7.176480/0.780542/6.094459ms. Different configurations/workloads are not an
optimization comparison or a production FPS guarantee. Candidate art, seven native
clip previews and DCC source remain ignored research, not shipped Prism content.

Actual Release FeatureLab executes 134 checks including scene.findEntities and its
game-data checks, with no Play diagnostics/error and a clean unchanged Stop. Owned
editors exit zero with empty stderr and retired controls; terminal close is verified
by process exit as documented. Original Prism exports with the new SDK and the
relocated native player exits zero. First render still costs 4876.151875ms and drops
4749.999810ms, with the explicit warnings: the separately sampled synchronous Metal
environment-pipeline compilation remains unresolved. Temporary native export is
removed after preserving manifests/results. Useful raw evidence stays in ignored
build/character-research, build/named-query-feature-lab and build/named-query-* logs.
The complete engine/editor/game goal, production loading/compilation and final Prism
visual quality remain unfinished.

## Textured Prism maintenance character workflow

Replaced Prism's functional mannequin rendering with an editable blue maintenance
armor/closed-helmet character, adapted from Irondust's CC0 Sci-fi soldier. Kept
Body, Head1 and HandSimple; no weapon or alternate heads are used. The real
63-joint rig retains all positive source influences, up to eight per vertex,
across three skinned parts. Four 2048px images provide sRGB base/emission and
linear normal/packed occlusion-roughness-metallic data. Source specular/glossiness
is converted to metallic/roughness during external authoring; the source emission
factor 1.3 is retained. Provenance, licenses and original/adapted hashes are recorded
in the project. The old CC0 Quaternius source remains a named animation donor,
not an alternate runtime path. No engine dependency or authoring executable is added.

Blender 4.5.14 LTS, confined to ignored build storage, transfers the seven existing
idle/walk/jog/sprint/jump-start/airborne/landing clips to the target's corrected rest
pose. Per-bone roll corrections and target leg-proportion/contact adjustments are
baked into ordinary animation tracks. This is external content authoring, not runtime
IK or a procedural skeleton. An initial export retained only four influences and
omitted a viewport-hidden helmet; both are corrected before adoption. Final GLB
contains all three parts and eight-slot influences, is 15,711,772 bytes and has SHA-256
d57844c3205a3d00f9815f7dd1c2dded758c9fb9bff38ae7866b040c9bc373d5.
The editable GLB, native resources and inputs/licenses remain with the example;
working DCC scripts/files stay ignored and are not required to open/run the project.

Actual Release editor operations import the GLB, instantiate the native source,
parent the animated Surveyor under Player, normalize height, preserve facing and
apply the native preview's idle joint transforms through ordinary document edits.
The final scene has 342 entities. All other 272 entity records and all 26 linked
placements are unchanged. After correcting influence export, the same product
importer regenerates the native bundle without changing scene bindings. Owned
editors exit zero and retire controls. Temporary investigation code is not promoted
into product tooling or acceptance executables.

The example regression checks the unique animated role, seven named valid sampled
clips, three 63-joint skins, vertex counts, retained extended weights, all five PBR
texture slots and four unique 2K images. Existing surface checks now cover the
textured character as well as 1K industrial inputs. Focused Debug passes 14/14
(9.57s). Complete Debug passes 1301/1301 (193.23s); Release/NDEBUG passes 1301/1301
(21.76s): 409 unit and 892 integration, with overlapping labels including 198 compiler,
142 offscreen, 33 platform, 32 audio-native and 20 process cases. Only macOS arm64
is executed. Changed C++ formatting and whitespace checks pass.

Created Prism Maintenance Review from the actual bundled Prism template: the new
GLB is byte-identical, with all native parts/images/licenses present. The actual
editor previews and captures all seven clips, ends preview, saves cleanly, closes
and reopens the project. Release Play then observes the Player airborne at
[-3,0.8501253,2], reaches an observed 1.7851506m, and lands near [-3,0,2], with no
Play diagnostics/error. One W/Space frame proves jump operation here; this sample
does not establish forward travel. Stop retains the exact clean authoring scene.
Author/stopped PNGs are byte-identical, SHA-256
77b56bcc914309b96692e3a34313f91eaa84e46152493019562592b57b353695.
The 12 retained application samples have median total step 7.103771ms; this short
observation is not a controlled optimization comparison or game-scale FPS claim.

Export from the real reopened editor succeeds. The 115 exported content files total
66,210,361 bytes. All but main scene match source bytes; main scene has one quaternion
normalization difference of 3.7252903e-9, with identical structure, other values and
linked records. Relocated DotGame runs 180 sampled frames from an unrelated directory
while its owned temporary authoring project is renamed away; it exits zero. All
120 retained samples are unpaused/error-free. Initial render still costs 5132.842875ms,
whole step 5149.062292ms, dropping 5016.666466ms fixed time on the following step.
The explicit one-second/drop warnings remain. The later retained median step is
11.579854ms; this does not resolve or hide the cold startup stall. Temporary native
export is removed after preserving its manifests, hashes, profiles and results.

Mac computer use reports the desktop locked during final review, so these new asset
checks use the real editor's strict operations and native viewport PNGs; they do not
claim a fresh human mouse/keyboard acceptance run. No UI behavior changes in this
stage. Evidence stays under build/character-research and build/surveyor-* logs.
Reviewed source licensing, color-space/packed-channel meaning, complete skin weights,
clip order/names, scene ownership, template propagation, unrelated scene/link stability
and export independence. No renderer/runtime budgets, dependencies, clock limits,
compatibility paths or distribution archives change. Final realistic art direction,
cohesive environment, background loading/compilation, large-game evidence and the
overall engine/editor/game objective remain unfinished.

## Asynchronous environment shader preparation

The sampled relocated-player stall motivates explicit preparation before gameplay.
Renderer::prepareEnvironmentPipeline starts one actual Dawn asynchronous compute
pipeline and polls its future with zero wait. Pending is false, reusable is true;
callback/timeout failures retain context rather than publishing a map. The pending
poll has a ten-second bound. Callback state owns its lifetime independently; device
teardown may finish outstanding driver work. The same pipeline is reused by explicit
synchronous bakeEnvironment, retaining its HDR/image/budget validation, GPU completion
and transactional map publication. Pipeline storage is internal renderer state, not
fabricated environment residency. HDR decoding, source mip/upload work, GPU bake
completion and all other pipeline groups remain synchronous.

SceneRenderer::prepareEnvironment only prepares when the scene needs an environment;
it changes no files, images, maps, lighting bindings or resource-cache entries.
Workspace polls between mailbox/event handling and drawing. Its Scene message and
strict workspace.view.loading = "environment-pipeline" distinguish pending from
error and completed pixels. Capture/pick cannot use a pending view. Play checks the
current document's environment and a pure renderer readiness query, including during
GUI layout, rather than a stale previous-frame loading flag. This closes the reviewed
same-frame add-environment/Play boundary. Mailbox close precedes new preparation work.

The native player keeps event/clear-surface presentation active while compiling,
names the preparation phase in its title/stderr, and invokes script Start/game
updates only after this pipeline is ready. Loading application steps remain profiled
but do not consume a finite game render-step budget. Application fixed-callback counts
are not represented as gameplay work before startup. Clock catch-up limits, delta
caps and warnings are unchanged. Removed the obsolete private acceptance-loop driver
and its imports; no current caller uses it and no new probe executable replaces it.

Three durable offscreen regressions cover preparation/reuse with real HDR output,
zero premature map/binding publication, wrong threads/moves, abandoned jobs and
scene preparation without image I/O/cache mutation. Existing GUI coverage checks
that the pure readiness query works during layout while starting/polling work is
rejected as reentry. Initial focused Debug passes 14/14 (11.63s); review-added query/
GUI checks pass 15/15 (12.19s). Initial complete Debug passes 1304/1304 (200.21s) and
Release/NDEBUG 1304/1304 (23.66s). Final full results after the readiness/mailbox-order
review are recorded below. Tests create no desktop windows; no sanitizer, dependency,
compiler/linker environment, content format or distribution archive changes.

Actual Release editor opens the adopted Prism template, reaches the same completed
PNG as the character-stage baseline (SHA-256
77b56bcc914309b96692e3a34313f91eaa84e46152493019562592b57b353695), runs W/Space and
observes Player [-3,1.4705257,1.3672982] after 31 fixed steps/39 rendered frames.
No Play diagnostics/error/dropped time; Stop restores the exact clean scene. A fresh
owned app-path run of the reviewed binary also reaches identical pixels, starts and
stops Play cleanly, exports through the live editor and exits zero with empty stderr
and retired controls. It does not capture the short environment-pending UI interval:
its first inspected view is already ready, and that request waits 4.7144145s behind
later synchronous work. Application diagnostics retain a 4678.859375ms render step.
Thus this evidence does not establish complete editor startup responsiveness or a
fresh native human mouse/keyboard run; the desktop remains locked to computer use.

The final 115-file native content export totals 66,210,361 bytes. All source files
match bytes except one normalized main-scene quaternion component (3.7252903e-9);
structure, other values and linked records agree. DotGame runs from a relocated path
with its owned authoring project renamed away, exits zero, and samples 229 application
steps: 49 loading steps plus the requested 180 game render steps. All 120 retained
samples are unpaused/error-free. First game rendering still takes 4743.441875ms,
whole step 4778.406584ms, followed by 4633.333148ms dropped fixed time and the explicit
warnings. Later retained median step is 11.5874375ms, not an optimization/FPS claim.
A preceding renamed run renders its slowest game frame in 696.001375ms; directory
relocation does not guarantee the same driver-cache context or a matched comparison.

A three-second sample of the final slow run now catches main-thread
ensureShadowPipelines -> Dawn APICreateRenderPipeline -> Metal newLibraryWithSource
for the group's three pipeline creations, after environment-compute preparation.
These are call-stack observations, not timing percentages or proof that all remaining
cost belongs to shadows. The next measured target is asynchronous shadow-pipeline
preparation, followed by renewed measurement of other synchronous stages. Cold startup,
general background loading/compilation, populated-game readiness and the overall
engine/editor/game goal remain unfinished. Raw responses, PNGs, profiles, manifests,
call stacks and diagnosis stay under build/environment-loading-review; temporary
native packages are removed after preserving evidence. Thirteen completed asset
investigation helpers are retired while useful authoring inputs/results remain.

Final reviewed builds succeed. Complete Debug passes 1304/1304 (199.03s);
Release/NDEBUG passes 1304/1304 (23.64s): 409 unit, 895 integration with overlapping
198 compiler, 145 offscreen, 33 platform, 32 audio-native and 20 process cases.
Nine changed C++ files pass clang-format; whitespace checks pass. Reviewed future/
callback ownership, retained errors, device/thread/move boundaries, no partial map
publication, existing bake budgets/output, strict pending-state/capture/Play handling,
mailbox-first close, session cleanup and frame-budget/profile meaning. Only macOS
arm64 is executed. Full passing suites do not resolve the recorded cold waits.

## Asynchronous shadow pipeline group

The previous slow native sample identifies three main-thread shadow render-pipeline
creations. Added a bounded asynchronous group for direct-lit shadows, environment-lit
shadows and depth coverage. All three owned futures/results must complete before
publishing the group; preparation allocates no shadow maps, changes no light bindings
and renders no frame. The ordinary synchronous draw path waits on those same jobs.
Surface and depth descriptor construction is shared, preserving shader text, vertex/
blend/depth state and raster behavior. Compute and render compilation now share one
callback-owned job with zero-wait polling, retained contextual failures and a remaining
pending-time budget of ten seconds; later synchronous waits use that remaining time.
No custom renderer thread, dependency, compatibility path or probe executable is added.

SceneRenderer preparePipelines/pipelineReadiness replace the environment-only scene
operation. They examine actual environment/enabled-shadow-light requirements and
report Ready/Environment/Shadows. Pure reads are allowed during GUI layout and do
not start/poll work. Ready shadow groups avoid further entity enumeration. Workspace
and native startup use this same implementation; Play checks current document data,
including same-frame added lights. workspace.view.loading distinguishes environment-
pipeline and shadow-pipelines. Native title/stderr report phase changes before Start
callbacks/game updates; the finite game-frame budget still excludes loading steps.

Viewport preparation failures now retain the error/unready state and allow editor
save/close operations while the GUI device remains available; they do not become
fake completed images or successful Play. Device/GUI failure can still stop the loop.
Descriptor validation, other pipeline families, HDR/image work, uploads and GPU bake
completion remain synchronous. This does not implement general resource streaming.

Three new offscreen cases verify real caster occlusion after preparation, no premature
map/target publication, reuse, foreign threads/moves, abandoned compilation, current
light enable/remove/add requirements, GUI-safe pure reads and no image I/O/cache
mutation. Existing HDR and GUI reentry cases use the shared scene preparation API.
Focused Debug passes 38/38 (33.08s). Initial builds reject a shadowed member name and
an omitted direct Core.Result import in the new test; both are corrected without
weakening warnings or changing the caller's compiler/linker settings.

The first full Debug run passes 1306/1307 (197.79s): Feature Lab audit onUpdate hits
the unchanged 100ms execution limit in its character/input/hot-reload case. That run
incorrectly overlaps a Release build. The same case then passes ten isolated runs
(21.37s); a full rerun without parallel building passes 1307/1307 (194.84s). This is
consistent with load-sensitive wall-time enforcement, not proof of a new runtime
cause or a reason to weaken the guard. Final Release/NDEBUG passes 1307/1307 (26.53s).
Counts: 409 unit, 898 integration, with overlapping 198 compiler, 148 offscreen,
33 platform, 32 audio-native and 20 process cases. Only macOS arm64 is executed.

Actual Release editor still starts/stops Play cleanly. Observed W/Space positions
include [-3,1.3340256,1.4826196] and [-3,1.5105758,1.3173810], with no Play error or
diagnostics. Before/stopped/captured pixels retain the character-stage SHA-256
77b56bcc914309b96692e3a34313f91eaa84e46152493019562592b57b353695. An early probe
observes environment-pipeline, ready=false/error=null, and an actual pending Play
rejection. Its screenshot assertion incorrectly expected immediate rejection:
capture-viewport correctly acknowledges queued work, not owned pixels. The corrected
warm probes do not observe that interval, so they do not establish a pending-shadow
capture terminal result. Later real missing-camera recovery confirms queued capture
fails with no completed frame and saving those failed pixels writes no file.

Native export/relocation with the owned authoring project renamed away succeeds:
115 content files, 66,210,361 bytes, with unchanged source bytes except the previously
observed single 3.7252903e-9 scene quaternion normalization difference. Native logs
both environment lighting and shadow shaders before starting the game. It records
183 application steps (three loading plus 180 game render steps), exits zero and
retains 120 unpaused/error-free samples. This run's first game render is 629.010958ms,
whole step 662.261000ms; 533.333312ms fixed time is still dropped with the explicit
warning. Retained median step is 12.393271ms. Cache contexts differ from the old slow
run, so these are observations, not a matched speedup/FPS claim. The sampler catches
ordinary skin-status waits; it does not reproduce the previous long shadow compile.

One editor run still has a 3468.446ms synchronous render / 3.498870s inspect request;
a subsequent sampled run has a 590.727083ms render / 0.630941s request. The latter
sample does not reproduce the cold wait. Other synchronous pipeline/resource work
remains to be prepared/measured; general editor startup responsiveness is unproven.
Computer use again reports the Mac locked. No fresh native human interaction is
claimed. Raw requests/responses, actual PNGs, profiles, native samples and manifests
remain under build/shadow-loading-review; verified temporary native output is removed.

The real missing-camera/save/undo/recovery workflow exposes a separate persistence
bug: Save after Undo falsely reports the file changed externally. The protected
restored scene is saved through Save as to the owned project's
scenes/surveyor-recovery.dotscene; its PNG matches the original bytes and the owned
editor exits cleanly. Comparing the actual save response with a fresh disk load
finds exactly one float quaternion component changing by 3.7252903e-9; allocation,
other components and links agree. Repeated normalization during loading is the next
concrete investigation. The owned temporary startup scene currently lacks its camera
from the deliberate recovery probe; repair it through product operations after that
fix. The formal source example is unchanged. Do not treat this recovery loop or the
overall engine/editor/game objective as complete.

## Stable float-unit scene rotations

The missing-camera recovery failure is reproducible without a window: after writing
one actual rounded joint quaternion, even an unchanged second Document save reports
an external change. Repeated scene codec round trips move its small component by one
ULP each time and change the world matrix. Both new regressions fail before the fix;
raw logs remain under build/rotation-persistence-repro*. This is repeated float
normalization, not a real filesystem conflict or metadata/allocation mismatch.

Scene's shared transactional transform boundary now preserves finite float-unit
quaternion components exactly when their double-evaluated squared length is within
two float epsilons of one. Other finite nonzero inputs still normalize; matrix math
continues to normalize. Math::normalized and the file format are unchanged. No
external-conflict comparison tolerance is widened and no post-write reload/allocation
is introduced. The scene contract and format documentation state the storage rule.

Three durable cases cover exact positive/negative rotations through sixteen codec
round trips and identical matrices/bytes; unchanged saves plus eight edit/save/undo/
save/reopen rounds; genuine external scene-name and one-ULP rotation edits preserving
both sides on rejection; extreme/denormal nonunit inputs; and zero/nonfinite rejection
without mutation. Units initialize no windows/devices. Focused Debug passes 7/7
(0.42s). Full builds succeed; complete Debug passes 1310/1310 (197.90s), then complete
Release/NDEBUG passes 1310/1310 (26.50s), without overlapping builds and tests.
Counts: 410 unit, 900 integration; overlapping 198 compiler, 148 offscreen,
33 platform, 32 audio-native and 20 process cases. Only macOS arm64 is executed.
Five changed C++ files pass clang-format; whitespace checks pass. Reviewed the
roundoff bound, finite/zero/extreme validation, normalized matrix/physics inputs,
transactional scene/history/save behavior, unchanged conflict detection and test
ownership. No dependency/toolchain/linkage/sanitizer changes are made.

Actual Release editor operations restore the owned PrismMaintenance startup camera
from the protected recovery scene, then perform eight name/edit/save/undo/save rounds
and unchanged saves with identical disk bytes. A second missing-camera probe returns
an explicit unready Game view; queued capture ends failed, saving it rejects and
creates no file. Saving the missing camera, Undo and saving again now all succeed.
The restored full scene equals the protected authoring scene. Closing/reopening
preserves it exactly. Restored/recovered/reopened/stopped PNGs are byte-identical to
the previous character baseline, SHA-256
77b56bcc914309b96692e3a34313f91eaa84e46152493019562592b57b353695.
Real W/Space Play observes Player [-3,1.4705257,1.3606409], no Play error/diagnostics,
and clean exact authoring restoration on Stop. Both editors exit zero, empty stderr,
with retired control directories. The owned startup scene is repaired; formal
examples/PrismRelay files are unchanged. Native mouse/keyboard acceptance remains
blocked by the previously reported locked Mac, with no fresh claim of that coverage.

Export now contains 116 files / 66,649,969 bytes, including the protected recovery
scene. Every content file, including the startup scene, matches authoring bytes
exactly. The relocated native product runs 180 game steps with the owned authoring
directory hidden, exits zero and records 307 application samples (127 preparation
plus 180 game steps); the 120 retained frames are unpaused/error-free. Its first game
step still takes 3575.157167ms, with 3540.134417ms in render; the following interval
reports 3433.333196ms of dropped fixed time and the explicit one-second variable cap.
The retained median step is 11.557021ms. This is an observed cold stall, not a matched
performance comparison or proof that general loading is asynchronous. Other pipeline
families/resource work remain the next measured investigation; timing guards and
warnings are unchanged. Raw requests/responses, four PNGs, manifests and profiles
remain under build/rotation-persistence-review. Verified temporary native output is
removed after retaining evidence. Overall engine/editor/game goals remain unfinished.

## Asynchronous SSAO pipeline group

After the rotation repair, another independent native sample reproduces a
3429.286625ms first-game render / 3463.146625ms step. It records 305 application
steps (125 preparation plus 180 game steps), then explicitly drops 3333.333200ms
of fixed time. A six-second process sample places the main thread inside all five
synchronous ensureOcclusionPipelines creations and the environment surface/sky
creations in setEnvironment, reaching Metal newLibraryWithSource. This identifies
concrete remaining creation paths rather than proving all render time is compilation.
Raw before evidence remains under build/startup-stall-review. The first headless
export attempt mistakenly supplies clang as the compiler argument; the product
correctly rejects it because it requires TypeScript 7.0.2, publishing no package.
The corrected export uses the existing bundled typescript-native/tsc.

Added prepareOcclusionPipelines/occlusionPipelinesReady with three render and two
compute jobs: geometry, search, bilateral filter and both environment composition
variants. Every job must succeed before publishing any renderer pipeline handle.
Descriptor/shader construction preserves prior vertex locations, formats, depth,
entry points and blend settings. Synchronous descriptor validation finishes before
asynchronous submission; callback context/results are owned. Explicit rendering
waits on these same jobs with their remaining ten-second deadlines, not fresh
pipeline creation. Retained errors prevent partial readiness. Preparation allocates
no working maps, changes no settings/bindings and renders no pixels.

SceneRenderer checks current SSAO requirements after environment/shadow requirements
and reports Occlusion. GUI-safe reads start/poll no work. Workspace's shared status
adds occlusion-pipelines and the visible ambient-occlusion preparation label; the
existing Play guard reads current requirements. Native loading names the same phase
and defers Start/game advancement until the required groups finish. Mailbox-first
closure, capture ownership, frame budgets, clocks and timing warnings are unchanged.
Environment surface/sky, skin, other pipeline families, images/uploads and GPU bake
completion remain synchronous. This is not a general resource streaming solution.

Three new durable offscreen cases cover current settings with no premature working
maps/target frames, retained real contact occlusion and identical repeated pixels,
reuse after settings removal, moves, foreign threads, abandoned jobs, GUI-safe pure
reads, GUI reentry rejection and current scene add/remove requirements without
asset/cache/binding mutation. Focused Debug passes 17/17 (18.71s), including the
existing environment/shadow cases and CPU SSAO boundaries. A final additional
pending-readiness assertion is covered by the complete suite. Complete Debug passes
1313/1313 (199.60s); Release/NDEBUG passes 1313/1313 (27.32s), without overlapping
builds/tests. Counts: 410 unit, 903 integration; overlapping 198 compiler,
151 offscreen, 33 platform, 32 audio-native and 20 process cases. Builds succeed;
seven changed C++ files pass clang-format and whitespace checks pass. Reviewed
callback/descriptor ownership, atomic publication, sticky errors, remaining budgets,
GUI/thread/device/move checks, scene requirement priority and unchanged shaders.
Only macOS arm64 is executed; no dependency/compiler/linker/sanitizer changes occur.

Actual Release editor observes environment-pipeline, shadow-pipelines and
occlusion-pipelines. During the latter, 18 inspect requests complete with median
51.674ms / maximum 77.984ms client wall time (including CLI/poll overhead). Play
explicitly rejects with pipelines still preparing. A queued capture finishes failed
with no completed frame; saving it rejects and writes no file. The subsequent ready
inspect still takes 1.353386s; application profiling attributes 1303.432625ms to
render. These measurements do not establish general editor responsiveness.
Ready/stopped PNGs retain SHA-256
77b56bcc914309b96692e3a34313f91eaa84e46152493019562592b57b353695.
Real W/Space Play observes [-3,1.4705257,1.3672982], no Play error/diagnostics,
clean exact authoring restoration on Stop and successful Save/export. The editor
exits zero with empty stderr and a retired control directory. Native human input
acceptance remains limited by the previously reported locked Mac; it is not rerun.

The independent export again matches all 116 content files / 66,649,969
bytes exactly while authoring is hidden. The native run logs all three preparation
phases, exits zero after 360 game steps and records 364 steps (four preparation plus
360 game). Its 120 retained samples are unpaused/error-free. First-game render is
558.198667ms, whole step 593.143750ms; the following interval explicitly drops
466.666648ms of fixed time. Retained median step is 11.679021ms. This run follows
editor compilation and has different cache context/frame count from the earlier
cold sample, so no matched speedup/FPS conclusion is drawn. Its main-thread sample
does not reproduce the prior long synchronous SSAO creations; remaining synchronous
work still needs preparation and measurement. Raw requests/responses, PNGs,
profiles, samples, content hashes, runtime/game manifests and executable hashes are
retained under build/occlusion-loading-review. Verified before/after native packages
and the latest owned editor copy are removed. Formal example assets remain unchanged;
overall engine/editor/game goals remain unfinished.

## Asynchronous environment surface and sky

The earlier cold native sample identifies setEnvironment's synchronous environment
surface and sky creations in addition to the already addressed SSAO group. Scene
preparation now starts/polls bake compute plus both render jobs. The public APIs are
prepareEnvironmentPipelines/environmentPipelinesReady, replacing the compute-only
singular names without aliases. Full readiness requires all three; surface and sky
handles publish together. The existing environment-pipeline product phase now covers
that full requirement, so native Start and editor Play cannot bypass pending render
jobs. No new progress percentage, compatibility path or probe executable is added.

Private compute preparation is shared with explicit baking. Bake alone starts/waits
only for compute, so it does not unnecessarily compile surface/sky or imply full
readiness. setEnvironment validates the existing owned map and parameters before
starting/waiting for its shared surface group. Pending jobs retain callback-owned
results/context and use their remaining ten-second budgets; failure retains context
and never replaces lighting bindings with partial success. Descriptor construction
reuses the original surface builder and now also shares sky construction. Shader
text, formats, vertex/depth/blend state, bake output and residency limits are unchanged.
Compute descriptor-validation failures are retained alongside callback/wait failures.

A new durable offscreen case verifies invalid HDR recovery, bake-only partial
readiness, full async preparation with unchanged owned maps/bindings/target frames,
analytic constant-HDR sky pixels and reuse. The existing independent analytic PBR
energy case now explicitly prepares asynchronously; the ordinary binding case still
verifies direct synchronous waiting and cached reuse. Existing moves/threads/abandoned
jobs, GUI-safe reads, current-scene requirements and shadow readiness use the full
API. Initial Debug builds catch a local name shadowing the surface member, then a
missing direct vector header in the new test; both are corrected without weakening
warnings or altering the caller's toolchain. Focused Debug passes 19/19 (20.21s),
plus the actual GUI validation/lifetime case 1/1 (0.49s). Complete Debug passes
1314/1314 (199.57s); Release/NDEBUG passes 1314/1314 (27.57s), with builds/tests
kept sequential. Counts: 410 unit, 904 integration; overlapping 198 compiler,
152 offscreen, 33 platform, 32 audio-native and 20 process cases. Six changed C++
files pass clang-format and whitespace checks pass. Reviewed descriptor/callback
ownership, partial compute readiness, atomic render publication, retained failures,
foreign/moved/thread/GUI checks, binding transactionality and remaining budgets.
Only macOS arm64 is executed; dependency/linker/compiler/sanitizer settings are unchanged.

Actual Release editor's first observed view is already ready: this warm run does
not observe a pending environment/shadow/SSAO interval, so it does not establish new
native pending-capture behavior. The existing pending-state offscreen contracts and
previous real SSAO pending evidence remain distinct. This run still records a
586.026500ms render / 587.370083ms application step, and the next observed inspect
request completes in 49.283ms. No general startup-responsiveness claim follows.
Ready/stopped/reopened PNGs retain SHA-256
77b56bcc914309b96692e3a34313f91eaa84e46152493019562592b57b353695.
Real W/Space Play observes [-3,1.2830756,1.5228012], no Play error/diagnostics and
clean exact authoring restoration on Stop. Save/export succeeds. A fresh editor
reopens the exact scene, saves unchanged twice with identical disk bytes, captures
the same pixels and exits zero. Both editors have empty stderr and retired control
directories. Native human interaction remains unverified since the locked-Mac
report; no new computer-use acceptance is claimed.

The native package is physically renamed/moved before launch, then the owned authoring
root is hidden. All 116 content files / 66,649,969 bytes match exactly. The player
logs environment, shadow and SSAO preparation, exits zero after 360 game steps and
records 365 steps (five preparation plus 360 game); all 120 retained samples are
unpaused/error-free. First-game render is 558.310000ms, whole step 592.073000ms;
the following interval explicitly drops 466.666648ms fixed time. Retained median step
is 11.887709ms. Cache conditions differ from earlier cold samples; these are
observations, not a matched speedup or FPS claim. The six-second main-thread sample
contains material image decoding/mip work and updateSkins -> mappedSkinWords ->
WaitAny, plus a smaller environment GPU completion wait. These are aggregate stacks
across the sample, not exact first-frame attribution. The sample does not reproduce
the old synchronous environment surface/sky creations. CPU image work, GPU bake
completion, skin synchronization/creation, transparent variants and other startup
work remain to be measured/prepared; clocks, budgets and warnings are unchanged.

Evidence stays under build/environment-surfaces-review: requests/responses, three
PNGs, application/native profiles, process sample, content hashes, runtime/game
manifests and executable hash. Verified temporary native output and the latest owned
editor copy are removed after source restoration. Formal example files are unchanged.
The overall engine/editor/game goal remains unfinished.

## Background visible-material image preparation

The previous native sample places LDR decode/mip work on the main thread. Added
incremental visible-material preparation to SceneRenderer::prepare/readiness and
ScenePreparationStage, replacing the pipeline-only API names without aliases.
Current requirements now include Materials; workspace.view.loading adds
material-textures and native initial loading names this phase before Start.
Explicit drawing/material resolution remain synchronous; meshes, small material
JSON, HDR I/O/baking, skin work and GPU uploads are not made generally asynchronous.

Each scene renderer owns at most one CPU image job. It captures its own filesystem
value, path/options and result state, never a scene/renderer/GUI borrow. The worker
reads/decodes and builds the same exact linear/sRGB mip chain. Existing image bounds
limit one logical pyramid below 86 MiB, plus file/codec working storage. Each poll
uploads at most one prepared texture on the owner. New textures remain staged until
the complete material resolves. Failure releases them and retains context; clear,
obsolete-scene or draft cancellation requests stop and discards late results without
waiting for a running codec on a frame. Destruction joins remaining work. A codec/read
already in progress may finish; there are no detached threads or forced interruption.
The pinned stb backend uses thread-local failure state. No dependency is changed.

Renderer::uploadTextureLevels validates every complete floor-half level before GPU
allocation and preserves its bytes. It shares the existing allocation/sampler/upload
implementation; ordinary upload still borrows level zero and generates lower levels,
without reintroducing the earlier base-image copy. Shared material resolution still
publishes only complete texture sets. Pruning is shared with actual rendering and
keeps textures still referenced by an active material/draft. GPU/cache byte and 4096
material/texture bounds apply to staging as well. Interleaved direct loads reuse the
published cache rather than publishing duplicate staged keys. GUI stats exposes the
actual building flag so preparation rejects GUI reentry even with ready pipelines;
readiness remains pure/GUI-safe. Draft synchronization now precedes preparation and
the Play guard, preventing a reset/closed draft's old cache from implying readiness.

Four new durable offscreen cases verify authored mip sampling and malformed levels
without allocation; async/direct pixel equivalence with no premature target/material
publication; moves, threads and GUI rejection; missing later textures releasing earlier
staging, explicit reload/recovery and a one-byte budget failing before upload; scene
visibility cancellation, clear/draft replacement, actual green preview pixels and
repeated teardown without GPU texture leaks. Initial focused Debug passes 9/9 (7.35s).
An assertion-message parenthesis error in the new test and a moved previewKey reference
are corrected during builds. Three full Debug checkpoints pass 1318/1318 (203.49s,
203.34s, 203.79s) as ordering/budget review is integrated. The last full Debug precedes
the final immediate-Play synchronization call; the final Debug build and six focused
workspace/GUI/material/mip cases then pass 6/6 (4.83s). Final Release/NDEBUG passes
1318/1318 (27.37s). Builds and tests remain sequential. Counts: 410 unit, 908
integration; overlapping 198 compiler, 156 offscreen, 33 platform, 32 audio-native
and 20 process cases. Eleven changed C++ files pass clang-format and whitespace
checks pass. Reviewed capture/result/thread lifetimes, cancel/late-result races,
scene/draft changes, staging rollback/publication, cache reuse, budget arithmetic,
all mip dimensions/storage, owner/device/GUI checks and unchanged timing limits.
Only macOS arm64 is executed; caller compiler/linker and sanitizer settings are unchanged.

Actual Release editor observes material-textures: seven inspect requests have median
47.355ms / maximum 51.894ms client wall time, including CLI/poll overhead. Play rejects
with resources still preparing; queued capture finishes failed, saving rejects and
writes no file. The ready inspect takes 98.048ms. Application sampling retains a
96.227375ms step with 92.014542ms in preparation and 4.193167ms render. This is an
observation of the owned run, not a general responsiveness bound. Real W/Space Play
observes [-3,1.3340256,1.4860593], no Play error/diagnostics, clean exact authoring
restoration on Stop and successful Save/export. A fresh editor reopens/saves twice
with identical scene/disk bytes. Ready/stopped/reopened pixels retain SHA-256
77b56bcc914309b96692e3a34313f91eaa84e46152493019562592b57b353695.

A third real editor stages concrete's base-color source to its existing normal image,
without saving. The owned draft capture reports materialPreview=true and changes
hash to 623663c4610bd62f4c218afd91a370eab3db4b14d683f5cd55130fea3da65c6a.
Reset/close restores the baseline hash, with byte-identical material file and clean,
unchanged authoring scene. This warm draft probe observes only the ready result,
not its pending interval. All three editors exit zero with empty stderr and retired
control directories. Fresh native mouse/keyboard acceptance remains unverified since
the locked-Mac report; no computer-use acceptance is claimed.

The physically relocated native export runs with authoring hidden and matches all
116 content files / 66,649,969 bytes. It logs environment/shadow/SSAO/material phases,
then exits zero after 360 game steps: 756 sampled steps (396 initial preparation plus
360 game). The 120 retained samples are unpaused/error-free. First-game render is
41.728375ms, whole step 76.275250ms; longest interval is 77.538708ms with no dropped
fixed time, and stderr has no dropped-time warning. Retained median step is
12.347396ms. Cache/context conditions are not matched to earlier runs; these figures
are not a speedup percentage or FPS claim. The process sample places LDR load/mip
work on background threads rather than the main thread. Main-thread skin status
readback and HDR completion waits remain. Initial/dynamic game-session work and
other assets still require broader background loading/streaming work; clocks, budgets
and warnings are unchanged. The full engine/editor/game objective remains unfinished.

Raw requests/responses, five PNGs, application/native profiles, process sample,
content hashes, runtime/game manifests and executable hash remain under
build/material-preparation-review. Verified temporary native output and the latest
owned editor copy are removed after source restoration. Formal example files and
user-owned reference/output material remain unchanged and untracked where requested.

## 2026-10-17 — owned HDR preparation and GPU completion

The preceding Prism workflow sample still placed HDR decode/mips and environment
completion in synchronous preparation. SceneRenderer now prepares actual HDR input
on the same bounded CPU-worker lane as material images: file/decode/validation and
the unchanged latitude solid-angle mip filter produce an immutable owned
EnvironmentSource. Input is moved rather than copied in this path; shared copies
are immutable. At most one HDR/LDR pyramid is active (HDR base at most 16,777,216
pixels, pyramid below 342 MiB, excluding encoded/codec storage). Cancellation checks
entry/mip boundaries, discards late results, and does not join a codec in a frame;
service destruction joins its owned worker.

Renderer beginEnvironmentBake uploads those levels and submits the existing GPU
algorithm; pollEnvironmentBake uses WaitAny with zero timeout and the unchanged
ten-second completion deadline. The explicit synchronous API reuses this path and
copies its borrowed source for owned CPU preparation. Pending input/output bytes
and job count are separate from ready maps and share maxTextureBytes and the
32-environment limit, including texture uploads. Dropping a pending job cancels
publication but retains its reservation/resources until a later owner mutation
observes GPU completion without waiting, or renderer teardown. Pure readiness and
resource-stat queries never collect. GPU failure retains context and faults the
device rather than allowing further allocations against abandoned work.

Only completed maps enter the scene cache. Preparation retains the old binding and
pixels until an actual draw; bound-map ownership is tracked separately so clear or
service destruction also detaches the old generation if the prepared cache has
already changed. Another service's binding remains protected. Current requirements
include HDR even after shadow pipelines are ready. Editor/native startup exposes
an environment-map/HDR environment phase and gates Play/current captures through
the same shared implementation. Small material metadata, uploads, meshes/skins,
other pipelines and dynamic game-session work remain synchronous; this is not
complete streaming or a first-frame solution. Clock limits/warnings are unchanged.

Five new durable cases cover independent weighted-mip expectations, immutable
sharing/moves/worker reads, invalid pixels/config/cancellation, GPU energy and
pending-budget accounting, thread/device/GUI rejection, abandoned completion and
post-renderer job lifetime, actual environment replacement, clear before drawing,
retained pixels/bindings and missing-file recovery. Existing readiness cases now
require real HDR preparation rather than assuming shaders imply scene readiness.
Initial focused Debug passes 19/19 (15.31s); subsequent review corrects canceled
GPU residency, readiness ordering and cache-versus-binding ownership. An in-progress
full Debug run is stopped to integrate that ownership correction, not reported as
a completed pass. Final Debug passes 1323/1323 (207.28s); final Release/NDEBUG passes
1323/1323 (28.15s), sequentially with builds. Counts: 412 unit / 911 integration;
overlapping labels include 159 offscreen, 198 compiler, 33 platform, 32 audio-native
and 20 process. Twelve changed C++ files pass clang-format; whitespace checks pass.
Reviewed stop/result races, renderer/job/source lifetimes, staging/publication,
peak byte/count bounds, error context, GUI/thread/device guards and unchanged
numerical filtering/dispatch. Only macOS arm64 is executed; caller environment,
platform targets and sanitizer settings are unchanged.

Actual Release editor observes environment-map (one inspect request, 51.626ms client
wall including CLI/poll overhead; not a responsiveness bound). Play rejects during
resource preparation; queued capture finishes failed, save rejects and writes no
file. Real W/Space input moves Player to [-3,1.2830756,1.5228012], with no Play
error/diagnostics. Stop restores clean exact authoring; Save/export succeed. A fresh
editor reopens and saves twice with identical scene/disk bytes. Ready/stopped/
reopened captures retain SHA-256
77b56bcc914309b96692e3a34313f91eaa84e46152493019562592b57b353695.
Application history retains a 99.299584ms step, including 98.972500ms rendering;
this remains a hitch, not evidence of solved first-frame costs.

A real environment processing-sample edit changes pixels to
6540443dea591f5658fbf1f2dbdb5d3c584d95c6b48b69c51d385519c631327f.
Undo restores baseline pixels and clean unchanged authoring. A nonexistent HDR path
reports its actual contextual NotFound error, rejects Play and screenshot saving,
and produces no file; Undo recovers the exact baseline. These warm edits are already
ready on the first inspect, so no pending interval is claimed. A separate real
material draft still changes pixels to
623663c4610bd62f4c218afd91a370eab3db4b14d683f5cd55130fea3da65c6a;
Reset/close restores baseline with unchanged material/scene bytes. All four owned
editors exit zero with empty stderr and retired control directories. Fresh native
mouse/keyboard acceptance remains unverified since the locked-Mac report.

The physically relocated native export, with authoring hidden, matches all 116
content files / 66,649,969 bytes, logs the actual five preparation phases and exits
zero after 360 game steps (769 sampled steps: 409 preparation + 360 game). Its 120
retained samples are unpaused/error-free with no dropped fixed time; stderr has no
dropped-time warning. The retained global slowest is step 0: 54.307875ms, including
48.569625ms events. Longest supplied interval is 55.570209ms; retained median is
12.544125ms. The first game step is evicted and is not separately measured here.
Cache/context conditions are not matched to earlier runs; these are not speedup or
FPS claims. Process sampling places actual HDR decode on startHdr's worker and LDR
mips on workers; main-thread mappedSkinWords/WaitAny remains. The broader engine,
editor and finished Prism Relay goals remain open.

Raw product requests/responses, eight PNGs, application/native profiles, process
sample, content hashes and package/executable metadata remain in
build/hdr-preparation-review. Source restoration and clean retained samples are
verified before removing owned native/editor copies. No distribution archive is
created. Formal examples and user-owned untracked reference/output material remain
unchanged.

## 2026-10-17 — prepare shared skin pipelines before drawing

The preceding actual editor run still retains a 98.972500ms rendering phase, and
native stacks include first-use synchronous skin pipeline creation as well as
per-pose status readback. Renderer now starts the existing skinVertices and
validateFaces compute pipelines through the shared owned PipelineJob mechanism.
prepareSkinPipelines polls both with zero wait; skinPipelinesReady is pure and
GUI-safe. A pair is ready only when both jobs complete. Contextual errors and the
existing ten-second per-job deadlines remain; callback state borrows no renderer.
Direct updateSkins validates the whole input batch before finishing these same
jobs if needed, with no duplicate synchronous compilation path.

Scene readiness requires these pipelines only for visible enabled saved skins,
without loading mesh/skin files or allocating controllers. Shader preparation
never publishes poses or target pixels. Editor/native initial loading exposes
skin-pipelines/skinning shaders and gates Play/current captures through the shared
scene implementation. The post-SSAO readiness refresh also ensures skins are not
skipped when multiple requirements become ready in one call. Actual deformation,
compact synchronous GPU status readback, batch-atomic buffer/bounds publication,
normal/tangent validation, shadow/SSAO/picking inputs and timing limits are unchanged.
Other assets/pipelines and populated-game CPU/GPU synchronization remain open work.

Three new durable offscreen cases cover pure queries, invalid inputs before any
compilation, owner/move/GUI checks, polling/repeated readiness, direct updates sharing
already-started jobs, independent CPU vertex comparison, failed pose retention,
repeated cancellation/teardown, hidden/disabled scene requirements, asset-root-free
preparation, no premature mesh/skin/target publication and actual bounds/picking.
Initial test build corrects two calls to a nonexistent Math helper; product code
builds. The focused Debug group passes 17/17 (14.91s), including existing physical
pixels, eight weights, reflection/failure atomicity, shadow/SSAO and resource budgets.

Final Debug passes 1326/1326 (211.66s); Release/NDEBUG passes 1326/1326 (29.86s),
sequentially with builds. Counts: 412 unit / 914 integration, including overlapping
162 offscreen, 198 compiler, 33 platform, 32 audio-native and 20 process labels.
Eight changed C++ files pass clang-format and whitespace checks pass. Review covers
pair readiness, callback/destructor lifetime, partial failure retention, shared
remaining deadlines, pre-compilation validation, unchanged layouts/shaders/dispatch,
visible-scene requirements and the GUI/thread/device boundaries. Only macOS arm64
is executed, preserving caller settings and target/sanitizer scope.

Actual Release editor observes skin-pipelines (one inspect request, 50.333ms client
wall including CLI/poll overhead, not a bound). Play rejects during preparation;
queued capture fails and saving rejects without a file. Real W/Space Play observes
[-3,1.4705257,1.3769660], no Play errors/diagnostics, and exact clean restoration on
Stop. Save/export succeed. A fresh editor reopens and saves twice with identical
scene and file bytes. Ready/stopped/reopened pixels retain baseline SHA-256
77b56bcc914309b96692e3a34313f91eaa84e46152493019562592b57b353695.
The separate concrete draft changes pixels to
623663c4610bd62f4c218afd91a370eab3db4b14d683f5cd55130fea3da65c6a;
Reset/close restores baseline with unchanged material/scene bytes. Application
sampling retains a 97.485834ms step, including 93.126459ms preparation and 4.339625ms
render. That observed hitch remains unresolved; it is not an aggregate speedup claim.
All three editors exit zero with empty stderr and retired control directories.
Fresh native mouse/keyboard acceptance remains unverified since the locked-Mac report.

The physically relocated native export again matches 116 content files /
66,649,969 bytes with authoring hidden, logs skinning along with the other real
preparation phases and exits zero after 360 game steps (769 sampled: 409 preparing
plus 360 game). Its retained 120 samples are unpaused/error-free with no dropped
fixed time or dropped-time warning. Retained global slowest is the first game step
409: 41.232709ms total, 33.544792ms preparation, 7.046625ms render. Longest supplied
interval is 43.880417ms and retained median is 11.682125ms. Because earlier runs can
evict first-game history, a separate 120-game-step run deliberately retains that
entire interval: first game step 415 is 40.932625ms total, 33.552250ms preparation,
6.729584ms render; all 120 samples are clean with no dropped time. Both runs retain
source relocation/isolation; only the 360-step run has an external process sample.
Cache/context/sampling conditions are not matched to older runs, so these are not
speedup percentages or FPS. The process sample still places mappedSkinWords/WaitAny
on the main thread; no per-pose synchronization improvement is claimed. Startup
session work, other assets, production art and the full engine/editor/game goals
remain unfinished.

Raw product requests/responses, five PNGs, editor/native profiles, first-game
history, process sample, content hashes and runtime/game/executable metadata remain
under build/skin-preparation-review. Source restoration, retired editors and clean
retained native history are checked before owned native/editor copies are removed.
No distribution archive or formal-example change is produced.

## 2026-10-17 — background encoding for frozen viewport PNGs

Fresh actual editor sampling changes the next diagnosis: the roughly 97ms step
retained after the preceding workflows includes synchronous screenshot saving.
On the unchanged 541d24a build, one run snapshots application history before and
after PNG save: the pre-capture maximum is 30.343334ms (frame 95); the later maximum
is 97.020375ms (frame 135), including 92.824542ms preparation. Main-thread stacks
explicitly pass through saveCapture -> ImageExport::write -> encodeImage ->
stbi_write_png_to_mem/zlib. The application preparation envelope includes mailbox
operations, so that observation must not be attributed wholesale to scene loading
or first-frame drawing. Raw diagnosis remains in build/preparation-diagnosis.

ImageExport now supports one owned asynchronous begin/poll/cancel attempt alongside
its explicit synchronous write. Both paths share the unchanged baseline check and
atomic commit implementation. The worker only encodes owned bounded RGBA pixels;
it borrows no viewport/renderer and performs no filesystem work. Owner polling
commits once, retains a completed report/error for repeated queries, and rechecks
external file changes before writing. Cancellation never commits and retires only
after the bounded codec returns; destruction joins an owned worker. Inputs remain
bounded to 16,777,216 pixels, encoded/old files to 64 MiB and scans to 65536 entries.
The PNG algorithm/settings and exact output bytes are unchanged.

Workspace/menu/native-dialog and strict save-capture share this job. The response
reports saving with null output, then inspection reports saved only after actual
commit, or ready plus a retained error for retry/discard. One encoder owns an image
copy in addition to the single retained capture; duplicate/new capture/save/browse
requests reject until retirement. Read-only savePending exposes active/cancelled
job lifetime. Discard cancels publication before releasing captured pixels. Native
close and machine close share a guard requiring a pending save to complete or an
explicit capture discard. Destination validation/write and catalog refresh remain
on the owner thread; there is no general filesystem-I/O or frame-bound claim.

Four durable cases add byte-identical sync/async output, owned input, owner/move and
invalid-input guards, commit only on poll, repeated completion without rewriting,
changed/created/lost destinations, cancellation/teardown without files, and frozen
capture failure/retry without recapturing. A missing explicit Core.Result import in
the new test is corrected during build. Focused Debug passes 12/12 (0.61s). A partial
full run is interrupted to add the observable cancelled-job retirement field; final
Debug passes 1330/1330 (211.95s), not the interrupted run. No desktop windows are
opened by these tests.

Final Release/NDEBUG passes 1330/1330 (29.90s), sequentially with builds/tests.
Counts: 412 unit / 918 integration; overlapping labels include 162 offscreen,
198 compiler, 33 platform, 32 audio-native and 20 process cases. Four changed C++
files pass clang-format and whitespace checks pass. Review covers input/PNG bounds,
worker/result/move/destructor ownership, cancellation before commit, external edits,
sticky results, frozen-image retention, strict protocol states and shared native/
machine close guards. Only macOS arm64 is executed; compiler/linker, targets,
dependency pins, sanitizer settings, clock limits and warnings remain unchanged.

Actual Release editor observes saving before saved with null then real output.
The dedicated save request returns in 46.782ms client wall time; two later inspect
requests observe saving/saved in 81.163/72.012ms, including CLI/poll/serialization
cost, not responsiveness guarantees. A real external create after the accepted
start causes the unchanged-baseline error, preserves external bytes and retains
ready pixels; explicit-overwrite retry of that same capture writes the baseline
PNG. Queued save followed by discard reports cancelled, eventually savePending=false,
and never writes cancelled.png. Closing during an accepted save rejects with the
explicit PNG-pending error; the file subsequently commits successfully. The actual
thread sample places encodeImage/stbi PNG compression under ImageExport::begin's
owned worker rather than saveCapture on the main thread. Its application history
still retains a 43.205041ms step (42.937875ms render envelope); owner filesystem,
readback and GUI work remain and that whole interval is not attributed to one task.

The ordinary complete editor workflow still performs real W/Space Play to
[-3,1.3340256,1.4860593] with no Play errors/diagnostics, exact clean Stop, Save and
export. Fresh reopen and two saves preserve scene/file bytes. A separate unsaved
concrete draft changes pixels to
623663c4610bd62f4c218afd91a370eab3db4b14d683f5cd55130fea3da65c6a;
Reset/close restores unchanged material/scene and baseline PNG bytes. All ready,
stopped, reopened, reset, asynchronous, conflict-retry and close-guard PNGs retain
SHA-256 77b56bcc914309b96692e3a34313f91eaa84e46152493019562592b57b353695.
The ordinary loading workflow history retains 32.091959ms total, including
21.049292ms preparation and 11.029958ms rendering. These runs have different
cache/context/request workloads from earlier records; no speedup percentage or
FPS is claimed. All four owned editors exit zero with empty stderr and retired
control directories. Fresh native pointer/keyboard acceptance remains unverified
since the locked-Mac report.

The physically relocated native export runs with authoring hidden, matches all
116 content files / 66,649,969 bytes and exits zero after 360 game steps (767 sampled:
407 initial preparation + 360 game). The 120 retained samples are unpaused/error-free,
with no dropped fixed time or dropped-time warning. Retained slowest is first game
step 407: 41.651791ms total, 32.401541ms preparation, 8.630167ms render. Longest
supplied interval is 42.913917ms; retained median is 12.290251ms. This verifies the
export path, not a native gameplay speed improvement from editor PNG work. Process
sampling still shows main-thread mappedSkinWords/WaitAny. Broader background I/O,
large-game performance, editor maturity and finished production art remain open.

Raw requests/responses, eight PNGs, application/native profiles, save/native thread
samples, hashes and package/executable metadata remain in build/png-export-review;
paired original-build diagnosis remains in build/preparation-diagnosis. Authoring
restoration and retained native history are verified before removing owned native/
editor copies, including the diagnostic copy. No distribution archive or formal
example change is produced.

## 2026-10-17 — indexed authoring hierarchy and search

Actual editor samples from the preceding workflow repeatedly enter tree traversal,
which rescans the full JSON entity list for child detection, recursion and descendant
search. A real project is created through project-action from the Prism template,
then 1000 plain root entities are authored through normal edit batches and saved.
The first 1000-operation request rejects at the real 256-operation limit without
mutation; four 250-operation batches succeed. This produces 1342 entities / 1147
expanded logical rows. On the unchanged c96aac4 Release build, six seconds of idle
application sampling has median total/render steps 102.083688/101.616479ms; stacks
place the repeated scans under tree/any_of. History also retains a 189.064459ms
supplied interval with 50.000001ms dropped fixed time; that record includes the
surrounding control workflow, not a claim about every idle step. The project adds
hierarchy metadata, not 1000 renderable meshes or populated-game evidence. Pixels
retain baseline SHA-256 77b56bcc914309b96692e3a34313f91eaa84e46152493019562592b57b353695.

HierarchyTree now derives an immutable ordered forest from the authoritative
snapshot. It owns bounded ID/name/parent metadata, canonical decimal keys and child
indices; root/sibling order follows the snapshot, including forward parent links.
Missing/duplicate/zero IDs, malformed text, cycles, more than 10000 nodes or 256
levels fail before publication. Copies share immutable storage; moved/default
models are empty and concurrent read-only filtering is supported. Reverse preorder
propagates each matching node to its ancestors once per query/snapshot change;
ASCII case and exact other UTF-8 matching preserve existing search semantics. A
matching parent does not automatically match unrelated children.

Workspace refresh publishes the snapshot, index and match flags together. Rendering
visits child lists and cached flags rather than searching JSON for every row/leaf.
Rows, range selection and agent geometry consume the actual same traversal. Drag
payload scope/revision and selection/edit operations remain shared with the product;
parent JSON is constructed only for an accepted drop. Deferred actions retain no
snapshot/index references across refresh. Requested expansion and forced search
expansion remain independent of the derived data. This is not a second authoring
scene, full row virtualization, or a change to the renderer/game clock.

Three durable pure-CPU cases verify stable forward-link/sibling/root order, exact
ancestor filtering, case/UTF-8 and text bounds, shared/owned/moved/thread lifetimes,
malformed graphs, 256-level and 10000-node boundaries and full-width IDs. The first
build corrects the UTF-8 helper import from Paths to Text. Focused Debug passes
17/17 (1.02s), including existing grouping/reparenting/atomic document regressions.
Full Debug passes 1333/1333 (212.25s), sequentially with builds. These unit checks
initialize no windows, devices, filesystems or compilers.

Release/NDEBUG passes 1333/1333 (30.00s), sequentially with builds/tests. Counts:
415 unit / 918 integration; overlapping 162 offscreen, 198 compiler, 33 platform,
32 audio-native and 20 process labels. Four changed C++ files pass clang-format;
whitespace checks pass. Review covers graph bounds/cycles, immutable ownership,
input-order preservation, reverse-order match propagation, snapshot/filter cache
invalidation, selection/expansion separation and deferred drag/revision checks.
Compiler/linker settings, pins, target/sanitizer scope and timing guards are unchanged.
Only macOS arm64 is executed.

The same saved project reopens in the new Release editor with identical serialized
scene, all 1147 displayed IDs in the same order, every row's geometry/open/selection/
active state exactly equal, and byte-identical viewport PNG. Same six-second/1ms
external sampling observes median total/render steps 2.560417/2.288875ms. The old
history retains 60 of 60 sampled steps; the new history retains 120 of 1399, so the
rolling wall-time windows differ. Comparing the last 32 steps in each gives median
total/render 101.929250/101.426188ms before, 2.506062/2.234792ms after. These are
specific editor CPU-envelope observations, not game FPS or a general performance
bound. The new retained slowest is 39.568625ms, longest supplied interval 72.115333ms,
with no recorded dropped fixed time. Backend/readback, serialization, row layout
and populated-game costs remain open; the index does not virtualize rows.

Another real editor independently checks filtered IDs against the authored JSON
parent/name relationships: exact ASCII/ancestor search, collapse rejection during
search, three-row range selection, toggle removal, and forward-ID parenting of two
old roots beneath a later root. Search then returns parent/child rows [661,659];
Undo removes the derived ancestry. Rename and deletion also invalidate matches;
their Undo restores exact original scene data without dirtying. Explicit expansion
and collapse succeed. Source review preserves the same drag edit operation and
scope/revision validation; fresh native pointer/keyboard acceptance remains
unverified since the locked-Mac report.

Actual W/Space Play moves to [-3,1.5479008,1.2734714], with no Play errors/diagnostics;
Stop restores exact clean authoring. Two saves preserve source file bytes and
restored/stopped captures keep the baseline PNG hash. Normal export succeeds. The
physically relocated export runs with authoring hidden and matches 115 content files
/ 66,837,021 bytes; the extra metadata is saved in its actual scene. It exits zero
after 360 game steps (746 sampled: 386 initial preparation + 360 game). All 120
retained samples are unpaused/error-free with no dropped fixed time or dropped-time
warning. Retained slowest is first game step 386: 43.214292ms total, 34.322166ms
preparation, 7.693250ms render; longest interval 48.378750ms and retained median
11.975438ms. This is export/game correctness for a hierarchy-heavy authoring fixture,
not 1342 renderable actors or a gameplay speed improvement from the editor index.
The larger engine/editor/game and final-art objectives remain unfinished.

Raw creation/edit/filter/selection/expansion/Play/export requests/responses, four
PNGs, before/after row snapshots and comparison, profiles/process samples, content
hashes and package/binary metadata remain under build/hierarchy-review. All owned
editors exit zero with empty stderr and retired controls, including the rejected
initial large-batch attempt. Source restoration and native history are verified
before owned app/native copies are removed. The editable temporary project remains
in ignored build storage. No distribution archive or formal example change is made.

## 2026-10-17 — selected animation key value adjustment

Continue the content-production workflow after the hierarchy stage. Existing clips
already support multi-key selection, retiming and copy/paste, but Inspector restricts
value edits to one key. Add relative local TRS adjustment to a bounded selected group,
shared by native clip editing, Inspector and strict product operations. This edits
existing bound clips; it does not complete the dope sheet or animation production goal.

Native TransformKeys resolves 1..16384 unique exact-time references without changing
key times/counts, channel interpolation or bindings. Translation adds a parent-space
channel offset, scale multiplies XYZ componentwise, and rotation postmultiplies XYZW
by a finite unit quaternion. Cubic scale/rotation derivatives receive the same linear
transform; translation derivatives remain unchanged. Quaternion derivatives are not
normalized, sign-corrected or treated as unit rotations. Non-cubic zero tangents and
vector W stay intact. Double intermediate arithmetic is checked before float conversion;
identity components skip arithmetic to retain native float bits. Missing/duplicate
keys, invalid parameters, overflow and final invalid clips reject the whole ordered
batch without changing its immutable source.

Inspector exposes translation offset, local Euler rotation degrees and scale factors
for several selected keys. set-animation-key-adjustment stages finite native-float
parameters; apply-animation-key-adjustment uses the same native TransformKeys kernel
as the strict generic transform-keys edit. Both use current resource identity/revision
and the existing form/move/dialog/close guards. Parameters remain after application,
so repeated Apply repeats the relative adjustment; opening a clip resets them. They
are not an unsaved resource or pending key form. Invalid application retains them
for correction. Group Apply preallocates the candidate selection/form before ordinary
native initial/current pose, authority and CPU skin preflight and document publication.
One changed Apply is one history state and preserves ordered selection/times while
updating the active payload. No-change Apply retains revision, redo and preview clock/
identity. Generic batches retain ordinary changed-edit selection clearing. The GUI
and agent operations share these implementations; no separate authoring scene or
preview-only success path is introduced.

Four pure unit cases verify analytical cubic translation/scale curves and quaternion
matrix composition at endpoints and intermediate times, derivative transformation,
unselected payloads, signed-zero identity, invalid/duplicate/missing references,
nonunit/nonfinite/singular parameters, overflow, atomic batches and the full 16384-key
boundary. One resource integration case verifies one history step, no-op revision,
save/reopen, Undo/Redo and invalid-batch retention. The first focused run catches a
new test using the pre-save revision for Undo; Save intentionally advances a dirty
resource revision. Correct the test to use its current snapshot, retaining that
product contract. Final focused Debug passes 48/48 (8.83s).

Debug build and full CTest pass 1338/1338 (212.12s). Release configures/builds with the
existing caller environment and full NDEBUG CTest passes 1338/1338 (29.73s), sequentially
with other builds/tests. Counts: 419 unit / 919 integration; overlapping 162 offscreen,
198 compiler, 33 platform, 32 audio-native and 20 process labels. Unit tests initialize
no windows/devices/filesystems/compilers; automatic integration opens no desktop
windows. Five changed C++ files pass clang-format and whitespace checks pass. Review
covers immutable ownership, checked conversion, quaternion multiplication conventions,
cubic derivative semantics, selected-key bounds/identity, allocation before resource
publication, authority/preview error propagation and UI/strict-operation parity.
Dependencies, compiler/linker settings, timing guards and platform/sanitizer scope
are unchanged. Only macOS arm64 is executed.

The real Release product project-action creates the ordinary editable PrismAnimation
project from Prism Relay. In the native editor, open Surveyor's actual Idle_Loop asset
(189 tracks / 1636 keys), pause/seek to 0.5s and frame the model. Product track/key
queries identify node 34, forearm.R: two step translation keys, 76 linear rotation
keys and two step scale keys. Select all 80 across those channels. Stage a 0.002m Y
offset, 15-degree local Z rotation and [1,1.01,1] scale factors. Staging alone retains
revision/history/preview; changed Apply changes exactly these 80 payloads in one
history step, preserving times/modes/selection and leaving authoring scene/disk clean.
Native viewport pixels change. Unknown fields, empty selection, an active move,
singular scale, a duplicate-reference batch after Rename and a stale revision all
reject without partial publication. Cancel the move and correct parameters normally.

Undo reproduces the original viewport PNG byte for byte; identity Apply after Undo
retains redo, revision and preview. Redo reproduces the adjusted PNG. Save, close and
reopen the actual animation asset at the same paused time reproduces adjusted pixels
exactly and resets parameters to identity. Inspect every serialized track/key against
the original: all unselected tracks stay equal; changed values match independent
TRS arithmetic and times/tangents/modes remain equal. The temporary observer initially
assumes non-cubic serialized keys include zero tangent fields; fix it to honor the
native format's omitted-zero contract and resume after Save without repeating the
relative adjustment. Original/saved clip SHA-256:

- bd61bf18dfc9e626ea4a06e141281ac2e3e8bd23672f634c4f6d89c0f90337da
- adc5ac2db019b93e4cbedb979add791fdbf3d85fa89997c7f60c7bc696e21cf3

Original/Undo preview PNG SHA-256 is db27fb651474d7d3751a8520f8a7e7fe534967878b8a7d6d8d7184b964d45d45;
adjusted/Redo/saved-reopened is e9e98cb112bbe3544d7901d26793d904a27df77491e76e948eea6b5755890727.
These are the same 1480x734 paused native model view, not fake pose data or UI mockups.
The current Prism animation uses step/linear channels; cubic correctness is established
by native behavioral tests, not claimed as cubic Prism authoring evidence. Native
app discovery confirms the Mac remains locked and automatic unlock is unavailable;
fresh pointer/keyboard acceptance is not claimed. Product CLI operations and actual
rendered frames remain usable; no lock/input-system settings are changed.

Actual Play reads the saved right-forearm Y scale 1.01, then real W/Space input moves
Player to [-3,1.2830756,1.5228012], without Play errors/diagnostics. Stop restores the
exact clean authoring scene. Game captures before/after Play retain baseline SHA-256
77b56bcc914309b96692e3a34313f91eaa84e46152493019562592b57b353695. Normal save/export
succeeds. Every exported content file equals the author's actual file, including the
saved animation hash. The physically relocated native export runs with authoring
hidden, exits zero after 360 game steps and verifies 115 files / 66,210,340 bytes.
Final profile has 476 application steps (116 preparation + 360 game), with 120 retained
unpaused/error-free steps and zero dropped fixed time. Native stderr contains normal
preparation progress and no warning. Retained global slowest is first game step 116:
41.485583ms total / 29.383708ms prepare / 9.561250ms render; retained median is
11.789542ms. These are this run's application envelopes, not an animation performance
comparison or game FPS. Initial/current native skin validation and synchronous skin
status readback remain intact; resource streaming and populated-game costs stay open.

Two earlier native observer runs also reach checked zero product exit and restore
authoring, then reject their own verification: one expects a nonexistent profiling
field, another incorrectly expects empty stderr rather than normal startup progress.
Correct the observer to the actual droppedNanoseconds field and explicit progress
output, then rerun the complete owned export validation successfully. Keep these
attempts separate from final evidence; no product timing/error guards are weakened.

Requests/applied responses, creation/track/key queries, original/saved clip, eight
PNGs, Play snapshots, export content hashes, final native profile/process sample and
binary metadata remain under build/key-adjustment-review. The owned editor exits
zero with empty stderr and retired control directory. Authoring restoration is
verified before owned app/native copies are removed; the writable example remains
in ignored build storage. No formal example asset is changed and no distribution
archive is produced. Full dope-sheet/tangent/binding/state-machine tools, broader
native gesture acceptance, final realistic art/game experience and overall production
readiness remain unfinished.

## 2026-10-17 — background live script compilation

Continue Prism Relay's real source-edit/Play workflow. Its Checkpoint interface is
hot-reload migration state, not persisted player progress. Before adding gameplay
state, inspect the actual editing interruption: LiveScripts captures and compiles
source synchronously inside frame polling. A warmed Release editor on the ordinary
PrismReload project records a 42.829625ms slowest application step, including
26.925334ms update. External 6-second/1ms sampling places compileSnapshot/runProcess/
poll beneath LiveScripts on com.apple.main-thread. The first unwarmed attempt instead
records a 54.683959ms step with 50.139959ms preparation and misses compiler sampling;
retain it as investigation, not compiler-delay evidence.

LiveScripts now captures bounded source on its owner and compiles one immutable
snapshot on an owned jthread. The worker owns only snapshot/compiler descriptors
and independent validation storage, never the active scene, runtime or services.
Old execution continues. Completion readiness uses a try-lock; it does not wait for
the compiler. Before migration, the owner captures current source again and compares
exact paths/bytes. Changed or unavailable source cancels/retires the obsolete job;
only the latest valid contents can publish. Intermediate edits are not queued.
Candidate runtime preparation, state export/import, cleanup and publication use the
existing owner-thread transactional SceneScripts reload. Failed candidates retain
old execution; identical rejected source is cached rather than repeatedly compiled.
Separate capture/compile errors preserve the rejected-code diagnostic after an
unrelated directory failure recovers, rather than falsely reporting clean code.

Core::ProcessConfig.stop prevents launch or cancels an active owned group/job while
still terminating descendants and reaping the direct child. compileSnapshot passes
its stop token through the real process and checks it across output validation;
partial output/private generations are discarded. Stop/destruction and game faults
cancel/join outstanding work. Quiescent cancelCompilation invokes no script hooks.
No thread is detached and no background task retains a scene. Existing process
bounds and VM timing guards remain unchanged; filesystem/OS teardown is not a hard
real-time guarantee. Capture, initial compilation, runtime migration and retired-file
cleanup still have synchronous costs; this is not general asynchronous filesystem I/O.

GameSession/PlaySession expose pollScripts without advancing gameplay frames, input
or timers. A current running hot-reload session is required. force schedules an
unchanged retry and coalesces into a current job. Review catches the return-to-active-
source edge: a force received while an obsolete job retires must survive until that
job is collected. Retain one bounded deferred retry; source-capture failure or
explicit cancellation clears it. New regression checks that original active bytes
still receive the requested retry. No synchronous fallback or format alias is added.

The viewport shows compiling and Retry scripts through the same strict
reload-play-scripts operation. Inspect exposes pending state, attempts/reloads/
obsolete-result counts and error without collecting results. Stale sessions, wrong
field types and unknown fields reject before effects. Manual source results and
retired cleanup failures enter ordinary diagnostics. Native faults cancel source
work as well as retiring scene/game-data services, so a faulted game does not retain
an unused compiler job. Prism README corrects the old 65-joint mannequin description
to the current 63-joint maintenance character, and explicitly separates migration
state from persistent saves. Formal example script/art assets remain unchanged.

Nine enduring additions cover real process cancellation before launch and after a
pipe-owning descendant starts; canceled immutable compilation; latest-only source
publication; missing-directory cancellation/recovery and retained type diagnostics;
stop/move/off-thread quiescent destruction with exact generation/VM ownership counts;
force coalescing and pure queries; continued old-code frame dispatch until commit;
session retry without gameplay advancement/inactive-mode rejection; and forced
retry through obsolete retirement. Existing VM-timeout regression now also faults
a source session with compilation outstanding and verifies process/generation
reclamation. Existing source/Prism/FeatureLab/input/Play tests await the real polling
API rather than assuming synchronous compilation or dispatching extra game ticks.
The shared test observation deadline does not change VM/process budgets.

Strict builds correct a missing aggregate default for stop_token, an explicit
Core.Process test import and a test comparison against Transform's individual
components. No warning/timing rule is weakened. Final focused Debug passes 41/41
(13.75s). Full Debug passes 1347/1347 (210.53s); Release/NDEBUG configures/builds and
passes 1347/1347 (30.09s), sequentially with all builds/tests. Counts: 419 unit / 928
integration; overlapping 162 offscreen, 206 compiler, 33 platform, 32 audio-native
and 21 process labels. Unit tests open no windows/devices; automatic integration
opens no desktop windows. Review covers worker/immutable-generation ownership,
completion synchronization, exact freshness/cancellation, retry/error state,
post-commit failure reporting, session retirement, strict fields, module direction,
formatting and unchanged compiler/linker/pin/platform/sanitizer scope. Only macOS
arm64 is executed. Final formatting uses trailing return types in the test helper;
its rebuilt focused checks are recorded separately.

The same saved PrismReload project and 740x287 logical viewport repeat the warmed
6-second/1ms sampling after the change. Compiler wait/private snapshot fsync now
appears under Compilation's worker thread, absent from the main-thread compiler
path. Exactly one changed source commits: attempts 2 / reloads 1 / no error. Global
slowest step is 19.605416ms (13.726125ms prepare, 0.999417ms update); retained median
7.247688ms versus 7.214771ms before. Histories retain 120 of 668/671 sampled steps,
so their rolling time windows differ. Worst observed control round trips are
54.024/52.489ms after/before, not an input-latency improvement claim. These are
specific application envelopes and stack evidence, not game FPS or proof that all
frame stalls are resolved. Background worker CPU is outside application-step timing.

A fresh current Release project-action creates ordinary editable PrismSource. In
its real native editor, malformed/stale retry requests leave source state intact.
An unchanged explicit retry succeeds without scene replacement. Hold real W input,
save a TS2322 source error externally and observe old code still move Player Z from
1.7741969 to 0.6810842. The same rejected bytes remain at one attempt until explicit
Retry; correction commits normally. Forty atomic valid source saves then produce
10 retired obsolete results with no intermediate commit. The original scene scope
56 remains throughout: before final Stop request, attempts 15 / successful reloads
2 / discarded 10 / no pending job or error. Another accepted force reports attempt
16 compiling, then normal Stop retires source work and restores exact clean
original authoring. Before/stopped viewport PNGs retain baseline SHA-256
77b56bcc914309b96692e3a34313f91eaa84e46152493019562592b57b353695. This is real editor
input/resource execution, not a test hook or private-state substitution. Fresh native
pointer/keyboard acceptance is not extended by these product-agent operations.

Save/export publishes the actual edited player source: SHA-256
128945ce99b1a9d3bffd32bb01a057e75ec62fb63fe2b49d0ec12d01e35d3d38. All 115 exported
content files / 66,210,729 bytes match authoring; published player.js contains the
saved speed * 0.85 / magnitude expression. The export has no TypeScript/source maps.
Physically relocate the native folder and hide authoring: the compiled game exits
zero after 360 game steps (470 application steps: 110 preparation + 360 game).
All 120 retained samples are unpaused/error-free with zero dropped fixed time;
stderr contains only normal preparation progress. Retained global slowest is step
0, 49.560083ms, including 45.297250ms event handling; retained median 11.946396ms.
This is portable source-free execution evidence, not a native-game speedup claim.
Authoring is restored after execution.

Raw creation/source/retry/error/input/export requests and applied responses,
profiles/process samples, source histories, original/final scene snapshots, three
PNGs, content hashes and binary metadata remain under build/script-reload-review.
Four owned editors exit zero with empty stderr and retired controls; native exit is
zero. Owned app/native copies and temporary helpers are removed after evidence and
restoration checks; editable projects remain in ignored build storage. No distribution
archive is made. Initial preparation, broader resource/background I/O, migration
costs, full animation tooling, persistent game-specific checkpoints, final art/game
experience and overall production maturity remain unfinished.

Final helper-format rebuilds pass the same 41 focused cases: Debug 13.87s,
Release/NDEBUG 3.58s. Final clang-format dry-run and staged whitespace checks pass.
All product code and runtime evidence above retain the reviewed final behavior.

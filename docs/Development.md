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

Rendering and all higher milestones remain to be implemented.

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

Filesystem, events, and all higher milestones remain to be implemented.

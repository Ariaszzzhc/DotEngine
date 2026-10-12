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

- Bootstrap: the existing macOS LLVM C++26 build configured and ran successfully.
  The executable links libc++, libc++abi, and libunwind using rpath-based names.
- Remaining milestones are planned; completion requires working implementations,
  tests, and executable acceptance cases.


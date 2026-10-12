# DotEngine

A C++26 game engine for Windows, macOS, and Linux, developed from tested core
modules toward a 3D runtime and editor.

## Build and test

Initialize the vcpkg submodule and provide LLVM Clang, CMake 3.31 or newer, and
Ninja on `PATH`. The build preserves compiler/linker flags from the environment,
including `CPPFLAGS`. No machine-specific toolchain paths are stored in the project.

```sh
git submodule update --init --recursive
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
build/debug/bin/DotRuntime --self-test
```

The `release` configure, build, and test presets provide the optimized configuration.
Executables use the `.exe` suffix on Windows.

## Current state

The core is a static library using C++ modules, with diagnostics, error results,
fixed-step timing, and memory statistics. The runtime currently exercises these
foundations, scoped events, and atomic project file access. Interfaces and
implementation units are separate. The graphical runtime and editor are under development.

See `docs/Development.md` for implementation milestones and actual validation.

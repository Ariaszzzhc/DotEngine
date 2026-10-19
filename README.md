# DotEngine

A C++26 game engine for arm64 macOS and x86_64 Windows/Linux. Runtime modules live
in the static `DotEngine` library. Editor-only `Dot.Editor.*` modules live in the
separate static `DotEditorModules` library, which depends on `DotEngine`; neither
runtime executable links the editor library. Module interfaces and implementations
are separate, without an installed public header tree.

## Build and test

Provide LLVM Clang and the platform's lld driver, CMake 3.31 or newer, and Ninja on
PATH. Set `DOT_LLVM_ROOT` when the intended LLVM installation is not first on PATH.
The project toolchain uses MSVC STL/CRT on Windows and LLVM libc++/libunwind on
macOS/Linux. Source dependencies are pinned under `vendor/`; the editor font,
TypeScript compiler and Windows DXC release are fetched by verified SHA-512.
Caller CPPFLAGS/LDFLAGS remain available; compiler/linker choice belongs to the
project toolchain. See [toolchains](docs/Toolchains.md) and
[dependencies](docs/Dependencies.md).

```sh
git submodule update --init --depth 1
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Unit tests initialize no windows or graphics/audio devices. Integration and
regression tests do not open desktop windows; durable GPU checks render offscreen.
Release/NDEBUG has matching presets. See [testing](docs/Testing.md). Configure with
`-DBUILD_TESTING=OFF` for a product-only build. Stage-specific acceptance executables
and visual demonstration drivers are not part of the project.

## Editor and examples

```sh
build/debug/bin/DotEditor
build/debug/bin/DotEditor open examples/FeatureLab
```

The editable [Feature Lab](examples/FeatureLab/README.md) project under `examples/`
validates engine components and the script SDK. Independently developed games live
in their own repositories.

Build-generated copies live under `build/<preset>/examples/`. Project-home creation
uses the same owned example templates. Open scripts in the external default editor;
the engine provides `dot:engine`/`dot:host`, and project imports omit extensions.

The editor must be fully controllable by AI agents. CLI, strict JSON commands and
the live control mailbox provide real authoring/Play/export operations. Prefer them
for integrated validation; computer use supplements human-style interaction and
unexpected UI investigations. See [project home](docs/ProjectHome.md) and
[editor operations](docs/Editor.md).

## Current boundaries

Implemented foundations include PBR textures/materials, HDRI IBL, directional/point/spot soft
shadows, SSAO, HDR/tone mapping, opt-in renderer sampling, glTF/GLB import, GPU skinning,
animation cross-fades and masked override layers with native clocks and weight fades,
rigid-body physics, native character physics and audio. The docked editor supports
scene/resource/material authoring, selection/gizmos, undo/redo, isolated Play and
native export. These features do not establish a mature editor or large-game
production readiness. Feature Lab exercises native authoring and runtime contracts;
finished game content, production workflows and populated-scene scalability remain
unfinished.

Engine development has resumed after cleanup. See
[current evidence](docs/Delivery.md), [goal boundaries](docs/EngineRoadmap.md) and
the chronological [development log](docs/Development.md). Only arm64 macOS has been
validated locally; Windows/Linux have implementation support without a local test claim.

Portable editor packaging remains available as an explicit
`cmake --build --preset release --target DotEditorArchive` target. It is not needed
for ordinary development or this cleanup. See [distribution](docs/Distribution.md).

# DotEngine

A C++26 game engine for arm64 macOS and x86_64 Windows/Linux. The engine is a static
library with separate module interfaces/implementations; the runtime, editor and
standalone player consume those modules without an installed public header tree.

## Build and test

Provide LLVM Clang, CMake 3.31 or newer and Ninja on PATH. Dependencies come from
the pinned vcpkg manifest. The build preserves caller CPPFLAGS/LDFLAGS and does not
hardcode local toolchain paths.

```sh
git submodule update --init --recursive
cmake --preset debug
cmake --build --preset debug-unit
ctest --preset debug-unit

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

Both editable demonstration projects live under `examples/`:

- [Feature Lab](examples/FeatureLab/README.md): engine component and script SDK showcase.
- [Prism Relay](examples/PrismRelay/README.md): industrial PBR environment; its requested
  non-shooting over-the-shoulder character template remains unfinished.

Build-generated copies live under `build/<preset>/examples/`. Project-home creation
uses the same owned example templates. Open scripts in the external default editor;
the engine provides `dot:engine`/`dot:host`, and project imports omit extensions.

The editor must be fully controllable by AI agents. CLI, strict JSON commands and
the live control mailbox provide real authoring/Play/export operations. Prefer them
for integrated validation; computer use supplements human-style interaction and
unexpected UI investigations. See [project home](docs/ProjectHome.md) and
[editor operations](docs/Editor.md).

## Current boundaries

Implemented foundations include PBR textures/materials, HDRI IBL, directional soft
shadows, SSAO, HDR/tone mapping, glTF/GLB import, GPU skinning/animation cross-fades,
rigid-body physics, native character physics and audio. The docked editor supports
scene/resource/material authoring, selection/gizmos, undo/redo, isolated Play and
native export. These features do not establish a mature editor or large-game
production readiness. Character scene/SDK/camera integration, production content
workflows and populated-scene scalability remain unfinished.

The original engine/editor goal is paused for cleanup and reassessment. See
[current evidence](docs/Delivery.md), [goal boundaries](docs/EngineRoadmap.md) and
the chronological [development log](docs/Development.md). Only arm64 macOS has been
validated locally; Windows/Linux have implementation support without a local test claim.

Portable editor packaging remains available as an explicit
`cmake --build --preset release --target DotEditorArchive` target. It is not needed
for ordinary development or this cleanup. See [distribution](docs/Distribution.md).

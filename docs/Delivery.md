# DotEngine 0.1 delivery evidence

DotEngine provides a complete authoring-to-native-game workflow for its current
simple 3D feature set. The following links connect delivered behavior to runnable
acceptance evidence, rather than treating a passing unit-test count as the product.

| Delivered capability | Implementation and verification |
| --- | --- |
| Static C++26 engine, separate module interfaces/implementations | `DotEngine` static target and CXX_MODULES file set; runtime/editor consume modules without installed public headers |
| Required native stack | Pinned vcpkg manifest: SDL3, Dawn, GLM, miniaudio, Jolt, EnTT, Go TypeScript compiler and QuickJS-NG; actual GPU, audio, physics and compiler acceptance |
| Create/open projects and edit games | [Project home](ProjectHome.md) creates three validated templates; [workspace](Editor.md) edits native scene/hierarchy/components, resources and TypeScript, with undo/redo and conflict-aware saves |
| Entity-attached ESM scripting | [Scripting](Scripting.md) compiles TypeScript, attaches lifecycle objects to native entities and exposes scene/components/input/physics/audio; hot reload retains compatible state |
| Agent control of authoring | GUI and strict JSON commands share implementation; CLI and live mailbox cover creation, inspection, scene/resource/script edits, type checks, Play, saving, export and close |
| Play and export | Play isolates the authoring scene; [native export](Export.md) includes player, compiled ESM, scene/assets and runtime libraries; relocated player runs after authoring deletion |
| Playable example | [Prism Relay](Examples.md): collection, collisions, hazards, audio, score, deadline, pause, victory/defeat, restart and reload; real native input/GPU/audio acceptance |
| Dedicated feature acceptance | [Feature Lab](Acceptance.md) covers every native component and all 81 runtime SDK members; focused subsystem suites add invalid input, boundaries, resource lifetime and failure recovery |
| Deployable editor | [Portable distribution](Distribution.md) includes compiler/SDK/font/templates/player and dependencies; relocated editor creates, checks, previews, runs and exports an independent game |

Development proceeded in reviewed, tested commits from core files/processes/events
through native devices, scenes, physics/audio, scripting, project services and the
editor. [Development log](Development.md) records actual stage results and earlier
limitations at the time of each entry. Local tests run with both Debug and optimized
Release/NDEBUG; acceptance includes real services rather than mocked success.

Final arm64 macOS Debug and Release/NDEBUG suites each pass 767/767 CTest cases
(714 doctest cases and 53 executable checks), taking 76.44 and 32.81 seconds.
All 211 C/C++ files pass clang-format verification. The final 219-entry native zip
passes CRC/executable-permission checks; all 16 Mach-O files pass strict signatures
and relative/system loader-reference inspection. Its extracted editor starts the
native project home successfully.

The supported implementation targets are arm64 macOS and x86_64 Windows/Linux.
macOS uses Metal through Dawn, Windows D3D12, Linux Vulkan with X11/Wayland surface
paths. Native processes, files and dependency packaging have platform branches.
Only arm64 macOS has been built and run locally; no Windows/Linux result is claimed.
The caller's LLVM/compiler/linker environment is preserved.

The current renderer draws colored meshes with cameras and depth; asset import is
bounded OBJ/dotmesh. The editor uses component inspectors and one active source
buffer. These are the delivered feature boundaries, not promises of material/lighting
systems or a general IDE. Runtime-member coverage is not exhaustive driver/device
or argument-permutation coverage. macOS copies are ad-hoc signed and locally tested;
release identity signing/notarization and installer publication have not been performed.

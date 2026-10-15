# Test layers

Doctest supplies behavioral assertions; CTest selects and reports tests. Using
`TEST_CASE` does not make a real GPU, compiler, filesystem or editor workflow a
pure unit test. Only `BUILD_TESTING=ON` compiles the test targets.

| Layer | Contract | Debug command after configuration |
| --- | --- | --- |
| Unit (`unit`) | Results, memory/events, input state, math/geometry/light validation, in-memory scene/body descriptions and CPU audio mixing/queue/spatial behavior; no filesystem/compiler/device initialization | `cmake --build --preset debug-unit` then `ctest --preset debug-unit` |
| Integration (`integration`) | Actual files, processes/compiler, script VM, physics and subsystem combinations; platform event/loop tests use SDL dummy display | Full build then `ctest --preset debug-integration` |
| Offscreen GPU (`integration;gpu;offscreen`) | Real device/shaders, image readback, material/color/depth and GPU resource lifetime without creating a desktop window | Full build then `ctest --preset debug-gpu` |
| Native (`native`) | Desktop surfaces, present/resize/repeated lifetime, project home/workspace and standalone games | Full build then `ctest --preset debug-native` |
| Acceptance (`acceptance`) | Executable commands and complete editor/game/export/distribution workflows; some require GPU/audio and visible windows | Full build then `ctest --preset debug-acceptance` |

The equivalent `release-*` presets exercise optimized code with NDEBUG. The
unfiltered `ctest --preset debug` / `release` commands run **all** layers and remain
the delivery checks. Labels may overlap: a native surface lifecycle is a GPU
integration check, while a relocated editor/game is a native acceptance check.
`debug-integration` excludes GPU/native/audio-device labels. `debug-gpu` means
real offscreen rendering, not a fake or null renderer. All device initialization
failures fail the test; no missing hardware is silently treated as success.

```sh
cmake --preset debug
cmake --build --preset debug-unit
ctest --preset debug-unit

# After a renderer or editor change, build the full test targets before selecting.
cmake --build --preset debug
ctest --preset debug-gpu
ctest --preset debug-native

# Complete checks before delivery (includes both selections above).
ctest --preset debug
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Keep visible window tests for contracts that require an actual surface/OS: creation,
resize, event dispatch, presentation and destruction. Unit tests must not create a
window just to inspect pure data. Separate CPU validation and pixel checks from
native presentation. Repeated creation/destruction specifically verifies lifetime;
do not share one global fixture if that would hide leaks or initialization failures.
The native ring/scale case uses device-tagged SDL pointer injection with a scoped
filter for its window, so desktop pointer traffic cannot overwrite its prescribed
samples. It retains real window/focus/keyboard events and restores the previous
filter. The native translator, editor drag/history and GPU pixels are still tested.
The current inventory contains 242 unit, 684 integration, 129 offscreen GPU,
71 native and 124 acceptance cases. Labels overlap; integration includes GPU and
native integration cases. All 1050 CTest identities are covered; existing subsystem
coverage is retained. Use CTest JSON inventory for exact filter intersections.
During iteration select the changed layer/cases; run the full configurations at a
cohesive stage boundary rather than repeatedly launching the entire editor suite.

This separation follows the useful distinction in the
[Godot testing documentation](https://docs.godotengine.org/en/stable/engine_details/architecture/unit_testing.html)
and its [mock display test setup](https://github.com/godotengine/godot/blob/master/tests/test_main.cpp),
and Epic's [low-level tests](https://dev.epicgames.com/documentation/unreal-engine/low-level-tests-in-unreal-engine?lang=en-US)
and [automation categories](https://dev.epicgames.com/documentation/en-us/unreal-engine/automation-test-framework-in-unreal-engine).
DotEngine retains its own test-only executables, APIs and lifecycle checks; these
references explain test methodology, not implementation compatibility.

A subsequent source review used the authenticated local GitHub CLI to access
EpicGames/UnrealEngine (`release`):
[DateTimeTest.cpp](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/Core/Tests/Misc/DateTimeTest.cpp)
performs date/time API assertions, while
[FoundationTests.Target.cs](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Programs/LowLevelTests/FoundationTests/FoundationTests.Target.cs)
defines a separate low-level test target. The connected GitHub app's earlier 404
was an access-path limitation, not evidence that the human account lacked access.
No Unreal source is copied into the engine.

Finish concurrent Debug/Release compilation before running full CTest configurations,
then run configurations sequentially. Integration tests intentionally enforce real
wall-clock script/process budgets; competing compiler work can obscure their results.
Investigate exact failures and verify them under the intended execution conditions;
do not inflate production budgets or weaken assertions to accommodate build load.

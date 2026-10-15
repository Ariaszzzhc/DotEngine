# Testing and editor validation

Permanent tests protect stable behavior: normal operation, invalid input, boundaries,
ownership, failure recovery and regressions. Test counts are not a delivery goal.
Do not retain one-off triangle/cube demonstrations, screenshot generators, or a
separate acceptance executable for each development stage.

| Layer | What it verifies | Desktop windows |
| --- | --- | --- |
| Unit (`unit`) | Pure data, math, state and validation; no OS/device initialization | Never |
| Integration (`integration`) | Files, processes, compiler/VM, real physics, document/resource transactions and headless game sessions | Never |
| Offscreen GPU (`gpu;offscreen`) | Durable shader, depth, material, image format and GPU resource regressions | Never |
| Audio device (`audio-native`) | Actual output/mixing ownership and device behavior, separately from unit tests | Never |

Platform event/loop integration uses SDL's dummy video driver. This validates event
translation and lifetime without creating a desktop window. Offscreen GPU checks
require a working graphics device; they do not turn into unit tests because they
use doctest. Missing devices fail explicitly.

```sh
cmake --preset debug
cmake --build --preset debug-unit
ctest --preset debug-unit

cmake --build --preset debug
ctest --preset debug-integration
ctest --preset debug-gpu
ctest --preset debug

cmake --preset release
cmake --build --preset release
ctest --preset release
```

The unfiltered presets run the current headless automatic suite. Release uses
NDEBUG. Complete both builds before full suites, then run Debug and Release
sequentially to avoid competing with real compiler/process time budgets. Configure
with `-DBUILD_TESTING=OFF` for a product-only build.

The editor must be fully controllable by AI agents. Prefer its real CLI/JSON
interface for project creation/opening, inspection, authoring, scripts, Play,
saving and export. Document/resource/command tests exercise the underlying product
contracts without desktop windows. Do not replace agent capabilities with test-only
hooks or fake editor state.

For integrated visual validation, open the real editor and a project from
`examples/`, control it through the same agent operations, and inspect actual
results. Computer use supplements this by simulating human pointer/keyboard input
and investigating unexpected UI failures. These sessions are deliberate product
checks, not automatically repeated CTest window launches. Save only useful findings
in the development log; temporary command files and investigation scripts belong
under ignored build storage.

Native exported executables still require manual product validation. The automatic
suite checks package ownership, relocation, compiled script/animation execution,
errors and cleanup through headless GameSession; this does not claim OS-window or
end-user visual verification.

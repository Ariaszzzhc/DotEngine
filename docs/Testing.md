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

HUD button cases exercise native hit/focus/action contracts without windows. The
Prism integration case drives ordinary InputState publication and editable game
scripts through paused save/restore, queued product intent, text-mode arbitration
and resume input ownership. Real editor/exported-player pointer and keyboard
workflows remain separate product validation; synthetic input success does not
establish physical keyboard, IME or pointer acceptance.

Dynamic-frame offscreen regressions compare captured camera/light/material/UI/skin
pixels byte-for-byte with an independent CPU-deformed reference. They cover exact
completed-frame picking, frozen output lifetime/backpressure, epoch/target/cache
cancellation, malformed/foreign/GUI/thread rejection, partial completion followed
by next-frame failure, failed GPU deformation/retry and unchanged shadow identity.
Skeleton tests retain delayed pose/projection ownership. HUD unit tests distinguish
button definition versions from cosmetic/input changes. Actual product workflows
separately verify editor capture and native presentation provenance.

Control transport cases also cover orphan receipt refusal before new publication,
regular/directory/symlink/dangling response boundaries, case spelling, malformed
request leaves, repair/retry, full 4096-entry retained history and response overflow.
They use owned temporary files without devices/windows. Actual CLI/editor/player
checks separately verify retained history, idempotent side effects and control cost.

Prepared-geometry unit cases retain immutable copy/move/thread ownership, malformed
inputs and static-versus-deformed UV requirements without a device. Offscreen cases
compare raw/prepared upload pixels, lifetime/owner rejection, shared incremental
geometry pixels, one-publication quotas, pure GUI readiness, cancellation/move/close,
sticky decode errors/reload, budget retry from owned CPU data after source removal,
shared skin data and progress during continuing material drafts. Existing skin tests
wait through geometry before asserting an actual pending initial GPU batch.

# Game data export

`Dot.Project.GamePackage` publishes the current native scene, regular resources
and compiled ESM into a new directory. The package can move independently of the
authoring project. Playback uses no TypeScript compiler or source SDK; engine modules are embedded
in the native player.
Data-only export requires a matching DotRuntime. Native export includes a
standalone player and its resolved native runtime libraries.

```text
DotEditor export-data <project-root> <existing-parent> <new-folder> <absolute-compiler>
DotRuntime --game <package-root> [frames]
```

The CLI exports the saved startup scene. The workspace **Export game** modal with **Data package only** selected and
its shared command export the current scene, including unsaved native scene edits:

```json
{"command":"export-data","revision":"2","parent":"/existing/output","folder":"MyGame"}
```

The current document revision is required. Play must be stopped, with no pending
inspector edits or resource text drafts. Export neither saves the authoring scene
nor changes its revision/dirty state. The output folder must be one portable name.
Existing destinations are preserved. Success returns `ok`, `kind: "game-data"`
and the canonical `package` path; failures retain contextual error codes/messages.
The optional runtime frame budget is an unsigned integer; zero or omission runs
until the window closes. Invalid budgets fail before opening native resources.

The version-1 directory contains:

```text
game.json
content/<project-relative scene and resource paths>
code/program.json
code/modules/<relative .js/.mjs modules>
```

Export compiles real TypeScript, strips trailing source-map directives, omits
.ts/.mts/.map files, project metadata and owned staging/control
directories. Other regular files are included with their relative names. It writes
the supplied scene at its supplied origin path and uses that path as the exported
startup scene, preserving other scene files. The editor supplies the active Document
path for both data and native export. Library callers without an origin path use the
project's configured startup path. Hot reload is disabled in the exported description. The compiler publishes within default load limits: 1024
sources, 1 MiB per ESM file, 16 MiB source bytes, 32 MiB output bytes, 8192 entries.
Default resource bounds are 8192 tree entries, 64 levels, 64 MiB per resource and
512 MiB total resource bytes. Each manifest is limited to 1 MiB.

Before exclusive publication, the staged package is reopened and validated, static
mesh/audio references are decoded and the compiled game session is prepared with
offline audio. Missing bindings/classes/resources or compiler failures prevent
publication. Preparation validates setup but does not run gameplay start/update
hooks or predict assets loaded dynamically by scripts.

Opening verifies strict JSON fields/versions, portable sorted unique paths,
ASCII case aliases including directories, exact tree membership and byte counts.
Symlinks and special files fail. It captures content and ESM into independent owned
temporary snapshots: subsequent source/package edits do not change the opened
game. Copies share ownership; keep a GamePackage alive while using its borrowed
FileSystem or a compiled session's assets. `scene()` creates a fresh scene scope.
Moved owners fail explicitly; last owners release private snapshots best effort.

Callers serialize filesystem changes. Counts and sizes are format checks, not
code authenticity, hostile-race isolation or crash durability guarantees. A
package contains executable JavaScript.

## Standalone native games

```text
DotEditor export <project-root> <existing-parent> <new-folder> <absolute-compiler> <runtime-kit>
<export-root>/DotGame [--frames <count>]
```

The finite budget counts game render steps after initial environment/shadow/SSAO
pipeline and visible-material preparation. Loading keeps processing events and showing a clear surface/title;
script Start callbacks run once the required groups are ready. It does not count loading as completed
gameplay or make all asset loading asynchronous. A zero budget runs until closed.

Windows uses DotGame.exe. The workspace **Export game** modal defaults to native
export; its **Data package only** checkbox retains the earlier data export. GUI
and automation use the same operation:

```json
{"command":"export","revision":"2","parent":"/existing/output","folder":"MyGame"}
```

The exported root includes DotGame, native libraries, runtime.json, license notices
and a game/ data package. Playback finds that package relative to the executable,
independently of the current working directory. It needs no authoring directory,
compiler, editor font or engine checkout. Zero/omitted frame budget runs until close;
malformed budgets fail before loading game resources. Automated package checks use
headless GameSession; native playback is checked through the actual product.

The build creates `bin/resources/runtime-kit`, also available via the RuntimeKit
installation component. macOS uses [CMake BundleUtilities](https://cmake.org/cmake/help/latest/module/BundleUtilities.html)
to copy non-system dependencies, rewrite loader references and verify the copied
executable; modified binaries receive ad-hoc signatures. Windows includes detected
CRT redistribution files and resolved non-system DLLs. Linux installs the player
with a relative library search path and copies its non-system dependencies.
Windows/Linux dependency scans reject non-system resolutions outside the kit after
copying. Operating-system libraries and graphics/audio drivers remain system
requirements. Packaging targets the build's native platform, with no cross-build
or cross-platform execution claim.

`Dot.Project.GameExport` provides RuntimeKit::open/publish and exportGame. Runtime
metadata validates version, native platform and executable name, portable sorted
unique records and directory aliases, exact tree membership/size and executable
permissions. Defaults bound a kit to 512 files, 512 MiB each, 1 GiB total, 1024
visited entries, 16 levels and a 1 MiB manifest. On macOS only, bounded regular
.DS_Store files up to 64 KiB are ignored and not copied; symlinks still fail.
This handles observed Finder metadata without accepting unlisted native code.

Kit files are borrowed: input changes must be serialized and files kept alive
through publication. Export revalidates the tree and exact manifest baseline,
copies notices and executable permissions into owned staging, validates the game
package and exclusively publishes the complete directory. Failures discard private
staging, preserve source data and never replace existing destinations. The runtime
manifest describes native files, while the data manifest describes `game/`; it is
not an authentication or executable-content hash guarantee. Runtime format version
1 requires a compatible player build.

The delivered notices include installed vcpkg package copyrights and LLVM runtime
licenses. macOS load-command changes are described in the LLVM notice. These
notices are preserved during export. Ad-hoc signing enables local execution;
release signing/notarization and native installers remain separate distribution
work, and no result is claimed for those workflows.

Headless integration tests relocate native exports, delete authoring inputs and
exercise actual package/script/animation behavior through GameSession. Verify
standalone native playback separately through the actual product. Other tests
cover malformed/mismatched kits, file sizes, extra files,
symlinks/Finder metadata, executable permissions, moved owners, manifest changes,
no overwrite, source preservation and failure after copying native files. Local
validation is arm64 macOS; Windows/Linux remain unvalidated.

The [Prism Relay example](Examples.md) is a complete ordinary project that can be
edited, played and exported through these operations. Runtime window titles follow
native scene names, allowing script-authored progress/status through the scene API.

## Stable player progress identity

Native/data exports preserve the project's required `applicationId` independently
of display name, startup scene or output directory. The native player uses that ID
with SDL's user-data directory and a `saves` child, never the package directory.
New projects instantiated from templates receive new IDs. See [save contracts](GameState.md).

## Opt-in native player control

The actual standalone player and source/data player can expose a local product
mailbox. No network service is opened. Parents must already exist; the player owns
and retires only its private control child. An explicit data directory is separately
owned by the caller and persists across process restarts:

```text
DotGame --control <existing-control-parent> --data-directory <existing-app-data-root>
DotRuntime --game <package-root> --control <existing-control-parent> --data-directory <existing-app-data-root>
DotRuntime --project <root> <compiler> --control <existing-control-parent> --data-directory <existing-app-data-root>
```

Both players also accept `--frames <count>` and `--profile <file>`; DotRuntime
retains its optional first positional frame budget. Unknown, missing, duplicate or
empty options reject. `--data-directory` uses that existing root's `saves` child,
creating the child as needed; it does not append another application ID. Use a
root owned by this game, outside content/install directories. An absent/invalid
explicit root fails, with no user-directory fallback. Without this option, normal
stable-application-ID user storage is unchanged.

`--control` explicitly selects agent operation: the normal application loop keeps
running while unfocused so bounded input frames have real gameplay dispatch. It
still polls native events and never changes the system input method, forces focus,
acknowledges nonexistent pointer capture or manually advances simulation. Pointer
acknowledgement continues to follow actual window focus/relative mode. Native
Escape/window close and the product close request use the same application stop
and session shutdown. Without `--control`, normal focus-pausing behavior remains.

After creating its real session, the process prints and flushes one JSON line with
`ok`, `kind: "player"` and the canonical `control` path. Mailbox readiness is not
game readiness: inspect `player.state` until `running`. Native compiler/VM/audio
setup before this announcement is still synchronous. Initial renderer preparation
continues in the regular loop and appears as `preparing`; this interface does not
make all startup/loading asynchronous.

Either binary can submit without opening a window or resolving game resources:

```text
DotGame send <returned-control> <unique-id> <request-file>
DotRuntime send <returned-control> <unique-id> <request-file>
```

The receipt's `queued: true` and `response` path confirm submission only. Read the
actual response file: `{source: "<original JSON>", response: {...}}`. IDs use 1..64
portable ASCII letters/digits/underscore/hyphen; reserved device names and case
aliases reject. Retrying identical ID/bytes returns the same receipt, without
re-executing a completed request. Reusing an ID with different bytes fails. File
access must be serialized by callers; this local channel is not an authenticated
or hostile-race-isolated service.

`Dot.Core.Control` shares this transport with the editor workspace, including its
1-MiB object/depth-64 input boundary, at most sixteen sorted dispatches per native
frame, 256 pending requests and 4096 retained request/response entries. Responses
are bounded to 64 MiB. Submission rejects a new request when pending capacity is
full, before publication; identical retries remain available. Source text is null
for invalid UTF-8. A normal command
failure is recorded as `ok:false` with contextual code/message and no automatic
retry. A response failure after dispatch is terminal and explicitly reports that
effects may have committed; neither the workspace nor player replays it. On exit,
the control directory is retired. A close response can disappear during retirement;
observe the owned process exit and control retirement to confirm shutdown.

Commands use strict complete objects with no unknown fields:

| Command | Additional fields | Actual operation |
| --- | --- | --- |
| inspect | none | Owned native scene and player state/input/source/transition/diagnostic snapshot |
| input | scope, value | Bounded frames through the shared `Dot.Platform.AgentInput` implementation |
| cancel-input | scope | Schedule one release frame for an active input request |
| game-data | value | Existing strict GameData protocol, shared with scripts/editor |
| request-scene | scope, path | Existing GameSession frame-boundary replacement request |
| scene-status | request | Retained actual replacement result |
| close | none | Request ordinary native shutdown; acknowledgement is not exit |

Scene scopes and scene-request IDs are nonzero decimal strings. Input/scene requests require
the current running scope, with no pending close. Scene replacement cancels old
scoped input and stale scopes reject. Game data spans scopes; before Running or
while closing, only in-memory snapshot/get are permitted. Loading save data does
not implicitly restore scene entities. The game still owns its checkpoint schema
and restoration policy.

```json
{"command":"inspect"}
{"command":"input","scope":"4","value":{"frames":30,"keys":["W","Space"],"buttons":[],"pointer":[0,0],"pointerDelta":[0,0],"wheel":[0,0],"text":""}}
{"command":"game-data","value":{"command":"snapshot"}}
{"command":"request-scene","scope":"4","path":"scenes/main.dotscene"}
{"command":"scene-status","request":"1"}
{"command":"close"}
```

The complete seven-field input value uses the editor Play contract: 1..600 native
frames, strict canonical key/button names, finite bounded coordinates/wheel/text,
once-only press/text edges and a final release frame. Escape/F5 are reserved
frontend controls. An active request must finish or release after cancellation
before replacement. `player.input.active/remaining/published` distinguish acceptance
from consumption. `native.focused/pointerCaptured` report actual OS observations
separately; agent key input does not invent pointer acknowledgement.

`Dot.Project.PlayerControl` provides these CPU-only operations over a borrowed
stable GameSession, without windows/devices. The frontend owns scheduling and
retirement. Observations retain at most 32 actual script failures; `failureCount`
and diagnostic sequences are decimal strings. Messages over 8 KiB are explicitly
marked `truncated` and retain valid UTF-8. Hot-reload errors remain visible under
`player.source`; native stderr retains normal lifecycle/startup diagnostics.

Mailbox polling shares the native owner loop. Current GameSession replacement still
validates/prepares resources and retires old services synchronously; a loading
request can therefore delay rendering and mailbox replies. Native transition
profiles retain actual fixed-time drops. Background resource preparation/retirement
and game-specific checkpoint restoration/feedback remain unfinished.

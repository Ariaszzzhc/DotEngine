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
the supplied scene at the configured startup path and disables hot reload in the
exported description. The compiler publishes within default load limits: 1024
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

`runtime.exported_game` relocates the package and deletes its authoring project,
then verifies actual native input, GPU pixel changes, audio endpoint and shutdown.
Focused tests cover immutable byte capture, strict formats/limits, invalid text,
resource/script failures, no overwrite, ownership and workspace/CLI behavior.
Local validation covers arm64 macOS; x86_64 Windows/Linux remain unvalidated.


## Standalone native games

```text
DotEditor export <project-root> <existing-parent> <new-folder> <absolute-compiler> <runtime-kit>
<export-root>/DotGame [--frames <count>]
```

Windows uses DotGame.exe. The workspace **Export game** modal defaults to native
export; its **Data package only** checkbox retains the earlier data export. GUI
and automation use the same operation:

```json
{"command":"export","revision":"2","parent":"/existing/output","folder":"MyGame"}
```

The exported root includes DotGame, native libraries, runtime.json, license notices
and a `game/` data package. Playback finds `game/` relative to the executable,
independent of the current working directory. It needs no authoring directory,
TypeScript compiler, editor font or engine build checkout. Zero/omitted frame
budget runs until close; malformed budgets fail before loading game resources.
Deterministic native checks belong to the development-only
`DotRuntimeAcceptance --game-verify <data-package>` driver. Exported DotGame accepts
normal playback/frame budgets and contains no acceptance command or input assertions.

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

Acceptance relocates the complete directory, deletes its authoring project and
intermediate kit, starts the delivered player from a different working directory
and checks real native input, GPU pixel changes, audio endpoint and shutdown.
Additional tests cover malformed/mismatched kits, file sizes, extra files,
symlinks/Finder metadata, executable permissions, moved owners, manifest changes,
no overwrite, source preservation, failure after copying native files and GUI/CLI
parity. Local validation is arm64 macOS; Windows/Linux remain unvalidated.


The [Prism Relay example](Examples.md) is a complete ordinary project that can be
edited, played and exported through these operations. Runtime window titles follow
native scene names, allowing script-authored progress/status through the scene API.

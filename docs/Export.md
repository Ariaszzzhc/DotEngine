# Game data export

`Dot.Project.GamePackage` publishes the current native scene, regular resources
and compiled ESM into a new directory. The package can move independently of the
authoring project. Playback uses no TypeScript compiler, source files or SDK.
This milestone exports data; executable and dynamic-library bundling remain later
work. A matching DotRuntime installation is required.

```text
DotEditor export-data <project-root> <existing-parent> <new-folder> <absolute-compiler>
DotRuntime --game <package-root> [frames]
```

The CLI exports the saved startup scene. The workspace **Export data** modal and
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
.ts/.mts/.map files, the source SDK, project metadata and owned staging/control
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

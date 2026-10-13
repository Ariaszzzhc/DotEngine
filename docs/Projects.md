# Projects and authoring commands

A project is a rooted directory containing `project.dotproject`, a startup
`.dotscene`, and TypeScript sources with the local `dot/engine.ts`/`host.d.ts` SDK.
Version 1 uses these exact JSON fields:

```json
{
  "version": 1,
  "name": "My Game",
  "startScene": "scenes/main.dotscene",
  "scriptsDirectory": "scripts",
  "window": {"width": 1280, "height": 720},
  "fixedHz": 60,
  "hotReload": true
}
```

Names are nonempty scalar UTF-8 up to 128 bytes without control characters.
Window dimensions are integers 1..8192 and fixedHz is 1..240. Paths are canonical
portable relative UTF-8, at most 4096 bytes and 240 bytes per component. They reject
traversal/dot/empty components, trailing dots/spaces, reserved Windows names and
platform-specific separators/characters. Startup paths end in lowercase .dotscene.
Strict parsing rejects missing/unknown/duplicate fields, incorrect types,
non-integer numbers, unsupported versions, nesting beyond 8, invalid bytes and
manifests larger than 64 KiB. Metadata parsing starts no subsystems.

`Dot.Project` exposes Description codecs and Project::create/open/files/description/
loadStartup/save/saveStartup. Open validates the startup scene and required SDK
files/directories and rejects symlink components, even when they point inside the
root. Loading produces fresh scene scope IDs while preserving entity values.
Copies hold description snapshots/file descriptors, not filesystem ownership;
callers serialize authoring operations. No compiler/window/backend starts during open.

Creation prepares a complete template in a private sibling staging directory,
then publishes it with native atomic no-replace rename. Existing directories
(including empty ones), files and symlinks are never overwritten. Failed preparation
cleans only owned staging data and leaves the destination untouched. Native
implementations use macOS renamex_np(RENAME_EXCL), Linux renameat2(RENAME_NOREPLACE),
and Windows MoveFileExW without replacement. Unsupported filesystem/kernel behavior
is an explicit error rather than a replacing fallback. Publication does not promise
crash durability or isolation against hostile concurrent filesystem changes.

The template contains a primary camera, a colored cube and an attached Mover
TypeScript class with owned speed properties. It copies the actual local SDK and
creates scenes/scripts/assets directories and a README. The class reads A/D through
the input SDK and edits its attached native entity; the scene remains native authoring
rather than replacing the engine with a standalone TypeScript game.

Save validates metadata/resources, prepares a description copy and atomically
replaces project.dotproject before changing memory. Failed validation or writes
retain the old in-memory descriptor. Startup scene saves use the existing atomic
SceneFiles implementation. Changing startup/script paths requires creating valid
resources first. SDK/scene data can change on disk; subsequent loads/checks report
actual failures. Moving a project directory and reopening works with relative paths;
old file descriptors still describe the old location.

## DotEditor command line

DotEditor currently provides JSON authoring commands. The graphical editor and
project export are subsequent work; these commands use the shared engine
project operations that those clients can consume.

```text
DotEditor create <existing-parent> <folder> <name> <sdk-root>
DotEditor inspect <project-root>
DotEditor configure <project-root> <description-json-file>
DotEditor save-scene <project-root> <dotscene-file>
DotEditor check <project-root> <absolute-typescript-compiler>
DotEditor document <project-root> <requests-json-file>
```

The SDK root contains dot/engine.ts and dot/host.d.ts. Creation takes one portable
folder name and does not create missing parent directories. Inspect/create/save
return one JSON object with ok=true, canonical root, project description and
owned startup scene. Configure expects the complete version-1 description.
Save-scene accepts the strict native scene format and persists it as the configured
startup scene. Check invokes the actual pinned native TypeScript compiler and
reports emitted modules/source counts/output sizes; it does not infer success
from file presence or use an author's tsconfig. Normal success exits zero;
validation/IO/compiler failures exit one with ok=false and a stable error code/
contextual message. Paths/names are literal arguments; no shell is invoked by the
engine process API. Unknown commands return a structured usage failure.

The document command exposes transactional native scene editing, selection,
bounded undo/redo and explicit conflict-checked saves. It emits one JSON response
line per request and shares the Document module used by editor clients. See
[editor documents and automation](Editor.md) for the strict protocol and lifetime
contracts. A graphical editor is still subsequent work.

`--project-test <absolute-compiler> <sdk-root> <existing-parent>` exercises exclusive
creation, real template compilation and attached script/input execution, native scene
and descriptor saves, relocation with fresh scene scope, recompilation and compiler/
process/VM cleanup. `--project-render-test` additionally verifies actual Metal pixels
from the template's script-driven movement. Tests own their temporary parent scopes.
Separate child-process tests exercise every public command and actual TypeScript
failure diagnostics. Local verification covers macOS; Windows/Linux implementations
remain uncompiled/unexecuted locally.

## Running a project

```text
DotRuntime --project <project-root> <absolute-typescript-compiler> [frame-budget]
```

This opens the configured native window, real audio endpoint and WebGPU surface,
loads a fresh startup scene, compiles attached scripts and runs the project.
A/D move the template cube; Escape or closing the window exits. A zero/omitted
budget runs until exit. The optional budget is a non-negative integer frame count.
Hot reload follows the project's flag; compile/candidate failures report diagnostics
while retaining old scripts. Individual script errors are reported and isolated;
realm/backend failures stop execution. Shutdown explicitly invokes stop hooks
and releases services, including after loop/render failure. Missing resources,
compiler/backend/device failures return errors without substitute content/devices.

`Dot.Project.GameSession` composes a scene and input/script/physics/audio services
without a window or renderer, for reusable runtime/editor scheduling. Creation
publishes empty input and prepares scripts without starting hooks. Publish input
before dispatch, call start, perform fixedUpdate at the configured 1..240 Hz, then
update once per frame. Fixed callbacks read the previous contact batch and control
physics before its actual step. Lower rates split into <=250 ms physics slices
and publish all contacts once in order. Update runs frame scripts and synchronizes
audio poses once after all fixed steps, so Doppler motion reflects the frame's
complete movement rather than duplicate synchronization. Deltas are finite [0,1];
zero audio delta represents teleportation. Live polling is synchronous and may
block a frame during compilation. The player reports and caps variable deltas
above one second, and reports fixed time dropped by Application's catch-up budget.

The session owns a fresh scene scope and stable services, checks owner thread/
reentry, preserves ownership on moves and rejects transferred/replaced scenes.
Close is terminal/idempotent and frees scripts/compiler generations/native services;
the scene stays available for inspection. Destruction releases storage without
calling script hooks. Offline mode renders real PCM; playback opens a real endpoint
and rejects offline rendering. Native failure after side effects faults the session,
with no rollback; closing releases it even when scene identity has been invalidated.

`--project-player-test <absolute-compiler> <sdk-root> <existing-parent>` creates a
complete temporary template, uses the same player/session loop with deterministic
published input, verifies real native presentation and changed rendered pixels,
opens a muted real endpoint, and checks compiler/VM/audio ownership returns to
baseline after explicit shutdown. This checks the endpoint opening and cleanup;
the audio playback acceptance cases separately verify delivered PCM/frame counters.

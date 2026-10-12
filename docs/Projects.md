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
project runtime/export are subsequent work; these commands use the shared engine
project operations that those clients can consume.

```text
DotEditor create <existing-parent> <folder> <name> <sdk-root>
DotEditor inspect <project-root>
DotEditor configure <project-root> <description-json-file>
DotEditor save-scene <project-root> <dotscene-file>
DotEditor check <project-root> <absolute-typescript-compiler>
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

`--project-test <absolute-compiler> <sdk-root> <existing-parent>` exercises exclusive
creation, real template compilation and attached script/input execution, native scene
and descriptor saves, relocation with fresh scene scope, recompilation and compiler/
process/VM cleanup. `--project-render-test` additionally verifies actual Metal pixels
from the template's script-driven movement. Tests own their temporary parent scopes.
Separate child-process tests exercise every public command and actual TypeScript
failure diagnostics. Local verification covers macOS; Windows/Linux implementations
remain uncompiled/unexecuted locally.

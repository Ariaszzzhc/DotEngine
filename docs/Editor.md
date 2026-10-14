# Editor documents and automation

The editor currently provides native scene authoring through a shared document
service, JSON commands and a native project editing window. Runtime play uses a
separate GameSession scene; the Document service opens no audio, physics, compiler
or window backend. Workspace adds window/GPU/GUI ownership around that document.

`Dot.Editor.Document` opens the configured project startup scene with fresh scope,
owns bounded history and exposes read-only scene access. Each changed edit,
undo and redo prepares a validated scene before replacing the stable scene object.
Borrowed scene addresses survive document moves; entity handles must be reacquired
after scope replacement. All operations require the creating thread.

Document revisions are nonzero decimal strings in JSON, as are entity values and
scene scopes. Every mutation requires the current revision; stale requests fail
before effects. Inspect returns the revision, scope, selected value, next available
entity value (null on exhaustion), dirty flag, undo/redo availability, history entry/
byte totals and the complete version-1 native scene. Responses also include ok=true
and an array of surviving newly created values. Owned snapshots may be freely edited
without modifying the document.

## Requests

```json
{"command":"inspect"}
```

```json
{"command":"select","revision":"1","entity":"2"}
```

Selection accepts null to clear it and does not create an undo step or dirty the
scene. A changed selection advances the revision; repeated identical selection
preserves it. Mutation requests for undo, redo and save have only command and
revision fields. Saving is explicit. Failed validation/history preparation/saves
preserve the current scene, selection, history and revision.

```json
{
  "command":"edit",
  "revision":"2",
  "operations":[
    {"op":"name","entity":"2","name":"Player"},
    {"op":"transform","entity":"2","value":{
      "position":[1.5,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]
    }}
  ]
}
```

Edit operations use strict fields:

| op | Additional fields | Effect |
| --- | --- | --- |
| create | name | Allocate an entity with default transform and no components |
| remove | entity | Remove the entity and its complete subtree |
| name | entity, name | Rename an entity |
| scene-name | name | Rename the scene |
| parent | entity, parent | Keep local transform; null unparents |
| transform | entity, value | Replace the complete native transform |
| component | entity, component, value | Replace/remove one native component |

Components are camera, mesh, rigidBody, audioSource, audioListener, script, material and light.
Values use the same schema as native scene files; null removes the component.
Script authoring describes a source/class/properties/enabled attachment and does
not execute code. Asset existence belongs to runtime bindings, so documents may
author valid paths to resources that have not yet been created.

A batch has 1..256 operations within 1 MiB of UTF-8 JSON, with maximum depth 64.
Unknown/missing/duplicate fields, malformed/canonical-ID violations and invalid
native component/transform/hierarchy values fail. Validation checks the final
scene, allowing a batch to switch primary cameras/listeners without exposing an
intermediate invalid state. Create operations obey the native 10000-entity capacity;
parents must exist at the point they are referenced. Successful batches form one
undo step. No-op batches preserve revision, scope and history, including redo.

Published entity values are never reused within a document, including after undo
and new branches. Failed/no-op candidates publish no values. Undo/redo restore
selection from the target entry and always obtain fresh scene scopes. Removing a
selected subtree clears selection; undo restores it in the new scope. Editing after
undo drops redo history. Default limits retain 128 entries within 256 MiB; configurable
bounds are 1..1024 entries and 1 byte..512 MiB, including the current entry. Older
entries are evicted as needed. A scene larger than the byte budget is rejected.
History shares immutable snapshots instead of copying their payloads per edit.
The saved baseline may retain one extra snapshot up to the native 64 MiB limit.

Dirty state compares the current canonical authoring scene with the saved baseline,
so undo can return to a clean state and undo after saving can make it dirty again.
Save checks on-disk metadata and the semantic startup-scene baseline before atomic
replacement. External changes report a conflict and preserve unsaved edits. This
check is not an OS compare-and-swap; clients must still serialize filesystem changes.
If response allocation fails after a successful mutation, the error states that
the document committed; retrying with an old revision will fail.

## Command-line authoring

```text
DotEditor document <project-root> <requests-json-file>
```

The request file is a single request or an array of 1..256 requests, within 1 MiB/
depth 64. The command owns one in-memory document for the entire sequence and
emits one JSON line per request. Requests commit independently; a failed request
emits ok=false with contextual code/message, exits one and stops subsequent
requests. Earlier successful requests, including explicit saves, remain effective.
All-success sequences exit zero. There is no implicit save; unsaved changes disappear
when this command's document is destroyed. Initial revision is 1.

This sequence edits and explicitly saves:

```json
[
  {"command":"edit","revision":"1","operations":[
    {"op":"name","entity":"2","name":"Player"}
  ]},
  {"command":"save","revision":"2"}
]
```

`DotEditorAcceptance --document-test <existing-parent>` creates an owned temporary project,
selects and moves the attached cube, rejects its stale handle, undoes/redoes,
saves and reopens. `--document-render-test` additionally verifies changed actual
GPU pixels and exact restoration of the initial/edited frames on undo/redo.
These acceptances test the authoring model and renderer, without claiming a
graphical editor window. Local verification is on macOS; Windows/Linux common
code remains uncompiled/unexecuted locally.

The native [GUI rendering layer](Gui.md) provides persistent GPU previews and
isolated ImGui/SDL3/WebGPU contexts.

## Native workspace

```text
DotEditor open <project-root> [frames]
DotEditor send <control-directory> <request-id> <request-file>
```

`open` opens the project startup scene, a native window and the packaged Noto Sans
SC font. A zero/omitted frame budget runs until closed; a positive budget is useful
for diagnostics. It prints and flushes one JSON line containing `ok` and an absolute
`control` path. Window dimensions start at 1280x800 with minimum 800x480.

The scene tree selects entities; the inspector edits names, local transforms,
parents and all six native components. Script properties use strict JSON object
input, including arrays/null/nested values. Add/remove and property changes are
buffered until Apply changes or Save; Reset discards that buffer. Applying the
inspector produces one native document history entry. Selecting another entity,
undo/redo or automation mutations are rejected while inspector changes remain
pending. Save applies the inspector first. A primary camera switch clears the
previous primary in the same transaction. Component attachments describe resource
paths and do not implicitly generate assets/scripts.

The toolbar provides Save, Undo, Redo, New entity and Delete; Cmd+S/Cmd+Z/
Cmd+Shift+Z are used on macOS, Ctrl equivalents on Windows/Linux. Delete is guarded
while text input is active. Native close/quit checks document dirtiness, pending inspector changes and
resource file drafts, opening Save and close / Discard / Cancel confirmation.
The viewport renders the actual game primary camera into a reusable GPU target;
missing camera/assets display errors and can be recovered by editing/undo. Native
renderer faults stop the loop. The toolbar now includes Play/Stop (F5); project
resource authoring and external source opening are provided through Resources. Export
publishes standalone native games or data packages through the shared command below.

`send` reads one <=1 MiB strict UTF-8 JSON object and atomically publishes a request.
It returns `queued:true` and an absolute `response` path, **not** applied success.
Read that JSON file to observe the actual outcome. The record contains the original
request string in `source` (null for unreadable/non-UTF-8 input) and the native
result/error in `response`. Document
commands are the same inspect/edit/select/undo/redo/save protocol above. Inspect
also returns `workspace` with running/close/inspector status, buffered entity fields,
properties text and validation status. `apply-inspector` and `reset-inspector` require
a current canonical decimal `revision`, share the GUI implementation and return
an updated inspection. Close uses this strict request without a revision:

```json
{"command":"close","discard":false}
```

Dirty close returns `confirmation:true` while keeping the application running;
clients can save then close, or explicitly close with `discard:true`. Applying
pending inspector changes is a separate explicit automation command.

Request IDs are 1..64 ASCII letters/digits/_/-. Identical-content retries reuse
one receipt without replay; different content for the same ID fails. Callers
serialize submissions and filesystem changes. Requests commit independently in
lexical ID order, at most sixteen per frame; at most 256 may be pending and 4096
IDs are retained per session. Use sequential submissions/current returned revisions
for dependent changes. A response failure after possible effects permanently faults
mailbox/frame processing without replay; the owner may still inspect/save the
Document directly. Readiness metadata identifies the owning session, not a process
liveness/heartbeat guarantee. No hostile filesystem-race isolation or crash durability
is promised. The private control directory is removed with Workspace ownership;
it is temporary session data, not project authoring content.

`DotEditorAcceptance --workspace-test <existing-parent>` exercises the packaged font, real
native project window, SDL Cmd/Ctrl+Z activation, changed/restored Metal viewport
pixels, online edit/stale/retry responses, moved/worker guards and control/context
cleanup. Focused workspace CTest cases cover strict commands, dirty close and actual
save, absent-camera recovery, malformed mailbox requests, terminal response-I/O
failure/no replay, pending overflow and repeated initialization/lifetime. Local
verification is on arm64 macOS; Windows/Linux remain uncompiled/unexecuted locally.


## Game preview

Play clones the current validated Document scene, including unsaved edits, into a
fresh scope and starts real TypeScript scripts, Jolt physics and native audio.
No authoring file is saved. Pending inspector changes must be applied or reset
first. Play/Stop and all close actions share the automation dispatcher. Scene
editing, selection, history and saving are locked while the game is running or
faulted; stopping returns to the authoring camera preview without changing the
Document's revision, dirty state, selection or undo history.

```json
{"command":"play","revision":"4"}
```

```json
{"command":"stop-play","revision":"4"}
```

These requests require the current authoring revision and return updated inspect
responses. Repeated play while active fails; stopping is idempotent. F5 toggles the
same operation. Inspect's `workspace.play` contains idle/running/faulted/stopped
state, frame/fixed-step counters, latest dropped time, input capture, errors and
at most 64 historical diagnostics. `workspace.message` exposes current GUI action
errors. `inspect-play` (only a command field) returns the actual game scene and
play status, including the retained final scene after stop.

Simulation owns independent fixed timing at the project's fixedHz and caps
catch-up work at eight steps per frame by default. Dropped fixed time is reported;
variable script/audio updates occur once per frame with delta capped at one second.
Native configuration may choose 1..64 steps. Scripts hot reload from project files
when configured; failed source keeps the old generation running and reports the
error. Ordinary component errors isolate that component and remain visible.
Realm/native game failures stop game advancement and stay observable in the editor;
Stop releases services and permits authoring/restarting. The editor frame itself
may succeed while displaying a faulted game; that does not claim game execution
succeeded. Stop executes script hooks and releases native audio/physics/script
ownership, including after game faults. Editor closure, frame failure and ownership
destruction also stop active games; destructor diagnostics go to stderr.

The packaged standalone TypeScript compiler includes its pinned standard library
files and license/notice, with no Node/npm dependency or hardcoded development
path. A custom WorkspaceConfig compiler path may be provided; missing/incompatible
compilers and missing native audio endpoints fail explicitly, without fallback.
Tests explicitly use offline audio where appropriate; the native acceptance uses
real playback with endpoint software volume zero.

```json
{"command":"capture-input","capture":true}
```

The Game input checkbox uses this same request. Capture starts disabled; it routes
native keyboard/text to the game, leaving editor shortcuts inactive except F5.
Pointer coordinates are relative to the viewport in logical pixels. Escape or
clicking outside the viewport releases capture; focus loss releases native held
keys. Released capture publishes released edges from the last game input, then
empty frames. Capture does not pause simulation/audio. No pause or single-step
control is provided by this stage. F5 and Escape are reserved editor controls.

```json
{"command":"save-close","revision":"4"}
```

```json
{"command":"cancel-close","revision":"4"}
```

These requests share the confirmation dialog's Save and close / Cancel operations.
Save and close first stops Play, applies the inspector, saves authoring and closes
only after success. Failure retains the editor and unsaved document; completed
stop/apply effects are not rolled back. Cancel clears the close confirmation.
Discard uses the existing close request with discard=true. Close cleanup errors
are returned by frame/run operations rather than substituted with successful exit.

The extended native workspace acceptance receives F5 through SDL and an actual
ImGui shortcut, opens real native playback, runs a script controlling a real rigid
body from captured D input and querying its real audio voice, releases capture via
Escape and stops via F5. It verifies changed/restored GPU pixels, authoring isolation,
stop hooks and compiled generation release. Additional tests cover unsaved scene
cloning, fixed timing/input bounds, hot reload, ordinary/realm errors, moves/source
lifetime/thread/reentry, partial creation cleanup, factory failure/recovery,
close/cancel/save while playing and save conflicts. Validation is on arm64 macOS.


## Resources and external source editing

The Resources tab lists assets and the configured script directory in sorted
project-relative order. Select a text resource, then use Open externally or double
click it. New script creates Behavior with a dot:engine import and opens the new
file externally. The Script inspector also opens its authored source. All paths
use the same resource metadata, exact spelling, symlink and readonly checks as
agent file operations. Protected scripts/dot resources cannot be opened for
external editing. An invalid source encoding can still be opened to repair it.

The built-in code page and source-edit widget are removed. The 3D viewport remains
visible. Files open through the system's default file application using an encoded
file URI and [SDL's native launch interface](https://wiki.libsdl.org/SDL3/SDL_OpenURL).
Errors remain visible. Acceptance means the launch request succeeded; the engine
owns no external application and does not promise it loaded the document. Configure
an OS file association for .ts/.mts to choose a source editor. No universal default
application API promises line/column navigation, so diagnostic buttons open the
file while displaying its exact source position. Native hosts may supply an owned
WorkspaceConfig.openExternal launch callback; empty uses the system service.

Check scripts compiles saved files with the real pinned compiler and static linker.
Bounded positional compiler errors map from captured src paths back to actual
project sources, with filename/line/column buttons; nonpositional errors remain raw
context. The resources response exposes scripts.message and scripts.diagnostics.
Compilation errors are never replaced by diagnostic-parser success. External .ts/
.mts saves feed existing Play hot reload; invalid candidates retain old execution.

Agent open-text/replace-text/save-text/close-text operations retain bounded drafts,
identities/revisions, exact disk conflict checks and protected-path enforcement.
Resource controls show the agent buffer status and allow explicit save/close/discard,
without a source editor. Dirty agent drafts must be saved/discarded before Check,
Play or external opening. Scene shortcuts now always address the scene; agent file
saves are explicit. OBJ imports/agent saves invalidate the mesh cache; automatic
external asset watching is not implemented.

The same operations are available through live workspace requests:

```json
{"command":"open-external","path":"scripts/mover.ts"}
```

Success returns kind=external-file, accepted=true, the project-relative path and
encoded file URI. This action preserves the agent buffer and scene, releases game
input capture, and never saves source. Unknown fields, unsafe/missing/protected
resources, pending agent drafts, unavailable handlers and launch exceptions fail.


```json
{"command":"resources"}
```

This returns entries with path, byte-count string, directory/text/binary type and
readOnly, plus the current file or null. File state includes path/text, a canonical
nonzero buffer identity and revision, dirty and readOnly. Opening or reopening
increments buffer identity, so requests from an older buffer cannot modify the
new one even if its local revision is 1.

```json
{"command":"create-directory","path":"scripts/controllers"}
```

```json
{"command":"create-script","path":"scripts/controllers/player.ts"}
```

```json
{"command":"create-text","path":"assets/notes.txt","text":"Owned project notes"}
```

```json
{"command":"import-resource","path":"assets/model.obj","source":"/absolute/source/model.obj"}
```

Directories are created one level at a time; parent directories must exist. Existing
files/directories are never replaced by create/import. These operations write
immediately and return the refreshed catalog; if refresh fails after writing, the
error explicitly reports that creation committed.

```json
{"command":"open-text","path":"scripts/controllers/player.ts","discard":false}
```

Opening another/reopening file rejects pending changes unless discard=true is
explicit. Unsupported/binary files cannot be opened as text. Supported extensions
are .ts/.mts/.json/.md/.txt/.obj/.wgsl. Script templates require .ts/.mts rather than
declaration files, and must be inside the configured script tree. There is no
implicit code attachment or source compilation.

```json
{"command":"replace-text","buffer":"1","revision":"1","text":"export const score=1;"}
```

```json
{"command":"save-text","buffer":"1","revision":"2"}
```

```json
{"command":"close-text","buffer":"1","revision":"3","discard":false}
```

Replace changes only the in-memory draft; save compares exact on-disk baseline
bytes before atomic replacement. Stale identity/revision, invalid UTF-8/NUL/size,
protected resource writes, external changes, missing files and changed project metadata fail
without losing the draft. No-op replace/save preserve revision. Close rejects
unsaved state unless explicitly discarded. Normal window close checks file drafts;
Save and close saves both the scene and resource file, retaining the editor if a
conflict occurs. Completed stop/apply/file saves are not rolled back if a later
scene save fails. Destruction never implicitly saves drafts.

```json
{"command":"check-scripts"}
```

This actually compiles/types-checks the on-disk project using the packaged native
compiler and releases the temporary generation after obtaining its result. It
returns actual sourceFiles on success and contextual diagnostics on failure.
Opening/editing/saving a file clears earlier check messages, avoiding a stale
success label for changed source.

Resource operations are restricted to assets and the configured scripts subtree.
The protected `scripts/dot` path also checks filesystem directory identity. Paths follow shared
lexical [portable naming rules](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file),
including Windows superscript device names, and reject symlinks/special files and
ASCII case aliases/collisions. The limits are 1 MiB per editable text, 64 MiB per
binary import, 8192 catalog/lookup entries and 64 levels by default. Native callers
may lower limits; bounds are enforced before writes. Lexical validity does not
promise every filesystem accepts the path. Callers serialize project filesystem
changes: the baseline check plus replacement is not an OS compare-and-swap, hostile
race isolation or crash-durability promise.

Focused resource tests cover actual Unicode files/catalog, protected-path enforcement,
revision/no-op/conflict behavior, binary source preservation/import boundaries,
invalid paths/text/metadata, symlinks/depth/capacity, moves/thread ownership and
shared descriptor lifetime. Workspace tests additionally verify buffer identity,
nested-template compilation, dirty close/recovery and actual cached-mesh GPU
changes. Native acceptance uses the agent file-save command to persist
source while Play runs, then verifies faster real rigid-body motion from hot-reloaded
TypeScript while authoring and native audio stay intact. Local validation remains
arm64 macOS; Windows/Linux code is not locally validated.


## Exporting the current scene

The toolbar's **Export game** modal uses the strict `export` or `export-data` command to
publish the current scene and saved resources without saving the authoring scene.
It requires stopped Play, the current revision and no pending inspector/file
drafts. See [game data export](Export.md) for the format, CLI, limits and ownership.
Native export includes the player and runtime libraries; data-only export remains
available through the checkbox.


## Starting and creating projects

DotEditor now opens a [native project home](ProjectHome.md) by default, with open
and validated creation from the starter project, Prism Relay and Feature Lab.
The GUI, direct JSON command and live mailbox share the same project operations.

## Material and direct light authoring

The inspector can add/remove `material` and `light` components. Material binds a
portable project-root `.dotmaterial` path; light kind uses directional/point/spot
choices with color, intensity/range, cone angles and enabled fields. Transform
controls move/rotate the light's local -Z ray. All edits, history, save and Play
use the same scene validation as strict agent `component` operations. For example:

```json
{"op":"component","entity":"2","component":"material","value":{"asset":"assets/copper.dotmaterial"}}
```

Materials are owned resource files with the strict schema in [Rendering](Rendering.md).
Resource creation/import/save supports `.dotmaterial`; successful saves refresh the
shared preview cache. Preview/Play/export load actual referenced data and expose
failures. Feature Lab is an editable project demonstrating persisted PBR and normal
mapping. This stage provides basic inspectors and resource/agent authoring; dedicated
material panels, visual light tools and the requested mature editor layout remain
in development. External source editing and the engine script namespace are
implemented; the remaining editor layout/tooling stays part of the roadmap.

## Scene environments

Use the Environment tab to choose a project-relative HDRI (or use the selected
`.hdr` resource), adjust intensity/yaw/background and choose a processing quality.
Apply commits one undoable scene transaction; Reset discards the pending draft.
Save preserves the settings. Pending drafts protect Save/Play/export/close, and
Save and close applies the draft before saving. Environment and entity-inspector
drafts cannot be silently committed over one another. Preview, Play and exported
players use the authored scene environment through the shared renderer/cache.

Agent operations use the same document transaction:

```json
{
  "command": "edit",
  "revision": "1",
  "operations": [{
    "op": "environment",
    "value": {
      "asset": "assets/studio.hdr",
      "intensity": 1,
      "rotationDegrees": 0,
      "background": true,
      "radianceSize": 256,
      "diffuseSize": 32,
      "specularSize": 128,
      "brdfSize": 128,
      "samples": 256
    }
  }]
}
```

A null value removes the environment. Live workspaces also accept `stage-environment`
with revision/value, followed by `apply-environment` or `reset-environment` with
revision. `inspect` reports pendingEnvironment/environmentDraft. `show-pane` with
pane `scene`, `environment`, `rendering` or `resources` controls graphical navigation without a
scene edit/revision. Resource failures appear with their paths; no substitute HDRI
is supplied. Higher quality increases first-bake work; background import/bake jobs
and advanced environment controls are still planned.

## Directional shadows

The Light inspector's Cast directional shadows control enables saved cascade
settings. Expand shadows to choose resolution/count/coverage/splitting/bias and
filter radius; the integer controls preserve the strict work types. Point/spot
shadow requests are rejected. Mesh inspectors expose independent casting and
receiving flags. Apply/undo/save, agent component edits, Play and exported players
share the scene configuration. One enabled directional shadow source is supported
per frame; additional sources report a contextual error rather than losing shadows.
Advanced light gizmos, projection previews and point/spot shadows remain planned.

## Saved ambient occlusion

The Rendering pane enables geometric SSAO and edits its radius/surface bias in
meters, strength and Low/Balanced/High search/filter quality. Low uses 4 directions,
2 steps and 1 filter pixel; Balanced 8/4/2; High 16/8/4. Quality preserves radius,
bias and strength. AO shades diffuse HDR environment light; it leaves direct light,
specular, emission and unlit colors unchanged. Add an HDRI through Environment to
light diffuse surfaces. Normal maps alone do not create geometric occlusion.

Apply rendering commits one undoable scene transaction; Reset AO draft discards
unapplied edits. Save, Play, export, undo/redo and other mutations reject pending
AO drafts. Native close prompts and Save and close applies the draft before saving;
Discard leaves the saved scene untouched. Environment, inspector and AO drafts are
exclusive. Reopened scenes, Play's isolated copy and relocated exported players
use the same saved settings; Feature Lab includes an authored configuration.

```json
{
  "command": "stage-occlusion",
  "revision": "1",
  "value": {
    "radius": 0.5,
    "bias": 0.02,
    "strength": 1,
    "directions": 8,
    "steps": 4,
    "filterRadius": 2
  }
}
```

`apply-occlusion` and `reset-occlusion` require the current revision. Stage accepts
null to disable AO. `inspect` exposes pendingOcclusion/occlusionDraft, and
`show-pane` accepts rendering without a revision. The direct document `edit`
operation is `{ "op": "occlusion", "value": ... }` with the same settings/null
and validation. Work values must be integers; unknown fields and unsupported
ranges fail before history or drafts change. AO is optional scene data. Temporal reprojection, multi-layer/offscreen geometry and
populated-scene quality/cost work remain planned.

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

Components are camera, mesh, rigidBody, audioSource, audioListener and script.
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

`--document-test <sdk-root> <existing-parent>` creates an owned temporary project,
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
while text input is active. Native close/quit checks both document dirtiness and
pending inspector changes, opening Save and close / Discard / Cancel confirmation.
The viewport renders the actual game primary camera into a reusable GPU target;
missing camera/assets display errors and can be recovered by editing/undo. Native
renderer faults stop the loop. The toolbar now includes Play/Stop (F5); project
resource/code authoring and export interfaces remain subsequent stages.

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

`--workspace-test <sdk-root> <existing-parent>` exercises the packaged font, real
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

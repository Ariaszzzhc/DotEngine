# Editor documents and automation

The editor currently provides native scene authoring through a shared document
service, JSON commands and a native project editing window. Runtime play uses a
separate GameSession scene; the Document service opens no audio, physics, compiler
or window backend. Workspace adds window/GPU/GUI ownership around that document.

`Dot.Editor.Document` initially opens the configured startup scene with fresh scope,
then owns one active scene file with bounded history and read-only scene access. Each changed edit,
undo and redo prepares a validated scene before replacing the stable scene object.
Borrowed scene addresses survive document moves; entity handles must be reacquired
after scope replacement. All operations require the creating thread.

Document revisions are nonzero decimal strings in JSON, as are entity values and
scene scopes. Every mutation requires the current revision; stale requests fail
before effects. Inspect returns the revision, scope, selected value, next available
entity value (null on exhaustion), dirty flag, undo/redo availability, history entry/
byte totals, `scenePath` and the complete version-1 native scene. Responses also include ok=true
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

Components are camera, mesh, rigidBody, character, audioSource, audioListener,
script, material, light, animation and skin.
Values use the same schema as native scene files; null removes the component.
Script authoring describes a source/class/properties/enabled attachment and does
not execute code. Asset existence belongs to runtime bindings, so documents may
author valid paths to resources that have not yet been created.

Character controllers share the scene codec's complete capsule/movement/contact
properties. The Inspector uses the same defaults and component transaction as agent
edits. Rigid body and character controller are mutually exclusive; removal/addition
can be applied in one document transaction. Physics binds them only when Play or
an exported game initializes. Invalid world tilt/scale is an explicit binding
error. Scene parent/pose edits remain ordinary authoring operations; property-only
reconfiguration preserves runtime motion. See [physics](Physics.md).

Rigid-body Collision detection uses the `discrete`/`continuous` selector and the
same required `collisionDetection` field in scene/agent component values. Continuous
requires solid Dynamic motion. Invalid Static/Kinematic/sensor combinations preserve
the document and remain editable drafts. Native Play and exports consume the saved
mode; changing it while stopped is ordinary history-aware component authoring.

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
Save checks on-disk metadata and the active scene file's semantic baseline before atomic
replacement. External changes report a conflict and preserve unsaved edits. This
check is not an OS compare-and-swap; clients must still serialize filesystem changes.
If response allocation fails after a successful mutation, the error states that
the document committed; retrying with an old revision will fail.

## Scene files

File → New scene / Open scene / Save scene as uses the same document operations as
CLI/JSON. Assets → Project exposes the `scenes` folder; double-click a `.dotscene`
file or choose Open scene. Dirty scene switches open the scene form so discard is
explicit. The viewport header and inspection's `scenePath` identify the active file.

```json
{"command":"create-scene","revision":"1","path":"scenes/Level.dotscene","name":"Level","discard":false}
{"command":"save-scene-as","revision":"2","path":"scenes/LevelCopy.dotscene"}
{"command":"open-scene","revision":"3","path":"scenes/main.dotscene","discard":false}
```

Each object is a separate strict request with the current revision. New files require
an existing `scenes`/`assets` parent and never overwrite an existing destination.
Opening also accepts the configured startup path if it lives elsewhere in the project.
Paths follow portable UTF-8/exact-case rules, reject symlinks and reserve `.dot-stage-*`
components for engine ownership. A new scene starts empty; add a primary Camera for
Game view/Play. A failed load, invalid path/name, collision or history-budget rejection
preserves the current document; no failed creation leaves a new scene file.

Open/Create prepare the new scene fully before replacement, clear selection/history
and advance revision/scope. The Scene object's borrowed address stays stable; old
handles and requests remain invalid. Newly allocated entity values stay monotonic
across file switches. Save as changes the save destination/baseline while retaining
scope, selection and history; undo may become dirty against the new baseline. It
leaves the old file unchanged. Normal Save affects only the active file. External
scene edits conflict on Save; Save as can rescue current content into a new file even
when an external editor has corrupted/deleted the startup file. Metadata conflicts
still reject writing. Caller filesystem serialization remains required.

Apply/reset Inspector/environment/AO forms and save/close dirty resource editors
before file operations; `discard` applies only to scene changes. Open/Create close
clean resource buffers, reset skeleton/viewport bindings and invalidate old previews.
Play/gestures/animation audition and active close/native-save dialogs block navigation.
Frozen completed PNG captures retain their original context. Project startup metadata
is unchanged: opening a project still chooses its configured startup scene, while
workspace Play and export consume the active scene (including unsaved scene edits).
This is single-document scene-file authoring; runtime/additive scene loading is not
implemented by these commands.

The actual file form is also completely observable/controllable by agents:

```json
{"command":"scene-dialog","mode":"open"}
{"command":"stage-scene-dialog","revision":"4","path":"scenes/Level.dotscene","name":"Level","discard":false}
{"command":"confirm-scene-dialog","revision":"4"}
{"command":"scene-dialog","mode":"close"}
```

Modes are `open`, `create`, `save-as`, `close`. `workspace.sceneDialog` is null when
closed, otherwise contains mode/path/name/discard. Stage replaces the entire owned
form with bounded UTF-8/no-NUL text (4096 bytes per field), without changing document
revision/history. Confirmation requires the current revision and uses the exact
same operation as the GUI button. Invalid confirmation retains the form for repair.
Cancel/Escape and the close command share cancellation. File forms block competing
authoring; project-close Save/Discard/Cancel remains available.

场景文件可通过菜单、资源浏览器及严格 JSON 共用操作创建、打开和另存。切换会清空当前
历史/选择并推进版本与范围；另存保留历史并改变保存目标，不改旧文件或启动配置。未保存
场景须明确丢弃，资源草稿须先完成。对话框字段可读取、暂存、确认或取消，不要求模拟鼠标。

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

The native [GUI rendering layer](Gui.md) provides persistent GPU previews and
isolated ImGui/SDL3/WebGPU contexts.

## Native workspace

```text
DotEditor open <project-root> [frames]
DotEditor send <control-directory> <request-id> <request-file>
```

`open` opens the project startup scene, a native window and the packaged Sarasa UI SC Regular font (with Inter Western glyphs). A zero/omitted frame budget runs until closed; a positive budget is useful
for diagnostics. It prints and flushes one JSON line containing `ok` and an absolute
`control` path. Window dimensions start at 1280x800 with minimum 800x480.

The scene tree selects entities; the inspector edits names, local transforms,
parents and native components. Script properties use strict JSON object
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
The viewport defaults to an independent editor camera in a reusable GPU target.
Editor navigation never adds a camera to the game scene. Explicit Game view and
Play use the actual game primary camera; missing camera/assets display errors and
can be recovered by editing/undo. Native renderer faults stop the loop. The toolbar now includes Play/Stop (F5); project
resource authoring and external source opening are provided through Resources. Export
publishes standalone native games or data packages through the shared command below.

`send` reads one <=1 MiB strict UTF-8 JSON object and atomically publishes a request.
It returns `queued:true` and an absolute `response` path, **not** applied success.
Read that JSON file to observe the actual outcome. The record contains the original
request string in `source` (null for unreadable/non-UTF-8 input) and the native
result/error in `response`. Document
commands are the same inspect/edit/select/undo/redo/save protocol above. Inspect
also returns `workspace` with running/close/inspector status, buffered entity fields,
properties text and validation status. `stage-inspector` takes exactly `command`,
`revision`, `entity` and `value`. `entity` must be the current selected decimal ID;
`value` is its complete native entity object from the inspected scene (including
null components). It validates the final scene before replacing the pending draft,
without changing document revision, dirty state or history. Staging can replace the
current entity draft; stale revisions, pending environment/AO drafts, an open material
editor, Play and active transform gestures reject it without mutation. Separate text-file
drafts can coexist with entity drafts, matching the GUI. Unknown/missing fields and invalid components
are errors. Choosing a primary camera/listener prepares demotion of the previous
owner in the same transaction.

`apply-inspector` and `reset-inspector` require a current canonical decimal `revision`,
share the GUI implementation and return an updated inspection. Apply commits one
history step; Reset restores canonical fields and script properties text immediately.
The native Inspector keeps wrapped labels beside controls, stacks them in narrow
panels and edits numeric vector elements independently without narrowing other values.
Close uses this strict request without a revision:

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

```json
{"command":"capture-input","capture":true}
```

The Game input checkbox uses this same request. Capture starts disabled; it routes
native keyboard/text to the game, leaving editor shortcuts inactive except F5.
Pointer coordinates are relative to the viewport in logical pixels. A running game
can request relative mouse mode with `input.capturePointer(true)`. It takes effect
only while Game input is enabled and the editor window is focused. Relative mode
hides/confines the cursor and supports continuous motion beyond window edges;
pointer buttons/motion/wheel are isolated from editor widgets. Escape, focus loss,
Stop or closing releases capture. Focus loss disables Game input until explicitly
enabled again. Outside-viewport clicks release ordinary absolute input capture.
While relative mode is active, SDL text/IME input is suspended so the current
input method cannot consume gameplay keys. Releasing relative mode restores text
input for editor fields; the operating system's selected input method is unchanged.
Released capture publishes released edges from the last game input, then
empty frames. Capture does not pause simulation/audio. No pause or single-step
control is provided by this stage. F5 and Escape are reserved editor controls.

`workspace.play.pointerCaptureRequested` reports the running script request;
`pointerCaptured` reports the last successfully applied native mode. A command
response can precede the next native frame that applies its request. The script's
`input.pointerCaptured` acknowledges the state published with that frame. These
fields also make the real pointer workflow inspectable by AI agents.
`textInputActive` reports the actual native text/IME mode; a failed native query
reports null with `inputModeError` instead of inventing a successful state.

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

Headless tests cover scene cloning, timing/input bounds, hot reload, script errors
and resource lifetime. Validate the actual workspace through its agent interface:
inspect authoring, start Play, inspect game state, stop and confirm authoring
isolation. Use computer use for F5/Escape, focus, pointer capture and visual behavior
when needed. Validation is on arm64 macOS.

## Agent gameplay input

Live Play inspection exposes a canonical decimal `play.session` identity. It changes
on each successful Play start even when the authoring revision stays unchanged.
A bounded request drives the ordinary InputState -> script -> native simulation
path; it does not change the scene directly or advance time outside the native loop.

```json
{"command":"input-play","session":"1","value":{"frames":30,"keys":["W"],"buttons":[],"pointer":[0,0],"pointerDelta":[0,0],"wheel":[0,0],"text":""}}
```

All fields are required. `value` is strict UTF-8 JSON within 64 KiB/depth 8. `frames`
is an integer in 1..600 and counts actual Play frames, rather than a duration or
fixed physics steps. Names match `dot:engine` input names exactly; unknown/duplicate
names and reserved editor keys Escape/F5 are rejected. Position and per-frame delta
are finite within +/-1e6, wheel within +/-1000, and text is UTF-8 within 16 KiB without
NUL. Pointer/wheel deltas repeat per held frame; text and press edges publish once.
The first input compares held keys/buttons against the last published frame. One
extra frame releases held keys/buttons and clears motion/wheel/text. A request is
accepted asynchronously; inspect the runtime scene and `play.agentInput` to verify
actual effects. `active`, `remaining` and `published` report its progression; the
published count includes its release frame. `play.inputSource` reports the latest
prepared input frame's `agent`, `native` or `none` source.

Only one request may be active, including its pending release frame. Cancel uses
exactly `command` and the current `session`:

```json
{"command":"cancel-play-input","session":"1"}
```

The GUI Release control button and Escape use this same cancellation mechanism.
Cancellation before first publication does not fabricate unseen presses/releases.
Stop, Play failure and destruction discard the controller; stale session identities
cannot inject into or cancel a restarted game. Pending close rejects new requests.

Agent input temporarily owns gameplay publication, independently of OS focus or
IME. It does not inject into the editor GUI or modify authoring history.
`focus-window` (only `command`) requests activation of the visible, non-minimized
editor through SDL; `inspect.workspace.focused` reports actual focus, which may
arrive asynchronously or be refused by the window manager. Native
input capture resumes afterward when enabled. Pointer capture remains an actual
frontend acknowledgement: an input request never claims OS pointer capture. Use the
existing `capture-input` operation when native relative capture is needed; focus loss
still releases that native mode. Agent deltas can be inspected without capture, but
scripts requiring acknowledged capture (such as the shoulder camera) obey that flag.

## Pointer input diagnostics

The ordinary strict `inspect` request includes `workspace.input`, a read-only snapshot
for investigating hit-testing, focus and DPI. It neither injects GUI input nor changes
the scene, resource history, camera or playback clock:

- `window.logical` and `window.pixels` are current SDL client extents, excluding native
  title-bar chrome.
- `native.pointer`/`delta` and named button `down`/`pressed`/`released` values are the
  application's last consumed InputState. Position starts at zero before any event;
  edges/deltas reset each application frame.
- `native.lastPress`, `lastRelease` and `lastMotion` retain only the last event of each
  kind for this window, or null before one arrives. They own window ID, SDL nanosecond
  timestamp as a decimal string and event coordinates. Button numeric codes are
  Unknown=0, Left=1, Middle=2, Right=3, Back=4, Forward=5. `hasPosition`/`positionValid`
  distinguish omitted and invalid coordinates; invalid values display null. Motion
  also reports `positionValid`/`deltaValid`. No keyboard/text event log is retained.
- `gui` reports the latest GUI NewFrame's context-local `frame` counter, logicalSize,
  framebufferScale, optional pointer and held-button mask. Mask bits 0..4 mean
  left/right/middle/back/forward. An unknown GUI position is null; finite coordinates
  outside the window remain observable. ImGui may floor coordinates to logical pixels.

GUI input is copied before drawing callbacks, so it remains inspectable after a callback
fails. The counter starts at zero and resets with the GUI context; it differs from the
renderer count of successful submissions. Native and GUI snapshots may have different
ages, especially when inspect runs before the next GUI NewFrame. Last button events do
not imply current held state; use the current named buttons or GUI mask for that.

When computer use appears to miss a target, compare requested location, native button/
motion coordinates and GUI coordinates before treating it as an editor defect. Window
activation is asynchronous; observe focused after focus-window. Current macOS evidence
shows virtual pointer requests can arrive in SDL without the requested motion; actual
product agent operations remain the primary verification/control interface.

inspect 现包含只读输入诊断：窗口逻辑/物理大小、原生输入状态、各一次按下/释放/移动事件，
以及 GUI 最近 NewFrame 的输入。坐标空间、采样时间和成功提交帧计数明确区分，不注入 GUI
输入、不修改场景或播放，不记录键盘文本。虚拟指针未产生 SDL 位移时不能当作编辑器命中
失败；通过同源产品智能体操作继续验证功能。

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

Resource operations are restricted to assets, scenes and the configured scripts subtree.
The protected `scripts/dot` path also checks filesystem directory identity. Paths follow shared
lexical [portable naming rules](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file),
including Windows superscript device names, and reject symlinks/special files and
ASCII case aliases/collisions. The defaults are 1 MiB per editable text, 64 MiB per
binary import, 8192 catalog/lookup entries and 64 levels by default. Native callers
may explicitly raise text capacity up to 64 MiB for native data or lower limits;
bounds are enforced before writes. Lexical validity does not
promise every filesystem accepts the path. Callers serialize project filesystem
changes: the baseline check plus replacement is not an OS compare-and-swap, hostile
race isolation or crash-durability promise.

Focused resource tests cover actual Unicode files/catalog, protected-path enforcement,
revision/no-op/conflict behavior, binary source preservation/import boundaries,
invalid paths/text/metadata, symlinks/depth/capacity, moves/thread ownership and
shared descriptor lifetime. Validate buffer identity, dirty close/recovery and
live source-save behavior through the actual workspace agent operations. Local validation remains
arm64 macOS; Windows/Linux code is not locally validated.

## Exporting the current scene

The toolbar's **Export game** modal uses the strict `export` or `export-data` command to
publish the current scene and saved resources without saving the authoring scene.
The exported startup path is the active file path. Other scene files retain their
paths and contents, including the original startup when another scene is active.
It requires stopped Play, the current revision and no pending inspector/file
drafts. See [game data export](Export.md) for the format, CLI, limits and ownership.
Native export includes the player and runtime libraries; data-only export remains
available through the checkbox.

## Starting and creating projects

The launcher separates Open/Create pages with aligned fields, persistent form values,
real template descriptions and a destination preview. SDL native folder pickers
support opening an existing project and choosing a creation parent. Cancellation
preserves the form; selection/errors use the shared validated project operations.

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
pane `scene`, `viewport`, `inspector`, `environment`, `rendering`, `resources` or `console` controls graphical navigation without a
scene edit/revision. Resource failures appear with their paths; no substitute HDRI
is supplied. Higher quality increases first-bake work; background import/bake jobs
and advanced environment controls are still planned.

## Directional and local shadows

The Light inspector's Cast shadows control enables saved shadow
settings. Expand shadows to choose resolution/count/coverage/splitting/bias and
filter radius; the integer controls preserve the strict work types. Directional
lights use cascades/count/splitting. Point lights use six faces; spots use one map
and require outerConeDegrees <90. Local distance is radial coverage capped by range.
Mesh inspectors expose independent casting and receiving flags. Apply/undo/save,
agent component edits, Play and exported players share the scene configuration. Multiple mixed light kinds share the renderer's
bounded atlas, preserving each light's resolution; excess work reports a contextual
budget error instead of losing shadows. Advanced light gizmos and projection
previews remain planned.

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

## Independent scene navigation and selection

The session-owned editor view provides perspective/orthographic browsing, RMB plus
WASD/QE flight (Shift accelerates), Alt+LMB orbit, MMB pan, wheel dolly and F to
frame the selected entity. Frame scene unions current visible mesh bounds, including
parent transforms and reflection; nonmesh entities focus on their world position.
Focus adjusts clip planes for larger bounds. Editor state is never written to the
scene or its undo history. Play temporarily uses the game camera and Stop restores
the editor view. An empty scene is editable without inventing a game camera.

Left-click selection rasterizes covered mesh fragments at the displayed pixel with
integer draw identifiers, depth testing and the same physical backface policy as
the surface pass. Full entity IDs remain on the CPU, including values above 2^53.
Background clicks clear selection. This is synchronous single-pixel GPU readback;
marquee selection, advanced manipulators and asynchronous readback are
not implemented. Drafts and stale scene/view revisions reject selection rather than
picking an image that no longer describes the document. Native drag ownership starts
inside the viewport and ends on release, Escape or focus loss.

Agent commands share these implementations:

- `set-view`: `mode` is `editor` or `game`. Optional editor `camera` contains
  `position`, `pivot`, `yaw`, `pitch`, `speed`, `orthographic`,
  `orthographicHeight`, `nearPlane`, `farPlane`; degrees and meters are explicit.
- `navigate-view`: `action` is `turn` (`delta`, `orbit`), `move` (`direction`,
  `seconds`, `fast`), `pan` (`delta`) or `dolly` (`steps`).
- `focus-view`: current `revision`, `entity` as a decimal string or null to frame
  all visible meshes.
- `pick-view`: current `revision`, top-left normalized `uv` in [0,1). A successful
  preview of the same view/document is required first.

The visual direction is recorded in [EditorDesign.md](EditorDesign.md). Its reference
art is a design input; the scene and controls must always reflect engine state.

## Docked workspace and themes

Real ImGui docked windows replace fixed child regions: Hierarchy at left, independent
Scene/Game viewport in the center, Inspector/Environment/Rendering tabs at right and
Assets/Console below left/center. Tabs and splitters can move, resize and float inside the
application window. Window focuses each pane and resets the initial 18/60/22 and
34-percent bottom proportions. No additional native windows or terminal are created
by docking. File/Edit/Build menus invoke the same existing scene/export operations.
Assets Actions contains actual resource/import/script-check and agent-buffer actions.
Hierarchy and asset filters search names/paths, retaining matching hierarchy ancestors;
ASCII search ignores case, other UTF-8 text matches exactly. Layout/theme are currently
session state; saving the scene never writes them into game data.

- `set-theme`: `theme` is `dark` or `light`. Palette changes preserve metrics, viewport
  pixels, camera, selection and document history, including during Play.
- `filter-panel`: `panel` is `hierarchy` or `resources`; `query` is UTF-8, at most
  128 bytes, without embedded NUL. GUI search uses this same validation.
- `reset-layout`: no additional fields. Applied in the next GUI frame; a pending
  docking command must finish first.
- `dock-panel`: `panel`/`target` are `hierarchy`, `viewport`, `inspector`, `environment`,
  `rendering`, `skeleton`, `animation`, `resources` or `console`; target must currently be docked. `placement` is `tab`,
  `left`, `right`, `up` or `down`, with `ratio` in [0.1,0.9] (unused for tabbing).
  Returns `queued:true`. `inspect.workspace.layout` reports pending, command generation,
  error and actual per-panel logical geometry/visibility/focus/docking after the owning frame.
  A target moved away before execution reports an error instead of false completion.

These commands require no scene revision and remain available with authoring drafts
or during Play; they do not mutate the scene. `inspect.workspace.theme` and `filters`
report session state. Sarasa UI SC Regular supplies Inter Western/CJK mixed text from
one pinned packaged font, rendered at a compact logical size with native DPI handling.
Cross-session layout profiles and detached native panels remain planned; current
manipulator and resource-preview workflows are described below.

Console presents actual workspace/viewport errors, Play failure context and compiler
locations that open externally. Runtime diagnostics remain available after Stop and
do not shrink the scene image. Unsaved status includes scene, inspector/environment/
AO and agent text drafts. Editor Undo/Redo poll command shortcuts across docked
windows while text input retains its local undo. Newly appearing panels do not steal
requested focus. `inspect.workspace.displayed` reports hierarchy IDs and asset paths
actually drawn in the last frame; searches expand matching ancestors. View status
also reports text-input capture for input diagnostics.

Viewport rendering resolves after GUI layout and before GUI submission using the
current physical dimensions, with explicit absence for recoverable scene failures.
GUI build-time renderer reentry remains forbidden. This keeps initialization, docking,
resize, undo and Play/Stop pixels consistent with the displayed widget.

## Transform manipulators / 变换操纵器

Scene view offers Select/Move/Rotate/Scale with world/local coordinates. Move snaps
in meters, Rotate in degrees and Scale in factor increments; zero is continuous.
Select an entity and drag an axis, rotation ring or scale endpoint; the live GPU preview uses
a disposable scene clone. Release commits one history entry; Escape or focus loss
cancels. The Inspector/document stays unchanged during dragging. Undo/Redo restores
both authored data and actual scene pixels. Local axes include the selected hierarchy's
rotation/reflection; world displacement is converted through the parent inverse.
Unrepresentable parent-local shear or singular matrices fail without substituting an
approximate transform. Behind-camera and near-parallel axis handles are omitted.
Scene 视图提供选择/移动、世界/局部坐标轴及米单位吸附（零表示连续）。拖动修改独立
预览场景，松开后提交一条历史记录；Esc 或失焦取消，拖动期间不更改 Inspector/文档。
父局部错切或奇异矩阵无损失败，不用近似变换掩盖错误。

The UI and agent use the same gesture implementation:

```json
{"command":"set-tool","tool":"move","space":"world","snap":0.5}
{"command":"begin-transform","revision":"3","axis":0,"uv":[0.5,0.5]}
{"command":"update-transform","revision":"3","uv":[0.7,0.5]}
{"command":"finish-transform","revision":"3","commit":true}
```

`axis` is integer 0/1/2 (X/Y/Z); UV is top-left [0,1]. Begin requires a selected
entity, current rendered Scene view and no pending authoring/text draft or navigation.
Update is absolute relative to the start sample, avoiding cumulative snapping drift.
Cancel uses `commit:false`. An active gesture locks other document/view/layout/tool
operations; inspect, updates, finish, close, theme and filtering remain available.
Agent gestures stay active across frames until finish; native gestures additionally
consume pointer release. Closing asks about the pending draft before discarding it.
`workspace.tool` reports configuration, active entity, errors and actual projected
handle segments. `workspace.view.source` distinguishes document/transform/Play previews;
changed/begun/cancelled drafts invalidate readiness until GPU drawing completes.
Rotation rays intersect the operation frame's canonical plane, unwrapping by the
shortest arc between samples (up to 100 turns). Local rotation uses parent-world *
local T*R without entity scale; the rendered ring and inverse-ray sampling use the
same affine frame, including parent stretch/reflection. Local rotation preserves
original position and scale; local scale preserves position and rotation. World
operations preserve the world pivot and reject unrepresentable parent-local shear.
Scale factors are .001..1000, preserving existing reflection without crossing zero.
Snap limits: meters 0..1000, degrees 0..360, factor increments 0..10.
`workspace.tool.value` reports the last successfully applied axis distance/angle/factor
while active, and null otherwise. Agent input is isolated from incidental native
mouse motion; both sources use the same preview/commit implementation.
Plane movement, uniform-scale handles, multi-selection and richer snapping remain
unimplemented. These tools do not establish mature-editor or large-game completion.

Rendering displays the last successful mesh submission's input/visible/culled counts
and shadow/SSAO geometry draws; `workspace.view.draws` exposes the same report.
The report is null before a successful mesh call, remains unchanged after failure
and describes the last submission while view.ready independently marks freshness.
Counts reflect conservative GPU draw submissions rather than visible-pixel estimates
or measured frame rate. Picking keeps its own report and never overwrites mesh data.

Mesh submission inspection now adds `colorDraws`, `shadowDraws` and `occlusionDraws`
for actual GPU commands, distinct from `input`/`visible`/`culled`/`shadow`/`occlusion`
instance quantities. Rendering labels the two rows explicitly. Per-instance variation
is preserved after batching and does not change scene/Inspector authoring contracts.

`shadowPagesUpdated` and `shadowPagesReused` count physical shadow-atlas layers
redrawn or retained in that submission, also shown in Rendering. Retained pages
still provide shadows, with no depth draws or instance uploads; `shadow` and
`shadowDraws` count only work submitted this frame. Camera/caster/coverage changes
invalidate relevant depth automatically through the shared renderer implementation.

The Rendering pane also shows actual CPU visibility tests: BVH nodes, world AABBs,
exact local-box tests and unindexed homogeneous tests. `inspect.workspace.view.draws.spatial`
includes indexed/changed/refitted quantities and rebuild status for the last successful
mesh pass. Camera-only changes reuse bounds/tree topology. These counts exclude
linear input validation and bounds generation; they are not frame-time or FPS claims.
渲染面板显示真实空间分类次数，检查接口还报告更新/重建状态；计数不含线性输入
校验和边界生成，也不表示帧耗时或 FPS。

## Model import and instantiation

Assets Actions offers Import glTF / GLB model using the source file and a new
project `assets/...` folder. After import, select its `model.dotscene` and use Add
scene instance. Imported hierarchy, transforms, material references and editable
native files use the actual engine; insertion can be undone/redone once and saved.
The current scene's camera/environment/lighting remain authoritative.

```json
{"command":"import-model","source":"/absolute/model.glb","path":"assets/robot"}
{"command":"edit","revision":"3","operations":[{"op":"instantiate-scene","path":"assets/robot/model.dotscene","parent":null,"transform":{"position":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}}]}
```

Import optionally accepts an unsigned `scene` index 0..1023. It reports source scene,
mesh/material/image/node and animation counts, generated scene and native model
descriptor paths, and metadata warnings. TRS clips and node bindings survive import;
Add scene instance retains the animation player and remaps its node targets under
one selected placement container. Imported primary cameras/listeners are demoted.
Inspector supports clip/clock/loop/auto-play settings; Play uses the same native
GameSession as exported games. TypeScript playback controls use the same players.
Isolated editor audition is available below; keyframe authoring remains unfinished.
Import publishes reusable resources without dirtying the scene; instantiation needs the
current authoring revision. A failed catalog refresh after publication explicitly
reports that the bundle was committed. Existing destinations are never overwritten.
Native `.gltf`, `.dotscene`, `.dotmodel` and `.dotanimation` text resources
can open in the external editor; internal text transport remains only an automation buffer.
Native `.dotmesh` assets use bounded binary geometry and remain available for mesh
preview and scene use. Their OBJ/glTF sources remain editable; native mesh bytes
are excluded from text buffers and external text-editor actions.

Material coverage is persisted in `.dotmaterial` alphaMode (`opaque`, `mask`, `blend`)
and alphaCutoff fields. Imports retain glTF alpha semantics; referenced files open in
the external editor. Rendering and `workspace.view.draws` show actual `solid`, `blended`,
`solidDraws` and `blendedDraws` values from the completed GPU pass. Feature Lab includes
Glass and textured Cutout entities using the actual engine policies.

## Structured material authoring / 结构化材质编辑

Select a `.dotmaterial` asset in Assets, or use **Edit material asset** on an entity's
Material component, to open its Inspector. Assets / Actions / New material creates
a complete material at a new `assets/*.dotmaterial` path; existing files are never
replaced. The Inspector provides PBR/unlit shading, base RGBA/emission, metallic,
roughness, normal scale, occlusion strength, sidedness, Opaque/Mask/Blend and cutoff.
Five texture sections select project PNG/JPEG images and edit color space, filtering,
addressing, mipmaps and anisotropy. Data slots retain linear color space. Missing
images, unsupported normal data and exhausted budgets report actual preview errors.

Fields are an unsaved staging form and preview through the real SceneRenderer on
all scene objects referencing that shared asset. They do not write the asset or
change scene history. **Apply material** makes one material history step, avoiding
one undo entry per slider frame. **Reset fields** restores the current history value;
**Save material** applies staged fields and atomically saves. Material history keeps
128 states, retains exact original source bytes and survives Save. Undo after Save
can make the asset dirty again. Save rejects exact disk-baseline conflicts and
retains the draft for recovery. Close/discard, project close confirmation, save-close,
Play and export observe pending material fields/history. Material mutations are
blocked during Play; Play renders saved assets, and Stop restores authoring preview.
The toolbar/File/Edit save and undo/redo actions address the open material; scene
history remains separate. Script editing continues in the external default editor.
Material graphs remain unfinished; the asset browser below provides actual thumbnails and a studio sphere.

GUI and live workspace commands use the same codec, document and preview path:

```json
{"command":"create-material","path":"assets/surface.dotmaterial"}
{"command":"open-material","path":"assets/surface.dotmaterial","discard":false}
```

The response contains `material` with decimal-string `buffer` and `revision`, complete
`value`, staged `draft`, `dirty`, `canUndo` and `canRedo`. `resources` and `inspect` /
`workspace.material` expose the same state. `stage-material` takes `buffer`, `revision`
and a complete material `value` and previews it without advancing history.
`edit-material` takes those same fields and commits one history step. `reset-material`,
`undo-material`, `redo-material` and `save-material` require current buffer/revision;
`close-material` additionally requires `discard`. Opening a different material/text
buffer requires no pending resource changes or explicit discard and invalidates old
buffer identities. Invalid descriptions/unknown fields/stale requests preserve state.
Undo/redo require staged fields to be applied or reset first. Texture contents are
validated at actual preview/Play/export use, rather than silently substituted.

## Asset browser and isolated previews / 资产浏览与独立预览

Assets has grid/list modes, folder breadcrumbs and project-wide search when the
filter is nonempty. Empty search shows the current folder's immediate children.
Visible PNG/JPEG, DotMesh/OBJ and material tiles request actual previews; other file
types show their type labels. Mesh tiles frame real geometry with neutral PBR studio
shading; material tiles shade a UV sphere through the existing five-texture/coverage
pipeline. Source image aspect/alpha are retained. Missing/invalid sources show an
error, never a sample thumbnail. Click an asset for Inspector preview, click a
material for typed editing; text resources still open externally on double-click.

Material Inspector shows a 256px sphere for the current unsaved form even when no
scene entity uses that material. Previews use a separate offscreen renderer and
packaged CC0 Studio Small 09 HDRI; they do not modify scene camera, lighting, SSAO,
HDR targets, draw statistics or files. This is a static studio preview, not a
material graph or a replacement for judging the material in its scene lighting.

One owned background thread performs source decoding, HDRI loading/baking, isolated
GPU creation/rendering/readback and GPU destruction. The GUI submits owned requests
and polls up to eight completed images per frame, then uploads current results to
its own device. It does not wait for preview rendering. Same-slot requests coalesce;
material forms have priority over FIFO thumbnails. Ticket/form checks discard late
results after edits, refresh, cache eviction and close/reopen.

The worker holds at most 64 pending requests, one active request and eight completed
images (16..256px each, at most 2 MiB total completed pixels). Completion saturation
pauses starting more work. GUI caches at most 64 128px thumbnails plus one 256px
material image (each CPU/GPU image set at most 4.25 MiB); the isolated texture resolver
retains its existing 64 MiB budget. These limits do not include bounded source
image decode/transient rendering. Play pauses starting queued work; an already-active
operation may finish, and results are collected after Stop. Refresh invalidates all
tickets and requests provider recreation on its own thread, without a UI-thread join.
Initialization errors remain explicit until refresh; there is no sample fallback.

GUI texture uploads still occur on the main thread; this is not a zero-copy pipeline
or a general stall-free/FPS guarantee. Close/destruction cancels queued work and
joins the worker, allowing an active file/GPU call to finish safely. No worker is
detached or forcibly interrupted. Cached images remain immutable until save/import
or explicit refresh; visible rows are clipped. Scene/model/audio/HDR/script thumbnail
formats and asynchronous model import are separate unfinished work.

Workspace GUI and automation share these strict commands:

```json
{"command":"set-assets-view","mode":"grid"}
{"command":"set-asset-directory","path":"assets"}
{"command":"select-asset","path":"assets/copper.dotmaterial"}
{"command":"request-preview","path":"assets/normal.png"}
{"command":"preview-assets"}
{"command":"refresh-previews"}
{"command":"clear-asset-selection"}
```

`set-assets-view` accepts grid/list; an empty directory path selects the project
root. Asset selection/directory requests validate against the current catalog.
`request-preview` queues supported files and does not claim generation succeeded.
`preview-assets` and `inspect.workspace.previews` report pending count, processed jobs,
ready/error/warning states, current view/directory, device presence and material
preview readiness/pending/error matched to the current form. Worker state includes
active/paused/stopped, pending/completed and submitted/started/finished/discarded
counters. Dirty material forms must be saved/discarded before asset
selection changes. `captureAssetPreview` returns owned pixels only for completed,
current cached previews; stale/pending/failed requests return errors. Scene/model
hierarchy, HDR panorama, audio and script thumbnails are not implemented.

Hierarchy mouse selection is committed after the tree traversal releases its scene
snapshot references. `inspect.workspace.hierarchyRows` reports the actual rendered
row rectangles and expansion state by entity ID for native input automation.
Selecting a parent, child or the current entity does not mutate scene data or add
undo history; a changed selection advances the document revision.

## Scene multiselection and group edits

The document owns an ordered set of selected entities. `selection` contains unique
canonical ID strings; `selected` is its last member (the active entity), or null
for an empty set. Selection changes advance the revision without dirtying the
scene or adding undo entries. Scene edits/undo/redo retain surviving selections
and restore the selection stored with each history entry. Selection ID payloads
count toward the existing bounded history budget. Saved scene files contain no
editor selection, so opening a project starts with none.

Hierarchy plain clicks replace selection; Command-click on macOS / Ctrl-click on
Windows/Linux toggles a member. Shift-click selects the inclusive open/filtered
row range from the anchor; adding Command/Ctrl preserves the prior selection.
The clicked range endpoint becomes active, including when selecting backwards.
Expanding an arrow does not select an entity. Highlighting shows every member.
The Inspector states the selected count and edits only the active entity's
properties. Viewport picking and Focus currently act on a single active entity.
Move/Rotate/Scale gizmos transform the current selection around its active pivot.
Group framing, box selection and mixed-value bulk properties remain planned.

The toolbar Duplicate (Command/Ctrl+D) and Delete operations act on the whole
selection in one undo transaction. Descendant unions prevent parent/child overlap
from duplicating/removing a subtree twice. Duplication assigns new monotonic IDs,
remaps internal parents, retains external parents/native components/asset bindings,
adds bounded UTF-8 ` Copy` names and selects the mapped input entities in order.
Copies of cameras/audio listeners are non-primary; originals remain primary.
Script properties are opaque and are copied unchanged, without guessing that an
arbitrary property string/number refers to an entity. Unsupported final scene
states (such as exceeding the light/entity limit) fail transactionally.

Shared GUI/agent commands require the current canonical decimal revision:

```json
{"command":"select-many","revision":"7","entities":["2","5"]}
{"command":"select-hierarchy","revision":"8","entity":"9","mode":"range-add"}
{"command":"duplicate-selection","revision":"9"}
{"command":"delete-selection","revision":"10"}
```

`select-many` replaces the ordered set, accepting an empty array to clear it;
duplicates, stale/foreign/missing entities and more than 10000 handles fail.
`select-hierarchy` uses the current displayed hierarchy order and the modes
replace/toggle/range/range-add; unknown/hidden IDs or modes fail. Explicit document
selection/edit/undo/redo resets its anchor to the active entity. Raw `edit` batches
can use `duplicate` or `remove-many` operations with a nonempty `entities` array.
Group deletion/duplication rejects an empty selection. Scene draft/Play/gesture
guards apply to GUI and agent paths alike.

Group gestures freeze the selected IDs, active pivot/frame, root world poses and
external parent inverses at the start. Only topmost selected roots receive local
changes; selected/unselected descendants inherit once through the hierarchy.
World operations use world axes around the active origin. Group Local rotation/
scale use the active entity's frozen parent-world × local T×R frame, excluding
its own scale while retaining ancestor stretch/reflection. This is a shared group
frame, not independent rotations around each member's center.

All samples are absolute from the gesture start; position/orientation/scale do not
accumulate rounding across pointer events. Every root must have representable
parent-local TRS. Shear/singular/overflow/invalid-factor failures leave the entire
last valid draft unchanged. Returning to the origin restores original local values
exactly. Escape/focus loss/cancel discard the owned draft; release commits one
typed transform transaction and selection-aware undo entry. Preview and committed
rendering use the same scene path. Single-object local behavior remains unchanged.

`begin-transform`/`update-transform`/`finish-transform` control the same group gizmo
used by native mouse input. `inspect.workspace.tool` reports frozen `entities`,
topmost `roots`, active `entity` and `pivot: "active"` during a gesture. The typed
Document transform transaction supports up to 10000 current-scene handles without
the generic JSON edit operation-count/transport bound; existing scene/history
limits still apply. Median/individual-origin pivot modes and multiselection group framing remain
unimplemented.

Saved [skin bindings](Skinning.md) render automatically in preview and isolated Play.
The Skin Inspector chooses an existing .dotskin asset, toggles enabled state and
edits ordered live joint entities; a mesh is required. Applying uses ordinary native
component/history validation. Duplicate requires all joints in the selected subtree
union, remaps their IDs and rejects external references without partial edits.
Model-scene instantiation remaps the same references. Bounds/framing/picking use the
current deformed mesh. Skeleton visualization and keyframe timeline authoring remain
unfinished; isolated animation playback/scrubbing is available below.

Move a character root that contains its joints, or edit the joint transforms, to
pose a skinned mesh. Its mesh-owner world transform cancels through the skin palette;
moving only that mesh entity does not move the joint-driven world geometry.

glTF/GLB import now creates these same native skin assets and scene components.
`import-model` reports generated `skins` and `skinnedMeshes`; instantiate the returned
scene to insert the complete joint hierarchy and animation bindings. Every copied
joint receives a fresh entity ID. Asset import does not silently insert a character
or enable different collision shapes. The [skin regressions](Skinning.md) check actual
import, Inspector, duplicate/undo, isolated Play and relocated native export.

Viewport inspection now includes a nullable `view.error` string with the latest
actual preview failure. `captureViewport` includes that context when no preview can
be captured; it does not report a successful frame for a failed asset load. A later
successful draw clears the error. A native missing-mesh/undo case checks diagnostic
visibility and recovery of the original pixels after the Docking layout settles.

## Viewport PNG capture / 视口 PNG 捕获

File → Save viewport PNG captures the actual completed scene render target, including
its current game/editor camera and material preview, without rerendering another view.
The PNG contains scene pixels only, excluding editor panels and transform handles.
Pixels are frozen before SDL opens the native save dialog, so Play may continue while
choosing a destination. Save captured PNG retries a retained image; Discard viewport
capture releases it. Native cancellation also releases the image.

File → Save viewport PNG 捕获实际完成的场景渲染目标，保留当前游戏/编辑相机与材质预览，
不另行渲染其他视角。PNG 仅含场景像素，不含编辑器面板和变换手柄。打开 SDL 系统保存
对话框前固定像素，因此选择目标期间 Play 可以继续。Save captured PNG 可重试保留的图像；
Discard viewport capture 和系统取消均释放图像。

The same operations are available through the live workspace mailbox:

```json
{"command":"capture-viewport","revision":"1","dialog":false}
{"command":"save-capture","capture":"1","parent":"/absolute/existing/folder","file":"viewport.png","overwrite":false}
{"command":"browse-capture","capture":"1"}
{"command":"discard-capture","capture":"1"}
```

Each object is a separate strict request. All listed fields are required; unknown
fields fail. Capture requires the current authoring revision and queues a new frame.
`dialog:true` requests the same native dialog as the menu. Inspect
`workspace.capture.state` before saving: `queued` acknowledges the request only;
`ready` means owned pixels exist. The next native render iteration resolves the request;
a hidden/deferred/failed viewport or changed scene/revision/view mode/Play session
fails rather than exporting an older frame. At most one queued/ready image is retained.
Finish or discard it before requesting another. IDs are canonical decimal strings,
monotonically increasing for this workspace; stale IDs cannot save/discard newer images.

`workspace.capture.frame` records the actual frame serial, authoring revision, scene
scope, Play or animation-preview session (zero otherwise), Play frame (zero outside Play),
mode/source, pixel dimensions, column-major
view-projection matrix and material-preview flag. These describe a completed capture
only in `ready`/`saved`; queued metadata describes the requested context and prior serial.
Later rendering, camera movement, Stop and document edits do not change retained pixels
or provenance. Save does not change document history/revision/dirty state. On success,
state becomes `saved`, pixels are released and `output` contains an owned absolute path
and decimal byte count. Errors/cancellation are observable through inspection.

The parent must be an existing absolute UTF-8 directory. The filename must be one
portable component ending in `.png` (case-insensitive). Existing files require explicit
`overwrite:true`; symlinks, special/read-only files and ASCII case aliases are rejected.
A prepared export checks an unchanged byte baseline before atomic replacement. Existing
content and encoded PNGs are bounded to 64 MiB; parent scans to 65536 entries. This is
serialized local editing, not hostile concurrent filesystem isolation or crash durability.
Save failures retain the image for retry/discard. A catalog refresh failure after file
commit explicitly reports that the PNG was saved; the capture remains `saved`.

Native save confirmation supplies explicit overwrite consent. While its dialog is open,
conflicting capture/save/discard commands and window close are rejected to retain the
parent window. Native dialog errors retain the frozen image and expose a message for
retry. Agent clients normally use `dialog:false` and direct `save-capture`, without OS UI.

每条 JSON 是独立的严格请求，字段必须完整且不能多余。先轮询 `workspace.capture.state`，
`queued` 仅表示受理，`ready` 才表示像素已固定。隐藏、延后、失败或上下文变化的视口明确
失败，不保存旧画面。一次至多保留一张图；过期编号不能作用于新图。检查结果包含实际帧号、
修订、场景范围、Play 会话/帧、相机矩阵、像素尺寸及材质预览状态。后续运行、Stop 或编辑
不改变已捕获图像，也不影响场景历史或保存状态。失败可重试，成功后释放像素并报告绝对路径
与字节数。已有目标须明确允许覆盖，拒绝链接、特殊/只读文件及大小写别名；原子替换前校验
字节基线。系统对话框打开时保留父窗口并拒绝冲突操作；智能体通常直接指定路径保存。

## Isolated animation audition / 独立动画预览

Select an entity with an enabled Animation player and use Preview animation in the
Inspector. Scene controls provide Play/Pause, Play clip, a time slider, speed [-16,16],
looping, Bind pose, Frame model and End preview. The controls resize with their content
and scroll within a bounded height in narrow panels. Current native clip names are
shown alongside the asset library. Selecting a clip starts it from its directional
start. Seeking preserves playing/paused state; pause first for a stationary sample.
Bind pose restores original animated fields and pauses. Playing from bind pose starts
the directional clip again. These runtime controls never save player settings or poses.

The editor clones the complete unsaved authoring scene into a separate scope. It uses
Dot.Animation.ScenePlayers, exactly as game sessions do, with all cloned auto-play
flags disabled. Only the chosen player is controlled; others retain authored poses.
Their writer reservations, library validation and rigid/character authority checks
still apply. Missing or conflicting resources fail instead of showing a substitute.
No scripts, physics simulation, audio device, compiler or independent animation timer
is started. Native elapsed time advances the chosen animation explicitly. Clip data
is owned after loading; external clip changes require ending and starting audition.

Start requires the current authoring revision, an enabled player, no other preview/
transform/Play mode, no pending scene/file drafts, close confirmation or save dialog.
Authoring and resource changes, hierarchy selection, scene save/export and Play are
locked until End preview. Navigation, camera focus, layout/theme, capture/PNG export,
inspection and close remain available. Authoring selection/history/revision/dirty state
are unchanged. Close can retire an active preview without running game stop hooks.

Strict product requests share the GUI implementation:

```json
{"command":"preview-animation","revision":"2","entity":"281"}
{"command":"inspect-animation-preview","session":"1"}
{"command":"control-animation-preview","session":"1","action":"play","clip":1,"restart":true}
{"command":"control-animation-preview","session":"1","action":"pause"}
{"command":"control-animation-preview","session":"1","action":"seek","seconds":0.5}
{"command":"control-animation-preview","session":"1","action":"configure","speed":-1,"looping":false}
{"command":"control-animation-preview","session":"1","action":"bind-pose"}
{"command":"control-animation-preview","session":"1","action":"end"}
```

Each line is a separate object with exactly its listed fields. Entity/revision/session
are canonical decimal strings; clip is a nonnegative integer within the native library.
Seconds must be finite within [0,duration]. Session identities increase even at the
same authoring revision. Old/ended session requests cannot affect a newer preview.
`inspect` reports `workspace.animationPreview`: session, entity, source scene scope,
generation, current clip/path/name, library paths, duration/time, speed, looping,
playing, bindPose and an optional native fault. `inspect-animation-preview` additionally
returns the actual posed clone scene. Saved player descriptions in that clone have
auto-play disabled; inspect the ordinary document for the untouched authoring data.

Invalid controls preserve pose, clock and generation. Native advancement errors retain
the last valid pose and expose a fault; End preview and recreate to recover. This is
animation audition. Existing-clip key editing uses the separate resource workflow
below; visual transitions, skeleton authoring and retargeting remain unfinished.
It does not pause or step a running Play session.

Frame model uses the ordinary revision-checked `focus-view` with the player entity.
Focus now includes visible meshes in the selected subtree, resolving IDs into the
displayed scene and measuring real posed skin bounds. A selected empty group falls
back to its own bounds; null entity still frames the complete visible scene.

Viewport PNG capture remains available. Its frame source is `animation`, session
identifies the audition and `animation` records entity/generation/clip/time/bindPose.
Binding pose is explicitly distinct from a paused clip at time zero. Pixel/provenance
snapshots remain fixed after seeking, clip changes, End preview and later Play.
Stale generations cannot be read as a current viewport frame.

选择启用动画的实体，在 Inspector 中启动 Preview animation，再通过 Scene 控件选片、
播放/暂停、拖动时间、调整速度/循环、恢复绑定姿态及结束预览。Frame model 按实际蒙皮
子树边界取景。控件随内容收缩，窄面板自动换行并在有界区域滚动。
预览克隆未保存场景并使用与游戏相同的原生动画播放器，不启动脚本、物理模拟或音频。
其他播放器保留编辑姿态，同时继续校验资源与写入权限。预览期间锁定编辑和 Play，结束后
恢复完整编辑场景，选择/历史/修订/保存状态不变。所有操作共用严格 JSON 实现；检查可读取
实际预览场景、片段、时间及绑定姿态标记，过期会话无法影响新预览。PNG 同时记录该来源，
结束预览后仍可保存已固定的画面。本功能用于动作检查；现有片段关键帧可通过下述编辑
流程修改，骨架绑定、曲线和状态图工具仍待完成。

## Live skeleton inspection

Window → Skeleton opens a dockable read-only viewer. Choose an enabled mesh skin;
Off releases its cached asset/frame. The cyan x-ray joints/bones use actual world
positions and nearest bound ancestors, including unbound helper nodes and multiple
roots. The selected joint and its incoming bone are yellow. Overlay geometry is
projected after the same scene frame renders, clipped to its viewport, respects GUI
window order and stays out of scene PNG/HDR pixels. It does not depth-test surfaces.

Select a joint in the filtered list or click within six logical pixels of its
projected point in Scene. Hover the list for palette index, world coordinates and
parent/link distance. Frame skeleton and Frame joint use actual padded world bounds;
Play retains its game camera. Edit selected joint selects the real authoring entity
through ordinary revision-checked document selection, then opens Inspector. Existing
transform/undo tools edit that node; this does not create animation keyframes. Editing
is disabled during animation audition, transforms and Play. Bone inspection/selection
and filtering remain available, without changing authoring history or saved state.

All controls use strict shared product operations. Read `workspace.view.scene` to
obtain the current canonical scene scope, which changes for clones/document updates.
Every request below requires that current scope. Replace sample IDs with observed IDs:

```json
{"command":"set-skeleton","scene":"12","entity":"283"}
{"command":"inspect-skeleton","scene":"12"}
{"command":"select-skeleton-joint","scene":"12","entity":"283","joint":"282"}
{"command":"select-skeleton-joint","scene":"12","entity":"283","joint":null}
{"command":"focus-skeleton","scene":"12","entity":"283","joint":null}
{"command":"focus-skeleton","scene":"12","entity":"283","joint":"282"}
{"command":"pick-skeleton","scene":"12","uv":[0.5,0.5]}
{"command":"filter-panel","panel":"skeleton","query":"Hand"}
{"command":"show-pane","pane":"skeleton"}
{"command":"set-skeleton","scene":"12","entity":null}
```

Unknown fields fail; IDs are canonical decimal strings. UV uses the latest completed
viewport with top-left origin and finite [0,1] coordinates, exactly like a native
click at processing time. The pick result includes the actual frame serial and hit
entity or null; a miss preserves selection. A hidden/unready viewport or changed
scene/view rejects picking. It does not promise that an independently moving pose
remains at a previously inspected UV. Inspector editing uses ordinary `select` and
`edit` requests with current authoring revision, as elsewhere in this document.

`workspace.skeleton` and `inspect-skeleton.skeleton` report enabled/target/selected,
filter, readiness and observable native errors. Only a current completed viewport
publishes `frame`: serial, actual scene scope, asset, root count, palette-ordered joints
with entity/name/nearest parent/parentLinks/world/optional projected UV, projected point/
bone counts and Play frame. Offscreen joints retain world data with null UV. Projected
counts describe x-ray projection, not occlusion visibility. The GUI list explicitly
labels its last completed frame while live animation advances.

The viewer follows the target entity value into the active audition/Play/transform
scene and revalidates bindings each render. Old scoped requests fail; removed joints
clear selection on successful rebinding. Missing/invalid resources clear old geometry
and expose retryable errors; invalid controls preserve prior settings. Immutable skin
assets are cached by project root/path; the normal resource refresh invalidates this
cache. No scene data, scripts, physics or audio are started by skeleton inspection.

骨架面板查看真实蒙皮节点，透视线与同一已完成场景帧对齐，不写入场景导出像素。
关节列表/视口拾取、搜索、全骨架/单骨段取景均有同源严格 JSON 操作；场景范围避免旧请求
影响新克隆。编辑关节复用普通文档选择和 Inspector/撤销，不代表关键帧或重定向制作工具。
预览/Play 中保持编辑场景不变，资源失败清除旧几何并报告错误。

## Animation clip editing

Select an enabled animation player in Inspector and choose Edit animation clip.
This opens its selected existing library asset in a resource document and starts an
isolated native draft preview. The Animation tab contains named tracks, key selection
and the playhead. Inspector edits the selected key's local TRS value/time and cubic
in/out tangents. Component rows preserve readable float precision in narrow panels.
Track/key selection scrolls into view. The ordinary Scene play/pause, bind-pose and
frame-model controls remain available; Playback options provides speed/looping.

The Curves column shows the selected channel's native sampled XYZ/XYZW components,
authored keys, value range and playhead. Hover reads a native sample; click a key to
select, click empty space to seek. From/To inputs apply the time range on Enter; Fit
time restores the clip range. The default lower dock now reserves about a third of
the workspace so the curve remains visible. Narrow docks wrap the time controls.

Insert / replace key explicitly applies the key form: equal float time replaces a
key, another time inserts without implicitly moving/deleting the former key. Reset
key form discards unapplied values. Key at playhead prepares a key from that bound
node's actual current local pose. A new channel can be prepared from any of the
player's real bound nodes. Rotations are unit XYZW quaternions, translation/scale
use XYZ with W=0. Scale cannot be singular. Changing interpolation explicitly clears
tangents; cubic needs at least two keys. Delete key removes an empty track; deleting
the last remaining channel fails because the native clip requires a channel.

Update / move selected key explicitly replaces the originally selected key using the
staged time/value/tangents. It keeps key count/interpolation and rejects an occupied
destination. This requires an existing selected source on the same channel/mode;
preparing a new key at an empty time must use Insert / replace key instead. Tangents
remain derivatives per second and are not automatically rescaled when moving time.
Horizontal curve dragging previews a ghost time, then submits one update-key edit on
release. Values/tangents are retained. Escape, focus loss, changed view, track or
resource revision cancel the gesture. No history state or posed scene changes during
the drag; a valid release is one resource undo step. Conflicts retain the old key.
Native curve-drag acceptance remains outstanding: computer-use drag attempts have not
produced a confirmed movement, including a comparison with the ordinary time slider.
The strict update/move resource operations are verified through actual product workflows;
this observation does not establish that native pointer behavior is fully validated.
The current macOS virtual-input mismatch is documented under pointer input diagnostics;
it is not evidence of a failed physical-mouse gesture.

Save clip, Undo clip and Redo clip affect the resource document, independently of
scene authoring history. File/Edit menus and existing save/history shortcuts route
to the animation document while it is open. A pending key form must be applied or
reset before save/history/selection changes. Close clip requires a clean document
and form; Discard and close explicitly releases unsaved work. Window/project close
uses the ordinary unsaved confirmation. No destruction implicitly saves.

The unsaved confirmation names the animation resource. Cancel preserves preview,
history and the form. Save and close uses the same animation save implementation,
then saves only the authoring document and refreshes its snapshot before closing;
it never serializes a posed preview as the scene. The global save-close/cancel-close
requests use the current authoring revision. Pending key forms must first be applied
or reset after Cancel. File conflicts retain the dialog/draft and remain retryable.
Saving multiple workspace resources is sequential: a later scene/refresh failure may
leave an already saved animation clean, with the preview/history retained for review.

Every operation uses the same strict product implementation. The ordinary inspect
response reports `workspace.animationClip`: buffer/path/name/resource revision,
duration, channel/key counts, dirty/undo/redo, history states/serialized bytes, key
form, pendingForm, ordered selection and the optional move transaction. The ordinary
authoring revision/scene/dirty state remains unchanged. `workspace.animationPreview.draft`
identifies the decoded asset path.
`animationClip.curve` reports the current time range; changing that view does not alter
resource revision/history, preview identity/clock, or staged key values.
Normal audition still supports library switching; close the editing document before
switching to another clip. Authoring/resource changes and Play are locked until close.

Open with the current authoring revision. All subsequent mutations use the observed
animation buffer and resource revision (canonical decimal strings); inspect requires
only the current buffer. Each JSON object below is a separate request shape. Observe
the new resource revision/session after mutations instead of reusing earlier IDs:

```json
{"command":"open-animation","revision":"1","entity":"281","clip":0}
{"command":"inspect-animation-clip","buffer":"1"}
{"command":"list-animation-tracks","buffer":"1","revision":"1","offset":0,"count":64}
{"command":"read-animation-track","buffer":"1","revision":"1","node":8,"path":"translation","offset":0,"count":64}
{"command":"select-animation-key","buffer":"1","revision":"1","node":8,"path":"translation","time":0}
{"command":"select-animation-key","buffer":"1","revision":"1","node":8,"path":"translation","time":null}
{"command":"stage-animation-key","buffer":"1","revision":"1","value":{"node":8,"path":"translation","interpolation":"linear","key":{"time":0,"value":[0,0.16,0,0],"inTangent":[0,0,0,0],"outTangent":[0,0,0,0]}}}
{"command":"apply-animation-key","buffer":"1","revision":"1"}
{"command":"move-animation-key","buffer":"1","revision":"1"}
{"command":"reset-animation-key","buffer":"1","revision":"1"}
{"command":"undo-animation-clip","buffer":"1","revision":"2"}
{"command":"redo-animation-clip","buffer":"1","revision":"3"}
{"command":"save-animation-clip","buffer":"1","revision":"4"}
{"command":"close-animation","buffer":"1","revision":"5","discard":false}
```

Fields are exact; unknown/duplicate/missing fields and wrong numeric types fail.
`clip` is an unsigned native library index; `node` is a logical integer 0..9999,
independent of world entity IDs. Track pages include actual bound entity/name, path,
interpolation and key count. Page offsets are unsigned and cannot exceed the total;
count is 1..256. Key pages contain exact float32 time/value/tangents. Time input is
converted to finite native float seconds, and exact equality after conversion selects
or replaces a key. Staged forms canonicalize those floats so values such as .0125
round-trip to the same key. `time:null` prepares the current local pose at the playhead,
rather than choosing an approximate nearby key. Staging checks structure/numbers;
final key/native pose validation occurs on application and preserves the form on error.

Batched edits use the same native edit layer, with 1..256 ordered operations. Supported
operation shapes are:

```json
{"command":"edit-animation-clip","buffer":"1","revision":"1","operations":[{"op":"rename","name":"Idle inspection"}]}
{"command":"edit-animation-clip","buffer":"1","revision":"1","operations":[{"op":"put-key","node":8,"path":"translation","interpolation":"linear","key":{"time":0.25,"value":[0,0.16,0,0],"inTangent":[0,0,0,0],"outTangent":[0,0,0,0]}}]}
{"command":"edit-animation-clip","buffer":"1","revision":"1","operations":[{"op":"remove-key","node":8,"path":"translation","time":0.25}]}
{"command":"edit-animation-clip","buffer":"1","revision":"1","operations":[{"op":"update-key","node":8,"path":"translation","time":0.25,"key":{"time":0.5,"value":[0,0.16,0,0],"inTangent":[0,0,0,0],"outTangent":[0,0,0,0]}}]}
{"command":"edit-animation-clip","buffer":"1","revision":"1","operations":[{"op":"retime-keys","keys":[{"node":8,"path":"translation","time":0.25,"target":0.5},{"node":8,"path":"translation","time":0.5,"target":0.25}]}]}
{"command":"edit-animation-clip","buffer":"1","revision":"1","operations":[{"op":"remove-track","node":8,"path":"translation"}]}
{"command":"edit-animation-clip","buffer":"1","revision":"1","operations":[{"op":"set-interpolation","node":8,"path":"translation","interpolation":"cubic"}]}
```

Path is translation/rotation/scale; interpolation is step/linear/cubic. Non-cubic keys
require zero tangents. Retime-keys resolves 1..16384 unique sources simultaneously,
permitting selected-source swaps while rejecting occupied unselected or duplicate
targets. It preserves all key payloads, count and channel modes. Batches validate the
final immutable native clip. The product preflights the candidate against the actual authoring scene/bindings and native writer/
physics authority before changing history. Enabled skins are CPU-validated both at the
new preview's initial sample and the retained current sample, catching poses that are
finite yet outside the supported skin matrix range. Failed binding/sample/skin checks
retain the old draft/history/preview/clock/session. This preflight does not prove every
future cubic sample or populated-scene performance.

Successful content edits and history changes replace only the isolated preview clone,
retain speed/looping/playing and clamp the old clock to a shorter new duration. Bind
pose stays explicit. New scene/session identities invalidate old control/capture
requests. Semantic no-ops retain revision, history/redo and preview session without
reloading assets. Save checks exact original disk bytes before atomic replacement,
retains history and leaves the already owned draft preview intact. Conflicts preserve
both disk and draft; a post-save catalog failure explicitly reports that saving occurred.
Close/reopen and later Play load the actual saved clip. Frozen PNG capture remains
available through the ordinary scene capture operations and survives later edits/close.

Curve view and bounded sample queries also use the current buffer/resource revision:

```json
{"command":"set-animation-curve-view","buffer":"1","revision":"1","from":0,"to":2.5}
{"command":"read-animation-curve","buffer":"1","revision":"1","node":8,"path":"rotation","from":0,"to":2.5,"samples":256}
```

Times must be finite numbers in 0..86400 seconds, with at least a one-microsecond
span. Queries require an unsigned count 2..1024 and return owned `points` with exact
double sample times and native float32 value components, including both endpoints.
They do not seek the preview, alter the key form, or validate unrelated channels at
those times. A native singular/overflow sample fails the query with its time; no
partial/fake-success curve is returned. Invalid view/query requests preserve state.

The GUI caches a 256-sample polyline per resource revision/channel/time range, with
at most 2048 visible authored keys. Narrow the range for denser tracks. Step lines
use authored discontinuity times; linear/cubic/rotation lines are sampled approximations,
with shortest-arc SLERP and normalized quaternion components from native evaluation.
Component W is shown for rotation only. The graph autofits visible sampled values and
keys; it is not a proof of all extrema or valid future poses. Curves expose sampling
errors rather than hiding them. Tangent values remain editable through Inspector.

Key movement has a scoped product transaction shared by GUI dragging and agents:

```json
{"command":"select-animation-keys","buffer":"1","revision":"1","keys":[{"node":8,"path":"translation","time":0},{"node":8,"path":"rotation","time":0}]}
{"command":"begin-animation-key-move","buffer":"1","revision":"1"}
{"command":"update-animation-key-move","session":"1","time":0.0125}
{"command":"finish-animation-key-move","session":"1","commit":true}
```

Selection alternatives and gesture cancellation have these request shapes:

```json
{"command":"select-all-animation-keys","buffer":"1","revision":"1"}
{"command":"select-animation-keys","buffer":"1","revision":"1","keys":[]}
{"command":"finish-animation-key-move","session":"1","commit":false}
```

Selection references are exact node/path/native-float times, ordered without duplicates,
with at most 16384 keys across tracks. Empty selection clears the form. Invalid or
stale selection retains the previous selection/form; selecting requires a clean form
and no active move. The last reference becomes the active Inspector key and move
anchor. The existing select-animation-key operation replaces selection with one exact
key; time:null prepares a pose form and clears authored-key selection. The strict
select-all-animation-keys request builds the same bounded selection in the product,
without requiring a large request body. An oversized clip is rejected without partial
selection. Explicit arrays also remain subject to the ordinary 1 MiB request bound.
Selection does not change resource history, preview or disk; ordinary changed edit
batches/undo/redo clear it, while no-op batches retain it. Paste explicitly selects its
destination keys even if replacing identical content. Multi-selection locks single-key
value/tangent form edits; select one key to use those controls.

Begin consumes the current nonempty selection and requires current resource IDs,
a clean form and no other move/dialog. It returns an independent move session.
Observe workspace.animationClip.move (null when absent): canonical session/buffer/
revision strings, ordered keys (node/path/sourceTime/time/occupied), absolute anchor
time, changed and occupied. Sessions increase across resource reopen; retired IDs
cannot affect a new gesture. Update takes the anchor's absolute finite native-float
time in 0..86400 seconds. The tool adds the same double offset to each original
source, then rounds each destination to native float. Relative timing is therefore
preserved up to float rounding; any resulting duplicate destination rejects commit.
Every translated key must remain in 0..86400, even if the anchor is valid. Invalid
updates retain the preceding ghost. Returning to the anchor origin restores all
original float representations exactly.

Updates move only curve ghosts, without copying/publishing the clip, changing preview
pose/clock/session or history, or writing files. Selected sources vacate together;
the group may occupy their former positions. Unselected occupied destinations and
float-collapsed selected destinations remain inspectable, and commit rejects them
without losing the gesture. Ghosts retain values/tangents/interpolation.

Commit uses one native RetimeKeys operation, initial/current pose and skin preflight,
and document publication boundary. A changed successful commit creates one history
step regardless of update/key count, retains key count/value/tangents/interpolation,
selects all moved keys and retires the move. No-op commits retain revision, redo and preview
identity. Preflight/history failures retain the move and previous resource/preview.
Cancel simply discards the ghost. Preview playback/seek/configuration and read queries
remain available; commit validates the current sample at that moment. Competing edits,
form/selection/range changes, save/history and clean resource close are blocked. Explicit
resource discard retires the transaction. An active move is pending work even when
unchanged: window close prompts, save-close is rejected, and cancel-close or move
cancellation preserves recovery without an implicit save.

The Animation panel supports Ctrl/Cmd toggle across tracks and Shift inclusive ranges
on the active track (Ctrl/Cmd+Shift adds a range); the clicked key becomes active.
Select all keys and Clear selection use the same product operations. Move selected
keys starts a group gesture. Dragging an already selected curve marker moves that
group; dragging an unselected marker selects/moves that one key. Modifier-clicking
markers only changes selection. The same ghosts and Move time / Apply move / Cancel
move controls expose occupied-time feedback; Move time is the last selected key's
absolute anchor time. A pointer drag includes the final release position, commits on
release, and cancels on Escape, lost focus or a changed graph width.
A failed commit keeps the ghost for correction. Agent-created gestures have no pointer
binding and remain available without OS focus; native focus/Escape cancellation applies
only to a pointer-bound drag. Computer-use virtual mouse acceptance remains limited by
the observed SDL/macOS coordinate behavior, separately from product-operation checks.

移键使用同源选择/开始/锚点绝对时间更新/提交/取消协议；支持跨轨道 16384 键有序选择，
末键为活动锚点。每个原键加同一 double 偏移后转 float，舍入重叠时拒绝提交；返回原位
精确恢复原 float。组内键同时腾位，不覆盖未选键。更新只移动标记，不修改片段/姿态/
历史/磁盘，失败可调整/取消；整组成功只产生一步撤销，数量/值/切线/插值不变。GUI
支持 Ctrl/Cmd 增减、Shift 范围及全选/清空，智能体使用同源严格操作。活动操作参与关闭
确认，旧会话不能影响重开的资源；智能体无需系统焦点，原生鼠标拖动另有焦点丢失取消。

Copy and paste reuse selected key groups within the current authoring scene/player:

```json
{"command":"copy-animation-keys","buffer":"1","revision":"1"}
{"command":"inspect-animation-clipboard"}
{"command":"read-animation-clipboard","clipboard":"1","offset":0,"count":64}
{"command":"paste-animation-keys","buffer":"2","revision":"1","clipboard":"1","time":0.25,"overwrite":false}
{"command":"clear-animation-clipboard","clipboard":"1"}
```

Observe clipboard IDs from successful copy/inspect rather than reusing an old example
ID. Copy requires current resource IDs, a nonempty authored-key selection, a clean form
and no active move/close/capture dialog. It changes only the workspace's typed clipboard;
selection, resource history, preview and files remain unchanged. Failed copies retain
previous content and its ID. The clipboard survives resource edits, undo/redo, save,
source discard and resource close/reopen. It owns copied values/tangents and modes, so
it does not reread the source animation file. Candidate preview preflight still checks
the actual destination player's complete library and referenced assets. The clipboard
is separate from the OS clipboard and is not persisted across workspace/project closure.

Ordinary inspect exposes workspace.animationClipboard, null when empty. Metadata has
canonical id/source/entity/buffer/revision strings, the source asset path, key/channel
counts, from/to (minimum/maximum copied time), and compatible for the currently open
animation document. Read pages require the current clipboard ID, an unsigned offset
up to key count, and unsigned count 1..256. Entries retain copy selection order and
contain node/path/interpolation/key (time/value/inTangent/outTangent). An offset at the
end returns an empty page. Inspect/read/clear are independent of an open resource or
Play; stale clear/read requests cannot affect a newly copied group. Clear retires the
ID, and later successful copies issue a new ID.

Pasting requires the same live authoring scene identity, player entity and complete
logical-node/entity binding map as the copy. Matching node numbers alone do not imply
matching skeletons. A different clip in the same player's library is supported; a
different player/scene or changed authoring scene requires copying again in that
context. No implicit bone mapping or retargeting is performed. compatible:false after
resource close means there is no current destination, not that owned data was lost.
Source buffer/revision/path remain provenance rather than destination requirements.

Time places the earliest copied key, independently of the last-selected move anchor.
All copied times add the same double offset before native float rounding and must
remain within 0..86400. Same-channel float-collapsed destinations fail. Values, tangent
derivatives and channel modes remain unchanged. Existing destination channels require
the same interpolation mode; absent channels inherit the copied mode. A partial cubic
selection is valid clipboard content, but its final destination channel must still
contain at least two valid keys. Mode conversion is an explicit ordinary edit.

Overwrite is a required boolean. False rejects every occupied exact native-float time;
true replaces only equal-time keys and leaves neighboring/unselected keys intact.
The whole paste uses one native edit plus the ordinary initial/current pose and skin
preflight, and publishes one resource history step. Preparation/preflight/history
failures retain clipboard, document/history/preview/selection and files. Successful paste selects all destinations
in copy order. Identical explicit replacements retain revision, redo and preview
identity; destination selection is still explicit. Saving uses the ordinary conflict-
checked resource save, and later preview/Play load the actual saved data. A rare
post-publication selection failure explicitly reports that keys were pasted and clears
stale selection; it does not claim rollback.

The Animation panel's Copy keys and Paste keys controls use these same operations.
The paste popup shows source/count/range, a destination time and Replace matching key
times (off after each new copy). It reports errors without closing the popup or discarding
copied content. Clear copied keys is available there and when no animation document is
open. Clipboard snapshots are owned and remain safe if content is cleared while the
GUI is drawing. Native computer-use mouse acceptance remains subject to the recorded
SDL/macOS input boundary; visible controls do not by themselves prove pointer delivery.

复制/粘贴复用同一编辑场景、同一人物播放器及完整节点绑定；可跨其库内不同动作，不把相同
节点编号当成骨架兼容，也不隐式重定向。复制保留选择顺序、值/切线/模式，与源草稿及文件
生命周期无关；失败保留旧内容，清空/替换后的旧标识不能影响新内容。粘贴把最早键放到指定
时间，各键经同一 double 偏移后转 float，默认拒绝占用，显式允许才替换同时间键，绝不自动
改变目标通道插值。三次部分键可复制，粘贴结果仍须合法。整次通过原生姿态/蒙皮预检后提交
一步历史，成功选择目标，失败不改草稿/预览/文件；系统剪贴板不受影响。

This workflow edits existing bound library clips. Dope-sheet, multi-key value and
graphical tangent-handle editing, visual binding/retargeting, state graphs/layers,
root motion/events and production animation scalability remain unfinished.

选择动画播放器后可编辑已有库片段：底部选择具名轨道、关键帧及时间，Inspector 编辑局部
值和三次切线。键表单需显式应用/重置；插入与替换按原生 float 时间判断，不隐式移动旧键。
候选片段和当前蒙皮姿态预检后才提交历史，失败保留旧草稿、姿态、时钟及会话。资源保存/
撤销独立于场景历史，磁盘冲突不覆盖外部修改；关闭须保存或明确丢弃。所有操作有同源严格
JSON 接口，查询有界分页，旧资源/预览标识不能影响新会话。图形切线、绑定、状态图和大规模
动画生产能力仍待完成。

曲线面板现可查看原生 XYZ/XYZW 采样、选择键和时间范围。显式更新/移动已有键保持数量，
新时间冲突时不覆盖原键；插入仍是独立操作。曲线仅是有界近似显示，不能证明全部中间姿态；
多键数值编辑、图形切线手柄、绑定和状态图等生产制作能力仍待完成。


## Runtime scene changes

While Play is running, the viewport's Change game scene menu queues a project scene
without changing the authoring Document, selection, history or revision. L in Feature
Lab demonstrates the script route between its main showcase and Lighting Studio.
Every cut gets a new scene scope, script realm, input, physics, animation and audio
services. Retained captures are owned snapshots; prior entity handles cannot address
the replacement scene. Play retains an owned initial authoring snapshot for its
origin path, including unsaved edits; returning to that path rebuilds this snapshot,
matching the exported startup content. Other targets load saved files. Stop returns
to the original authoring document.

```json
{"command":"request-play-scene","session":"1","revision":"4","path":"scenes/LightingStudio.dotscene"}
{"command":"play-scene-status","session":"1","request":"1"}
```

These are separate strict operations. A request needs the current Play token and
Document revision, running Play and no pending close. Its `request` field is an
acceptance ID, not a completed load. The second operation reads the owned result,
including after Stop, while that Play session remains present. Unknown/expired IDs,
stale tokens/revisions, malformed fields and busy requests fail. Inspect's `play.scene`
contains `{scope,path}` and `play.transition` contains the most recently queued
request or null. Request states are pending/loading/succeeded/failed/cancelled;
`committed` distinguishes preflight failure (old game remains running) from a failed
startup after irreversible replacement. The error owns a code/message. The last
16 request results survive scene changes; new Play creates an independent sequence.

Loading currently runs synchronously at the end of the variable update, after fixed
steps and current animation/audio synchronization. Only one cut is processed per
update; a target Start callback may queue the following cut. File spelling/type,
scene data, referenced CPU graphics data and native/script service initialization
are prepared before retiring the old game. Native target audio remains prepared
until startup. Stop/Start callbacks are irreversible; a committed realm/native fault
is reported and faults Play. Invalid target files, assets or script preparation keep
the old game and report the failed request. Scene rendering/GPU allocation can still
fail after commit; GPU staging, background streaming, additive scenes and persistent
cross-scene gameplay state are not implemented by this operation.

## Play game state and saves

Window → Game state provides owned state inspection and explicit save-slot controls.
Play uses a fresh temporary store, deleted on Stop; it cannot access player saves.
The live `game-data` operation and GUI share the strict product implementation.
See [protocol, lifecycle and examples](GameState.md#product-protocol-and-editor).
Opening/focusing this docked panel uses `{"command":"show-pane","pane":"game-data"}`.
State survives in-game scene replacement while authoring data remains independent.

Terminal close retires the private control directory, so a supervising agent must
verify the owned editor process exits rather than waiting indefinitely for a response
file that may already have been removed. Rejected/pending close remains observable
through the live mailbox. This does not make `queued:true` an applied-success receipt.


## Hierarchy organization

Edit → Group selection (Cmd/Ctrl+G) creates a plain container at the active
(last selected) entity's world origin. Edit → Ungroup selection
(Cmd/Ctrl+Shift+G) removes only component-free containers, exposing their children.
Both preserve world poses. Drag selected hierarchy rows onto another entity to
reparent their minimal selected roots, or onto **Scene root (drop to detach)**
to detach them while preserving world poses. The Inspector's **Parent (local pose)**
field retains local TRS. Neither mode silently approximates shear that cannot be
represented by local TRS; failures leave the document intact.

These operations use the shared document transaction. Group selects the new
container; Ungroup selects affected surviving children in entity-value order.
Undo/redo restores hierarchy, transforms and ordered selection together. Save
persists ordinary scene entities, without prefab/link semantics. No operation is
available during Play/animation audition/transform interaction or conflicting
pending edits. Grouping is scene organization, not reusable linked content.

```json
{"command":"group-selection","revision":"12","name":"Material studies"}
{"command":"ungroup-selection","revision":"13"}
{"command":"edit","revision":"14","operations":[{"op":"reparent","entities":["2","3"],"parent":"4","space":"world"}]}
```

For explicit selections, edit operations also include
`{"op":"group","entities":["2","3"],"name":"Group"}` and
`{"op":"ungroup","entities":["5"]}`. Reparent requires `space` equal to
`local` or `world`, and `parent` is a live entity value or null. Duplicate or stale
IDs, cycles, functional-container removal and invalid transforms fail atomically.
Use the revision returned by the preceding operation, not literal example numbers.
A drag freezes its scene scope, revision and ordered IDs when it starts; an old
payload cannot address a replacement scene or revised selection. GUI mutations
are deferred until tree traversal releases its borrowed snapshot.

Tree arrows and agents use the same strict view operation:

```json
{"command":"set-hierarchy-expanded","revision":"15","scope":"8","entity":"5","expanded":true}
```

This requires the current authoring scene scope/revision and a live entity. It
changes view state without adding history, advancing revision or dirtying the
scene. A nonempty hierarchy search forces matching branches open and rejects
collapse requests. Inspect exposes requested `workspace.hierarchyExpansion` and
actual rendered `workspace.hierarchyRows` states; wait for a later rendered frame
to observe the view update. Opening another scene resets expansion; save-as of the
same scene retains it. Deleted IDs are pruned. Group selection opens its new group.


## Reusable scene content

Assets → Save selection as scene publishes a new `.dotscene` under assets using the
Path and Content name fields. It saves the descendant union around the active
entity's world origin, preserving internal native references and authored data.
Open that file with ordinary scene editing tools. Select any assets/scenes
`.dotscene` and choose Add scene instance; the inserted placement root is selected
for existing transform, hierarchy and undo/redo tools. Both actions share strict
agent operations. See [content contracts and examples](SceneContent.md).

Feature Lab includes `assets/MaterialStudies.dotscene`, saved and edited through
these product operations. It shares the original materials/meshes and can be
inserted into another scene in the same project. Scene instances are independent
authored copies. [Linked instances](SceneContent.md#linked-instances-and-local-overrides)
add explicit source refresh, local property/deletion overrides, revert and unlink.
These edits use the same revision/history implementation in the GUI and agent API.
The linked override inspector adds paged property/deletion/addition inspection and
selected/full stored-baseline revert, including operation without the source file.
See the [query and revert contracts](SceneContent.md#inspecting-and-reverting-applied-overrides).
Override records also expose persistent `sourceKey` provenance. Canonical baseline
references can change during source materialization; these layered keys retain the
logical source identity. See [source identity contracts](SceneContent.md#layered-source-identities).
Insertion and source refresh now resolve saved nested sources from the leaves upward,
preserving intermediate overrides and rejecting cycles, missing dependencies and graph
budget failures atomically. See [resolution contracts](SceneContent.md#recursive-source-resolution).


## Persistent entity allocation

Document snapshots expose the same current allocation floor in top-level
`nextEntity` and `scene.nextEntity`. It is a canonical decimal string or null when
exhausted. Undo restores authored content/selection while keeping allocation
monotonic. Returning to saved authored content can therefore be clean even when the
current floor exceeds the file's last saved floor. Save and Save scene as publish
that current floor; reopening a file restores its reservations. Metadata-only
external file changes participate in save conflict checks.

History stores authored scene data and selection bytes; the non-undoable floor is
held separately, without duplicating full snapshots. Dirty flags compare authored
content, not allocator metadata. Rejected edits and complete no-op batches publish
neither new values nor floor changes. Root scene JSON includes the current metadata,
so content-only undo comparisons should exclude `nextEntity` and check its
monotonicity separately. This does not add a user command that can rewind IDs.

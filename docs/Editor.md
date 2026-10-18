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

## Background scene source updates

The linked-source Inspector prepares single/all-instance refresh, restore and relink
in a document-owned background job. Its controls in Inspector and Assets use the
same strict Workspace commands:

```json
{"command":"begin-source-update","revision":"12","operations":[{"op":"refresh-scene-instance","entity":"472"}]}
{"command":"inspect-source-update","session":"1"}
{"command":"apply-source-update","session":"1","revision":"2"}
{"command":"cancel-source-update","session":"1","revision":"2"}
{"command":"close-source-update","session":"1","revision":"5"}
```

Begin accepts only 1..256 refresh/revert/retarget operations (the existing `edit`
operation schemas), at most 1 MiB. It captures an immutable authored snapshot,
allocation floor, selection, document identity/revision and scene path without
copying a live Scene to another thread. One job is open per Workspace. Preparing
uses the same native source transaction as document edits. Other authoring remains
available; any subsequent document/selection/file-path change invalidates publication.

`workspace.sourceUpdate` and the dedicated status contain session, job revision,
captured document revision, state, optional error and final edit result. Job and
document revisions are distinct. States are preparing → ready → validating →
validated → applied; cancellation uses cancelling/cancelled, and failures use failed.
Ready changes nothing. Apply accepts intent at the current ready job revision;
the worker rechecks exact bytes and safe resource paths for every observed source,
including recursive dependencies and project metadata. Accepted validation reserves
other Workspace mutations until publication/cancellation; inspection remains available.
The worker seals changed CPU scene ownership together with canonical authored text;
unchanged content needs no receiver scene. Workspace then adopts and publishes on
the document owner, provided drafts, gestures,
previews, dialogs and other publication are finished. Applied reports the document
revision/scope, changed flag and surviving created values. Undo/redo/save use the
ordinary document history. Failed/stale/cancelled jobs cannot replace the scene.
Old CPU scene descriptions return to the same worker for retirement. Applied means
the document is published; Close/shutdown can still join bounded cleanup. Cancel
remains cancelling until the worker discards its candidate. Close requires the
current applied/cancelled/failed job revision; cancel first for unfinished jobs. Session/revision/unknown-field errors have no effects.

The observation set retains at most 4096 files/128 MiB, in addition to bounded
source/candidate scene storage. Status, apply intent and cancel do no file I/O.
Cancellation and shutdown join cooperatively around bounded file/codec/native calls;
this does not promise a hard deadline. Writers must still be serialized: worker
validation plus owner publication is not an operating-system snapshot or compare-
and-swap against arbitrary external writers. Final ownership adoption/history/scene
publication and changed-workspace refresh remain synchronous, without another owner
decode/encode pass. No-op updates skip scene replacement/history/workspace rebuild.
History-capacity rejection occurs during preparation; the owner still rechecks
history before changing state. General resource streaming and broader retirement
remain unfinished. Mixed document `edit`
transactions retain their existing atomic synchronous contract.

## Applying an instance to its source

`apply-scene-instance` accepts exactly command/revision/root and uses the same
implementation as Inspector's Apply instance contents to source. The linked body
includes node properties, additions, deletions and reparenting; the placement
container and scene settings remain local. The response adds `sourceApplied` with
path/written/sceneChanged, as for field application. Review shared-source effects:
Undo changes the local document, while other placements refresh explicitly. Conflicts,
invalid references/topology, proposed source cycles, history limits and file guards
reject before publication. See [whole-instance source contracts](SceneContent.md#applying-complete-instance-contents).

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

Linked scene instances can explicitly adopt a moved/copied source through the
Inspector's **Use selected scene as source** and **Relink all N instances in scene**
actions. Both use the strict `retarget-scene-instance` edit, including atomic
multi-placement batches and ordinary undo/save. Consecutive source updates share
one native candidate and final validation; later edit calls reread saved sources.
See [source association and identity
contracts](SceneContent.md#relinking-moved-or-copied-sources).

## Native workspace

```text
DotEditor open <project-root> [frames]
DotEditor send <control-directory> <request-id> <request-file>
```

`open` opens the project startup scene, a native window and the packaged Sarasa UI SC Regular font (with Inter Western glyphs). A zero/omitted frame budget runs until closed; a positive budget is useful
for diagnostics. It prints and flushes one JSON line containing `ok` and an absolute
`control` and persistent `receipts` paths. Window dimensions start at 1280x800 with minimum 800x480.

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
Read that JSON file to observe the actual outcome, including after endpoint retirement.
Read/copy results before `ack-control`; clear owned closed sessions explicitly with
`clear-control`. See [reply lifetime and capacity](Control.md). The record contains the original
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
same operation. A successful Play request acknowledges `preparing`; poll inspect
until `running` before sending game input. The editor keeps displaying authoring
while real audio initialization and initial source compilation run in owned workers.
Only exact current source can seed the initial runtime; obsolete compilation is
cancelled and replaced with the latest capture. Source capture, scene service setup
and script construction still run on the owner, so this is not fully asynchronous
scene loading. `preparation` reports scripts/resources/audio/scene/activation/teardown while pending.
`preparationCompileAttempts` and `discardedScriptCompilations` count initial source
jobs and obsolete results; running hot-reload attempts remain in `source`.
`started` becomes true only after actual activation. Preparing locks
editing and unpublished game controls/inspection reject. Stop requests `cancelling`,
retains ownership until native teardown completes, then reports `stopped` (or
`faulted` with its error). Canceling unpublished startup invokes no Stop hooks.
Window/agent close waits through this cancellation while frames continue.
Inspect's `workspace.play` contains idle/preparing/running/cancelling/faulted/stopped
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
input method cannot consume gameplay keys. While Game input remains enabled, text
mode follows the script's explicit
`input.requestTextInput` request (initially false), including absolute-pointer
menus. Releasing Game input restores text mode for editor fields; the operating
system's selected input method is unchanged.
Released capture publishes released edges from the last game input, then
empty frames. Capture does not pause simulation/audio. Native simulation pause
uses the separate
[game clock](GameClock.md), including explicit paused single steps. F5 and Escape are reserved
editor controls.

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

A failed Play preparation preserves the first script failure's entity and diagnostic
in `workspace.play.error` before cleaning up the rejected session; the original
Play acknowledgement does not claim startup completion. Realm faults during
play retain the same cause alongside the structured frame reports. Failed startup
releases its temporary save storage and leaves the authoring scene available for
correction and retry.

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
accepted asynchronously. Input and cancel replies contain `{ok,play}`, without
serializing the authoring document or runtime scene. Poll `play-status` with the
current session to observe `play.agentInput`; inspect the runtime scene separately
to verify actual effects. `active`, `remaining` and `published` report its progression; the
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

## Static mesh collider authoring

Select an entity with a native asset mesh and choose **Use render mesh as static
collider** in the Inspector. This validates the resource and stages static motion,
mesh shape/resource, zero velocity, discrete CCD and solid collision. Apply creates
one ordinary document history step; Reset discards the draft. Undo/redo/save/reopen
use the native scene codec. Characters and deforming skins cannot share this collider.
Malformed/missing resources fail before staging; Apply validates resource bytes
again. Direct component edits retain the ordinary strict scene validation and Play
validates referenced geometry before publishing its world.

```json
{"command":"stage-mesh-collider","revision":"3","entity":"10","path":"assets/equipment/meshes/0.dotmesh"}
{"command":"apply-inspector","revision":"3"}
```

Separate strict requests require the current selection/revision, stopped Play and
preview, and no pending forms/gestures/dialogs. Staging changes no document revision
or history. The GUI calls this same product operation. Shape `mesh` exposes
`meshAsset`; primitive variants forbid it. See [geometry limits](Physics.md#static-triangle-mesh-colliders).

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
mapping. The structured material Inspector below shares staging, history and save
with agents. Visual light tools and the remaining mature editor workflow are still
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
is supplied. Higher quality increases first-bake work. Live scene preparation uses
owned background HDR decoding/mips and zero-wait GPU completion polling; uploads
remain on the renderer owner. This is not general background import/streaming;
advanced environment controls remain planned.

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

Rendering provides an opt-in **Profile mesh rendering** checkbox, recent GPU history,
per-pass GPU milliseconds and renderer CPU wall intervals. It names the device and
shows unsupported timestamps, pending/dropped samples and errors explicitly. The
control is session state; it does not edit the scene or mark the document dirty.
The corresponding strict mailbox operations are:

```json
{"command":"set-profiling","enabled":true}
{"command":"inspect-profile"}
{"command":"set-profiling","enabled":false}
```

Application CPU steps are sampled separately with `set-application-profiling`.
The same `inspect-profile` response includes `application`, and ordinary inspection
includes `workspace.applicationProfiling`. Window → Rendering provides Profile
application frames through that same operation. Its independent Events/Prepare/Fixed/
Update/Render peak rows retain their own step indices after ordinary history eviction;
`application.phasePeaks` exposes the complete corresponding frames in compact and
history JSON. These peaks can come from different steps and are not additive. See
[native frame diagnostics](Profiling.md) for boundaries and standalone `--profile` output.

`enabled` must be boolean and unknown fields fail. Both operations remain available
during transform/animation inspection. The standard `workspace.view.profiling`
snapshot includes capability, adapter/backend, session, sample counts and latest
CPU/GPU records; `inspect-profile.profile` additionally includes bounded `cpu` and
`gpu` arrays. Session/serials and nanoseconds are decimal strings, dimensions/work
counts remain JSON integers, and absent passes are null. GPU category order is shadows,
SSAO, HDR surface/sky, tone mapping, each summing complete physical-pass pairs. Every
timing record owns its resolution and work report rather than borrowing the current
viewport's counters.

Changing enablement resets history and starts a new session on enable; repeating
the current value preserves it. Disabling releases query storage. Async GPU results
may lag CPU samples, busy slots drop GPU samples, and timestamp precision can produce
zero differences. These are renderer scopes, excluding scene preparation, separate
skinning, scripts/physics, GUI and full application/presentation pacing. See the
[exact timing boundaries](Rendering.md#renderer-cpu-and-gpu-profiling).

The Rendering pane also shows actual CPU visibility tests: BVH nodes, world AABBs,
exact local-box tests and unindexed homogeneous tests. `inspect.workspace.view.draws.spatial`
includes indexed/changed/refitted quantities and rebuild status for the last successful
mesh pass. Camera-only changes reuse bounds/tree topology. These counts exclude
linear input validation and bounds generation; they are not frame-time or FPS claims.
渲染面板显示真实空间分类次数，检查接口还报告更新/重建状态；计数不含线性输入
校验和边界生成，也不表示帧耗时或 FPS。

## Model import and instantiation

Assets Actions offers **Import glTF / GLB model** using an absolute source file and
new project `assets/...` folder. The command starts one background preparation;
it does not publish files or report successful persistence. The Model import section
shows preparing, ready, publishing, published, cancelling, cancelled or failed, the
proposed native paths/counts/warnings and contextual failures. **Publish imported
model** accepts a ready bundle, and **Cancel model import** requests cooperative
cleanup before publication. Only `published` proves completion. Close a terminal
import before starting another import or reimport review.

```json
{"command":"import-model","source":"/absolute/model.glb","path":"assets/robot"}
{"command":"inspect-model-import","session":"1"}
{"command":"publish-model-import","session":"1","revision":"2"}
{"command":"inspect-model-import","session":"1"}
{"command":"close-model-import","session":"1","revision":"4"}
{"command":"edit","revision":"3","operations":[{"op":"instantiate-scene","path":"assets/robot/model.dotscene","parent":null,"transform":{"position":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}}]}
```

Use returned session/revisions rather than these illustrative values. Inspect requires
exact command/session; publish, cancel and close require command/session/revision.
Cancel uses `cancel-model-import`. Unknown fields, stale identities/revisions and
invalid states reject. Every response owns `import` with session, revision, state,
path, proposed result (or null), structured error (or null) and catalogError. Status
is also visible as `workspace.modelImport`, without opening the Assets panel.
Result counts/paths can be present while ready or failed; state determines whether
the files were published. `ok:true` for a publication request acknowledges intent;
poll the actual terminal state. Publication/cancellation revisions advance on both
acceptance and completion. Close accepts only published/cancelled/failed.

Import optionally accepts an unsigned source `scene` 0..1023. The native result reports
source scene, mesh/material/image/node, clip, skin and skinned-mesh counts, generated
scene/model paths and warnings. Add scene instance retains imported animation players
and remaps their targets under one placement container; it can be undone/redone and
saved. Imported primary cameras/listeners are demoted. Current scene environment,
camera and lighting remain authoritative. Inspector, Play and exports use the same
native model resources and GameSession; imports do not dirty the authoring scene.

Preparation opens its own resource store and writes native files under an owned
project-root staging directory, outside resource roots and game packages. No World,
script realm or device handle crosses the worker boundary. The existing bounded model
parser and writer run cooperatively between calls; cancellation does not forcibly
interrupt either. Ready cancellation cleans the private bundle on the worker. Destruction
cancels unpublished work and joins, possibly waiting for the current bounded codec call.
Accepted publication completes and cannot be cancelled or imply rollback. There is no
background import concurrency within a workspace, percentage estimate or synchronous
workspace fallback. Native ResourceStore::importModel remains a synchronous tool API
using the same prepare/publish implementation.

Publication rechecks exact project metadata bytes, current catalog capacity/depth,
parent/path aliases and absent destination. Existing destinations are never overwritten.
The same-filesystem exclusive rename exposes a complete bundle; cross-device failure
is explicit. Callers must serialize external source/project writers; this is not hostile
race isolation or crash recovery. The editor serializes other product operations during
accepted publication. Ordinary editing/navigation may continue during preparation;
publication requires stopped Play/previews and finished drafts/dialogs. Source reading,
decode, native serialization and file writes run on the worker; the final asset-catalog
refresh still scans on the owner, independent of panel visibility. A refresh error is
reported separately from an already-published bundle and can be retried with Refresh files.

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
The import job result reports generated `skins` and `skinnedMeshes`; after publication instantiate the returned
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
The PNG contains scene pixels and running native HUD labels, excluding editor
panels and transform handles.
Pixels are frozen before SDL opens the native save dialog, so Play may continue while
choosing a destination. Save captured PNG retries a retained image; Discard viewport
capture releases it. Native cancellation also releases the image.

File → Save viewport PNG 捕获实际完成的场景渲染目标，保留当前游戏/编辑相机与材质预览，
不另行渲染其他视角。PNG 包含场景像素与运行中的原生 HUD 文字，不含编辑器面板和变换手柄。打开 SDL 系统保存
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
or changed HUD/presentation revision fails rather than exporting an older frame. At most one
queued/ready/saving image is retained.
Finish or discard it before requesting another. IDs are canonical decimal strings,
monotonically increasing for this workspace; stale IDs cannot save/discard newer images.

`workspace.capture.frame` records the actual frame serial, authoring revision, scene
scope, Play or animation-preview session (zero otherwise), Play frame and scene-local
hudRevision (zero outside Play),
mode/source, pixel dimensions, column-major
view-projection matrix and material-preview flag. These describe a completed capture
only in `ready`/`saving`/`saved`; queued metadata describes the requested context and prior serial.
Later rendering, camera movement, Stop and document edits do not change retained pixels
or provenance. Save does not change document history/revision/dirty state. `save-capture`
starts owned background PNG encoding and returns the `saving` state, with no output
path yet. Poll `inspect` until the same capture becomes `saved`, or returns to
`ready` with an error for retry/discard. A successful request receipt is not file
completion. On successful owner-thread baseline check and atomic write,
state becomes `saved`, pixels are released and `output` contains an owned absolute path
and decimal byte count. Errors/cancellation are observable through inspection.
At most one encoder is active, owning one bounded image copy in addition to the
retained capture. Worker encoding never writes files or touches a renderer. Discard
cancels publication immediately; the bounded codec may finish before its job retires.
Further capture/save/browse requests wait for that retirement.
Read-only `savePending` remains true until the active or cancelled encoder retires;
wait for false before requesting another capture. Close requires the
pending save to complete or an explicit capture discard. The menu/native close and
agent guard share this rule. Destination checks/writes/catalog refresh remain on the
owner thread; this is not general background filesystem I/O.

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
不改变已捕获图像，也不影响场景历史或保存状态。保存请求返回 `saving`，须继续轮询至
`saved` 才证明文件完成；异步失败保留 `ready` 图像并报告错误，可重试或丢弃。后台仅编码，
所属线程完成目标基线检查与原子写入；丢弃会取消后续发布，编解码可在后台结束后回收。
保存中关闭须等待完成或明确丢弃截图，菜单与机器接口遵守同一规则。成功后释放像素并报告绝对路径
与字节数。已有目标须明确允许覆盖，拒绝链接、特殊/只读文件及大小写别名；原子替换前校验
字节基线。系统对话框打开时保留父窗口并拒绝冲突操作；智能体通常直接指定路径保存。

## Create animation from authoring pose / 从编辑姿态创建动画

Select an ordinary scene object or an existing animation owner. In Inspector, expand
**New animation from authoring pose**, choose an unused `assets/*.dotanimation` path,
name and duration, then **Create clip and stage player**. This creates an exclusive
resource file and stages the Animation component; **Apply changes** attaches/selects
it in scene history. Reset or scene Undo keeps the separately created file. The same
operation is available to agents:

```json
{"command":"create-animation","revision":"1","entity":"281","path":"assets/survey.dotanimation","name":"Survey","duration":2}
```

The fields are exact. The entity must be the active selection at the current authoring
revision, while stopped, with no other preview/gesture/dialog, pending scene form or
open text/material editor. Existing animation players keep every logical node binding,
library entry and playback setting, append the new path and select its index. Disabled
settings remain disabled, but creation preflights the candidate enabled. An ordinary
object gets node zero bound to itself and normal player defaults. This does not guess
rig bindings or retarget clips.

Each bound entity contributes translation, rotation and scale channels from its current
**authoring local pose**, with identical linear keys at zero and the requested end.
Duration must be finite, positive after native float32 conversion and at most 86400
seconds. Names, paths, library/binding/channel/key limits use the native codecs. Missing
libraries of enabled players, conflicting animation writers, physics authority and invalid skin poses
fail before file creation. Preparation uses the actual isolated animation implementation;
it never saves a sampled preview pose or changes the source scene.

Success returns `ok`, `path`, `staged:true`, `entity` and the unchanged scene `revision`.
Inspect exposes the pending component; applying it creates one normal scene history
entry. The file uses ResourceStore's path, metadata, size and exclusive-publication
checks. Existing files are not overwritten. A failure refreshing the asset catalog
explicitly reports that the file was created and the component staged; inspect/reset
or apply that staged component before retrying. Scene history and resource publication
are separate, serialized local operations.

The Animation Inspector's **Clip library** supports Add, replacement and Remove;
**Node bindings** supports Add, node/entity changes and Remove. These edit the ordinary
staged Inspector value, using the same `stage-inspector` / `apply-inspector` transport.
At least one clip and binding remain; native validation rejects duplicate/out-of-range
nodes, duplicate entities/paths and missing targets. Removing a clip remaps the selected
index and shifts later library indices; scripts that choose indices must be updated.
Apply first, then **Edit animation clip** to use existing key editing, audition,
undo/redo and save. Actual preview/Play/export validates referenced clip coverage.

创建使用当前编辑局部姿态：已有播放器保留全部逻辑绑定及设置并追加资源，普通物体绑定自身。
生成文件并暂存组件后，需明确应用到场景；重置/场景撤销不会删除资源文件。创建前用真实动画
预览校验完整库、写入权限、物理与蒙皮，失败不发布新文件。绑定和库列表使用普通 Inspector
草稿与严格接口，删除片段会改变后续索引，须相应修改按索引播放的脚本。此功能不包含自动
重定向、骨骼制作、分层或完整 dope sheet。

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
流程修改；片段创建、库与节点映射见上文。骨骼结构制作、完整曲线/切线与状态图工具仍待完成。

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
value/tangent form edits; select one key to use those controls. The group adjustment
controls below remain available for selected keys across tracks.

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

### Selected key value adjustment

With several keys selected, Inspector offers translation offset, local rotation in
XYZ Euler degrees and componentwise scale factors. Apply adjustment changes only
selected existing keys, preserving their times/count, channels, interpolation and
selection order. Translation adds an offset in the channel's parent coordinates;
rotation appends a local rotation (unit XYZW right multiplication); scale multiplies
XYZ. Cubic scale and rotation derivatives receive the same linear transform;
translation derivatives remain unchanged. Derivatives are never normalized as
rotations. Unselected keys remain unchanged, so adjusting part of a curve can also
change its interpolation near those keys. This is not a world-space pose gizmo.

These controls share the strict operations below. Each request requires the current
buffer/revision and a clean single-key form, without an active move/close/dialog.
The parameter form is reported by animationClip.adjustment. Setting/resetting it
changes no resource, history, preview or disk. Parameters remain after Apply so
another Apply repeats the relative adjustment; opening a clip resets them to the
identity. Unapplied parameters are not an unsaved resource or pending key form.
Empty selection, singular scale, overflow or invalid native clip/pose/skin fails
without changing the document/history/preview/selection or parameter form.

```json
{"command":"set-animation-key-adjustment","buffer":"1","revision":"1","value":{"translation":[0,0.02,0],"rotationDegrees":[0,0,5],"scale":[1,1,1]}}
{"command":"apply-animation-key-adjustment","buffer":"1","revision":"1"}
```

A changed Apply prepares the new selection/form and native candidate preview before
publishing one resource history state. Times and ordered references remain selected;
the active key's form shows its new payload. Identity/no-change Apply retains
revision, redo, preview session/clock and selection. Save, Undo/Redo and close use
the ordinary resource workflow. Generic edit batches also accept this operation
with explicit 1..16384 unique node/path/time references and a unit XYZW rotation:

```json
{"command":"edit-animation-clip","buffer":"1","revision":"1","operations":[{"op":"transform-keys","keys":[{"node":8,"path":"rotation","time":0},{"node":8,"path":"rotation","time":0.5}],"translation":[0,0,0],"rotation":[0,0,0.043619387,0.999048222],"scale":[1,1,1]}]}
```

Generic batches retain their ordinary selection-clearing behavior on a changed
commit. Identity components preserve native float bits. No retiming, mode conversion,
binding remap, quaternion sign correction or tangent rescaling is implicit.

多选 Inspector 支持局部通道平移偏移、追加局部旋转和缩放系数，复用同源严格操作。
三次缩放/旋转切线按同一线性变换处理，平移切线不变，不把导数归一化为旋转。仅改选中键，
时间、数量、模式和选择顺序不变；部分键调整可能影响邻近插值。参数不是未保存草稿，重复
Apply 会再次调整；打开片段重置参数。候选选择、原生姿态和蒙皮预检后提交一步历史，失败
保留文档、历史、预览、选择和参数。恒等操作保留修订、重做与预览时钟。

This workflow edits bound library clips, including newly created and applied pose clips.
Graphical tangent-handle editing, skeleton retargeting, state graphs/layers, root motion/events
and production animation scalability remain unfinished.

选择动画播放器后可编辑已有库片段：底部选择具名轨道、关键帧及时间，Inspector 编辑局部
值和三次切线。键表单需显式应用/重置；插入与替换按原生 float 时间判断，不隐式移动旧键。
候选片段和当前蒙皮姿态预检后才提交历史，失败保留旧草稿、姿态、时钟及会话。资源保存/
撤销独立于场景历史，磁盘冲突不覆盖外部修改；关闭须保存或明确丢弃。所有操作有同源严格
JSON 接口，查询有界分页，旧资源/预览标识不能影响新会话。图形切线、骨骼制作/重定向、状态图和大规模
动画生产能力仍待完成。

曲线面板现可查看原生 XYZ/XYZW 采样、选择键和时间范围。显式更新/移动已有键保持数量，
新时间冲突时不覆盖原键；插入仍是独立操作。曲线仅是有界近似显示，不能证明全部中间姿态；
多键相对 TRS 调整已实现，图形切线手柄、骨骼制作/重定向和状态图等生产制作能力仍待完成。


### Cubic tangent handles / 三次切线手柄

In Curves, select one existing cubic key, pause audition and choose X/Y/Z/W under
Tangent component. Square handles use one third of the neighboring segment; first
incoming and last outgoing derivatives have no used segment and no handle. W is
only meaningful for rotation. Drag a square vertically to change that side and
component's native derivative per second. Incoming/outgoing sides are independent;
there is no implicit mirroring, smoothing, time snapping or interpolation conversion.
Rotation handles edit raw XYZW derivatives before native quaternion normalization,
not Euler angles. The colored thick candidate curve still uses the native normalized
sampler; its handles are control derivatives, not a promise of its projected slope.

A tangent transaction owns the source key and immutable clip. Updates only change a
ghost; the resource revision, history, posed preview, source scene and disk are
unchanged. The curve range freezes during a pointer drag. Release includes final
pointer displacement and preflights the candidate with the ordinary native
pose/skin pipeline before one history publication. A no-op retains revision, redo
and preview identity. Failed update/sample/commit retains the preceding scalar or
candidate as applicable, with an explicit error; Apply tangent/Derivative per s
allows correction and Cancel tangent discards it. Original and candidate curves
are shown together; a sample error does not hide the source keys/handles. Bounded
curve samples do not prove every intermediate pose is valid. Escape, focus loss,
hidden Animation pane or changed graph geometry cancels physical gestures. Agent
transactions remain available without OS focus. Physical tangent dragging/narrow
layout/IME acceptance is still unverified on this Mac.

The following strict commands share that implementation. Begin requires exactly
one selected current cubic key, clean form, paused preview and no move/dialog.
Component is an integer 0..3 and must exist on the channel; side is in/out. Sessions
are decimal strings from the response and are scoped to buffer/revision. Missing,
retired or stale sessions fail. While active, selection/history/save/ranges, frame
settings, key moves and preview control are blocked; reads and explicit cancel or
close-with-discard remain available. A failed commit keeps the session for repair.

```json
{"command":"set-animation-curve-component","buffer":"2","revision":"4","component":1}
{"command":"begin-animation-tangent","buffer":"2","revision":"4","component":1,"side":"out"}
{"command":"update-animation-tangent","session":"5","slope":-0.08}
{"command":"read-animation-tangent-curve","session":"5","from":0,"to":2,"samples":129}
{"command":"finish-animation-tangent","session":"5","commit":true}
```

Use commit:false to cancel. Candidate reads return the same native bounded curve
shape as read-animation-curve; that existing command continues to read the published
resource. animationClip.tangent reports the owned key, side/component, original and
current slope, nominal control point and changed state; null means no transaction.
curveComponent is a workspace view preference and does not dirty assets.

三次键暂停后可逐分量拖动单侧切线；手柄取相邻间隔三分之一，未使用端点侧不提供操作。
导数以秒为单位，旋转编辑归一化前 XYZW。拖动只生成候选曲线；释放通过原生姿态/蒙皮
预检后提交一步历史。无变化保留版本/重做/预览，失败保留候选以供修正；取消不写资源。
GUI/JSON 共用事务与采样，实际鼠标、窄布局和输入法验收仍待完成。

## Multitrack dope sheet / 多轨时间表

The animation editor defaults to **Dope sheet**. Switch to **Curves** for native value
sampling and individual tangent fields; both views share key selection, time range,
clipboard, group moves, resource history and the isolated native preview. View changes
never edit the clip or seek playback. From/To and Fit time define the shared seconds
axis. Rows are sorted by logical node then translation/rotation/scale, independently
of the asset's stored channel order; row indices belong to the current resource
revision and must be refreshed after tracks change.

Each row shows actual key diamonds on that axis. Drag empty row space to select a
rectangle across rows/time; dragging beyond the visible body scrolls through rows.
Ctrl/Cmd toggles the rectangle's exact keys, and Shift-click between keys spans their
rows and times (Ctrl/Cmd+Shift adds). Clicking a row name selects its keys in the
current time range. Clicking empty space or the ruler seeks the isolated preview.
A normal click on a selected key starts the existing whole-selection move, including
keys on other rows. Ghosts show occupied destinations in red; release uses the same
native atomic retime/pose preflight as the curve view. Escape/focus loss, retired
geometry or a hidden Animation panel cancels pointer-only feedback/moves. A failed
commit retains the move and permits correction/cancel.

The row list is clipped to the visible viewport. Up to 256 visible keys per row are
exact diamonds; denser rows explicitly show occupied density bins. Each bin reports
its actual first/last native float time and count. Selected bins indicate that they
contain selected keys, not that every key is selected. Clicking a bin selects its
exact interval; zoom to resolve individual keys. Neither the density display nor a
page budget silently drops keys. A rectangle resolves exact source keys, fails
before changing selection above 16384, and may legitimately select no keys.

Strict agent commands use the same model and selection/publication paths:

```json
{"command":"set-animation-editor-view","buffer":"1","revision":"1","mode":"dope-sheet"}
{"command":"set-animation-editor-view","buffer":"1","revision":"1","mode":"curves"}
{"command":"read-animation-timeline","buffer":"1","revision":"1","from":0,"to":2,"offset":0,"count":32,"columns":128}
{"command":"select-animation-range","buffer":"1","revision":"1","from":0.5,"to":1.5,"first":0,"last":31,"mode":"replace"}
{"command":"delete-animation-selection","buffer":"1","revision":"1"}
```

Fields are exact. Timeline pages require current buffer/revision, seconds in
[0,86400] with at least a 1us span, offset <= total rows, count 1..32 and density
columns 1..128. Empty tail pages succeed. Responses contain total/offset and rows
with index, node/path, actual bound entity/name, total/visible counts, exact `times`
or explicit `bins` (first/last/count). Queries may read the underlying document
while a move is active; ghosts remain in `animationClip.move`. Results own their
values and never expose mutable clip or scene pointers.

Selection uses inclusive row/time bounds and replace/add/toggle modes. Equal times
select only that exact instant, using native float values returned by the query;
interval bounds themselves are double precision and are not rounded toward nearby
keys. Empty matches are allowed. Add deduplicates; toggle preserves existing keys
outside the rectangle. Newly included keys follow row/time order, with the last key
anchoring a move. Mode changes, range selection and deletion require no pending key
form/move/dialog. Reads remain observational. `animationClip.editorView` reports
the active mode; old resource identities cannot affect a new clip.

**Delete selected keys** handles 1..16384 unique exact keys in one native edit,
independent of the <=256 batch-operation limit. Empty channels are removed; other
values/tangents/interpolation stay intact. An empty final clip or cubic channel with
only one key fails without inventing a replacement. Native pose/skin preflight,
file conflict behavior and Undo/Redo remain shared with ordinary edits. The batch
form is also available:

```json
{"command":"edit-animation-clip","buffer":"1","revision":"1","operations":[{"op":"remove-keys","keys":[{"node":8,"path":"rotation","time":1}]}]}
```

多轨视图使用同一时间轴展示真实通道，框选、跨轨 Shift 选择、拖动整组、复制/粘贴及批量
删除共用原生文档与严格接口。密集行显示实际首尾时间与数量，不把聚合标记当成单个键；
框选仍解析全部精确键，越界/过量/冲突保留此前状态。删除空通道后校验原生片段，拒绝空片段
与单键三次通道，不造替代数据。图形切线手柄、骨骼重定向与分层/状态图
制作仍未完成，不能仅因时间表存在就宣称动画生产能力成熟。

## Animation frame grid / 动画帧网格

**Frame options** selects an editing rate (integer presets and exact 24000/1001,
30000/1001, 60000/1001), display units and snapping. Rates are normalized rational
values between 1 and 240fps, with bounded input terms (numerator <=240000,
denominator <=1001). The default workspace preference is 30/1, seconds display and
snapping off. It persists across clips in that workspace, not into clip files or
new editor processes. Clips remain continuous native float seconds. This is not
a simulation-rate change, clip resampling, or drop-frame/SMPTE timecode.

The ruler shows bounded aligned frame ticks when frame display or snapping is active.
From/To fields then show frame coordinates (fractional view bounds are allowed);
Curves retains the same native time geometry and shows frame coordinates/ruler lines.
Frame tooltips distinguish on-grid native samples from approximate off-grid frame
coordinates. **Frame target**, **Previous frame** and **Next frame** use exact zero-based
frame indices. Successful frame/time scrubbing pauses with its native pose/clock
transaction; invalid bounds or samples preserve the preceding playing/paused state.
The ordinary audition `control-animation-preview` seek remains continuous and preserves
playing state, independently of these explicit editing controls.

Strict shared operations, each with exactly the fields shown:

```json
{"command":"set-animation-frame-grid","buffer":"1","revision":"1","numerator":30,"denominator":1,"snap":true,"display":"frames"}
{"command":"set-animation-frame-view","buffer":"1","revision":"1","from":0,"to":60}
{"command":"seek-animation-frame","buffer":"1","revision":"1","frame":24}
{"command":"step-animation-frame","buffer":"1","revision":"1","direction":-1}
{"command":"seek-animation-time","buffer":"1","revision":"1","seconds":0.798}
{"command":"snap-animation-selection","buffer":"1","revision":"1"}
{"command":"update-animation-key-move-frame","session":"1","frame":24}
```

Frame/rate fields are unsigned JSON integers, not strings/booleans/decimal approximations;
frame zero is valid. Step direction is exactly integer -1 or 1. Grid/display changes, explicit seeks and selected-key snapping require the current resource
buffer/revision and no pending key form/move/dialog. Frame views use the existing
curve-view guards and never edit key forms. Frame-move updates use
the existing move session. The frame rate cannot change during that move.
`animationClip.frameGrid` reports canonical rate, snap/display, current continuous frame
coordinate, nearest global grid sample (index/nominal seconds/native float), onGrid
and an optional precision error. A nearest global grid sample may exceed a short clip;
a mutation still validates actual clip duration before changing playback.

With snapping enabled, dope-sheet/curve ruler clicks and scrubbing choose the nearest
valid grid frame and pause only on success. Ordinary key-move time updates snap the
**anchor**, retaining relative offsets for all selected keys. An exact return to its
original source time restores original floats and a semantic no-op, even when that
source was off-grid. An explicit frame update always uses the requested grid point.
**Snap selected keys** instead quantizes each key independently in one native retime,
with normal pose/skin preflight/history; occupied or collapsed times fail the entire
edit. Repeated already-aligned snapping preserves revision, redo and preview identity.
No snapping overwrites an unselected key or invents replacement keys.

Nearest ties at a computed positive half-frame round upward. At the native 24-hour
limit, neighboring high-rate frames can share one float time. An editing sample must
round back to its intended nearest valid frame; otherwise it fails explicitly. Nominal
ruler ticks do not promise that every tick can be stored. Frame stepping recognizes the
same native float grid sample, so rounding at .8 seconds/30fps does not repeatedly step
to frame 24 instead of frame 23. Off-grid times step strictly forward/back; no available
adjacent frame or a frame past clip duration is an error, rather than a silent clamp.

帧网格为工作区视图偏好，不写入资源、不改变游戏步频。设置帧率本身不改姿态、历史或时钟。
显式定位/步进通过同一原生事务提交姿态、时钟与暂停，失败保留此前播放状态。拖动只吸附锚点，
保留整组选中键的相对时间；“吸附所选键”则逐键量化，冲突整次拒绝。长时高帧率不能明确
表示的 float 帧不自动改成邻帧。新进程应明确选择所需编辑帧率，运行时按保存的秒数播放。

## Live script compilation

Play polls source contents and compiles changed snapshots in a single owned background
task while the current generation continues running. The viewport reports compiling
and offers Retry scripts for an unchanged rejected source. The button and strict
operation use the same native source polling/retry, without advancing Play timing or
input. Scheduling is not a successful reload; inspect its eventual state/errors.

```json
{"command":"reload-play-scripts","session":"1","force":true}
{"command":"inspect-play"}
```

reload-play-scripts requires exactly command, the current running Play session and a
boolean force; pending close, stopped/faulted sessions or disabled hot reload reject
it. force:false polls normally, force:true schedules a retry and coalesces into an
already current compilation. A retry waits through retirement of an obsolete job
without being lost when source returns to active bytes. The reply contains source checked/changed/compiling/
discarded/committed/error and current play status. Reload cleanup failures also enter
the ordinary Console diagnostics. Inspect is read-only; it never collects a result.

play.source (also workspace.play.source) is null without an active source controller;
otherwise it reports compiling, compileAttempts, reloads, discarded and the current
error. Counters are decimal strings; compiling is boolean. Errors from rejected source
remain visible while a correction compiles and clear after a successful replacement.
A compilation failure keeps the old generation running. Before migration, completion
must match a fresh exact source capture; stale results are discarded. Stop, game faults and scene
replacement cancel and reap owned compilation work. Initial Play preparation,
source capture and owner-thread runtime/state migration still have synchronous costs.
See [scripting contracts](Scripting.md#source-hot-reload).

Play 的后台编译状态与重试按钮共用严格接口；排队不等于重载成功，需查看最终状态及错误。
查询不推进游戏或提交结果。旧代在编译期间继续运行，提交前重新核对当前源码，失败或过期
结果不覆盖活动执行；停止/场景替换回收拥有的任务。初次准备、源码捕获和状态迁移仍同步。

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

A queued load begins owned CPU resource preflight at the end of the variable update.
Saved scene loading, graphics/image/HDR checks, initial skin validation and animation
library file decoding run on a worker's independent scene; the old game continues
input, scripts, physics, animation/audio and rendering while the request is Loading. The owner later
rehydrates exactly the validated scene and prepares services before retiring the
old game. Only one cut is processed per update; a target Start callback may queue
the following cut for a later update. `GameSession::pollScene` can consume an
already-started preflight without advancing Update/fixed time; it does not begin
a pending cut or recursively load a Start request.

File spelling/type, scene data and full CPU graphics/skin contracts remain checked;
there is no failed-file fallback or GPU residency claim. `Dot.Project.ScenePreparation`
owns the worker and `PreparedScene` matches exact canonical scene bytes and asset
root before reuse. Rehydration creates new caller-thread World handles. The caller
must retain the asset root and serialize external asset edits through publication;
this is not a filesystem-wide snapshot against concurrent edits. Close/fault cancels
uncommitted work and retires its flow record; destruction joins without detaching.
Cancellation is cooperative: a decoder or bounded skin-validation pass can finish
before the join completes.

The preparation result also owns immutable initial ClipAsset handles, shared by exact
authored path. Reference accounting preserves the existing scene limits of 1024 clip
references, 30000 channels and one million keys, including repeated references to
shared clips. Only initially enabled libraries load. The owner requires complete
prepared coverage and keeps normal target/binding, authority and sampling validation;
there is no missing-clip disk fallback. Initial preparation caches are then released
while live players own their libraries. Unchanged players keep those values/clocks;
later description replacement reads current files as before. No worker World/player
handle crosses threads. Initial game/Play startup, runtime description reloads and
remaining native/script service work still execute on the owner.

After old Stop, the retired native audio requests real worker teardown and stays
in one owned retirement slot. Old realm/physics/animation are released on the
owner. Update or `pollScene` reaps the slot only after actual native teardown;
`retiringAudio` reports unreaped ownership in Play status (and standalone player
inspection), rather than claiming the old endpoint is already released or audible.
The next cut stays Pending until that slot clears, bounding this game to two
endpoints including a prepared replacement. A teardown fault is contextual and
faults the current game. Close/destruction still join all owned endpoints. Already
queued/driver PCM can retain its tail while native stop completes; this is not a
sample-accurate crossfade or a guaranteed zero-overlap cut.

Scene rehydration, script compilation/VM/service setup, native audio opening and
activation, and non-audio retirement still run on the owner and can stall. Failure
cleanup and final close can also wait for native teardown. Target audio remains
silent until startup. Stop/Start callbacks are irreversible; a committed realm/native
fault is reported and faults Play. Invalid target files/assets or script preparation
keep the old game and report the failed request. GPU allocation can still fail after
commit. Shared GameData survives changes, but automatic entity checkpoint restore,
background GPU staging/streaming and additive runtime scenes remain unfinished.

## Play game state and saves

Required environment bake/surface/sky, shadow, SSAO and skin compute pipelines compile asynchronously
while editor events, agent requests and GUI continue. `workspace.view.loading` is
`"environment-pipeline"`, `"shadow-pipelines"`, `"occlusion-pipelines"`,
`"skin-pipelines"`, `"initial-skin-poses"`, `"environment-map"` or
`"material-textures"` while pending,
null otherwise, independently of `error`. A pending view has no completed pixels for picking or
capture. Screenshot requests still use the normal queue/confirmation protocol;
inspect their final state, since queued is not pixel success. Scene names environment
lighting, shadow, ambient occlusion or skinning shaders, HDR environment or material textures.
Play checks current requirements and
returns an explicit pending error until preparation completes. No synthetic progress percentage or
completed-scene image is reported. HDR and visible material reads/decode/mips share
one owned CPU worker, retaining at most one bounded pyramid. Uploads still run on
the owner thread. HDR GPU completion uses zero-wait polling; only completed maps
enter the scene cache, and the previous binding/pixels survive until actual drawing.
Only complete materials publish. Cancellation discards late CPU results; abandoned
GPU work remains budgeted until completion or renderer teardown. This is not
general runtime streaming; explicit synchronous rendering APIs remain available.
Only visible, enabled saved skins require skin pipeline preparation; no mesh/skin
files are loaded and no poses published by that pipeline stage. Direct skin updates
finish the same two jobs if necessary. Play uses zero-wait dynamic skin completion;
invalid deformation cannot publish buffers, bounds or picking state. Authoring and
animation audition retain exact synchronous drawing.

Window → Game state provides owned state inspection and explicit save-slot controls.
Play uses a fresh temporary store, deleted on Stop; it cannot access player saves.
The live `game-data` operation and GUI share the strict product implementation.
See [protocol, lifecycle and examples](GameState.md#product-protocol-and-editor).
Opening/focusing this docked panel uses `{"command":"show-pane","pane":"game-data"}`.
State survives in-game scene replacement while authoring data remains independent.

Scene-local game text is controlled by the same native service as scripts:
`{"command":"hud","session":"1","scope":"83","value":{"command":"snapshot"}}`.
Use current session/scope IDs from inspect-play. The inner set/remove/clear operations
change live UI without document history or dirty changes. Strict label fields,
retirement and read-only phases are described in [native game text](GameUi.md).
`workspace.view.hudText` reports textureBytes, rasterizations and overflow keys;
capture frame hudRevision proves which completed UI model supplied the frozen pixels.
Stopped authoring and animation preview contain no running HUD.

Play viewport controls expose Pause simulation/Resume simulation and Step simulation.
The latter requires a paused running session without pending work and uses
`{"command":"clock","session":"1","scope":"83","value":{"command":"step"}}`.
Pause/resume uses
`{"command":"clock","session":"1","scope":"83","value":{"command":"set-paused","paused":true}}`
through the same native product route. Current Play status reports clock.paused,
clock.revision, clock.step and simulatedUpdates; fixedSteps counts actual completed
native fixed phases, including single steps, across scene replacement.
The unscaled frame/UI hook and product controls continue during pause, and Stop
retains its usual authoring restoration. See [pause scheduling](GameClock.md).

**Show game UI** temporarily hides Play's text/buttons so a paused character and
its animation remain visible. It uses the shared strict product operation:

```json
{"command":"set-play-ui-visible","session":"1","scope":"83","visible":false}
```

Use the current published Play session/scope; running and published faulted scenes
permit this view change. Preparation, unpublished startup failures, stopped Play,
stale identities, pending close and a native capture dialog reject it. `visible`
must be boolean and extra fields fail. Same-value requests retain the presentation
revision and frame; changes invalidate old rendered input/capture provenance.
`workspace.view.gameUi` reports the requested visible/revision pair. Read view.ready
and completed rendering metadata to observe actual presentation.

Hiding suppresses native pointer/keyboard widget interaction. The next ordinary
running input publication cancels an armed gesture while retaining focus,
labels/buttons and game script state; faulted HUD models retain their read-only state. Gameplay input,
unscaled callbacks, the simulation clock and explicit HUD service actions remain
available. It does not clear or rewrite the native HUD. A new Play starts visible;
scene cuts within that Play retain the view choice. Save/export never persist it.

PNG capture records gameUiVisible/gameUiRevision. Hidden frames carry hudRevision
zero because no HUD was drawn, so unseen HUD changes cannot prevent capture. A
hide/show cycle advances its presentation revision even if the final visibility
matches an older frame. Already frozen PNGs keep their original metadata/pixels.

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

The hierarchy derives an immutable parent/child index from each authoring snapshot;
roots and siblings keep scene order. Search computes matching rows and ancestors
once per snapshot/query change, folding ASCII case and matching other UTF-8 bytes
exactly. A matching parent does not automatically include unrelated descendants.
Rendering and machine-visible row order consume that same projection; selection,
expansion, drag scope and document edits retain their existing shared operations.
The index validates the scene's 10000-node/256-level bounds, unique IDs, parent
references and acyclicity rather than silently truncating an invalid hierarchy.
Rows are still laid out individually; this is not full hierarchy virtualization or
a claim about populated-game rendering performance.


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
Inspector field counts and starred headings identify applied overrides independently
of list pagination. Restore field reverts a whole native component/top-level field
through the shared `revert-scene-field` edit. Pending drafts remain separate; see
[field contracts](SceneContent.md#field-indicators-and-restoring-a-component).
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

### Applying a linked field

The overridden field's Apply field to source button uses the strict
`apply-scene-field` command, shared with headless documents and the live mailbox.
It writes the complete field to the immediate saved source and rebases only this
instance; other placements refresh explicitly. Scene Undo does not undo the external
file. Inspect `sourceApplied.path`, `written` and `sceneChanged` in the applied response.
See [source application contracts](SceneContent.md#applying-a-complete-field-to-its-source)
for conflicts, nested identities, native-reference boundaries and publication behavior.


The workspace mailbox transport now uses `Dot.Core.Control`, and bounded Play input
uses `Dot.Platform.AgentInput`, shared with the opt-in standalone player. Existing
workspace commands and temporary Play-storage policy retain their meanings. See
[native player control](Export.md#opt-in-native-player-control) for source-free
inspection, gameplay input, scene requests and persistent game-data operations.

## Completed Play frame provenance

Play renders bounded owned snapshots independently of continuing simulation.
workspace.view.rendering exposes submitted/completed/deferred/cancelled counters,
pending tag, latest receipt (serial/tag/scene/size), renderedPlayFrame and
presentedButtonRevision. The rendered Play frame may precede the logical frame;
capture metadata, HUD revision, camera and skeleton overlay describe the same
completed target. Acceptance/pending status never claims new pixels. Authoring
viewport picking is unavailable during Play, matching the graphical operation.

Buttons receive pointer/navigation input only after their definition version has
actually drawn in the current source and target dimensions. Cosmetic focus/hover/
press changes do not change that version or cancel a held gesture. Recreating a
blank target disables UI input until the first matching frame completes.
Scene/layout/epoch/target changes retire old frames; Stop restores authoring through
the existing exact path. This overlap does not make font rasterization, resource
loading or every driver call asynchronous.

Mailbox polling and submission share the bounded directory index described in
[native control](Export.md#opt-in-native-player-control). Malformed request/response
leaves, spelling conflicts and an orphan response for a new ID reject before new
publication. Observe the actual error and repair malformed owned entries. For a
selected orphan ID, a fresh ID is also available; a queued path alone is never
completion. Filesystem scans,
JSON dispatch and writes still run on the calling thread.

The shared viewport loading state also includes geometry. It covers background
mesh/skin reads, decoding and surface analysis plus incremental owner-thread GPU
residency. workspace.view.loading reports geometry and the viewport shows Preparing
geometry through the same implementation. Ready includes all visible sources; a
loading/error acknowledgement is not a completed view. Capture waits for actual
completed matching pixels. Material drafts continue independently while geometry
prepares; Stop, source changes and resource cache refresh retire obsolete results.


## Animation layer authoring and live controls / 动画层制作与运行控制

Select an animation player in the stopped scene's Inspector and expand Animation
layers. Add/remove/reorder up to eight named layers. Choose their library clip,
weight/speed/loop/auto-play settings, override/additive blend mode, and explicit
node/path/channel weights. Additive exposes Reference time (s) in the same clip;
choosing override resets this field to its required zero value. The virtualized
mask list uses the player's real binding map and scene names. Names
are stable ASCII identifiers, 1..64 letters/digits/_/-, starting with a letter.
Apply/reset/undo/save/reopen use the ordinary strict stage-inspector and component
implementation. Apply validates shape/bindings/budgets without file IO; Preview,
Play and export validate actual masked clip/reference channels, duration/scale and
native writer authority. Structural invalidity rejects the staged form; file-dependent
reference errors remain explicit Preview/Play/export failures. Form validation does
not claim resource readiness.
A referencing layer prevents the GUI from removing its clip; removing an earlier
unused slot adjusts layer indices to keep their assets, while scripts still need
updated numeric indices. Transport/document/history byte budgets apply together
with element limits; maximum element counts do not promise a single transport fits.

Preview animation exposes the same layer rows as the running game's Animation
panel: restart, resume, pause, stop, time, weight, speed/loop, and 0.15-second fade
in/out-and-stop. Native independent clocks, blend mode, reference time and weight
fades are visible.
Preview status uses mode/referenceTime; SDK/live Play state uses mode/
referenceTimeSeconds. These are observed saved definitions, not mutable runtime
mode controls. Layer pause freezes its own clip and fade while holding contribution; player pause freezes all.
Layer controls do not dirty saved definitions. When editing a clip, native draft
recreation restores the complete layer snapshot together with the base, preserving
weights/fades/clocks and checking the resulting pose/skin before resource history.
Normal clip-form/gesture/dialog guards also protect layer controls.

```json
{"command":"control-animation-preview-layer","session":"2","name":"Inspection","value":{"action":"seek","seconds":0.8}}
{"command":"control-animation-preview-layer","session":"2","name":"Inspection","value":{"action":"fade","weight":0,"seconds":0.15,"stop":true}}
```

During Play the Animation panel chooses among the actual running scene's enabled
players and shows their live base clock and layer state. Selection is view state,
not authoring; source entity/scene replacements do not reuse an old scope. The
panel and strict operations call the same native layer controls. These explicit
controls can recompose a pose while game simulation is paused, without advancing
any clock or editing the source scene. Restarting Play starts authored definitions;
native script reload preserves the running animation service. Stop discards live
changes and restores the authoring scene.

```json
{"command":"select-play-animation","session":"1","scope":"12","entity":"589"}
{"command":"inspect-play-animation","session":"1","scope":"12","entity":"589"}
{"command":"control-play-animation-layer","session":"1","scope":"12","entity":"589","name":"Inspection","value":{"action":"stop"}}
```

Use the actual current Play session and runtime scene scope from inspect/inspect-play;
entity IDs are canonical decimal strings. Unknown fields, wrong types, retired
sessions/scopes, disabled/missing players and nonrunning Play fail. The read returns
animation with the same units/shape as the SDK's AnimationState. workspace.playAnimation
reports a current scoped UI selection or null. The complete strict layer control
objects are specified in [animation layers](Animation.md#masked-animation-layers--遮罩动画层).
Native player control exposes equivalent animation-state/animation-layer operations.
A CPU pose commit is synchronous; actual presentation/capture still follows normal
completed-frame provenance. Physical layer buttons/dragging, narrow layout and IME
acceptance remain unverified on the locked Mac.

停止时在 Inspector 编辑有序层及通道遮罩，沿用普通草稿/Apply/撤销/保存。预览和实际游戏
的 Animation 面板共用层控制行及原生实现；运行控制带会话/场景范围，不写作者场景。
暂停游戏时仍可显式重构姿态供逐通道检查，时钟不会自行推进；Stop 恢复作者场景。
完整草稿重建一次恢复全部层，避免分步重放导致暂时无效的混合。原生交互/窄布局/输入法
验收仍待完成，不以 JSON 操作成功替代。


## Animation controller authoring / 动画状态机制作

For an enabled manual player, Inspector's Create controller publishes a new
`.dotcontroller` resource after native preview validation and stages its binding.
Apply/Save the Inspector separately. Choose an existing controller or Manual clips
to bind/detach. A bound controller owns base clip/speed/loop; retained manual
settings are used again after detach. Edit animation controller opens the typed
Animation panel with parameters, states/library clips, ordered edges/AND conditions,
exit progress and fade/minimum time. Rows support addition/removal and transition
reordering. There is no embedded source editor or raw-JSON-only form.

```json
{"command":"create-controller","revision":"2","entity":"589","path":"assets/locomotion.dotcontroller","name":"Locomotion"}
{"command":"open-controller","revision":"4","entity":"589"}
{"command":"inspect-controller","buffer":"3","revision":"1"}
{"command":"stage-controller","buffer":"3","revision":"1","value":{"name":"Locomotion","initial":"Idle","parameters":[],"states":[{"name":"Idle","clip":"assets/idle.dotanimation","speed":1,"looping":true}],"transitions":[]}}
{"command":"apply-controller","buffer":"3","revision":"1"}
{"command":"reset-controller-form","buffer":"3","revision":"2"}
{"command":"undo-controller","buffer":"3","revision":"2"}
{"command":"redo-controller","buffer":"3","revision":"3"}
{"command":"save-controller","buffer":"3","revision":"4"}
{"command":"close-controller","buffer":"3","revision":"5","discard":false}
```

Use actual inspected identities. Structural form staging allows incomplete references;
Apply validates the full graph and cloned native bindings/authority/entry pose before
publishing one resource history entry. Failures retain document, native audition and
disk bytes. Apply and history explicitly restart a paused audition; no-op Apply
retains history/revision/preview identity. History is bounded to 128 states/32 MiB;
exact source bytes remain available for undo. Save uses resource metadata, path,
external-byte conflict checks and atomic replacement. Pending forms block Save,
history and runtime controls. Unsaved resources participate in close/Play/export
checks. Ctrl-S/Z/Shift-Z target the open controller. Clip audition explicitly detaches
the controller in its clone and leaves the authoring scene binding intact.

The preview and running Play Animation panel show native state/age/last edge/count
and editable typed parameters. These operations share SDK/native controls:

```json
{"command":"control-animation-preview","session":"6","action":"parameters","value":{"Speed":3}}
{"command":"control-animation-preview","session":"6","action":"reset-controller"}
{"command":"play-animation-parameters","session":"1","scope":"12","entity":"589","value":{"Speed":3}}
{"command":"reset-play-animation-controller","session":"1","scope":"12","entity":"589"}
```

Standalone player control uses animation-parameters {scope,entity,value} and
reset-animation-controller {scope,entity}; exact current running scope is required.
Parameter changes do not advance time. Game scripts can replace intent on the next
Update; pause first for deliberate inspection. Native pause/step scheduling remains
unchanged. These operations do not rewrite saved controller definitions.

状态机创建、绑定、表单、历史、保存、预览和运行参数均有共用产品实现的严格机器操作。
表单可暂留不完整引用，应用须先通过完整原生预检；应用/撤销重做明确重启独立暂停预览。
存盘检查外部冲突并原子替换；场景绑定与状态机资源分别应用/保存，不混入脚本源码编辑器。

### Reviewed model reimport

Assets > Actions offers **Review model reimport** using Import source and the existing
native bundle Path (for example `assets/utility-box`). Save the scene and close other
asset editors first. The review prepares a private project on one CPU worker; changed
existing files begin unresolved. Inspect text before/after (at most 16 KiB per side),
choose **Use source** or **Keep project** for each entire file, then **Validate model
choices** and **Apply reviewed model**. Added files use source. Files no longer generated
by the source remain in the bundle. Binary changes report sizes; inspect their source
in the appropriate external authoring tool. Keeping a material preserves all its
project settings, rather than merging selected properties.

This first reimport contract requires matching native node IDs/names/parents,
mesh/material/skin/animation bindings and model descriptor layout. Structural changes
fail explicitly. It does not provide stable source GUIDs, infer node renames/reordering,
merge material properties, delete retired assets, or edit source files. Review the source
correspondence when similarly named source nodes or primitives are rearranged. Geometry,
pixels, material values, poses and clip contents can change within the accepted layout.

Candidate validation uses the product's CPU scene preparation for every saved scene in
a complete private project snapshot, including retained project resources. It does not
compile scripts or prove GPU/audio residency. Apply/revert require saved, stopped
authoring without drafts; asset readers are stopped and caches invalidated first.
Exact resource catalog and file bytes (including project metadata) must still match the
review baseline. A conflict leaves the published bundle unchanged and requires preparing
a new review. External editors must serialize writes through publication. Replacement
uses two no-overwrite renames with rollback, not an atomic directory exchange or a
crash/hostile-race guarantee. A failed rollback reports where original files remain.

After apply, refresh linked scene instances explicitly using their existing source
refresh operation, review local overrides, then save. Reimport changes asset files and
is separate from scene undo. **Revert model files** restores the complete previous
bundle only while saved project bytes remain unchanged; later scene saves or external
asset edits reject it. Accepting/closing an applied review keeps the published files and
releases its retained backup. Closing a preparing/validating review cancels and joins
its worker; destruction does not automatically revert applied files.

The same strict operations are available through the running editor's JSON channel:

```json
{"command":"prepare-model-reimport","revision":"12","source":"/absolute/utility.gltf","path":"assets/utility-box"}
{"command":"inspect-model-reimport","session":"1","offset":0,"limit":64}
{"command":"inspect-model-reimport-file","session":"1","revision":"2","path":"assets/utility-box/materials/0.dotmaterial"}
{"command":"choose-model-reimport","session":"1","revision":"2","path":"assets/utility-box/materials/0.dotmaterial","choice":"project"}
{"command":"choose-model-reimport","session":"1","revision":"3","path":"assets/utility-box/model.dotscene","choice":"source"}
{"command":"validate-model-reimport","session":"1","revision":"4"}
{"command":"inspect-model-reimport","session":"1"}
{"command":"apply-model-reimport","session":"1","revision":"6"}
{"command":"revert-model-reimport","session":"1","revision":"7"}
{"command":"close-model-reimport","session":"1","revision":"8"}
```

Use returned identities/revisions, not the illustrative numbers above. Preparation and
validation advance the review revision on completion. Inspect returns `preparing`,
`review`, `validating`, `ready`, `applied`, `reverted` or `failed`, plus errors/warnings and
paged changes with path, before/after sizes, added flag and choice. Begin optionally
accepts source `scene` 0..1023. Pages allow offset 0..8192 and limit 1..256. File previews
return explicit text/truncated flags. Every mutation rejects stale identities/revisions;
unknown fields and invalid choices are errors. One review per workspace bounds ownership.
Limits are 8192 resource entries, 64 levels, 64 MiB/file and 512 MiB each for project
snapshot and generated bundle. First import uses the background preparation/publication workflow above.


## Additive content during Play

The Play viewport's **Add scene content** chooser appends a saved scene while the
current game continues. Each active instance shows its source path and a
**Remove scene content** button. These use the same GameSession and strict product
operations as scripts and the standalone player:

```json
{"command":"append-play-scene","session":"1","revision":"4","path":"assets/room.dotscene"}
{"command":"play-scene-status","session":"1","request":"2"}
{"command":"play-scene-instances","session":"1"}
{"command":"remove-play-scene","session":"1","revision":"4","scope":"9","root":"350"}
```

Mutations require the current document revision, running Play, current session and
no pending close; removal additionally requires the instance's current scope/root.
Accepted requests return an ID. Status includes `kind`, scoped `root` or null,
`committed` and terminal errors. The instance query returns `{instances:[{root,path}]}`
without advancing gameplay. One request is active at a time. These operations do
not edit the authoring document or undo history. See the [runtime content contract](Scripting.md#additive-runtime-scene-content)
for exact membership, dependency rejection and publication failure semantics.

Play 中可追加已保存片段并按实例卸载，GUI/严格 JSON/脚本共用产品实现；操作不修改编辑
文档。请求接受不等于加载完成，须读取最终状态；外部原生引用会阻止卸载，发布后故障不伪装回滚。


## Lightweight Play status

```json
{"command":"play-status","session":"1"}
```

This exact command/session query returns `{ok,play}` from the same native status
used by the Play panel. It is available throughout current-session preparation,
running, cancellation and retained Stop/failure, including while another resource
preview is open. Unknown/retired session tokens and extra fields reject. A query
does not dispatch input, advance time or publish pending scene/script work. Use it
for input completion and lifecycle polling. `inspect-play` remains the full world/
view observation after scene publication; `inspect` observes the authoring document.
These full inspections deliberately cost more and are unnecessary for every frame
of a held input. Input/cancel acceptance is status, not completed gameplay.

轻量状态查询与 Play 面板共用原生状态，不序列化编辑文档/运行世界，不推进时间或提交任务。
使用当前会话标识轮询输入/生命周期；完整场景与画面仍通过 inspect-play 单独检查。


Initial Play now starts an independent CPU scene-resource preparation job from the
frozen authored bytes, alongside its existing compilation and deferred audio tasks.
It validates graphics/skins and owns decoded animation/controller/collision/PCM/UI
resources under the same limits used by scene transitions. The owner consumes that
proof before native service/script construction; startup does not reread those
initial binding resources. The initial snapshot can contain unsaved edits and does
not save or borrow the live document. Source captures continue to invalidate obsolete
script compilations while the frozen resource job remains owned by this Play request.
Asset edits must be serialized through publication, as for scene transitions.

Preparing exposes no runtime scene/game-data/animation/input controls. Stop and
failure request cancellation of both CPU jobs and native teardown, then remain
Cancelling until every owned task is reaped. Only real backend activation publishes
Running. Resource failure retains its contextual cause and permits a fresh Play after
cleanup; no component hooks run before initial native bindings succeed. The toolbar
and strict play-status operation use the same actual preparation phase, including
resources when resource work remains after compilation.

Source capture, scene snapshot/cloning, native binding, script construction/Start,
GPU residency and some teardown still have owner-thread costs. Synchronous PlaySession
creation retains its convenience path. Native project/exported-game startup now
has its own real pending GameSession and live control/teardown polling; see
[native preparation](Control.md#native-initial-preparation--独立游戏初始准备). Owner
binding, package/device setup and broader streaming work remain.

初次 Play 以冻结的未保存场景字节启动独立 CPU 资源任务，与编译及延后音频端点并行。
CPU 准备完成前不构造脚本/执行 Start，设备激活后才发布运行场景；Stop/错误须回收全部
拥有任务及原生清理才终止。
工具栏与严格状态操作显示同一真实阶段。源码捕获、场景克隆、原生绑定及脚本构造仍有
所有者线程开销；同步便利工厂保留现有路径；独立游戏的真实准备/控制流程另见上述契约，绑定及包/设备
建立开销仍待继续推进。


## Animation retarget authoring

Select an existing animation owner and expand Inspector → Retarget animation.
Choose saved source/target reference scenes and their players, inspect bindings,
then select the source native clip. Save matching reference poses as ordinary scene
content first when the imported rest poses differ. Suggestions match unique names
ignoring ASCII case and `._-`; they never infer humanoid roles or accept ambiguous
names. Review/add explicit source/target rows, choose a rational rate and optionally
one mapped root displacement/scale. Virtualized rows remain editable for larger rigs.

Prepare motion runs the owned CPU bake and native preflight without creating output.
Ready reports frame/node/track counts and unmapped bindings. Accept prepared clip
creates a new native resource and stages the complete player in Inspector. Apply
commits the player through normal scene history; Save persists the scene separately.
Discard/cancel releases unpublished work. Other authoring/resource writes are blocked
until preparation/result is finished or discarded. Status/read-only inspection and
close remain available. Errors are retained; a failed/cancelled/published session
may be followed by a new preparation. Cancellation cannot claim rollback of an
already created clip. GUI buttons and the following strict commands share implementation:

```json
{"command":"inspect-retarget-reference","role":"source","path":"assets/motion-source/model.dotscene"}
{"command":"inspect-retarget-reference","role":"target","path":"assets/actions/surveyor-reference.dotscene"}
{"command":"suggest-retarget-mappings"}
{"command":"stage-retarget-profile","value":{"sourceScene":"assets/motion-source/model.dotscene","sourceEntity":"1","targetScene":"assets/actions/surveyor-reference.dotscene","targetEntity":"2","clip":"assets/motion-source/animations/1.dotanimation","name":"Hip motion","rate":{"numerator":30,"denominator":1},"mappings":[{"source":3,"target":4,"sourceName":"pelvis","targetName":"hips"}],"translation":{"target":4,"scale":1}}}
{"command":"save-retarget-profile","path":"assets/actions/hip.dotretarget"}
{"command":"load-retarget-profile","path":"assets/actions/walk.dotretarget"}
{"command":"begin-animation-retarget","revision":"12","entity":"589","path":"assets/actions/new-walk.dotanimation","value":{"sourceScene":"assets/motion-source/model.dotscene","sourceEntity":"1","targetScene":"assets/actions/surveyor-reference.dotscene","targetEntity":"2","clip":"assets/motion-source/animations/1.dotanimation","name":"Hip motion","rate":{"numerator":30,"denominator":1},"mappings":[{"source":3,"target":4,"sourceName":"pelvis","targetName":"hips"}],"translation":null}}
{"command":"animation-retarget-status"}
{"command":"cancel-animation-retarget","session":"1"}
{"command":"publish-animation-retarget","session":"2","revision":"12"}
```

Each object is a separate operation. Use actual returned identities/revisions; the
single-hip examples illustrate the schema, while the saved walk profile has 51 pairs.
Status returns retarget with session/state/entity/revision/path/profilePath/profile,
source/target inspected players, result and nullable error. States are idle,
preparing/cancelling, ready, cancelled, failed and published. Source/target arrays
include player entity/name/clips and bound nodes with name/logical ID/nearest bound
parent/anchor. IDs identifying sessions/entities/revisions are decimal strings.
Unknown fields/types, stale sessions, conflicting drafts/modes/jobs, renamed bindings
and changed exact input bytes reject. Save profile replaces only its loaded exact
baseline or creates a new resource; external baseline conflicts remain recoverable.
Generic text limits remain separate from native clip publication.

The result does not auto-detach an existing controller. Individual clip audition
uses the ordinary clone; actual authoring flags/controller/layers survive creation.
No automatic rest-pose alignment, feet/contact solver or real-time retarget playback
is implied. See [retarget contracts](Animation.md#saved-motion-retargeting--保存的动作重定向).


## Saving an animation pose as scene content

In isolated animation controls (including an open clip or controller), pause at a
reviewed pose, expand **Save pose as scene**, enter a new assets scene path/name
and choose **Save animation pose**. Save/apply pending clip/controller edits first.
The same strict product operation is:

```json
{"command":"save-animation-pose","revision":"12","session":"3","generation":"7","path":"assets/actions/idle-reference.dotscene","name":"Idle reference"}
```

Use current authoring revision and preview session/generation from inspect. Unknown
fields/types, stale identities/generation, playing/faulted previews, other drafts,
gestures/jobs/dialogs, invalid paths and existing files/aliases reject. The selected
preview owner is authoritative; hierarchy selection does not change which player
is captured. Capture preserves its current composed pose, native skin mappings and
shared asset paths and source environment while disabling all copied animation/autoplay. Saved values are
compact and may differ from the authoring scene; use the returned `pose.owner` for
reference inspection or ordinary hierarchy selection after Open scene.

Success returns pose with path/name/session/generation/source/sourceOwner/owner,
entity count, clip/asset/time and bindPose. IDs and generations use decimal strings;
clip/count/time use numbers. `workspace.animationPose` retains the last successfully
saved result, rather than implying a later failed write succeeded. The operation
never advances the preview generation, changes authoring history or writes keys.
It creates a regular scene resource; end audition, Open scene to edit it, then use
it in retargeting or insert it as ordinary scene content. A catalog failure after
publication explicitly reports that the scene was saved. No new pose file format,
mesh rebinding or automatic calibration is introduced.

## Resource creation conflicts

New resource and single-file import operations now use native complete-file
no-replace publication after validation. This also protects pose/reference and
retarget-clip creation if another creator occupies the destination after the UI's
initial check. A failed clip publication leaves its Ready result and unstaged
Inspector intact; recover the destination explicitly before accepting again.
Existing-file editing/saving remains a separate baseline-checked replacement
operation. This does not introduce project-wide concurrent editing or alias locks;
see [atomic resource creation](Assets.md#atomic-creation-of-new-resources).

## Two-bone pose tool / 双骨骼姿态工具

Select a chain's hand/foot end, then expand **Two-bone pose** in Inspector. The tool
shows its direct parent and grandparent, and initially fills Target with the end's
world position and Pole with the middle's world position. Adjust the world-space
Target and Pole, then **Solve pose**. A straight starting chain requires moving
Pole away from the root-target axis. The shared product command is:

```json
{"command":"pose-two-bone","revision":"7","root":"10","middle":"11","end":"12","target":[0.3,1.2,0.2],"pole":[0.8,1.1,-0.2]}
```

Fields are exact. IDs and revision are decimal strings; Target/Pole are three finite
native float numbers, with no booleans, unknown fields or dimension coercion. The
[solver contract](Animation.md#two-bone-pose-construction--双骨骼姿态制作) validates
actual hierarchy/scale/precision. One atomic document transform batch preserves
selection, translations, scales and end world orientation. Undo/Redo restore all
three joint rotations together; Save/Reopen retains ordinary scene data. A failed
solve, stale revision or failed history preparation leaves the document unchanged.

The response adds `twoBone` with root/middle/end, target/pole, actual reached point,
residual and explicit clamped status. Workspace inspection retains the last successful
report at `workspace.twoBonePose`, together with its document revision and scene path;
that historical report does not assert the current scene still has the same pose.
Inspector displays it only at the matching current revision, scene and end selection.
Posing requires stopped authoring, completed previews/gestures/dialogs, applied scene
forms and closed resource/clip/controller/UI editors. GUI and agent routes dispatch
the same native document transaction. The GUI fields are ordinary unsaved tool input.
Save a reference scene or create a clip from the solved pose for further production;
an enabled animation player can overwrite these authoring rotations during playback.
No new asset format or persistent runtime IK component is introduced.

选择末端关节后，通过 Inspector 同一原生事务求解三节点直接骨链。Target/Pole 均为世界
坐标，不可达目标显示夹取及残差；成功为一次可撤销的普通姿态修改。保存、重开、动作
制作和导出沿用现有资源流程；无效输入或旧版本不会留下部分变换。

## Key two-bone pose / 双骨骼姿态写键

Open an individual library clip in the animation editor, pause and seek the desired
time. Expand **Key two-bone pose** in key properties, choose the bound hand/foot,
then enter world Target/Pole and **Solve and key pose**. The end's direct parents
are inferred from the real hierarchy; all three must be bound to that player. Initial
form coordinates come from the selected end and middle at the observed sample.
These are unsaved tool inputs; agents pass the same values directly:

```json
{"command":"key-animation-two-bone","buffer":"2","revision":"3","session":"7","generation":"4","end":12,"target":[0.3,1.2,0.2],"pole":[0.8,1.1,-0.2]}
```

Fields are exact. Buffer/resource revision and preview session/generation are canonical
decimal strings observed from current inspection. End is a logical integer binding
0..9999, distinct from a world entity ID; Target/Pole are three finite float values.
Unknown/missing fields, stale identities, wrong types, bind-pose display, unpaused/
faulted preview or a base transition reject. Finish key/curve gestures and pending
key forms/dialogs first. The shared native command prepares the three-key selection,
solves and preflights a recreated real preview before committing one resource history
entry. Active layers that prevent reproducing the desired pose reject; stop the
conflicting layer or author a different pose explicitly. Source authoring stays intact.

The result has `animationClip`, `preview` and `poseKeys`: key `time`, retained
`sampleTime`, logical `end`, requested target/pole, actual preview `reached`/`residual`,
explicit clamping and `changed`, buffer/revision. Target/Pole echo the request;
residual uses the validated native float goal. `animationClip.poseKeys` retains
the last successful report with its resource identity/revision, so an older report
is not a claim about an undone, newly selected or differently sampled pose. GUI
shows it only for the matching resource revision. Three resulting rotation keys
are selected together for existing group operations. Undo/Redo use ordinary resource
history and recreate native preview; Save/Reopen/export use the existing clip format.
See [key/tangent and native resample contracts](Animation.md#keying-an-ik-pose--以-ik-姿态写键).

暂停片段预览后，可按逻辑末端绑定和世界目标/pole 一次写入三关节旋转键。GUI 与严格
机器操作共用真实求解、重采样和单次历史提交，保留源场景。混合层若改变所求姿态会在
历史修改前拒绝；清理冲突后继续。报告包含实际预览残差及资源身份，保存和导出沿用
普通动画资源流程，不新增运行时 IK 约束或私有片段格式。
